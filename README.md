# PRORMRXCN-STA

An independent, modular C++20 static timing analysis prototype. It reads a
Yosys JSON gate-level netlist and one or more NLDM Liberty corners, resolves
cell/pin connectivity, builds pin-to-pin timing arcs, propagates rise/fall
arrival and slew, and reports endpoint paths and slack. The analyzer implements
its own Liberty/JSON parsing, timing graph, interpolation, propagation, and
reporting; it does not call or link against Yosys, ABC, or another STA engine.

This is an early STA engine, not a replacement for signoff STA. It has not
been cross-validated against OpenSTA; see the current limitations below.

## Build and test

With CMake:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The executable uses only the C++ standard library at runtime. GoogleTest is an
optional test-only dependency. If GoogleTest is available, CMake builds the
GoogleTest suite. Otherwise the standalone
smoke-test runner works with:

```sh
make -j
make test
```

## Input and command line

The first netlist format is **Yosys JSON**, not source Verilog. The analyzer
does not launch Yosys or synthesize the design; use any front end that emits
compatible JSON, or synthesize Verilog externally first, then run:

```sh
./xsta \
  --netlist build/design.json \
  --liberty corner.lib \
  --clock-period 10ns \
  --input-slew 100ps \
  --output-load 5fF \
  --report timing_report.txt
```

Time values accept `s`, `ms`, `us`, `ns`, `ps`, or `fs`; capacitance values
accept `F`, `mF`, `uF`, `nF`, `pF`, or `fF`. Bare values are interpreted in SI
units. `--clock-period` is optional; when omitted, arrival times are reported
without setup slack. Primary input arrival time and clock latency are ideal
(zero); `--input-slew` sets primary-input slew, which defaults to zero.

Corners are kept independent rather than merged. Repeating `--liberty`
produces a separate analysis/report section for each corner:

```sh
./xsta --netlist build/design.json \
  --liberty slow.lib --liberty typical.lib --liberty fast.lib \
  --clock-period 10ns --report corners.txt
```

The Liberty parser extracts each library name, operating conditions, time and
capacitance units, cell area, pin direction/capacitance/limits, combinational
timing arcs, timing sense/type/related pin, and NLDM delay, transition, setup,
and hold tables. Tables are normalized to seconds and farads.

## 8-bit ALU: Yosys + Sky130

The included combinational ALU is `examples/alu8.v`. With the supplied
OSS-CAD-Suite Yosys and Sky130 PDK:

```sh
source /opt/oss-cad-suite/environment
LIB=/home/prormrxcn/pdk/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib
mkdir -p build
yosys -p "read_verilog examples/alu8.v; hierarchy -top alu8; synth -top alu8 -noabc; abc -liberty $LIB; clean; write_json build/alu8.json"
./xsta --netlist build/alu8.json --liberty "$LIB" \
  --clock-period 10ns --input-slew 100ps --output-load 5fF \
  --report build/alu8_timing.txt
```

The resulting report lists the critical input-to-output path, cell-by-cell
delays, endpoint arrival/required times, and setup slack. Different Liberty
files can be passed independently to compare corners; the current engine does
not combine their constraints or perform simultaneous multi-corner checks.

## Model and current assumptions

- NLDM lookup uses linear interpolation in one dimension and bilinear
  interpolation in two dimensions.
- Values outside a table's index range are **clamped to the nearest boundary
  value**. This explicit policy avoids silently extrapolating beyond
  characterization.
- Input pin capacitances and the requested primary-output load are summed at
  the driven net.
- Timing arcs marked positive-unate and negative-unate preserve/invert edge
  polarity; non-unate or unspecified senses conservatively propagate both
  polarities.
- Sequential Q pins are ideal launch points; characterized clock-to-Q delay,
  clock latency/skew, generated clocks, input/output delays, and clock
  uncertainty are not yet modeled. Setup/hold constraints found on sequential
  data pins are evaluated against an ideal capture clock.
- Combinational loops are detected. Cyclic timing points are not propagated,
  and the report includes a diagnostic.
- Missing cells, pins, delay tables, transition tables, and incompatible pin
  directions are reported. Missing delay characterization is never replaced
  with a fabricated constant; affected arcs/continuations are omitted.
- Yosys JSON module selection uses the top attribute, or the sole module when
  there is exactly one. Vector cell pins are currently unsupported; bus
  signals that Yosys has bit-blasted into scalar cell pins are supported.

## Modules

- `liberty`: Liberty syntax, units, cell/pin models, NLDM table interpolation.
- `json` / `netlist`: Yosys JSON parsing and Liberty cell/pin resolution.
- `timing`: timing graph construction, transition-aware propagation, loop
  detection, endpoint constraints, and critical-path extraction.
- `report`: human-readable output independent of parsing and analysis.
