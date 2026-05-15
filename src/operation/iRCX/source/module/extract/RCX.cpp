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
#include <algorithm>
#include <filesystem>
#include <omp.h>
#include <system_error>
#include <utility>

#include "RCX.hpp"
#include "TopologyBuilder.hpp"
#include "Environment.hpp"
#include "ProcessVariation.hpp"
#include "ProcessCorner.hpp"
#include "ItfBuilder.hpp"
#include "ParasiticXIDBAdapter.hpp"
#include "CapacitanceCalc.hpp"
#include "RCXConfig.hh"
#include "ResistanceCalc.hpp"
#include "SpefDumper.hpp"
#include "idm.h"
#include "log/Log.hh"
namespace ircx {

std::unique_ptr<::itf::ProcessCorner> RCX::loadProcessCorner(const Str& corner_name,
                                                             const Str& itf_file)
{
  LOG_FATAL_IF(corner_name.empty()) << "corner name is empty.";
  LOG_FATAL_IF(itf_file.empty()) << "itf file is empty for corner " << corner_name;

  ::itf::ItfBuilder itf_builder;
  LOG_INFO << "read itf " << itf_file << " for corner " << corner_name << " start";
  itf_builder.buildItf(itf_file);
  LOG_INFO << "read itf " << itf_file << " for corner " << corner_name << " end";

  auto pcs = itf_builder.get_itf_service()->take_process_corners();
  std::unique_ptr<::itf::ProcessCorner> loaded_corner;
  Size valid_corner_num = 0;
  for (auto& pc : pcs) {
    if (!pc || pc->get_technology().empty()) {
      continue;
    }
    ++valid_corner_num;
    if (!loaded_corner) {
      loaded_corner = std::move(pc);
    }
  }

  LOG_FATAL_IF(valid_corner_num != 1)
      << "read_corner expects exactly one process corner in ITF file "
      << itf_file << ", but got " << valid_corner_num;
  LOG_FATAL_IF(!loaded_corner);

  const Str original_corner_name = loaded_corner->get_technology();
  if (original_corner_name != corner_name) {
    LOG_INFO << "rename process corner "
             << original_corner_name << " -> " << corner_name;
    loaded_corner->set_technology(corner_name);
  }

  return loaded_corner;
}

parser::CapTable RCX::loadCapTable(const Str& corner_name, const Str& captab_file)
{
  LOG_FATAL_IF(corner_name.empty()) << "corner name is empty.";
  LOG_FATAL_IF(captab_file.empty()) << "captab file is empty for corner " << corner_name;

  LOG_INFO << "read captab " << captab_file
           << " for corner " << corner_name << " start";

  parser::CapTable cap_table;
  LOG_FATAL_IF(!cap_table.loadFromFile(captab_file))
      << "failed to load captab: " << captab_file;

  LOG_INFO << "read captab " << captab_file
           << " for corner " << corner_name << " end";
  return cap_table;
}

void RCX::registerProcessLayers(const ::itf::ProcessCorner& pc)
{
  if (process_layers_registered_) {
    return;
  }

  auto& cond_layers = pc.get_layers()->get_conductor_layers();
  auto& via_layers = pc.get_layers()->get_via_layers();

  for (auto* layer : cond_layers)
    layer_table_.registerProcessLayer(layer->get_id(), layer->get_name());
  for (auto* layer : via_layers)
    layer_table_.registerProcessLayer(layer->get_id(), layer->get_name());

  process_layers_registered_ = true;
}

void RCX::validateProcessLayers(const ::itf::ProcessCorner& pc) const
{
  if (corners_.empty()) {
    return;
  }

  const ::itf::ProcessCorner& ref = *corners_.front().process_corner;
  const auto& ref_layers = ref.get_layers()->get_layers();
  const auto& cur_layers = pc.get_layers()->get_layers();

  LOG_FATAL_IF(ref_layers.size() != cur_layers.size())
      << "process layer count mismatch between corner "
      << pc.get_technology() << " and " << ref.get_technology()
      << ": " << cur_layers.size() << " vs " << ref_layers.size();

  for (Size idx = 0; idx < ref_layers.size(); ++idx) {
    const auto* ref_layer = ref_layers[idx];
    const auto* cur_layer = cur_layers[idx];

    LOG_FATAL_IF(ref_layer == nullptr || cur_layer == nullptr)
        << "null process layer in corner layer list.";
    LOG_FATAL_IF(ref_layer->get_type() != cur_layer->get_type())
        << "process layer type mismatch at index " << idx
        << " between corner " << pc.get_technology()
        << " and " << ref.get_technology();
    LOG_FATAL_IF(ref_layer->get_id() != cur_layer->get_id())
        << "process layer id mismatch at index " << idx
        << " between corner " << pc.get_technology()
        << " and " << ref.get_technology()
        << ": " << cur_layer->get_id() << " vs " << ref_layer->get_id();
    LOG_FATAL_IF(ref_layer->get_order() != cur_layer->get_order())
        << "process layer order mismatch at index " << idx
        << " between corner " << pc.get_technology()
        << " and " << ref.get_technology();
    LOG_FATAL_IF(ref_layer->get_name() != cur_layer->get_name())
        << "process layer name mismatch at index " << idx
        << " between corner " << pc.get_technology()
        << " and " << ref.get_technology()
        << ": " << cur_layer->get_name() << " vs " << ref_layer->get_name();
  }
}

bool RCX::hasCorner(const Str& corner_name) const
{
  return std::any_of(corners_.begin(), corners_.end(),
                     [&](const CornerData& corner) {
                       return corner.name == corner_name;
                     });
}

void RCX::resetConfigData()
{
  layout_.clear();
  spef_context_.clear();
  layer_table_.clear();
  corners_.clear();
  mapping_builder_.clear();
  topo_pool_.clear();
  rc_table_.clear();
  process_layers_registered_ = false;
}

std::vector<const parser::CapTable*> RCX::corner_cap_tables() const
{
  std::vector<const parser::CapTable*> result;
  result.reserve(corners_.size());

  for (const auto& corner : corners_) {
    result.push_back(&corner.cap_table);
  }

  return result;
}

unsigned RCX::readCorner(const Str& corner_name,
                         const char* itf_file,
                         const char* captab_file)
{
  LOG_FATAL_IF(corner_name.empty()) << "corner name is empty.";
  LOG_FATAL_IF(itf_file == nullptr || itf_file[0] == '\0')
      << "itf file is empty for corner " << corner_name;
  LOG_FATAL_IF(captab_file == nullptr || captab_file[0] == '\0')
      << "captab file is empty for corner " << corner_name;
  LOG_FATAL_IF(hasCorner(corner_name)) << "duplicate corner: " << corner_name;

  CornerData corner;
  corner.name = corner_name;
  corner.itf_file = itf_file;
  corner.captab_file = captab_file;
  corner.process_corner = loadProcessCorner(corner.name, corner.itf_file);
  corner.cap_table = loadCapTable(corner.name, corner.captab_file);

  if (corners_.empty()) {
    registerProcessLayers(*corner.process_corner);
  } else {
    validateProcessLayers(*corner.process_corner);
  }

  corners_.push_back(std::move(corner));
  return 1;
}

unsigned RCX::readMapping(const char* mapping_file)
{
  LOG_INFO << "read mapping "
           << mapping_file << " start";

  layer_table_.clearMappings();
  mapping_builder_.read(mapping_file);

  for (const auto& [dn, pn] : mapping_builder_.design_to_process_layer_names())
    layer_table_.registerMapping(dn, pn);

  LOG_INFO << "read mapping "
           << mapping_file << " end";
  return 1;
}

unsigned RCX::adaptDB()
{
  if (!dmInst) {
    LOG_ERROR << "adapt db failed: dmInst is null.";
    return 0;
  }

  auto* idb_builder = dmInst->get_idb_builder();
  if (!idb_builder) {
    LOG_ERROR << "adapt db failed: idb builder is null.";
    return 0;
  }

  ParasiticXIDBAdapter adapter(idb_builder);

  if (!adapter.adapt(layout_, layer_table_, spef_context_)) {
    LOG_ERROR << "adapt db failed.";
    return 0;
  }

  return 1;
}

unsigned RCX::checkShortOpen()
{
  // if (!db_) {
  //   if (!adaptDB()) {
  //     LOG_WARNING << "check short/open: database is empty, call adaptDB() first.";
  //     return 0;
  //   }
  // }

  // LOG_INFO << "check short/open start";


  // LOG_INFO << "check short/open end";
  return 1;
}

unsigned RCX::buildTopology()
{
  LOG_INFO << "build topology start";

  topo_pool_.clear();
  TopologyBuilder tb(topo_pool_);
  tb.build_all(layout_);
  tb.build_special(layout_);

  LOG_INFO << "build topology end";
  return 1;
}

unsigned RCX::buildEnvironment() {

  LOG_INFO << "build environment start";


  Environment& env = Environment::getOrCreateInst();
  env.set_layout_data(&layout_);
  env.set_topo_pool(&topo_pool_);

  env.buildNetEnvPools();

  LOG_INFO << "build environment end";
  return 1;
}

unsigned RCX::buildProcessVariation() {

  LOG_INFO << "build process variation start";

  const auto process_corners = corners();

  ProcessVariation& pv = ProcessVariation::getOrCreateInst();
  pv.set_layout_data(&layout_);
  pv.set_topo_pool(&topo_pool_);
  pv.set_layer_table(&layer_table_);
  pv.set_corners(process_corners);
  
  pv.buildEtchPools();

  LOG_INFO << "build process variation end";
  return 1;
}

unsigned RCX::extractParasitics()
{
  LOG_INFO << "extract parasitics start";

  const auto process_corners = corners();
  const auto cap_tables = corner_cap_tables();
  LOG_FATAL_IF(process_corners.size() != cap_tables.size())
      << "corner/captab size mismatch.";
  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    LOG_FATAL_IF(cap_tables[corner_idx] == nullptr)
        << "captab not loaded for corner "
        << process_corners[corner_idx]->get_technology();
  }

