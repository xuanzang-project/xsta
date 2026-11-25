#include "sta/liberty.hpp"
#include "sta/netlist.hpp"
#include "sta/timing.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream output(path);
    output << contents;
}

} // namespace

int main() {
    try {
        const std::string liberty_path = "/tmp/prormrxcn_sta_smoke.lib";
        const std::string netlist_path = "/tmp/prormrxcn_sta_smoke.json";
        write_file(liberty_path, R"(
library (smoke) {
  time_unit : "1ns";
  capacitive_load_unit (1, pf);
  lu_table_template (delay_2d) {
    variable_1 : input_net_transition;
    variable_2 : total_output_net_capacitance;
    index_1 ("0.1, 0.2");
    index_2 ("0.01, 0.02");
  }
  cell (INV) {
    pin (A) { direction : input; capacitance : 0.01; }
    pin (Y) {
      direction : output;
      timing () {
        related_pin : "A";
        timing_sense : negative_unate;
        timing_type : combinational;
        cell_rise (delay_2d) { values ("1, 3", "3, 5"); }
        cell_fall (delay_2d) { values ("2, 4", "4, 6"); }
        rise_transition (delay_2d) { values ("0.1, 0.1", "0.2, 0.2"); }
        fall_transition (delay_2d) { values ("0.1, 0.1", "0.2, 0.2"); }
      }
    }
  }
}
)");
        write_file(netlist_path, R"({
  "modules": {
    "top": {
      "attributes": {"top": "1"},
      "ports": {
        "A": {"direction": "input", "bits": [2]},
        "Y": {"direction": "output", "bits": [3]}
      },
      "cells": {
        "u_inv": {
          "type": "INV",
          "connections": {"A": [2], "Y": [3]},
          "port_directions": {"A": "input", "Y": "output"}
        }
      }
    }
  }
})");

        sta::LookupTable table;
        table.index_1 = {0.0, 2.0};
        table.index_2 = {0.0, 2.0};
        table.values = {{0.0, 20.0}, {20.0, 40.0}};
        require(std::abs(table.interpolate(1.0, 1.0) - 20.0) < 1e-12,
                "bilinear interpolation failed");
        require(std::abs(table.interpolate(10.0, 10.0) - 40.0) < 1e-12,
                "out-of-range clamp policy failed");

        const auto library = sta::LibertyLibrary::parse_file(liberty_path);
        require(library.name == "smoke", "Liberty library name was not parsed");
        require(std::abs(library.time_scale_s - 1e-9) < 1e-20, "time unit scale is wrong");
        require(std::abs(library.capacitance_scale_f - 1e-12) < 1e-24, "capacitance unit scale is wrong");
        const auto* inverter = library.find_cell("INV");
        require(inverter && inverter->pins.at("Y").timing.size() == 1, "timing arc was not parsed");
        require(std::abs(inverter->pins.at("Y").timing.front().cell_fall.index_2.front() - 1e-14) < 1e-27,
                "capacitance table index was not converted to SI");

        const auto netlist = sta::Netlist::parse_yosys_json(netlist_path);
        require(sta::resolve(netlist, library).errors.empty(), "cell resolution failed");
        sta::TimingOptions options;
        options.input_slew_s = 0.1e-9;
        options.output_load_f = 0.01e-12;
        const auto result = sta::TimingAnalyzer::analyze(netlist, library, options);
        require(result.critical_path.has_value(), "critical path was not found");
        require(result.critical_path->steps.size() == 1, "expected one cell arc in path");
        require(std::abs(result.critical_path->arrival_s - 2e-9) < 1e-18,
                "cell delay was not interpolated/scaled correctly");
        require(result.critical_path->startpoint == "A" && result.critical_path->endpoint == "Y",
                "path endpoints are incorrect");

        auto unresolved = netlist;
        unresolved.cells.front().type = "MISSING_CELL";
        require(!sta::resolve(unresolved, library).errors.empty(), "missing-cell diagnostic was not produced");

        sta::Netlist loop;
        loop.module_name = "loop";
        loop.ports.emplace("Y", sta::NetPort{"Y", "output", {"net:2"}});
        loop.cells.push_back(sta::CellInstance{"u1", "INV", {{"A", {"net:3"}}, {"Y", {"net:2"}}},
                                               {{"A", "input"}, {"Y", "output"}}});
        loop.cells.push_back(sta::CellInstance{"u2", "INV", {{"A", {"net:2"}}, {"Y", {"net:3"}}},
                                               {{"A", "input"}, {"Y", "output"}}});
        const auto loop_result = sta::TimingAnalyzer::analyze(loop, library, options);
        require(loop_result.combinational_loops != 0, "combinational loop was not detected");

        write_file("/tmp/prormrxcn_sta_invalid.json", "{not-json");
        bool invalid_rejected = false;
        try {
            (void)sta::Netlist::parse_yosys_json("/tmp/prormrxcn_sta_invalid.json");
        } catch (const std::exception&) {
            invalid_rejected = true;
        }
        require(invalid_rejected, "invalid Yosys JSON was accepted");
        std::cout << "STA smoke tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "STA smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
