#pragma once

#include "sta/netlist.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sta {

struct TimingOptions {
    std::optional<double> clock_period_s;
    double input_slew_s = 0.0;
    double output_load_f = 0.0;
};

struct PathStep {
    std::string instance;
    std::string from_pin;
    std::string to_pin;
    double delay_s = 0.0;
};

struct TimingPath {
    std::string startpoint;
    std::string endpoint;
    double arrival_s = 0.0;
    std::optional<double> required_s;
    std::optional<double> slack_s;
    std::optional<double> hold_slack_s;
    std::vector<PathStep> steps;
};

struct TimingResult {
    std::string module_name;
    std::string library_name;
    std::string operating_conditions_name;
    std::optional<double> process;
    std::optional<double> voltage_v;
    std::optional<double> temperature_c;
    std::vector<std::string> diagnostics;
    std::vector<TimingPath> paths;
    std::optional<TimingPath> critical_path;
    std::size_t combinational_loops = 0;
};

class TimingAnalyzer {
public:
    static TimingResult analyze(const Netlist& netlist,
                                const LibertyLibrary& library,
                                const TimingOptions& options);
};

} // namespace sta
