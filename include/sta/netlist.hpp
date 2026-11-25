#pragma once

#include "sta/json.hpp"
#include "sta/liberty.hpp"

#include <map>
#include <string>
#include <vector>

namespace sta {

struct NetBit {
    std::string id;
    bool constant = false;
};

struct NetPort {
    std::string name;
    std::string direction;
    std::vector<std::string> bits;
};

struct CellInstance {
    std::string name;
    std::string type;
    std::map<std::string, std::vector<std::string>> connections;
    std::map<std::string, std::string> directions;
};

struct Netlist {
    std::string module_name;
    std::map<std::string, NetPort> ports;
    std::vector<CellInstance> cells;
    static Netlist parse_yosys_json(const std::string& path);
};

struct ResolvedNetlist {
    const Netlist* netlist = nullptr;
    const LibertyLibrary* library = nullptr;
    std::vector<std::string> errors;
};

ResolvedNetlist resolve(const Netlist& netlist, const LibertyLibrary& library);

} // namespace sta
