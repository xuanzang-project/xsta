CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
SOURCES = src/liberty.cpp src/json.cpp src/netlist.cpp src/timing.cpp src/report.cpp

.PHONY: all clean test
all: xsta

xsta: $(SOURCES) src/main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

sta-smoke-test: $(SOURCES) tests/smoke_test.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: sta-smoke-test
	./sta-smoke-test

clean:
	rm -f xsta prormrxcn-sta sta-smoke-test
