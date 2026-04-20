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

#include <map>
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

inline constexpr int kDefaultThreadCount = 64;

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
  [[nodiscard]] unsigned readItf(const std::vector<std::string>&);
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

  // Report
  [[nodiscard]] unsigned reportSpef(const Str& output_dir);

  // setters & getters
  void set_num_threads(unsigned value) { num_threads_ = value; }
  [[nodiscard]] unsigned num_threads() const { return num_threads_; }

  std::vector<::itf::ProcessCorner*> corners() {
    std::vector<::itf::ProcessCorner*> out;
    out.reserve(process_corners_.size());
    for (auto& [_, ptr] : process_corners_) out.push_back(ptr.get());
    return out;
  }
  std::vector<const ::itf::ProcessCorner*> corners() const {
    std::vector<const ::itf::ProcessCorner*> result;
    result.reserve(process_corners_.size());
    for (const auto& [_, ptr] : process_corners_) result.push_back(ptr.get());
    return result;
  }

 private:
  RCX();
  ~RCX();

  [[nodiscard]] unsigned readItf(const Str&, const char*);
  [[nodiscard]] unsigned readCaptab(const Str&, const char*);

  std::vector<std::unique_ptr<::itf::ProcessCorner>> loadItfFiles(
      const std::vector<std::string>& itf_files);
  void registerProcessLayers(const ::itf::ProcessCorner& pc);
  void storeProcessCorner(std::unique_ptr<::itf::ProcessCorner> pc);
  std::vector<const parser::CapTable*> corner_cap_tables() const;

 private:
  // running settings
  int num_threads_ = kDefaultThreadCount;

  // from db
  LayoutData layout_;
  SpefContext spef_context_;

  // unified layer id/name mapping (design ↔ process)
  // from 1.db adapter 2.itf 3.mapping
  LayerTable layer_table_;

  // process corners (from itf file)
  std::map<Str,
        std::unique_ptr<::itf::ProcessCorner>> process_corners_;

  // per-corner cap table (from captab file)
  std::map<Str, parser::CapTable> corner_cap_tables_;
  // mapping table (from mapping file)
  parser::MappingBuilder mapping_builder_;

  // buildTopology
  TopoPool topo_pool_;

  // extractParasitics results
  RCTable rc_table_;

  bool process_layers_registered_{false};
};

}  // namespace ircx
