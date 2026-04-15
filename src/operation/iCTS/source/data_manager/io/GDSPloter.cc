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
/**
 * @file GDSPloter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "GDSPloter.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <limits>
#include <vector>

#include "CTSAPI.hh"
#include "CtsDBWrapper.hh"
#include "json.hpp"

namespace icts {

namespace {

CtsPin* resolveDriverPin(CtsNet* clk_net)
{
  return clk_net == nullptr ? nullptr : clk_net->get_driver_pin();
}

CtsInstance* resolveDriverInst(CtsNet* clk_net, CtsPin* driver_pin)
{
  if (clk_net == nullptr) {
    return nullptr;
  }
  if (auto* driver = clk_net->get_driver_inst(driver_pin); driver != nullptr) {
    return driver;
  }
  return driver_pin != nullptr ? driver_pin->get_instance() : nullptr;
}

int resolveDriverLevel(CtsInstance* driver)
{
  return driver != nullptr ? driver->get_level() : 0;
}

std::string resolveDesignGdsPath(const std::string& requested_path)
{
  if (!requested_path.empty()) {
    return requested_path;
  }
  auto* config = CTSAPIInst.get_config();
  if (config != nullptr) {
    if (!config->get_gds_file().empty()) {
      return config->get_gds_file();
    }
    return std::filesystem::path(config->get_work_dir()).append("output").append("cts_design.gds").string();
  }
  return "cts_design.gds";
}

std::string resolveFlylineGdsPath(const std::string& requested_path)
{
  if (!requested_path.empty()) {
    return requested_path;
  }
  auto design_path = resolveDesignGdsPath("");
  return std::filesystem::path(design_path).parent_path().append("cts_flyline.gds").string();
}

void ensureParentDirectory(const std::string& file_path)
{
  auto parent_path = std::filesystem::path(file_path).parent_path();
  if (!parent_path.empty() && !std::filesystem::exists(parent_path)) {
    std::filesystem::create_directories(parent_path);
  }
}

enum class GdsRecordType : uint8_t
{
  kHeader = 0x00,
  kBgnLib = 0x01,
  kLibName = 0x02,
  kUnits = 0x03,
  kEndLib = 0x04,
  kBgnStr = 0x05,
  kStrName = 0x06,
  kEndStr = 0x07,
  kBoundary = 0x08,
  kPath = 0x09,
  kSRef = 0x0A,
  kLayer = 0x0D,
  kDataType = 0x0E,
  kWidth = 0x0F,
  kXY = 0x10,
  kEndEl = 0x11,
  kSName = 0x12,
  kPathType = 0x21
};

enum class GdsDataType : uint8_t
{
  kNoData = 0x00,
  kBitArray = 0x01,
  kInt16 = 0x02,
  kInt32 = 0x03,
  kReal8 = 0x05,
  kAscii = 0x06
};

void appendUInt16BE(std::vector<uint8_t>& payload, uint16_t value)
{
  payload.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  payload.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendInt16BE(std::vector<uint8_t>& payload, int16_t value)
{
  appendUInt16BE(payload, static_cast<uint16_t>(value));
}

void appendInt32BE(std::vector<uint8_t>& payload, int32_t value)
{
  auto raw = static_cast<uint32_t>(value);
  payload.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
  payload.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
  payload.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
  payload.push_back(static_cast<uint8_t>(raw & 0xFF));
}

void appendUInt64BE(std::vector<uint8_t>& payload, uint64_t value)
{
  for (int shift = 56; shift >= 0; shift -= 8) {
    payload.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
}

int16_t clampToInt16(int value)
{
  return static_cast<int16_t>(std::clamp(value, 0, static_cast<int>(std::numeric_limits<int16_t>::max())));
}

std::array<int16_t, 6> makeTimestamp(time_t when)
{
  std::tm tm_value;
  localtime_r(&when, &tm_value);
  return {static_cast<int16_t>(tm_value.tm_year + 1900), static_cast<int16_t>(tm_value.tm_mon + 1),
          static_cast<int16_t>(tm_value.tm_mday), static_cast<int16_t>(tm_value.tm_hour), static_cast<int16_t>(tm_value.tm_min),
          static_cast<int16_t>(tm_value.tm_sec)};
}

uint64_t encodeGdsReal8(double value)
{
  if (value == 0.0) {
    return 0;
  }

  const bool negative = value < 0.0;
  double magnitude = std::fabs(value);
  int exponent = 64;

  while (magnitude >= 1.0) {
    magnitude /= 16.0;
    ++exponent;
  }
  while (magnitude < 0.0625) {
    magnitude *= 16.0;
    --exponent;
  }

  if (exponent <= 0 || exponent >= 128) {
    LOG_FATAL << "GDS real8 exponent out of range for value " << value;
  }

  auto mantissa = static_cast<uint64_t>(std::llround(magnitude * static_cast<double>(1ULL << 56)));
  if (mantissa >= (1ULL << 56)) {
    mantissa >>= 4;
    ++exponent;
  }

  auto sign_and_exp = static_cast<uint8_t>((negative ? 0x80 : 0x00) | (exponent & 0x7F));
  return (static_cast<uint64_t>(sign_and_exp) << 56) | (mantissa & 0x00FFFFFFFFFFFFFFULL);
}

std::vector<uint8_t> packInt16Payload(std::initializer_list<int16_t> values)
{
  std::vector<uint8_t> payload;
  payload.reserve(values.size() * 2);
  for (auto value : values) {
    appendInt16BE(payload, value);
  }
  return payload;
}

std::vector<uint8_t> packInt32Payload(const std::vector<int32_t>& values)
{
  std::vector<uint8_t> payload;
  payload.reserve(values.size() * 4);
  for (auto value : values) {
    appendInt32BE(payload, value);
  }
  return payload;
}

std::vector<uint8_t> packAsciiPayload(const std::string& text)
{
  std::vector<uint8_t> payload(text.begin(), text.end());
  if ((payload.size() & 1U) != 0U) {
    payload.push_back('\0');
  }
  return payload;
}

std::vector<uint8_t> packReal8Payload(std::initializer_list<double> values)
{
  std::vector<uint8_t> payload;
  payload.reserve(values.size() * 8);
  for (auto value : values) {
    appendUInt64BE(payload, encodeGdsReal8(value));
  }
  return payload;
}

std::vector<uint8_t> packTimestampPayload(time_t begin_time, time_t end_time)
{
  std::vector<uint8_t> payload;
  payload.reserve(24);
  for (const auto& ts : {makeTimestamp(begin_time), makeTimestamp(end_time)}) {
    for (auto value : ts) {
      appendInt16BE(payload, value);
    }
  }
  return payload;
}

void writeRecord(std::fstream& gds_ofs, GdsRecordType record_type, GdsDataType data_type, const std::vector<uint8_t>& payload = {})
{
  const auto record_length = static_cast<uint16_t>(payload.size() + 4);
  char header[4] = {static_cast<char>((record_length >> 8) & 0xFF), static_cast<char>(record_length & 0xFF),
                    static_cast<char>(record_type), static_cast<char>(data_type)};
  gds_ofs.write(header, sizeof(header));
  if (!payload.empty()) {
    gds_ofs.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
  }
}

void writeRecordInt16(std::fstream& gds_ofs, GdsRecordType record_type, std::initializer_list<int16_t> values)
{
  writeRecord(gds_ofs, record_type, GdsDataType::kInt16, packInt16Payload(values));
}

void writeRecordInt32(std::fstream& gds_ofs, GdsRecordType record_type, const std::vector<int32_t>& values)
{
  writeRecord(gds_ofs, record_type, GdsDataType::kInt32, packInt32Payload(values));
}

void writeRecordAscii(std::fstream& gds_ofs, GdsRecordType record_type, const std::string& text)
{
  writeRecord(gds_ofs, record_type, GdsDataType::kAscii, packAsciiPayload(text));
}

void writeBoundary(std::fstream& gds_ofs, int layer, int data_type, const std::vector<Point>& points)
{
  writeRecord(gds_ofs, GdsRecordType::kBoundary, GdsDataType::kNoData);
  writeRecordInt16(gds_ofs, GdsRecordType::kLayer, {clampToInt16(layer)});
  writeRecordInt16(gds_ofs, GdsRecordType::kDataType, {clampToInt16(data_type)});

  std::vector<int32_t> coords;
  coords.reserve(points.size() * 2);
  for (const auto& point : points) {
    coords.push_back(point.x());
    coords.push_back(point.y());
  }
  writeRecordInt32(gds_ofs, GdsRecordType::kXY, coords);
  writeRecord(gds_ofs, GdsRecordType::kEndEl, GdsDataType::kNoData);
}

void writePath(std::fstream& gds_ofs, int layer, int width, const Point& begin, const Point& end)
{
  writeRecord(gds_ofs, GdsRecordType::kPath, GdsDataType::kNoData);
  writeRecordInt16(gds_ofs, GdsRecordType::kLayer, {clampToInt16(layer)});
  writeRecordInt16(gds_ofs, GdsRecordType::kDataType, {0});
  writeRecordInt16(gds_ofs, GdsRecordType::kPathType, {0});
  writeRecordInt32(gds_ofs, GdsRecordType::kWidth, {width});
  writeRecordInt32(gds_ofs, GdsRecordType::kXY, {begin.x(), begin.y(), end.x(), end.y()});
  writeRecord(gds_ofs, GdsRecordType::kEndEl, GdsDataType::kNoData);
}

void writeSRef(std::fstream& gds_ofs, const std::string& name)
{
  writeRecord(gds_ofs, GdsRecordType::kSRef, GdsDataType::kNoData);
  writeRecordAscii(gds_ofs, GdsRecordType::kSName, name);
  writeRecordInt32(gds_ofs, GdsRecordType::kXY, {0, 0});
  writeRecord(gds_ofs, GdsRecordType::kEndEl, GdsDataType::kNoData);
}

}  // namespace

void GDSPloter::plotDesign(const std::string& path)
{
  auto* design = CTSAPIInst.get_design();
  auto& clk_nets = design->get_nets();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto file_path = resolveDesignGdsPath(path);
  ensureParentDirectory(file_path);
  auto ofs = std::fstream(file_path, std::ios::out | std::ios::trunc | std::ios::binary);

  head(ofs);

  for (auto& clk_net : clk_nets) {
    size_t wire_id = 0;
    auto net_name = clk_net->get_net_name();
    auto* driver_pin = resolveDriverPin(clk_net);
    auto* driver = resolveDriverInst(clk_net, driver_pin);
    if (driver_pin == nullptr) {
      LOG_WARNING << "Skip design GDS for net " << net_name << " because driver pin is null.";
      continue;
    }
    const int driver_level = resolveDriverLevel(driver);

    for (const auto& wire : clk_net->get_signal_wires()) {
      auto wire_name = "WIRE_" + net_name + "_" + std::to_string(wire_id++);
      auto first = wire.get_first().point;
      auto second = wire.get_second().point;
      insertWire(ofs, first, second, wire_name, driver_level);
    }
  }

  auto* idb_design = db_wrapper->get_idb()->get_def_service()->get_design();
  auto idb_insts = idb_design->get_instance_list()->get_instance_list();
  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto name = idb_inst->get_name();
    auto* cts_inst = design->findInstance(name);
    if (cts_inst) {
      insertInstance(ofs, cts_inst);
    } else {
      insertPolygon(ofs, inst_box, name, 1, 0);
    }
  }

  auto idb_blockages = idb_design->get_blockage_list()->get_blockage_list();
  for (auto* blockage : idb_blockages) {
    auto blockage_rect_list = blockage->get_rect_list();
    if (blockage_rect_list.empty()) {
      LOG_WARNING << "rectangles of blockage are empty!";
      continue;
    }
    int i = 0;
    for (auto* blockage_rect : blockage_rect_list) {
      auto name = blockage->get_instance_name() + std::to_string(i);
      ++i;
      insertPolygon(ofs, blockage_rect, name, 1, 0);
    }
  }

  auto core = db_wrapper->get_core_bounding_box();
  insertPolygon(ofs, core, "core", 100, 0);
  strBegin(ofs);
  topBegin(ofs);
  for (auto* idb_inst : idb_insts) {
    auto name = idb_inst->get_name();
    refPolygon(ofs, name);
  }

  for (auto* blockage : idb_blockages) {
    auto blockage_rect_list = blockage->get_rect_list();
    if (blockage_rect_list.empty()) {
      LOG_WARNING << "rectangles of blockage are empty!";
      continue;
    }
    for (size_t i = 0; i < blockage_rect_list.size(); ++i) {
      auto name = blockage->get_instance_name() + std::to_string(i);
      refPolygon(ofs, name);
    }
  }
  refPolygon(ofs, "core");
  for (auto& clk_net : clk_nets) {
    auto net_name = clk_net->get_net_name();
    if (resolveDriverPin(clk_net) == nullptr) {
      continue;
    }

    for (size_t i = 0; i < clk_net->get_signal_wires().size(); ++i) {
      auto wire_name = "WIRE_" + net_name + "_" + std::to_string(i);
      refPolygon(ofs, wire_name);
    }
  }
  strEnd(ofs);

  tail(ofs);
}

void GDSPloter::plotFlyLine(const std::string& path)
{
  auto* design = CTSAPIInst.get_design();
  auto& clk_nets = design->get_nets();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto file_path = resolveFlylineGdsPath(path);
  ensureParentDirectory(file_path);
  auto ofs = std::fstream(file_path, std::ios::out | std::ios::trunc | std::ios::binary);

  head(ofs);

  for (auto& clk_net : clk_nets) {
    auto* driver_pin = resolveDriverPin(clk_net);
    auto* driver = resolveDriverInst(clk_net, driver_pin);
    if (driver_pin == nullptr) {
      LOG_WARNING << "Skip flyline GDS for net " << clk_net->get_net_name() << " because driver pin is null.";
      continue;
    }
    const int driver_level = resolveDriverLevel(driver);
    size_t wire_id = 0;
    for (auto* load_pin : clk_net->get_load_pins()) {
      auto wire_name = "WIRE_" + clk_net->get_net_name() + "_" + std::to_string(wire_id++);
      insertWire(ofs, driver_pin->get_location(), load_pin->get_location(), wire_name, driver_level);
    }
  }

  auto* idb_design = db_wrapper->get_idb()->get_def_service()->get_design();
  auto idb_insts = idb_design->get_instance_list()->get_instance_list();
  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto name = idb_inst->get_name();
    auto* cts_inst = design->findInstance(name);
    if (cts_inst) {
      insertInstance(ofs, cts_inst);
    } else {
      insertPolygon(ofs, inst_box, name, 1, 0);
    }
  }

  auto idb_blockages = idb_design->get_blockage_list()->get_blockage_list();
  for (auto* blockage : idb_blockages) {
    auto blockage_rect_list = blockage->get_rect_list();
    if (blockage_rect_list.empty()) {
      LOG_WARNING << "rectangles of blockage are empty!";
      continue;
    }
    int i = 0;
    for (auto* blockage_rect : blockage_rect_list) {
      auto name = blockage->get_instance_name() + std::to_string(i);
      ++i;
      insertPolygon(ofs, blockage_rect, name, 1, 0);
    }
  }

  auto* core = db_wrapper->get_core_bounding_box();
  insertPolygon(ofs, core, "core", 100, 0);
  strBegin(ofs);
  topBegin(ofs);
  for (auto* idb_inst : idb_insts) {
    auto name = idb_inst->get_name();
    refPolygon(ofs, name);
  }

  for (auto* blockage : idb_blockages) {
    auto blockage_rect_list = blockage->get_rect_list();
    if (blockage_rect_list.empty()) {
      LOG_WARNING << "rectangles of blockage are empty!";
      continue;
    }
    for (size_t i = 0; i < blockage_rect_list.size(); ++i) {
      auto name = blockage->get_instance_name() + std::to_string(i);
      refPolygon(ofs, name);
    }
  }
  refPolygon(ofs, "core");

  for (auto& clk_net : clk_nets) {
    const auto load_pins = clk_net->get_load_pins();
    for (size_t i = 0; i < load_pins.size(); ++i) {
      auto wire_name = "WIRE_" + clk_net->get_net_name() + "_" + std::to_string(i);
      refPolygon(ofs, wire_name);
    }
  }
  strEnd(ofs);

  tail(ofs);
}

void GDSPloter::writePyDesign(const std::string& path)
{
  auto* design = CTSAPIInst.get_design();

  auto* config = CTSAPIInst.get_config();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto file_path = path;
  if (file_path.empty()) {
    auto dir = std::filesystem::path(config->get_work_dir()).append("output").string();
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directories(dir);
    }
    file_path = std::filesystem::path(dir).append("cts_design.py").string();
  }
  int max_level = 0;
  auto* idb_design = db_wrapper->get_idb()->get_def_service()->get_design();
  auto idb_insts = idb_design->get_instance_list()->get_instance_list();
  for (auto* idb_inst : idb_insts) {
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    if (cts_inst) {
      max_level = std::max(max_level, cts_inst->get_level());
    }
  }
  // gen py file
  std::ofstream py_ofs(file_path, std::ios::out | std::ios::trunc);
  py_ofs << "import matplotlib.pyplot as plt" << std::endl;
  py_ofs << "import numpy as np" << std::endl;
  py_ofs << "from matplotlib.patches import Rectangle" << std::endl;
  py_ofs << "import scienceplots" << std::endl;
  py_ofs << "plt.style.use(['science','no-latex'])" << std::endl;
  py_ofs << "def generate_color_sequence(n): " << std::endl;
  py_ofs << "    cmap = plt.get_cmap('summer')" << std::endl;
  py_ofs << "    colors = [cmap(i) for i in np.linspace(0, 1, n)]" << std::endl;
  py_ofs << "    return colors" << std::endl;
  if (max_level > 5) {
    py_ofs << "colors = generate_color_sequence(" << max_level + 2 << ")" << std::endl;
  } else {
    py_ofs << "colors = ['#FF0000', '#00FF00', '#008080', '#ff8000', '#910000', '#800080', '#FF1493', '#008B8B', '#8A2BE2', '#32CD32']"
           << std::endl;
  }
  py_ofs << "line_width = np.linspace(0.5, " << (max_level + 2) * 0.5 << ", " << max_level + 2 << ")" << std::endl;
  py_ofs << "inst_colors = generate_color_sequence(" << max_level + 2 << ")" << std::endl;
  py_ofs << "fig = plt.figure(figsize=(8,8), dpi=300)" << std::endl;

  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    int level = 0;
    if (cts_inst) {
      level = cts_inst->get_level();
    }
    if (level > 1) {
      continue;
    }
    py_ofs << "plt.gca().add_patch(Rectangle((" << inst_box->get_low_x() << "," << inst_box->get_low_y() << "),"
           << inst_box->get_high_x() - inst_box->get_low_x() << "," << inst_box->get_high_y() - inst_box->get_low_y()
           << ",linewidth=0.1,edgecolor='#c0c0c0',facecolor='#c0c0c0',zorder=" << level + 1 << "))" << std::endl;
  }
  auto& clk_nets = design->get_nets();
  for (auto& clk_net : clk_nets) {
    auto* driver_pin = resolveDriverPin(clk_net);
    auto* driver = resolveDriverInst(clk_net, driver_pin);
    if (driver_pin == nullptr) {
      LOG_WARNING << "Skip design Python plot for net " << clk_net->get_net_name() << " because driver pin is null.";
      continue;
    }
    if (driver != nullptr && driver->get_location() == Point(-1, -1)) {
      continue;
    }
    auto level = resolveDriverLevel(driver);

    for (const auto& wire : clk_net->get_signal_wires()) {
      auto first = wire.get_first().point;
      auto second = wire.get_second().point;
      // line width should add with level
      py_ofs << "plt.plot([" << first.x() << "," << second.x() << "],[" << first.y() << "," << second.y() << "],color=colors[" << level
             << "],linewidth=line_width[" << level << "],zorder=" << level << ")" << std::endl;
    }
  }
  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    int level = 0;
    if (cts_inst) {
      level = cts_inst->get_level();
    }
    if (level <= 1) {
      continue;
    }
    py_ofs << "plt.gca().add_patch(Rectangle((" << inst_box->get_low_x() << "," << inst_box->get_low_y() << "),"
           << inst_box->get_high_x() - inst_box->get_low_x() << "," << inst_box->get_high_y() - inst_box->get_low_y()
           << ",linewidth=" << 1 + 1.0 * level / 10 << ",edgecolor='black',facecolor=inst_colors[" << level + 1 << "],zorder=" << level + 1
           << "))" << std::endl;
  }
  py_ofs << "plt.axis('square')\n";
  py_ofs << "plt.axis('off')\n";
  py_ofs << "plt.savefig('cts_design.png', dpi=300, bbox_inches='tight')" << std::endl;
}

void GDSPloter::writePyFlyLine(const std::string& path)
{
  auto* design = CTSAPIInst.get_design();

  auto* config = CTSAPIInst.get_config();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto file_path = path;
  if (file_path.empty()) {
    auto dir = std::filesystem::path(config->get_work_dir()).append("output").string();
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directories(dir);
    }
    file_path = std::filesystem::path(dir).append("cts_flyline.py").string();
  }
  int max_level = 0;
  auto* idb_design = db_wrapper->get_idb()->get_def_service()->get_design();
  auto idb_insts = idb_design->get_instance_list()->get_instance_list();
  for (auto* idb_inst : idb_insts) {
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    if (cts_inst) {
      max_level = std::max(max_level, cts_inst->get_level());
    }
  }
  // gen py file
  std::ofstream py_ofs(file_path, std::ios::out | std::ios::trunc);
  py_ofs << "import matplotlib.pyplot as plt" << std::endl;
  py_ofs << "import numpy as np" << std::endl;
  py_ofs << "from matplotlib.patches import Rectangle" << std::endl;
  py_ofs << "import scienceplots" << std::endl;
  py_ofs << "plt.style.use(['science','no-latex'])" << std::endl;
  py_ofs << "def generate_color_sequence(n): " << std::endl;
  py_ofs << "    cmap = plt.get_cmap('summer')" << std::endl;
  py_ofs << "    colors = [cmap(i) for i in np.linspace(0, 1, n)]" << std::endl;
  py_ofs << "    return colors" << std::endl;
  if (max_level > 5) {
    py_ofs << "colors = generate_color_sequence(" << max_level + 2 << ")" << std::endl;
  } else {
    py_ofs << "colors = ['#FF0000', '#00FF00', '#008080', '#ff8000', '#910000', '#800080', '#FF1493', '#008B8B', '#8A2BE2', '#32CD32']"
           << std::endl;
  }
  py_ofs << "line_width = np.linspace(0.5, " << (max_level + 2) * 0.5 << ", " << max_level + 2 << ")" << std::endl;
  py_ofs << "inst_colors = generate_color_sequence(" << max_level + 2 << ")" << std::endl;
  py_ofs << "fig = plt.figure(figsize=(8,8), dpi=300)" << std::endl;

  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    int level = 0;
    if (cts_inst) {
      level = cts_inst->get_level();
    }
    if (level > 1) {
      continue;
    }
    py_ofs << "plt.gca().add_patch(Rectangle((" << inst_box->get_low_x() << "," << inst_box->get_low_y() << "),"
           << inst_box->get_high_x() - inst_box->get_low_x() << "," << inst_box->get_high_y() - inst_box->get_low_y()
           << ",linewidth=0.1,edgecolor='#c0c0c0',facecolor='#c0c0c0',zorder=" << level + 1 << "))" << std::endl;
  }
  auto& clk_nets = design->get_nets();
  for (auto& clk_net : clk_nets) {
    auto* driver_pin = resolveDriverPin(clk_net);
    auto* driver = resolveDriverInst(clk_net, driver_pin);
    if (driver_pin == nullptr) {
      LOG_WARNING << "Skip flyline Python plot for net " << clk_net->get_net_name() << " because driver pin is null.";
      continue;
    }
    if (driver != nullptr && driver->get_location() == Point(-1, -1)) {
      continue;
    }
    auto level = resolveDriverLevel(driver);
    for (auto load_pin : clk_net->get_load_pins()) {
      py_ofs << "plt.plot([" << driver_pin->get_location().x() << "," << load_pin->get_location().x() << "],["
             << driver_pin->get_location().y() << "," << load_pin->get_location().y() << "],color=colors[" << level
             << "],linewidth=line_width[" << level << "],zorder=" << level << ")" << std::endl;
    }
  }
  for (auto* idb_inst : idb_insts) {
    auto* inst_box = idb_inst->get_bounding_box();
    auto* cts_inst = design->findInstance(idb_inst->get_name());
    int level = 0;
    if (cts_inst) {
      level = cts_inst->get_level();
    }
    if (level <= 1) {
      continue;
    }
    py_ofs << "plt.gca().add_patch(Rectangle((" << inst_box->get_low_x() << "," << inst_box->get_low_y() << "),"
           << inst_box->get_high_x() - inst_box->get_low_x() << "," << inst_box->get_high_y() - inst_box->get_low_y()
           << ",linewidth=" << 1 + 1.0 * level / 10 << ",edgecolor='black',facecolor=inst_colors[" << level + 1 << "],zorder=" << level + 1
           << "))" << std::endl;
  }
  py_ofs << "plt.axis('square')\n";
  py_ofs << "plt.axis('off')\n";
  py_ofs << "plt.savefig('cts_flyline.png', dpi=300, bbox_inches='tight')" << std::endl;
}

void GDSPloter::writeJsonDesign(const std::string& path)
{
  auto* design = CTSAPIInst.get_design();
  auto* config = CTSAPIInst.get_config();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();

  auto file_path = path;
  if (file_path.empty()) {
    auto dir = std::filesystem::path(config->get_work_dir()).append("output").string();
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directories(dir);
    }
    file_path = std::filesystem::path(dir).append("cts_design.json").string();
  }

  // Calculate the maximum level of instances
  int max_level = 0;
  auto* idb_design = db_wrapper->get_idb()->get_def_service()->get_design();
  auto idb_insts = idb_design->get_instance_list()->get_instance_list();
  for (auto* idb_inst : idb_insts) {
    if (auto* cts_inst = design->findInstance(idb_inst->get_name())) {
      max_level = std::max(max_level, cts_inst->get_level());
    }
  }

  // Create JSON object
  nlohmann::json json_data;
  json_data["design"]["max_level"] = max_level;

  // Add instance information
  nlohmann::json instances = nlohmann::json::array();
  for (auto* idb_inst : idb_insts) {
    nlohmann::json instance;
    instance["name"] = idb_inst->get_name();
    instances.push_back(instance);
  }
  json_data["design"]["instances"] = instances;

  // Add wire information
  nlohmann::json nets = nlohmann::json::array();
  for (auto& clk_nets = design->get_nets(); auto& clk_net : clk_nets) {
    auto* driver_pin = resolveDriverPin(clk_net);
    auto* driver = resolveDriverInst(clk_net, driver_pin);
    if (driver_pin == nullptr) {
      LOG_WARNING << "Skip JSON design export for net " << clk_net->get_net_name() << " because driver pin is null.";
      continue;
    }
    if (driver != nullptr && driver->get_location() == Point(-1, -1)) {
      continue;
    }

    nlohmann::json net;
    net["name"] = clk_net->get_net_name();
    net["driver_level"] = resolveDriverLevel(driver);
    net["driver_location"]["x"] = driver_pin->get_location().x();
    net["driver_location"]["y"] = driver_pin->get_location().y();

    // Add signal wires
    nlohmann::json wires = nlohmann::json::array();
    for (const auto& signal_wires = clk_net->get_signal_wires(); const auto& wire : signal_wires) {
      auto first = wire.get_first().point;
      auto second = wire.get_second().point;

      nlohmann::json wire_obj;
      wire_obj["start"]["x"] = first.x();
      wire_obj["start"]["y"] = first.y();
      wire_obj["end"]["x"] = second.x();
      wire_obj["end"]["y"] = second.y();

      wires.push_back(wire_obj);
    }
    net["wires_layout"] = wires;

    // Add delay information
    nlohmann::json delays = nlohmann::json::array();

    // Only consider a single clock source for now.
    auto clk_port = design->get_clocks().front()->get_clock_name();
    
    for (auto& p : clk_net->get_load_pins()) {
      auto delay = CTSAPIInst.getClockAT(p->get_full_name(), clk_port);

      nlohmann::json delay_obj;
      delay_obj["from"] = driver_pin->get_full_name();
      delay_obj["to"] = p->get_full_name();
      delay_obj["delay"] = delay;
      delays.push_back(delay_obj);
    }
    net["wires_delay"] = delays;

    nets.push_back(net);
  }
  json_data["design"]["nets"] = nets;

  // Add blockage information
  nlohmann::json blockages = nlohmann::json::array();
  for (auto idb_blockages = idb_design->get_blockage_list()->get_blockage_list(); auto* blockage : idb_blockages) {
    auto blockage_rect_list = blockage->get_rect_list();
    if (blockage_rect_list.empty()) {
      continue;
    }

    for (size_t i = 0; i < blockage_rect_list.size(); ++i) {
      auto* blockage_rect = blockage_rect_list[i];

      nlohmann::json blockage_obj;
      blockage_obj["name"] = blockage->get_instance_name() + "_" + std::to_string(i);
      blockage_obj["bounding_box"]["low_x"] = blockage_rect->get_low_x();
      blockage_obj["bounding_box"]["low_y"] = blockage_rect->get_low_y();
      blockage_obj["bounding_box"]["high_x"] = blockage_rect->get_high_x();
      blockage_obj["bounding_box"]["high_y"] = blockage_rect->get_high_y();

      blockages.push_back(blockage_obj);
    }
  }
  json_data["design"]["blockages"] = blockages;

  // Add core bounding box information
  auto* core = db_wrapper->get_core_bounding_box();
  json_data["design"]["core"]["bounding_box"]["low_x"] = core->get_low_x();
  json_data["design"]["core"]["bounding_box"]["low_y"] = core->get_low_y();
  json_data["design"]["core"]["bounding_box"]["high_x"] = core->get_high_x();
  json_data["design"]["core"]["bounding_box"]["high_y"] = core->get_high_y();

  std::ofstream json_file(file_path);
  json_file << json_data.dump(2);
  json_file.close();
}

void GDSPloter::refPolygon(std::fstream& log_ofs, const string& name)
{
  writeSRef(log_ofs, name);
}

void GDSPloter::insertInstance(std::fstream& log_ofs, CtsInstance* inst)
{
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto rect = db_wrapper->get_bounding_box(inst);
  string name = inst->get_name();
  int layer = inst->get_level();
  insertPolygon(log_ofs, rect, name, layer);
}

void GDSPloter::insertWire(std::fstream& log_ofs, const Point& begin, const Point& end, const string& name, const int& layer,
                           const int& width)
{
  strBegin(log_ofs);
  writeRecordAscii(log_ofs, GdsRecordType::kStrName, name);
  writePath(log_ofs, layer, width, begin, end);
  strEnd(log_ofs);
}

void GDSPloter::refInstance(std::fstream& log_ofs, CtsInstance* inst)
{
  refPolygon(log_ofs, inst->get_name());
}

void GDSPloter::plotPolygons(std::fstream& log_ofs, const std::vector<IdbRect*>& polys, const string& name, int layer)
{
  head(log_ofs);
  size_t idx = 0;
  for (auto poly : polys) {
    insertPolygon(log_ofs, poly, name + std::to_string(idx++));
  }
  tail(log_ofs);
}

void GDSPloter::insertPolygon(std::fstream& log_ofs, IdbRect* poly, const string& name, int layer, const int& type)
{
  insertPolygon(log_ofs, *poly, name, layer, type);
}

void GDSPloter::insertPolygon(std::fstream& log_ofs, IdbRect& poly, const string& name, int layer, const int& type)
{
  std::vector<Point> points
      = {Point(poly.get_low_x(), poly.get_low_y()), Point(poly.get_low_x(), poly.get_high_y()), Point(poly.get_high_x(), poly.get_high_y()),
         Point(poly.get_high_x(), poly.get_low_y()), Point(poly.get_low_x(), poly.get_low_y())};
  strBegin(log_ofs);
  writeRecordAscii(log_ofs, GdsRecordType::kStrName, name);
  writeBoundary(log_ofs, layer, type, points);
  strEnd(log_ofs);
}

void GDSPloter::topBegin(std::fstream& log_ofs)
{
  writeRecordAscii(log_ofs, GdsRecordType::kStrName, "top");
}

void GDSPloter::strBegin(std::fstream& log_ofs)
{
  auto now = std::time(nullptr);
  writeRecord(log_ofs, GdsRecordType::kBgnStr, GdsDataType::kInt16, packTimestampPayload(now, now));
}

void GDSPloter::strEnd(std::fstream& log_ofs)
{
  writeRecord(log_ofs, GdsRecordType::kEndStr, GdsDataType::kNoData);
}

void GDSPloter::plotInstances(std::fstream& log_ofs, vector<CtsInstance*>& insts)
{
  head(log_ofs);

  for (auto* inst : insts) {
    insertInstance(log_ofs, inst);
  }

  strBegin(log_ofs);
  topBegin(log_ofs);
  for (auto* inst : insts) {
    refInstance(log_ofs, inst);
  }
  strEnd(log_ofs);

  tail(log_ofs);
}

void GDSPloter::head(std::fstream& log_ofs)
{
  auto now = std::time(nullptr);
  writeRecordInt16(log_ofs, GdsRecordType::kHeader, {600});
  writeRecord(log_ofs, GdsRecordType::kBgnLib, GdsDataType::kInt16, packTimestampPayload(now, now));
  writeRecordAscii(log_ofs, GdsRecordType::kLibName, "CTS_Lib");
  writeRecord(log_ofs, GdsRecordType::kUnits, GdsDataType::kReal8, packReal8Payload({0.001, 1e-9}));
}

void GDSPloter::tail(std::fstream& log_ofs)
{
  writeRecord(log_ofs, GdsRecordType::kEndLib, GdsDataType::kNoData);
}

}  // namespace icts
