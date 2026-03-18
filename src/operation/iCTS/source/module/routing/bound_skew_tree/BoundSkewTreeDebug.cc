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
 * @file BoundSkewTreeDebug.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Debug-only helpers for bound-skew tree visualization and logging.
 */

#include "BoundSkewTreeDebug.hh"

#include <filesystem>
#include <fstream>

#include "GeomCalc.hh"
#include "config/Config.hh"
#include "logger/Logger.hh"

namespace icts::bst::debug {

void PrintPoint(const Point& pt)
{
  CTS_LOG_INFO << "x: " << pt.x << " y: " << pt.y << " max: " << pt.max << " min: " << pt.min << " val: " << pt.val;
}

void PrintArea(const Area* area)
{
  CTS_LOG_INFO << "area: " << area->get_name();
  std::ranges::for_each(area->getMrLines(), [&](Line& line) {
    PrintPoint(line[kHead]);
    PrintPoint(line[kTail]);
  });
}

void WritePy(const std::vector<Point>& pts, const std::string& file)
{
  auto dir = ConfigInst.get_work_dir() + "/file";
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }
  std::ofstream ofs(dir + "/" + file + ".py");
  ofs.setf(std::ios::fixed, std::ios::floatfield);
  ofs.precision(4);
  ofs << "import matplotlib.pyplot as plt\n";
  ofs << "import numpy as np\n";
  ofs << "x = [";
  for (auto pt : pts) {
    ofs << pt.x << ", ";
  }
  ofs << pts.front().x << "]\n";
  ofs << "y = [";
  for (auto pt : pts) {
    ofs << pt.y << ", ";
  }
  ofs << pts.front().y << "]\n";
  ofs << "plt.plot(x, y)\n";
  ofs << "plt.show()\n";
  ofs << "plt.savefig('" + file + ".png')\n";
}

void WritePy(Area* area, const std::string& file)
{
  auto dir = ConfigInst.get_work_dir() + "/file";
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }
  std::ofstream ofs(dir + "/" + file + ".py");
  ofs.setf(std::ios::fixed, std::ios::floatfield);
  ofs.precision(4);
  ofs << "import matplotlib.pyplot as plt\n";
  ofs << "import numpy as np\n";
  std::vector<Area*> stack{area};
  while (!stack.empty()) {
    auto* cur = stack.back();
    stack.pop_back();
    if (cur->get_right()) {
      stack.push_back(cur->get_right());
    }
    if (cur->get_left()) {
      stack.push_back(cur->get_left());
    }
    if (cur->get_mr().empty()) {
      continue;
    }

    ofs << "x = [";
    for (auto pt : cur->get_convex_hull()) {
      ofs << pt.x << ", ";
    }
    ofs << cur->get_convex_hull().front().x << "]\n";
    ofs << "y = [";
    for (auto pt : cur->get_convex_hull()) {
      ofs << pt.y << ", ";
    }
    ofs << cur->get_convex_hull().front().y << "]\n";
    ofs << "plt.plot(x, y, \"--b\", linewidth=1)\n";

    ofs << "x = [";
    for (auto pt : cur->get_mr()) {
      ofs << pt.x << ", ";
    }
    ofs << cur->get_mr().front().x << "]\n";
    ofs << "y = [";
    for (auto pt : cur->get_mr()) {
      ofs << pt.y << ", ";
    }
    ofs << cur->get_mr().front().y << "]\n";
    ofs << "plt.plot(x, y, \"o\", color='red', markersize=1)\n";

    auto center = GeomCalc::centerPt(cur->get_convex_hull());
    ofs << "plt.text(" << center.x << ", " << center.y << ", '" << cur->get_name() << "', fontsize=4)\n\n";
  }
  ofs << "plt.savefig('" + file + ".png', dpi=900)\n";
  ofs << "plt.show()\n";
}

}  // namespace icts::bst::debug
