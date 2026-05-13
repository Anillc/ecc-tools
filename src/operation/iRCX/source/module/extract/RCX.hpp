// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Geoms.hpp"
#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "SpefContext.hpp"
#include "Types.hpp"
#include "TopoPool.hpp"
#include "CapTable.hpp"
#include "MappingBuilder.hpp"
#include "RCTable.hpp"
namespace idb {
  class IdbDesign;
}

namespace itf {
  class ProcessCorner;
}

namespace ircx {
namespace parser {
class CapTable;
class MappingBuilder;
}  // namespace parser
}

namespace ircx {

inline constexpr unsigned kDefaultThreadCount = 64U;

class RCX final {
 public:
  // Meyer's singleton
  static RCX& getOrCreateInst() {
    static RCX inst;  // C++11 thread-safe
    return inst;
  }

  // Disallow copy/move
  RCX(const RCX&) = delete;
  RCX& operator=(const RCX&) = delete;
  RCX(RCX&&) = delete;
  RCX& operator=(RCX&&) = delete;

  // I/O
  [[nodiscard]] unsigned readCorner(const Str&, const char*, const char*);
  [[nodiscard]] unsigned readMapping(const char*);

  // DB
  [[nodiscard]] unsigned adaptDB();

  // Checks
  [[nodiscard]] unsigned checkShortOpen();

  // Topology
  [[nodiscard]] unsigned buildTopology();

  // Environment
  [[nodiscard]] unsigned buildEnvironment();

  // Process
  [[nodiscard]] unsigned buildProcessVariation();

  // Extraction
  [[nodiscard]] unsigned extractParasitics();
  [[nodiscard]] unsigned run();
  [[nodiscard]] unsigned runFromConfig(const Str& config);

  // Report
  [[nodiscard]] unsigned reportSpef(const Str& output_dir);

  // setters & getters
  void set_num_threads(unsigned value) { num_threads_ = value == 0 ? 1U : value; }
  [[nodiscard]] unsigned num_threads() const { return num_threads_; }

  std::vector<::itf::ProcessCorner*> corners() {
    std::vector<::itf::ProcessCorner*> out;
    out.reserve(corners_.size());
    for (auto& corner : corners_) out.push_back(corner.process_corner.get());
    return out;
  }
  std::vector<const ::itf::ProcessCorner*> corners() const {
    std::vector<const ::itf::ProcessCorner*> result;
    result.reserve(corners_.size());
    for (const auto& corner : corners_) result.push_back(corner.process_corner.get());
    return result;
  }

 private:
  struct CornerData {
    Str name;
    Str itf_file;
    Str captab_file;
    std::unique_ptr<::itf::ProcessCorner> process_corner;
    parser::CapTable cap_table;
  };

  RCX();
  ~RCX();

  std::unique_ptr<::itf::ProcessCorner> loadProcessCorner(const Str& corner_name,
                                                          const Str& itf_file);
  parser::CapTable loadCapTable(const Str& corner_name, const Str& captab_file);
  void registerProcessLayers(const ::itf::ProcessCorner& pc);
  void validateProcessLayers(const ::itf::ProcessCorner& pc) const;
  [[nodiscard]] bool hasCorner(const Str& corner_name) const;
  void resetConfigData();
  std::vector<const parser::CapTable*> corner_cap_tables() const;

 private:
  // running settings
  unsigned num_threads_ = kDefaultThreadCount;

  // from db
  LayoutData layout_;
  SpefContext spef_context_;

  // unified layer id/name mapping (design ↔ process)
  // from 1.db adapter 2.itf 3.mapping
  LayerTable layer_table_;

  // per-corner process/cap data loaded by readCorner()
  std::vector<CornerData> corners_;

  // mapping table (from mapping file)
  parser::MappingBuilder mapping_builder_;

  // buildTopology
  TopoPool topo_pool_;

  // extractParasitics results
  RCTable rc_table_;

  bool process_layers_registered_{false};
};

}  // namespace ircx