  // Pre-allocate RCTable for parallel access
  rc_table_.init(process_corners.size(), layout_.regular_net_count(), topo_pool_);

  // Resistance 
  ResistanceCalc& res_calc = ResistanceCalc::getOrCreateInst();
  res_calc.set_layout_data(&layout_);
  res_calc.set_topo_pool(&topo_pool_);
  res_calc.set_layer_table(&layer_table_);
  res_calc.set_rc_table(&rc_table_);
  res_calc.set_corners(process_corners);
  res_calc.calc();

  // Capacitance
  CapacitanceCalc& cap_calc = CapacitanceCalc::getOrCreateInst();
  cap_calc.set_layout_data(&layout_);
  cap_calc.set_topo_pool(&topo_pool_);
  cap_calc.set_layer_table(&layer_table_);
  cap_calc.set_rc_table(&rc_table_);
  cap_calc.set_cap_tables(cap_tables);
  cap_calc.set_corners(process_corners);
  cap_calc.calc();

  LOG_INFO << "extract parasitics end";
  return 1;
}

unsigned RCX::run()
{
  LOG_INFO << "RCX run begin...";

  omp_set_num_threads(num_threads_);

  if (!adaptDB() ||
      !buildTopology() ||
      !buildEnvironment() ||
      !buildProcessVariation() ||
      !extractParasitics()) {
    LOG_INFO << "RCX run end.";
    return 0;
  }

  LOG_INFO << "RCX run end.";
  return 1;
}

