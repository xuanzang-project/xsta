#include "sta/liberty.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace sta {
namespace {

struct Node {
    std::string name;
    std::vector<std::string> args;
    std::map<std::string, std::vector<std::string>> attributes;
    std::vector<Node> children;
};

class Lexer {
public:
    explicit Lexer(std::string text) : text_(std::move(text)) {}

    std::string next() {
        skip_space();
        if (pos_ == text_.size()) return {};
        const char c = text_[pos_++];
        if (c == '"' || c == '\'') {
            std::string value;
            while (pos_ < text_.size()) {
                char ch = text_[pos_++];
                if (ch == c) return value;
                if (ch == '\\' && pos_ < text_.size()) {
                    ch = text_[pos_++];
                    if (ch == 'n') value.push_back('\n');
                    else if (ch == 't') value.push_back('\t');
                    else value.push_back(ch);
                } else {
                    value.push_back(ch);
                }
            }
            throw std::runtime_error("Unterminated quoted string in Liberty file");
        }
        if (std::string("(){}:;,").find(c) != std::string::npos) return std::string(1, c);
        std::string value(1, c);
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)) ||
                std::string("(){}:;,\"'").find(ch) != std::string::npos) break;
            if (ch == '/' && pos_ + 1 < text_.size() &&
                (text_[pos_ + 1] == '/' || text_[pos_ + 1] == '*')) break;
            value.push_back(ch);
            ++pos_;
        }
        return value;
    }

private:
    void skip_space() {
        while (pos_ < text_.size()) {
            if (std::isspace(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            } else if (text_[pos_] == '\\' && pos_ + 1 < text_.size() &&
                       (text_[pos_ + 1] == '\n' || text_[pos_ + 1] == '\r')) {
                ++pos_;
                if (text_[pos_] == '\r') ++pos_;
                if (text_[pos_] == '\n') ++pos_;
            } else if (text_[pos_] == '/' && pos_ + 1 < text_.size() &&
                       text_[pos_ + 1] == '/') {
                pos_ += 2;
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
            } else if (text_[pos_] == '/' && pos_ + 1 < text_.size() &&
                       text_[pos_ + 1] == '*') {
                pos_ += 2;
                const auto end = text_.find("*/", pos_);
                if (end == std::string::npos) throw std::runtime_error("Unterminated Liberty comment");
                pos_ = end + 2;
            } else {
                break;
            }
        }
    }

    std::string text_;
    std::size_t pos_ = 0;
};

class Parser {
public:
    explicit Parser(std::string text) : lexer_(std::move(text)), token_(lexer_.next()) {}

    std::vector<Node> parse() {
        std::vector<Node> nodes;
        while (!token_.empty()) nodes.push_back(parse_statement());
        return nodes;
    }

private:
    void consume(const std::string& expected) {
        if (token_ != expected) throw std::runtime_error("Expected '" + expected + "', got '" + token_ + "'");
        token_ = lexer_.next();
    }

    Node parse_statement() {
        if (token_.empty() || token_ == "}" || token_ == ")" || token_ == ";")
            throw std::runtime_error("Unexpected token in Liberty file: " + token_);
        Node node;
        node.name = token_;
        token_ = lexer_.next();
        if (token_ == "(") {
            consume("(");
            while (token_ != ")") {
                if (token_.empty()) throw std::runtime_error("Unterminated Liberty argument list");
                if (token_ == ",") {
                    consume(",");
                    continue;
                }
                node.args.push_back(token_);
                token_ = lexer_.next();
            }
            consume(")");
        }
        if (token_ == "{") {
            consume("{");
            while (token_ != "}") {
                if (token_.empty()) throw std::runtime_error("Unterminated Liberty group: " + node.name);
                node.children.push_back(parse_statement());
            }
            consume("}");
            if (token_ == ";") consume(";");
            return node;
        }
        if (token_ == ":") {
            consume(":");
            auto& values = node.attributes[node.name];
            while (token_ != ";") {
                if (token_.empty()) throw std::runtime_error("Unterminated Liberty attribute: " + node.name);
                if (token_ != ",") values.push_back(token_);
                token_ = lexer_.next();
            }
            consume(";");
            return node;
        }
        if (token_ == ";") {
            consume(";");
            return node;
        }
        throw std::runtime_error("Expected '{', ':', or ';' after Liberty statement '" + node.name + "'");
    }

