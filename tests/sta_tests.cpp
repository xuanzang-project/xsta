#include "sta/liberty.hpp"
#include "sta/netlist.hpp"
#include "sta/timing.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <string>

namespace {

std::string temp_file(const std::string& name, const std::string& contents) {
    const auto path = std::string("/tmp/prormrxcn_sta_") + name;
    std::ofstream output(path);
    output << contents;
    return path;
}

std::string sample_liberty() {
    return R"(
library (sample) {
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
)";
}

std::string sample_netlist(const std::string& output_bit = "3") {
    return std::string(R"({
 "modules":{"top":{
   "attributes":{"top":"1"},
   "ports":{"A":{"direction":"input","bits":[2]},"Y":{"direction":"output","bits":[)") +
        output_bit + R"(]}},
   "cells":{"u_inv":{"type":"INV","connections":{"A":[2],"Y":[)" + output_bit +
        R"(]},"port_directions":{"A":"input","Y":"output"}}}
 }}
})";
}

} // namespace

TEST(LookupTable, BilinearAndClamp) {
    sta::LookupTable table;
    table.index_1 = {0.0, 2.0};
    table.index_2 = {0.0, 2.0};
    table.values = {{0.0, 20.0}, {20.0, 40.0}};
    EXPECT_DOUBLE_EQ(table.interpolate(1.0, 1.0), 20.0);
    EXPECT_DOUBLE_EQ(table.interpolate(10.0, 10.0), 40.0);
}

TEST(Liberty, ParsesUnitsPinsAndTimingTables) {
    const auto library = sta::LibertyLibrary::parse_file(temp_file("sample.lib", sample_liberty()));
    ASSERT_EQ(library.name, "sample");
    EXPECT_NEAR(library.time_scale_s, 1e-9, 1e-20);
    EXPECT_NEAR(library.capacitance_scale_f, 1e-12, 1e-24);
    ASSERT_NE(library.find_cell("INV"), nullptr);
    const auto& arc = library.find_cell("INV")->pins.at("Y").timing.front();
    EXPECT_EQ(arc.timing_sense, "negative_unate");
    EXPECT_NEAR(arc.cell_fall.interpolate(0.1e-9, 0.01e-12), 2e-9, 1e-20);
}

TEST(StaticTiming, ResolvesAndReportsCriticalPath) {
    const auto library = sta::LibertyLibrary::parse_file(temp_file("sample.lib", sample_liberty()));
    const auto netlist = sta::Netlist::parse_yosys_json(
        temp_file("sample.json", sample_netlist()));
    EXPECT_TRUE(sta::resolve(netlist, library).errors.empty());
    sta::TimingOptions options;
    options.input_slew_s = 0.1e-9;
    options.output_load_f = 0.01e-12;
    options.clock_period_s = 5e-9;
    const auto result = sta::TimingAnalyzer::analyze(netlist, library, options);
    ASSERT_TRUE(result.critical_path.has_value());
    EXPECT_EQ(result.critical_path->startpoint, "A");
    EXPECT_EQ(result.critical_path->endpoint, "Y");
    EXPECT_NEAR(result.critical_path->arrival_s, 2e-9, 1e-18);
    ASSERT_TRUE(result.critical_path->slack_s.has_value());
    EXPECT_NEAR(*result.critical_path->slack_s, 3e-9, 1e-18);
}