unsigned RCX::runFromConfig(const Str& config)
{
  RCXConfig rcx_config;
  if (!rcx_config.loadFromFile(config)) {
    return 0;
  }

  resetConfigData();
  set_num_threads(rcx_config.get_thread_num());

  for (const auto& corner : rcx_config.get_corners()) {
    if (!readCorner(corner.name, corner.itf_file.c_str(), corner.captab_file.c_str())) {
      return 0;
    }
  }
  if (!readMapping(rcx_config.get_mapping_file().c_str())) {
    return 0;
  }

  if (!run()) {
    return 0;
  }

  const Str output_dir =
      rcx_config.get_output_dir().empty() ? "." : rcx_config.get_output_dir();
  return reportSpef(output_dir);
}

unsigned RCX::reportSpef(const Str& output_dir)
{
  LOG_INFO << "report spef start";
  const Str resolved_output_dir = output_dir.empty() ? "." : output_dir;
  std::error_code ec;
  std::filesystem::create_directories(resolved_output_dir, ec);
  if (ec) {
    LOG_ERROR << "Failed to create RCX output directory "
              << resolved_output_dir << ": " << ec.message();
    return 0;
  }

  const auto process_corners = corners();

  SpefDumper dumper;

  dumper.set_spef_context(&spef_context_);
  dumper.set_layout_data(&layout_);
  dumper.set_layer_table(&layer_table_);
  dumper.set_topo_pool(&topo_pool_);
  dumper.set_rc_table(&rc_table_);
  dumper.set_corners(process_corners);

  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    dumper.dump(resolved_output_dir, corner_idx);
  }

  LOG_INFO << "report spef end";
  return 1;
}

RCX::RCX()
{
  char config[] = "iRCX";
  char* argv[] = {config, nullptr};
  ieda::Log::init(argv);
}
RCX::~RCX() = default;

}  // namespace ircx
