#include "RCX.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <system_error>
#include <utility>


#include "ItfBuilder.hpp"
#include "ProcessCorner.hpp"

#include "ParasiticXIDBAdapter.hpp"
#include "LayerTable.hpp"
#include "TopologyBuilder.hpp"
#include "Environment.hpp"
#include "ProcessVariation.hpp"
#include "ResistanceCalc.hpp"
#include "CapacitanceCalc.hpp"
#include "SpefDumper.hpp"

#include "idm.h"
#include "log/Log.hh"

namespace ircx {

std::vector<std::unique_ptr<::itf::ProcessCorner>>
RCX::loadItfFiles(const std::vector<std::string>& itf_files)
{
  LOG_FATAL_IF(itf_files.empty()) << "itf file list is empty.";

  ::itf::ItfBuilder itf_builder;
  for (const auto& itf_file : itf_files) {
    LOG_INFO << "read itf " << itf_file << " start";
    itf_builder.buildItf(itf_file);
    LOG_INFO << "read itf " << itf_file << " end";
  }

  return itf_builder.get_itf_service()->take_process_corners();
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

void RCX::storeProcessCorner(std::unique_ptr<::itf::ProcessCorner> pc)
{
  if (!pc || pc->get_technology().empty()) {
    return;
  }

  registerProcessLayers(*pc);
  const Str corner_name = pc->get_technology();
  process_corners_[corner_name] = std::move(pc);
}

std::vector<const parser::CapTable*> RCX::corner_cap_tables() const
{
  std::vector<const parser::CapTable*> result;
  result.reserve(process_corners_.size());

  for (const auto& [corner_name, _] : process_corners_) {
    auto iter = corner_cap_tables_.find(corner_name);
    result.push_back(iter == corner_cap_tables_.end() ? nullptr : &iter->second);
  }

  return result;
}

unsigned RCX::readCorner(const Str& corner_name,
                         const char* itf_file,
                         const char* captab_file)
{
  if (!readItf(corner_name, itf_file)) {
    return 0;
  }
  return readCaptab(corner_name, captab_file);
}

unsigned RCX::readCaptab(const Str& corner_name, const char* captab_file)
{
  LOG_FATAL_IF(corner_name.empty()) << "corner name is empty.";
  LOG_FATAL_IF(!process_corners_.contains(corner_name))
      << "process corner not loaded: " << corner_name;

  LOG_INFO << "read captab " << captab_file
           << " for corner " << corner_name << " start";

  parser::CapTable cap_table;
  LOG_FATAL_IF(!cap_table.loadFromFile(captab_file))
      << "failed to load captab: " << captab_file;
  corner_cap_tables_[corner_name] = std::move(cap_table);

  LOG_INFO << "read captab " << captab_file
           << " for corner " << corner_name << " end";
  return 1;
}

unsigned RCX::readItf(const Str& corner_name, const char* itf_file)
{
  LOG_FATAL_IF(corner_name.empty()) << "corner name is empty.";

  auto pcs = loadItfFiles({itf_file});

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

  storeProcessCorner(std::move(loaded_corner));
  return 1;
}

unsigned RCX::readItf(const std::vector<std::string>& itf_files)
{
  auto pcs = loadItfFiles(itf_files);
  for (auto& pc : pcs) {
    storeProcessCorner(std::move(pc));
  }

  return 1;
}

unsigned RCX::readMapping(const char* mapping_file)
{
  LOG_INFO << "read mapping "
           << mapping_file << " start";

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

unsigned RCX::reportSpef(const Str& output_dir)
{
  LOG_INFO << "report spef start";
  const auto process_corners = corners();

  SpefDumper dumper;

  dumper.set_spef_context(&spef_context_);
  dumper.set_layout_data(&layout_);
  dumper.set_layer_table(&layer_table_);
  dumper.set_topo_pool(&topo_pool_);
  dumper.set_rc_table(&rc_table_);
  dumper.set_corners(process_corners);

  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    dumper.dump(output_dir, corner_idx);
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
