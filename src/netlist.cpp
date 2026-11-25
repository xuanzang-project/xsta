#include "sta/netlist.hpp"

#include <algorithm>
#include <stdexcept>

namespace sta {
namespace {

std::vector<std::string> bit_list(const Json& value) {
    std::vector<std::string> bits;
    for (const auto& bit : value.array()) {
        const auto text = bit.string();
        if (text == "0" || text == "1" || text == "x" || text == "z")
            bits.push_back("const:" + text);
        else
            bits.push_back("net:" + text);
    }
    return bits;
}

bool truthy(const Json* value) {
    if (!value) return false;
    try {
        const std::string text = value->string();
        return text == "1" || text == "00000000000000000000000000000001";
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

Netlist Netlist::parse_yosys_json(const std::string& path) {
    const Json root = Json::parse_file(path);
    const auto& modules = root.at("modules").object();
    if (modules.empty()) throw std::runtime_error("Yosys JSON contains no modules");

    auto selected = modules.end();
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        const Json* attributes = it->second.find("attributes");
        if (attributes && truthy(attributes->find("top"))) {
            if (selected != modules.end()) throw std::runtime_error("Yosys JSON marks multiple top modules");
            selected = it;
        }
    }
    if (selected == modules.end()) {
        if (modules.size() != 1)
            throw std::runtime_error("Yosys JSON has multiple modules and no unique top attribute");
        selected = modules.begin();
    }

    Netlist result;
    result.module_name = selected->first;
    if (const Json* ports = selected->second.find("ports")) {
        for (const auto& [name, port_json] : ports->object()) {
            NetPort port;
            port.name = name;
            port.direction = port_json.at("direction").string();
            port.bits = bit_list(port_json.at("bits"));
            result.ports.emplace(name, std::move(port));
        }
    }
    if (const Json* cells = selected->second.find("cells")) {
        for (const auto& [name, cell_json] : cells->object()) {
            CellInstance cell;
            cell.name = name;
            cell.type = cell_json.at("type").string();
            if (const Json* connections = cell_json.find("connections")) {
                for (const auto& [pin, bits] : connections->object())
                    cell.connections.emplace(pin, bit_list(bits));
            }
            if (const Json* directions = cell_json.find("port_directions")) {
                for (const auto& [pin, direction] : directions->object())
                    cell.directions.emplace(pin, direction.string());
            }
            result.cells.push_back(std::move(cell));
        }
    }
    return result;
}

ResolvedNetlist resolve(const Netlist& netlist, const LibertyLibrary& library) {
    ResolvedNetlist result{&netlist, &library, {}};
    for (const auto& instance : netlist.cells) {
        const LibertyCell* cell = library.find_cell(instance.type);
        if (!cell) {
            result.errors.push_back("Instance '" + instance.name + "' uses missing Liberty cell '" + instance.type + "'");
            continue;
        }
        for (const auto& [pin_name, bits] : instance.connections) {
            const auto pin = cell->pins.find(pin_name);
            if (pin == cell->pins.end()) {
                result.errors.push_back("Instance '" + instance.name + "' connects unknown pin '" + pin_name +
                                        "' on Liberty cell '" + instance.type + "'");
                continue;
            }
            if (bits.size() != 1) {
                result.errors.push_back("Instance '" + instance.name + "' pin '" + pin_name +
                                        "' is not scalar; vector cell pins are unsupported");
            }
            const auto direction = instance.directions.find(pin_name);
            if (direction != instance.directions.end() && !pin->second.direction.empty() &&
                direction->second != pin->second.direction) {
                result.errors.push_back("Instance '" + instance.name + "' pin '" + pin_name +
                                        "' direction mismatch: netlist says " + direction->second +
                                        ", Liberty says " + pin->second.direction);
            }
        }
        for (const auto& [pin_name, pin] : cell->pins) {
            if (instance.connections.find(pin_name) == instance.connections.end())
                result.errors.push_back("Instance '" + instance.name + "' is missing Liberty pin '" + pin_name + "'");
            if (pin.direction != "input" && pin.direction != "output" && pin.direction != "inout")
                result.errors.push_back("Liberty pin '" + instance.type + "/" + pin_name +
                                        "' has unsupported direction '" + pin.direction + "'");
        }
    }
    return result;
}

} // namespace sta
