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
 * @file CTSContext.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <functional>
#include <iosfwd>
#include <memory>

#include "DesignCommitter.hh"
#include "SolverRuntimeContext.hh"
#include "context/EvaluatorRuntimeContext.hh"

namespace ista {
class TimingEngine;
}  // namespace ista

namespace icts {

class CtsConfig;
class CtsDBWrapper;
class CtsDesign;
class CtsLibs;
class CtsReportTable;
class CtsRuntimeInterface;
class Evaluator;
class ModelFactory;

struct CTSContext
{
  CtsConfig* config = nullptr;
  CtsDesign* design = nullptr;
  CtsDBWrapper* db_wrapper = nullptr;
  CtsReportTable* report = nullptr;
  std::ofstream* log_ofs = nullptr;
  CtsLibs* libs = nullptr;
  Evaluator* evaluator = nullptr;
  ModelFactory* model_factory = nullptr;
  ista::TimingEngine* timing_engine = nullptr;

  SolverRuntimeContext solver_runtime;
  DesignCommitter::RuntimeContext committer_runtime;
  std::shared_ptr<CtsRuntimeInterface> source_runtime;
  std::shared_ptr<EvaluatorRuntimeContextInterface> evaluator_runtime;

  [[nodiscard]] bool isReady() const
  {
    return config != nullptr && design != nullptr && db_wrapper != nullptr && evaluator != nullptr && solver_runtime.isReady()
           && committer_runtime.isValid() && source_runtime != nullptr && evaluator_runtime != nullptr;
  }
};

}  // namespace icts
