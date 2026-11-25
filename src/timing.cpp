#include "sta/timing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace sta {
namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();

struct Edge {
    std::size_t from = 0;
    std::size_t to = 0;
    std::string instance;
    std::string from_pin;
    std::string to_pin;
    const TimingArc* arc = nullptr;
    double load_f = 0.0;
};

struct Predecessor {
    int edge = -1;
    double delay_s = 0.0;
    std::string startpoint;
    int source_transition = 0;
};

struct EventState {
    double earliest_s = infinity;
    double latest_s = -infinity;
    double early_slew_s = 0.0;
    double late_slew_s = 0.0;
    bool early_slew_valid = false;
    bool late_slew_valid = false;
    Predecessor predecessor;
};

struct NodeState {
    std::string name;
    EventState rise;
    EventState fall;
};

struct Endpoint {
    std::size_t node = 0;
    std::string name;
    bool primary_output = false;
    const LibertyPin* sequential_pin = nullptr;
    std::optional<double> setup_s;
    std::optional<double> hold_s;
};

std::vector<std::string> split_pins(const std::string& value) {
    std::istringstream stream(value);
    std::vector<std::string> result;
    std::string pin;
    while (stream >> pin) result.push_back(pin);
    return result;
}

bool is_constraint_arc(const TimingArc& arc) {
    return arc.timing_type.find("setup") != std::string::npos ||
           arc.timing_type.find("hold") != std::string::npos ||
           arc.timing_type.find("recovery") != std::string::npos ||
           arc.timing_type.find("removal") != std::string::npos;
}

double lookup_constraint(const LookupTable& table, double data_slew, double clock_slew) {
    if (table.empty()) return -infinity;
    const auto axis_value = [&](const std::string& variable, bool first) {
        if (variable.find("constrained_pin_transition") != std::string::npos) return data_slew;
        if (variable.find("related_pin_transition") != std::string::npos) return clock_slew;
        return first ? data_slew : clock_slew;
    };
    return table.interpolate(axis_value(table.variable_1, true), axis_value(table.variable_2, false));
}

std::optional<double> constraint_value(const LibertyPin& pin, bool setup, double data_slew) {
    double selected = -infinity;
    for (const auto& arc : pin.timing) {
        const bool matches = setup ? arc.timing_type.find("setup") != std::string::npos
                                   : arc.timing_type.find("hold") != std::string::npos;
        if (!matches) continue;
        const auto rise = lookup_constraint(arc.rise_constraint, data_slew, 0.0);
        const auto fall = lookup_constraint(arc.fall_constraint, data_slew, 0.0);
        if (std::isfinite(rise)) selected = std::max(selected, rise);
        if (std::isfinite(fall)) selected = std::max(selected, fall);
    }
    if (!std::isfinite(selected)) return std::nullopt;
    return selected;
}

struct Graph {
    std::vector<NodeState> nodes;
    std::map<std::string, std::size_t> index;
    std::vector<Edge> edges;
    std::vector<std::vector<std::size_t>> outgoing;
    std::vector<std::vector<std::size_t>> incoming;

    std::size_t get(const std::string& bit) {
        const auto found = index.find(bit);
        if (found != index.end()) return found->second;
        const std::size_t id = nodes.size();
        index.emplace(bit, id);
        NodeState state;
        state.name = bit;
        nodes.push_back(std::move(state));
        outgoing.emplace_back();
        incoming.emplace_back();
        return id;
    }

    void add_edge(Edge edge) {
        const auto id = edges.size();
        outgoing[edge.from].push_back(id);
        incoming[edge.to].push_back(id);
        edges.push_back(std::move(edge));
    }
};

std::vector<std::string> connection(const CellInstance& instance, const std::string& pin) {
    const auto found = instance.connections.find(pin);
    return found == instance.connections.end() ? std::vector<std::string>{} : found->second;
}

const LibertyPin* liberty_pin(const LibertyCell& cell, const std::string& name) {
    const auto found = cell.pins.find(name);
    return found == cell.pins.end() ? nullptr : &found->second;
}

std::optional<double> evaluate(const LookupTable& table, double slew, double load,
                               std::vector<std::string>& diagnostics,
                               const std::string& label, std::set<std::string>& noted) {
    if (table.empty()) {
        if (noted.insert(label).second) diagnostics.push_back("Missing delay table for " + label + "; affected arc omitted");
        return std::nullopt;
    }
    try {
        return table.interpolate(slew, load);
    } catch (const std::exception& error) {
        if (noted.insert(label).second) diagnostics.push_back("Invalid timing table for " + label + ": " + error.what());
        return std::nullopt;
    }
}

} // namespace

