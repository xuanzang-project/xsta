#pragma once

#include "sta/timing.hpp"

#include <iosfwd>

namespace sta {

void write_report(const TimingResult& result, std::ostream& out);

} // namespace sta