    Lexer lexer_;
    std::string token_;
};

const Node* child(const Node& node, const std::string& name) {
    const auto it = std::find_if(node.children.begin(), node.children.end(),
                                 [&](const Node& item) { return item.name == name; });
    return it == node.children.end() ? nullptr : &*it;
}

std::vector<const Node*> children(const Node& node, const std::string& name) {
    std::vector<const Node*> result;
    for (const auto& item : node.children) if (item.name == name) result.push_back(&item);
    return result;
}

std::string attribute(const Node& node, const std::string& name, std::string fallback = {}) {
    const auto it = node.attributes.find(name);
    if (it != node.attributes.end() && !it->second.empty()) return it->second.front();
    const auto nested = std::find_if(node.children.begin(), node.children.end(),
                                     [&](const Node& item) { return item.name == name; });
    if (nested != node.children.end()) {
        const auto value = nested->attributes.find(name);
        if (value != nested->attributes.end() && !value->second.empty()) return value->second.front();
    }
    return fallback;
}

std::vector<double> numbers(const std::vector<std::string>& values) {
    std::vector<double> result;
    for (const auto& value : values) {
        std::size_t start = 0;
        while (start < value.size()) {
            while (start < value.size() && (std::isspace(static_cast<unsigned char>(value[start])) || value[start] == ',')) ++start;
            if (start == value.size()) break;
            std::size_t end = start;
            while (end < value.size() && value[end] != ',') ++end;
            const auto part = value.substr(start, end - start);
            double number = 0.0;
            try {
                std::size_t consumed = 0;
                number = std::stod(part, &consumed);
                if (consumed == 0) throw std::runtime_error("no numeric prefix");
            } catch (const std::exception&) {
                throw std::runtime_error("Invalid numeric value in Liberty table: '" + part + "'");
            }
            result.push_back(number);
            start = end + 1;
        }
    }
    return result;
}

double unit_scale(const std::string& unit, bool time) {
    std::size_t pos = 0;
    while (pos < unit.size() && (std::isdigit(static_cast<unsigned char>(unit[pos])) ||
           unit[pos] == '.' || unit[pos] == '+' || unit[pos] == '-')) ++pos;
    const double multiplier = pos ? std::stod(unit.substr(0, pos)) : 1.0;
    std::string suffix = unit.substr(pos);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (time) {
        if (suffix == "s") return multiplier;
        if (suffix == "ms") return multiplier * 1e-3;
        if (suffix == "us") return multiplier * 1e-6;
        if (suffix == "ns") return multiplier * 1e-9;
        if (suffix == "ps") return multiplier * 1e-12;
        if (suffix == "fs") return multiplier * 1e-15;
    } else {
        if (suffix == "f") return multiplier;
        if (suffix == "mf") return multiplier * 1e-3;
        if (suffix == "uf") return multiplier * 1e-6;
        if (suffix == "nf") return multiplier * 1e-9;
        if (suffix == "pf") return multiplier * 1e-12;
        if (suffix == "ff") return multiplier * 1e-15;
    }
    throw std::runtime_error("Unsupported Liberty unit: " + unit);
}

std::optional<double> optional_number(const Node& node, const std::string& key) {
    const auto value = attribute(node, key);
    if (value.empty()) return std::nullopt;
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric Liberty attribute '" + key + "': " + value);
    }
}

struct TableTemplate {
    std::vector<double> x;
    std::vector<double> y;
    std::string variable_1;
    std::string variable_2;
};

double axis_scale(const std::string& variable, double time_scale, double cap_scale) {
    if (variable.find("transition") != std::string::npos ||
        variable.find("time") != std::string::npos) return time_scale;
    if (variable.find("capacitance") != std::string::npos) return cap_scale;
    return 1.0;
}