TimingResult TimingAnalyzer::analyze(const Netlist& netlist, const LibertyLibrary& library,
                                     const TimingOptions& options) {
    TimingResult result;
    result.module_name = netlist.module_name;
    result.library_name = library.name;
    result.operating_conditions_name = library.conditions.name;
    result.process = library.conditions.process;
    result.voltage_v = library.conditions.voltage_v;
    result.temperature_c = library.conditions.temperature_c;
    if (options.input_slew_s == 0.0)
        result.diagnostics.push_back("Primary-input slew is idealized at 0 s; use --input-slew to set a nonzero slew");

    const auto resolution = resolve(netlist, library);
    result.diagnostics.insert(result.diagnostics.end(), resolution.errors.begin(), resolution.errors.end());
    Graph graph;
    std::map<std::string, double> loads;
    std::map<std::string, std::string> input_starts;
    std::vector<Endpoint> endpoints;
    std::set<std::string> noted;
    std::map<std::string, std::string> instance_types;

    for (const auto& [name, port] : netlist.ports) {
        for (std::size_t bit = 0; bit < port.bits.size(); ++bit) {
            const auto& id = port.bits[bit];
            const auto node = graph.get(id);
            const std::string label = port.bits.size() == 1 ? name : name + "[" + std::to_string(bit) + "]";
            if (port.direction == "input" || port.direction == "inout")
                input_starts.try_emplace(id, label);
            if (port.direction == "output" || port.direction == "inout") {
                endpoints.push_back(Endpoint{node, label, true, nullptr, std::nullopt, std::nullopt});
                loads[id] += options.output_load_f;
            }
        }
    }

    for (const auto& instance : netlist.cells) {
        instance_types.emplace(instance.name, instance.type);
        const LibertyCell* cell = library.find_cell(instance.type);
        if (!cell) continue;
        for (const auto& [pin_name, bits] : instance.connections) {
            const auto* pin = liberty_pin(*cell, pin_name);
            if (!pin || bits.empty()) continue;
            for (const auto& bit : bits) graph.get(bit);
            if (pin->direction == "input" || pin->direction == "inout")
                for (const auto& bit : bits) loads[bit] += pin->capacitance_f;
        }
    }

    for (const auto& instance : netlist.cells) {
        const LibertyCell* cell = library.find_cell(instance.type);
        if (!cell) continue;
        if (cell->sequential) {
            bool has_clock_to_q = false;
            for (const auto& [unused_name, pin] : cell->pins) {
                (void)unused_name;
                for (const auto& arc : pin.timing)
                    if (arc.timing_type.find("rising_edge") != std::string::npos ||
                        arc.timing_type.find("falling_edge") != std::string::npos)
                        has_clock_to_q = true;
            }
            for (const auto& [pin_name, pin] : cell->pins) {
                const auto bits = connection(instance, pin_name);
                if (bits.empty()) continue;
                if (pin.direction == "output") {
                    for (const auto& bit : bits) {
                        const auto node = graph.get(bit);
                        const std::string start = instance.name + "/" + pin_name;
                        auto& state = graph.nodes[node];
                        for (EventState* event : {&state.rise, &state.fall}) {
                            if (event->latest_s < 0.0) {
                                event->earliest_s = 0.0;
                                event->latest_s = 0.0;
                                event->early_slew_s = options.input_slew_s;
                                event->late_slew_s = options.input_slew_s;
                                event->early_slew_valid = event->late_slew_valid = true;
                                event->predecessor.startpoint = start;
                            }
                        }
                    }
                    continue;
                }
                if (pin.direction != "input") continue;
                bool is_setup_hold_pin = false;
                for (const auto& arc : pin.timing) {
                    if (is_constraint_arc(arc)) is_setup_hold_pin = true;
                    if (arc.timing_type.find("rising_edge") != std::string::npos ||
                        arc.timing_type.find("falling_edge") != std::string::npos) has_clock_to_q = true;
                }
                if (is_setup_hold_pin) {
                    for (const auto& bit : bits) {
                        Endpoint endpoint;
                        endpoint.node = graph.get(bit);
                        endpoint.name = instance.name + "/" + pin_name;
                        endpoint.primary_output = false;
                        endpoint.sequential_pin = &pin;
                        endpoints.push_back(std::move(endpoint));
                    }
                }
            }
            if (!has_clock_to_q && noted.insert(instance.type + ":clktoq").second)
                result.diagnostics.push_back("Sequential cell '" + instance.type +
                    "' is treated as an ideal launch point; clock-to-Q delay and clock latency are not modeled");
            continue;
        }

        for (const auto& [to_pin_name, to_pin] : cell->pins) {
            if (to_pin.direction != "output") continue;
            const auto to_bits = connection(instance, to_pin_name);
            if (to_bits.empty()) continue;
            for (const auto& arc : to_pin.timing) {
                if (is_constraint_arc(arc)) continue;
                if (arc.timing_type.find("combinational") == std::string::npos &&
                    !arc.timing_type.empty()) continue;
                if (arc.timing_type.empty())
                    result.diagnostics.push_back("Arc " + instance.name + "/" + to_pin_name +
                        " has no timing_type; treated as combinational");
                if (arc.timing_sense != "positive_unate" && arc.timing_sense != "negative_unate" &&
                    arc.timing_sense != "non_unate" &&
                    noted.insert(instance.name + "/" + to_pin_name + ":sense").second)
                    result.diagnostics.push_back("Unspecified/unknown timing sense on " + instance.name + "/" +
                        to_pin_name + "; both output transition polarities are propagated");
                if (arc.related_pin.empty()) {
                    result.diagnostics.push_back("Timing arc on '" + instance.name + "/" + to_pin_name +
                                                 "' has no related_pin and was omitted");
                    continue;
                }
                for (const auto& from_pin_name : split_pins(arc.related_pin)) {
                    const auto* from_pin = liberty_pin(*cell, from_pin_name);
                    const auto from_bits = connection(instance, from_pin_name);
                    if (!from_pin || from_pin->direction != "input" || from_bits.empty()) continue;
                    for (const auto& from_bit : from_bits) for (const auto& to_bit : to_bits) {
                        graph.add_edge(Edge{graph.get(from_bit), graph.get(to_bit), instance.name,
                                            from_pin_name, to_pin_name, &arc, loads[to_bit]});
                    }
                }
            }
        }
    }

    for (const auto& [id, label] : input_starts) {
        if (id.rfind("const:", 0) == 0) continue;
        auto& state = graph.nodes[graph.get(id)];
        for (EventState* event : {&state.rise, &state.fall}) {
            if (event->latest_s < 0.0) {
                event->earliest_s = 0.0;
                event->latest_s = 0.0;
                event->early_slew_s = options.input_slew_s;
                event->late_slew_s = options.input_slew_s;
                event->early_slew_valid = event->late_slew_valid = true;
                event->predecessor.startpoint = label;
            }
        }
    }
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].name.rfind("const:", 0) != 0) continue;
        for (EventState* event : {&graph.nodes[i].rise, &graph.nodes[i].fall}) {
            event->earliest_s = event->latest_s = 0.0;
            event->early_slew_s = event->late_slew_s = options.input_slew_s;
            event->early_slew_valid = event->late_slew_valid = true;
            event->predecessor.startpoint = graph.nodes[i].name;
        }
    }

    std::vector<std::size_t> indegree(graph.nodes.size());
    std::deque<std::size_t> ready;
    for (std::size_t node = 0; node < graph.nodes.size(); ++node) {
        indegree[node] = graph.incoming[node].size();
        if (indegree[node] == 0) ready.push_back(node);
    }
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const auto node = ready.front();
        ready.pop_front();
        order.push_back(node);
        for (const auto edge_id : graph.outgoing[node])
            if (--indegree[graph.edges[edge_id].to] == 0) ready.push_back(graph.edges[edge_id].to);
    }
    std::vector<int> discovery(graph.nodes.size(), -1);
    std::vector<int> low_link(graph.nodes.size(), -1);
    std::vector<std::size_t> stack;
    std::vector<bool> on_stack(graph.nodes.size(), false);
    int next_discovery = 0;
    std::function<void(std::size_t)> visit = [&](std::size_t node) {
        discovery[node] = low_link[node] = next_discovery++;
        stack.push_back(node);
        on_stack[node] = true;
        for (const auto edge_id : graph.outgoing[node]) {
            const auto target = graph.edges[edge_id].to;
            if (discovery[target] == -1) {
                visit(target);
                low_link[node] = std::min(low_link[node], low_link[target]);
            } else if (on_stack[target]) {
                low_link[node] = std::min(low_link[node], discovery[target]);
            }
        }
        if (low_link[node] != discovery[node]) return;
        std::size_t component_size = 0;
        std::size_t member = 0;
        do {
            member = stack.back();
            stack.pop_back();
            on_stack[member] = false;
            ++component_size;
        } while (member != node);
        const bool self_loop = std::any_of(graph.outgoing[node].begin(), graph.outgoing[node].end(),
            [&](std::size_t edge_id) { return graph.edges[edge_id].to == node; });
        if (component_size > 1 || self_loop) ++result.combinational_loops;
    };
    for (std::size_t node = 0; node < graph.nodes.size(); ++node)
        if (discovery[node] == -1) visit(node);
    if (result.combinational_loops != 0) {
        result.diagnostics.push_back("Detected " + std::to_string(result.combinational_loops) +
            " combinational timing loop(s); cyclic and dependent timing points were not propagated");
    }

    const auto outputs_for = [](const TimingArc& arc, int input_transition) {
        std::vector<int> transitions;
        if (arc.timing_sense == "positive_unate") transitions.push_back(input_transition);
        else if (arc.timing_sense == "negative_unate") transitions.push_back(1 - input_transition);
        else {
            transitions.push_back(0);
            transitions.push_back(1);
        }
        return transitions;
    };

    for (const auto node : order) {
        for (const auto edge_id : graph.outgoing[node]) {
            const Edge& edge = graph.edges[edge_id];
            const std::string label = edge.instance + "/" + edge.from_pin + "->" + edge.to_pin;
            const auto* edge_cell = library.find_cell(instance_types.at(edge.instance));
            const auto* to_pin = edge_cell ? liberty_pin(*edge_cell, edge.to_pin) : nullptr;
            if (to_pin && to_pin->max_capacitance_f && edge.load_f > *to_pin->max_capacitance_f &&
                noted.insert(label + ":cap").second)
                result.diagnostics.push_back("Output load exceeds max_capacitance at " + edge.instance + "/" + edge.to_pin);
            const auto& source = graph.nodes[edge.from];
            auto& destination = graph.nodes[edge.to];
            for (int input_transition = 0; input_transition < 2; ++input_transition) {
                const EventState& input = input_transition == 0 ? source.rise : source.fall;
                const auto targets = outputs_for(*edge.arc, input_transition);
                for (const int output_transition : targets) {
                    const LookupTable& delay_table = output_transition == 0 ? edge.arc->cell_rise : edge.arc->cell_fall;
                    const LookupTable& slew_table = output_transition == 0 ? edge.arc->rise_transition : edge.arc->fall_transition;
                    EventState& output = output_transition == 0 ? destination.rise : destination.fall;
                    if (input.early_slew_valid && std::isfinite(input.earliest_s)) {
                        const auto delay = evaluate(delay_table, input.early_slew_s, edge.load_f, result.diagnostics, label, noted);
                        if (delay) {
                            const double candidate = input.earliest_s + *delay;
                            if (candidate < output.earliest_s) {
                                output.earliest_s = candidate;
                                if (!slew_table.empty()) {
                                    output.early_slew_s = slew_table.interpolate(input.early_slew_s, edge.load_f);
                                    output.early_slew_valid = true;
                                } else {
                                    output.early_slew_valid = false;
                                }
                            }
                        }
                    }
                    if (input.late_slew_valid && std::isfinite(input.latest_s)) {
                        const auto delay = evaluate(delay_table, input.late_slew_s, edge.load_f, result.diagnostics, label, noted);
                        if (delay) {
                            const double candidate = input.latest_s + *delay;
                            if (candidate > output.latest_s) {
                                output.latest_s = candidate;
                                output.predecessor = Predecessor{static_cast<int>(edge_id), *delay,
                                    input.predecessor.startpoint, input_transition};
                                if (!slew_table.empty()) {
                                    output.late_slew_s = slew_table.interpolate(input.late_slew_s, edge.load_f);
                                    output.late_slew_valid = true;
                                    if (to_pin && to_pin->max_transition_s &&
                                        output.late_slew_s > *to_pin->max_transition_s &&
                                        noted.insert(label + ":transition").second)
                                        result.diagnostics.push_back("Output transition exceeds max_transition at " +
                                                                     edge.instance + "/" + edge.to_pin);
                                } else {
                                    output.late_slew_valid = false;
                                    if (noted.insert(label + ":slew").second)
                                        result.diagnostics.push_back("Missing output transition table for " + label +
                                            "; downstream timing arcs requiring slew cannot be evaluated");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto& endpoint : endpoints) {
        if (!endpoint.sequential_pin) continue;
        const auto& state = graph.nodes[endpoint.node];
        const double slew = std::max(state.rise.late_slew_valid ? state.rise.late_slew_s : options.input_slew_s,
                                     state.fall.late_slew_valid ? state.fall.late_slew_s : options.input_slew_s);
        endpoint.setup_s = constraint_value(*endpoint.sequential_pin, true, slew);
        endpoint.hold_s = constraint_value(*endpoint.sequential_pin, false, slew);
    }

    std::vector<std::array<double, 2>> required(graph.nodes.size(),
        std::array<double, 2>{infinity, infinity});
    for (const auto& endpoint : endpoints) {
        if (options.clock_period_s) {
            double req = *options.clock_period_s;
            if (endpoint.setup_s) req -= *endpoint.setup_s;
            for (int transition = 0; transition < 2; ++transition)
                required[endpoint.node][static_cast<std::size_t>(transition)] =
                    std::min(required[endpoint.node][static_cast<std::size_t>(transition)], req);
        }
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto node = *it;
        for (const auto edge_id : graph.outgoing[node]) {
            const Edge& edge = graph.edges[edge_id];
            for (int input_transition = 0; input_transition < 2; ++input_transition) {
                const EventState& source = input_transition == 0 ? graph.nodes[edge.from].rise : graph.nodes[edge.from].fall;
                if (!source.late_slew_valid) continue;
                for (const int output_transition : outputs_for(*edge.arc, input_transition)) {
                    const auto& table = output_transition == 0 ? edge.arc->cell_rise : edge.arc->cell_fall;
                    const auto delay = evaluate(table, source.late_slew_s, edge.load_f, result.diagnostics,
                                                edge.instance + "/" + edge.from_pin + "->" + edge.to_pin, noted);
                    if (!delay) continue;
                    const auto endpoint_req = required[edge.to][static_cast<std::size_t>(output_transition)];
                    if (std::isfinite(endpoint_req))
                        required[edge.from][static_cast<std::size_t>(input_transition)] =
                            std::min(required[edge.from][static_cast<std::size_t>(input_transition)], endpoint_req - *delay);
                }
            }
        }
    }

    for (const auto& endpoint : endpoints) {
        const auto& state = graph.nodes[endpoint.node];
        const EventState& event = state.rise.latest_s >= state.fall.latest_s ? state.rise : state.fall;
        if (!std::isfinite(event.latest_s)) continue;
        TimingPath path;
        path.startpoint = event.predecessor.startpoint.empty() ? graph.nodes[endpoint.node].name :
                          event.predecessor.startpoint;
        path.endpoint = endpoint.name;
        path.arrival_s = event.latest_s;
        if (options.clock_period_s) {
            if (endpoint.setup_s) {
                path.required_s = *options.clock_period_s - *endpoint.setup_s;
                path.slack_s = *path.required_s - path.arrival_s;
            } else if (endpoint.primary_output) {
                path.required_s = *options.clock_period_s;
                path.slack_s = *path.required_s - path.arrival_s;
            } else {
                result.diagnostics.push_back("No characterized setup constraint for endpoint " + endpoint.name);
            }
        }
        if (endpoint.hold_s) {
            const double earliest = std::min(state.rise.earliest_s, state.fall.earliest_s);
            if (std::isfinite(earliest)) path.hold_slack_s = earliest - *endpoint.hold_s;
        }
        std::size_t current = endpoint.node;
        std::size_t guard = 0;
        std::vector<PathStep> reverse_steps;
        int transition = event.latest_s >= state.fall.latest_s ? 0 : 1;
        EventState* cursor = const_cast<EventState*>(&event);
        while (cursor->predecessor.edge >= 0 && guard++ <= graph.nodes.size()) {
            const auto& edge = graph.edges[static_cast<std::size_t>(cursor->predecessor.edge)];
            reverse_steps.push_back(PathStep{edge.instance, edge.from_pin, edge.to_pin,
                                             cursor->predecessor.delay_s});
            current = edge.from;
            const auto& predecessor_node = graph.nodes[current];
            transition = cursor->predecessor.source_transition;
            cursor = transition == 0 ? const_cast<EventState*>(&predecessor_node.rise) :
                                       const_cast<EventState*>(&predecessor_node.fall);
        }
        std::reverse(reverse_steps.begin(), reverse_steps.end());
        path.steps = std::move(reverse_steps);
        result.paths.push_back(std::move(path));
    }
    if (!result.paths.empty()) {
        result.critical_path = result.paths.front();
        for (const auto& path : result.paths) {
            if ((path.slack_s && (!result.critical_path->slack_s ||
                                  *path.slack_s < *result.critical_path->slack_s)) ||
                (!path.slack_s && !result.critical_path->slack_s &&
                 path.arrival_s > result.critical_path->arrival_s))
                result.critical_path = path;
        }
    }
    return result;
}

} // namespace sta
