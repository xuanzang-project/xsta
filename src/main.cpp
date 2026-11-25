#include "sta/liberty.hpp"
#include "sta/netlist.hpp"
#include "sta/report.hpp"
#include "sta/timing.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

double quantity(const std::string& value, bool time) {
    std::size_t end = 0;
    const double multiplier = std::stod(value, &end);
    std::string unit = value.substr(end);
    if (unit.empty()) return multiplier;
    if (time) {
        if (unit == "s") return multiplier;
        if (unit == "ms") return multiplier * 1e-3;
        if (unit == "us") return multiplier * 1e-6;
        if (unit == "ns") return multiplier * 1e-9;
        if (unit == "ps") return multiplier * 1e-12;
        if (unit == "fs") return multiplier * 1e-15;
    } else {
        if (unit == "fF" || unit == "ff") return multiplier * 1e-15;
        if (unit == "pF" || unit == "pf") return multiplier * 1e-12;
        if (unit == "nF" || unit == "nf") return multiplier * 1e-9;
        if (unit == "uF" || unit == "uf") return multiplier * 1e-6;
        if (unit == "F" || unit == "f") return multiplier;
    }
    throw std::runtime_error("Unsupported quantity unit: " + unit);
}

void usage(std::ostream& out) {
    out << "Usage: xsta --netlist design.json --liberty corner.lib [options]\n"
        << "Options:\n"
        << "  --liberty FILE       Liberty corner; repeat for independent corner reports\n"
        << "  --report FILE        Write report to FILE (default: standard output)\n"
        << "  --clock-period TIME  Required output time, e.g. 10ns\n"
        << "  --input-slew TIME    Ideal primary-input slew (default: 0s)\n"
        << "  --output-load CAP    Additional primary-output load, e.g. 2fF\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string netlist_path;
        std::string report_path;
        std::vector<std::string> liberty_paths;
        sta::TimingOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("Missing value after " + arg);
                return argv[++i];
            };
            if (arg == "--help" || arg == "-h") {
                usage(std::cout);
                return 0;
            } else if (arg == "--netlist") netlist_path = next();
            else if (arg == "--liberty") liberty_paths.push_back(next());
            else if (arg == "--report") report_path = next();
            else if (arg == "--clock-period") options.clock_period_s = quantity(next(), true);
            else if (arg == "--input-slew") options.input_slew_s = quantity(next(), true);
            else if (arg == "--output-load") options.output_load_f = quantity(next(), false);
            else throw std::runtime_error("Unknown option: " + arg);
        }
        if (netlist_path.empty() || liberty_paths.empty()) {
            usage(std::cerr);
            return 2;
        }
        const auto netlist = sta::Netlist::parse_yosys_json(netlist_path);
        std::ofstream report_file;
        std::ostream* output = &std::cout;
        if (!report_path.empty()) {
            report_file.open(report_path);
            if (!report_file) throw std::runtime_error("Cannot write report file: " + report_path);
            output = &report_file;
        }
        bool resolution_failed = false;
        for (std::size_t i = 0; i < liberty_paths.size(); ++i) {
            const auto library = sta::LibertyLibrary::parse_file(liberty_paths[i]);
            const auto result = sta::TimingAnalyzer::analyze(netlist, library, options);
            if (i != 0) *output << "\n\n";
            sta::write_report(result, *output);
            resolution_failed |= !sta::resolve(netlist, library).errors.empty();
        }
        return resolution_failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "xsta: " << error.what() << '\n';
        return 2;
    }
}
