#pragma once

#include <string>
#include <map>
#include <vector>

#include "Types.hpp"

namespace ircx {

// SpefContext
//
// SPEF output metadata: compressed name tables and instance-to-cell mapping.
// Previously stored inside Database; now a standalone value type populated
// by the layout adapter.
//
struct SpefContext {
  // Ordered lists for SPEF *NAME_MAP output.
  // Index 0 = first entry; SPEF IDs start at *1.
  std::vector<Str> net_names;
  std::vector<Str> port_names;
  std::vector<char> port_io;
  std::vector<Str> instance_names;

  // instance name → cell name (for *CELL entries)
  std::map<Str, Str> instance_to_cell;
};

}  // namespace ircx
