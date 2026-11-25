#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sta {

struct LookupTable {
    std::string template_name;
    std::string variable_1;
    std::string variable_2;
    std::vector<double> index_1;
    std::vector<double> index_2;
    std::vector<std::vector<double>> values;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] double interpolate(double x, double y) const;
};

struct TimingArc {
    std::string related_pin;
    std::string timing_sense;
    std::string timing_type;
    LookupTable cell_rise;
    LookupTable cell_fall;
    LookupTable rise_transition;
    LookupTable fall_transition;
    LookupTable rise_constraint;
    LookupTable fall_constraint;
};

struct LibertyPin {
    std::string name;
    std::string direction;
    double capacitance_f = 0.0;
    std::optional<double> max_capacitance_f;
    std::optional<double> max_transition_s;
    std::vector<TimingArc> timing;
};

struct LibertyCell {
    std::string name;
    std::optional<double> area;
    std::map<std::string, LibertyPin> pins;
    bool sequential = false;
};

struct OperatingConditions {
    std::string name;
    std::optional<double> process;
    std::optional<double> voltage_v;
    std::optional<double> temperature_c;
};

struct LibertyLibrary {
    std::string name;
    std::string time_unit;
    std::string capacitance_unit;
    double time_scale_s = 1.0;
    double capacitance_scale_f = 1.0;
    OperatingConditions conditions;
    std::map<std::string, LibertyCell> cells;

    static LibertyLibrary parse_file(const std::string& path);
    [[nodiscard]] const LibertyCell* find_cell(const std::string& cell_name) const;
};

} // namespace sta
