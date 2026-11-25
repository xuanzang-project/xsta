#include "sta/report.hpp"

#include <iomanip>
#include <ostream>

namespace sta {
namespace {

double ps(double seconds) {
    return seconds * 1.0e12;
}

void write_path(const TimingPath& path, std::ostream& out, bool critical) {
    if (critical) out << "Critical path\n";
    else out << "Path " << path.startpoint << " -> " << path.endpoint << "\n";
    out << "  Startpoint: " << path.startpoint << '\n'
        << "  Endpoint:   " << path.endpoint << '\n'
        << "  Data arrival: " << std::fixed << std::setprecision(3) << ps(path.arrival_s) << " ps\n";
    if (path.required_s)
        out << "  Required:     " << std::fixed << std::setprecision(3) << ps(*path.required_s) << " ps\n";
    if (path.slack_s)
        out << "  Setup slack:  " << std::fixed << std::setprecision(3) << ps(*path.slack_s) << " ps\n";
    if (path.hold_slack_s)
        out << "  Hold slack:   " << std::fixed << std::setprecision(3) << ps(*path.hold_slack_s) << " ps\n";
    if (!path.steps.empty()) {
        out << "  Cell delays:\n";
        for (const auto& step : path.steps)
            out << "    " << step.instance << " (" << step.from_pin << " -> " << step.to_pin
                << "): " << std::fixed << std::setprecision(3) << ps(step.delay_s) << " ps\n";
    } else {
        out << "  Cell delays: none (direct input/constant/ideal sequential launch)\n";
    }
}

} // namespace

void write_report(const TimingResult& result, std::ostream& out) {
    out << "XSTA timing report\n"
        << "===================\n"
        << "Design:  " << result.module_name << '\n'
        << "Library: " << result.library_name << '\n'
        << "Operating conditions: " << (result.operating_conditions_name.empty() ? "unspecified" :
                                          result.operating_conditions_name) << '\n';
    if (result.process) out << "Process: " << *result.process << '\n';
    if (result.voltage_v) out << "Voltage: " << *result.voltage_v << " V\n";
    if (result.temperature_c) out << "Temperature: " << *result.temperature_c << " C\n";
    out
        << "Combinational timing loops detected: " << result.combinational_loops << '\n'
        << "Analyzed endpoints: " << result.paths.size() << "\n\n";
    if (result.critical_path) write_path(*result.critical_path, out, true);
    else out << "No reachable timing endpoints were found.\n";
    out << "\nEndpoint paths\n--------------\n";
    for (const auto& path : result.paths) write_path(path, out, false);
    if (!result.diagnostics.empty()) {
        out << "\nDiagnostics\n-----------\n";
        for (const auto& diagnostic : result.diagnostics) out << "- " << diagnostic << '\n';
    }
}

} // namespace sta