LookupTable parse_table(const Node& node, const std::map<std::string, TableTemplate>& templates,
                        double time_scale, double cap_scale) {
    LookupTable table;
    if (!node.args.empty()) table.template_name = node.args.front();
    const auto found = templates.find(table.template_name);
    TableTemplate axes;
    if (found != templates.end()) axes = found->second;
    table.variable_1 = axes.variable_1;
    table.variable_2 = axes.variable_2;
    if (const Node* index = child(node, "index_1")) {
        table.index_1 = numbers(index->args);
    } else {
        table.index_1 = axes.x;
    }
    if (const Node* index = child(node, "index_2")) {
        table.index_2 = numbers(index->args);
    } else {
        table.index_2 = axes.y;
    }
    const double xscale = axis_scale(axes.variable_1, time_scale, cap_scale);
    const double yscale = axis_scale(axes.variable_2, time_scale, cap_scale);
    for (double& value : table.index_1) value *= xscale;
    for (double& value : table.index_2) value *= yscale;
    const Node* values = child(node, "values");
    if (!values) return table;
    const double value_scale = time_scale;
    const auto row_values = values->args;
    if (row_values.size() == 1) {
        auto flat = numbers(row_values);
        if (table.index_2.empty()) {
            for (const double value : flat) table.values.push_back({value * value_scale});
        } else {
            const auto width = table.index_2.size();
            for (std::size_t i = 0; i < flat.size(); i += width) {
                const auto end = std::min(flat.size(), i + width);
                table.values.emplace_back(flat.begin() + static_cast<std::ptrdiff_t>(i),
                                          flat.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
    } else {
        for (const auto& row : row_values) table.values.push_back(numbers({row}));
    }
    if (!table.index_2.empty())
        for (auto& row : table.values) for (double& value : row) value *= value_scale;
    return table;
}

TimingArc parse_arc(const Node& node, const std::map<std::string, TableTemplate>& templates,
                    double time_scale, double cap_scale) {
    TimingArc arc;
    arc.related_pin = attribute(node, "related_pin");
    arc.timing_sense = attribute(node, "timing_sense");
    arc.timing_type = attribute(node, "timing_type");
    for (const auto& item : node.children) {
        if (item.name == "cell_rise") arc.cell_rise = parse_table(item, templates, time_scale, cap_scale);
        else if (item.name == "cell_fall") arc.cell_fall = parse_table(item, templates, time_scale, cap_scale);
        else if (item.name == "rise_transition") arc.rise_transition = parse_table(item, templates, time_scale, cap_scale);
        else if (item.name == "fall_transition") arc.fall_transition = parse_table(item, templates, time_scale, cap_scale);
        else if (item.name == "rise_constraint") arc.rise_constraint = parse_table(item, templates, time_scale, cap_scale);
        else if (item.name == "fall_constraint") arc.fall_constraint = parse_table(item, templates, time_scale, cap_scale);
    }
    return arc;
}

} // namespace

bool LookupTable::empty() const {
    return values.empty() || values.front().empty();
}

double LookupTable::interpolate(double x, double y) const {
    if (empty()) throw std::runtime_error("Cannot interpolate an empty lookup table");
    if (index_1.empty() || values.size() != index_1.size())
        throw std::runtime_error("Lookup-table row count does not match index_1");
    if (!std::is_sorted(index_1.begin(), index_1.end()) ||
        (!index_2.empty() && !std::is_sorted(index_2.begin(), index_2.end())))
        throw std::runtime_error("Lookup-table indices are not sorted");
    for (const auto& row : values) {
        const std::size_t expected = index_2.empty() ? 1 : index_2.size();
        if (row.size() != expected)
            throw std::runtime_error("Lookup-table column count does not match index_2");
    }
    const auto bracket = [](const std::vector<double>& axis, double value) {
        if (axis.empty()) return std::pair<std::size_t, std::size_t>{0, 0};
        if (axis.size() == 1 || value <= axis.front()) return std::pair<std::size_t, std::size_t>{0, 0};
        if (value >= axis.back()) return std::pair<std::size_t, std::size_t>{axis.size() - 1, axis.size() - 1};
        const auto upper = std::upper_bound(axis.begin(), axis.end(), value);
        const auto hi = static_cast<std::size_t>(upper - axis.begin());
        return std::pair<std::size_t, std::size_t>{hi - 1, hi};
    };
    const auto [x0, x1] = bracket(index_1, x);
    const auto [y0, y1] = bracket(index_2, y);
    const auto at = [&](std::size_t row, std::size_t col) {
        if (row >= values.size() || col >= values[row].size())
            throw std::runtime_error("Malformed Liberty table dimensions");
        return values[row][col];
    };
    if (index_2.empty()) {
        if (x0 == x1) return at(x0, 0);
        const double ratio = (x - index_1[x0]) / (index_1[x1] - index_1[x0]);
        return at(x0, 0) + ratio * (at(x1, 0) - at(x0, 0));
    }
    if (x0 == x1 && y0 == y1) return at(x0, y0);
    const double tx = x0 == x1 ? 0.0 : (x - index_1[x0]) / (index_1[x1] - index_1[x0]);
    const double ty = y0 == y1 ? 0.0 : (y - index_2[y0]) / (index_2[y1] - index_2[y0]);
    const double low = at(x0, y0) + ty * (at(x0, y1) - at(x0, y0));
    const double high = at(x1, y0) + ty * (at(x1, y1) - at(x1, y0));
    return low + tx * (high - low);
}

LibertyLibrary LibertyLibrary::parse_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open Liberty file: " + path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto roots = Parser(std::move(contents)).parse();
    const auto lib = std::find_if(roots.begin(), roots.end(), [](const Node& node) { return node.name == "library"; });
    if (lib == roots.end() || lib->args.empty()) throw std::runtime_error("No library(name) group found in " + path);

    LibertyLibrary result;
    result.name = lib->args.front();
    result.time_unit = attribute(*lib, "time_unit", "1s");
    result.capacitance_unit = attribute(*lib, "capacitive_load_unit", "1f");
    if (const Node* cap_unit = child(*lib, "capacitive_load_unit"); cap_unit && cap_unit->args.size() >= 2)
        result.capacitance_unit = cap_unit->args[0] + cap_unit->args[1];
    result.time_scale_s = unit_scale(result.time_unit, true);
    result.capacitance_scale_f = unit_scale(result.capacitance_unit, false);

    for (const auto* condition : children(*lib, "operating_conditions")) {
        result.conditions.name = condition->args.empty() ? "" : condition->args.front();
        result.conditions.process = optional_number(*condition, "process");
        result.conditions.voltage_v = optional_number(*condition, "voltage");
        result.conditions.temperature_c = optional_number(*condition, "temperature");
        break;
    }
    if (!result.conditions.voltage_v) result.conditions.voltage_v = optional_number(*lib, "nom_voltage");
    if (!result.conditions.temperature_c) result.conditions.temperature_c = optional_number(*lib, "nom_temp");

    std::map<std::string, TableTemplate> templates;
    for (const auto& item : lib->children) {
        if (item.name.find("table_template") == std::string::npos || item.args.empty()) continue;
        TableTemplate table;
        table.variable_1 = attribute(item, "variable_1");
        table.variable_2 = attribute(item, "variable_2");
        if (const auto* index = child(item, "index_1")) table.x = numbers(index->args);
        if (const auto* index = child(item, "index_2")) table.y = numbers(index->args);
        templates[item.args.front()] = std::move(table);
    }

    for (const auto* cell_node : children(*lib, "cell")) {
        if (cell_node->args.empty()) continue;
        LibertyCell cell;
        cell.name = cell_node->args.front();
        cell.area = optional_number(*cell_node, "area");
        cell.sequential = child(*cell_node, "ff") || child(*cell_node, "latch") ||
                          child(*cell_node, "ff_bank") || child(*cell_node, "statetable");
        for (const auto* pin_node : children(*cell_node, "pin")) {
            if (pin_node->args.empty()) continue;
            LibertyPin pin;
            pin.name = pin_node->args.front();
            pin.direction = attribute(*pin_node, "direction");
            pin.capacitance_f = optional_number(*pin_node, "capacitance").value_or(0.0) * result.capacitance_scale_f;
            if (const auto cap = optional_number(*pin_node, "max_capacitance"))
                pin.max_capacitance_f = *cap * result.capacitance_scale_f;
            if (const auto slew = optional_number(*pin_node, "max_transition"))
                pin.max_transition_s = *slew * result.time_scale_s;
            for (const auto* timing_node : children(*pin_node, "timing"))
                pin.timing.push_back(parse_arc(*timing_node, templates, result.time_scale_s,
                                               result.capacitance_scale_f));
            cell.pins.emplace(pin.name, std::move(pin));
        }
        result.cells.emplace(cell.name, std::move(cell));
    }
    if (result.cells.empty()) throw std::runtime_error("No cells found in Liberty library " + result.name);
    return result;
}

const LibertyCell* LibertyLibrary::find_cell(const std::string& cell_name) const {
    const auto found = cells.find(cell_name);
    return found == cells.end() ? nullptr : &found->second;
}

} // namespace sta
