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
 * @file ScipMathHtreeSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-25
 * @brief External SCIP MILP backend implementation for the mathematical H-tree slot-choice model.
 */

#include "synthesis/htree/analytical_solver/solver/ScipMathHtreeSolver.hh"

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <ratio>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace icts::htree::analytical_solver {
namespace {

constexpr double kAnchorFloor = 1e-30;
constexpr double kUsableAnchorGap = 0.25;
constexpr int kCommandSuccess = 0;
constexpr const char* kBackendName = "SCIP external LP";

struct SolverPaths
{
  std::filesystem::path model_path;
  std::filesystem::path solution_path;
  std::filesystem::path log_path;
};

struct MathHtreeModelStats
{
  std::size_t binary_variable_count = 0U;
  std::size_t continuous_variable_count = 0U;
  std::size_t constraint_count = 0U;
};

auto SanitizeName(std::string name) -> std::string
{
  for (char& ch : name) {
    const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    if (!valid) {
      ch = '_';
    }
  }
  return name.empty() ? "math_htree" : name;
}

auto Trim(const std::string& text) -> std::string
{
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1U);
}

auto VarName(const std::string& prefix, std::size_t first_index) -> std::string
{
  return prefix + "_" + std::to_string(first_index);
}

auto VarName(const std::string& prefix, std::size_t first_index, std::size_t second_index) -> std::string
{
  return prefix + "_" + std::to_string(first_index) + "_" + std::to_string(second_index);
}

auto LevelBufferVarName(std::size_t level_index) -> std::string
{
  return VarName("level_buf", level_index);
}

auto BoundMax(const std::vector<double>& values) -> double
{
  double bound = 0.0;
  for (const double value : values) {
    if (std::isfinite(value)) {
      bound = std::max(bound, std::abs(value));
    }
  }
  return bound;
}

auto EvalRangeBound(const MathHtreeAffineFunction& function, const MathHtreeDomain& domain) -> double
{
  return BoundMax({
      function.evaluate(domain.input_slew_min_ns, domain.load_cap_min_pf),
      function.evaluate(domain.input_slew_min_ns, domain.load_cap_max_pf),
      function.evaluate(domain.input_slew_max_ns, domain.load_cap_min_pf),
      function.evaluate(domain.input_slew_max_ns, domain.load_cap_max_pf),
  });
}

auto ResolveGlobalInputSlewUpper(const MathHtreeProblem& problem) -> double
{
  double upper = problem.root_input_slew_ns;
  for (const auto& slot : problem.slots) {
    for (const auto& choice : slot.choices) {
      upper = std::max(upper, choice.domain.input_slew_max_ns);
      upper = std::max(upper, EvalRangeBound(choice.output_slew_ns, choice.domain));
      if (std::isfinite(choice.max_output_slew_ns)) {
        upper = std::max(upper, choice.max_output_slew_ns);
      }
    }
  }
  return std::max(upper, problem.root_input_slew_ns);
}

auto ResolveGlobalLoadCapUpper(const MathHtreeProblem& problem) -> double
{
  double upper = problem.leaf_load_cap_pf;
  for (const auto& slot : problem.slots) {
    for (const auto& choice : slot.choices) {
      upper = std::max(upper, choice.domain.load_cap_max_pf);
      upper = std::max(upper, EvalRangeBound(choice.source_cap_pf, choice.domain));
      if (std::isfinite(choice.max_source_cap_pf)) {
        upper = std::max(upper, choice.max_source_cap_pf);
      }
    }
  }
  return std::max(upper, problem.leaf_load_cap_pf);
}

auto ResolveGlobalMetricUpper(const MathHtreeProblem& problem, bool delay) -> double
{
  double upper = 0.0;
  for (const auto& slot : problem.slots) {
    double slot_upper = 0.0;
    for (const auto& choice : slot.choices) {
      const auto& metric = delay ? choice.delay_ns : choice.power_w;
      slot_upper = std::max(slot_upper, EvalRangeBound(metric, choice.domain));
      slot_upper = std::max(slot_upper, -metric.evaluate(choice.domain.input_slew_min_ns, choice.domain.load_cap_min_pf));
      slot_upper = std::max(slot_upper, -metric.evaluate(choice.domain.input_slew_min_ns, choice.domain.load_cap_max_pf));
      slot_upper = std::max(slot_upper, -metric.evaluate(choice.domain.input_slew_max_ns, choice.domain.load_cap_min_pf));
      slot_upper = std::max(slot_upper, -metric.evaluate(choice.domain.input_slew_max_ns, choice.domain.load_cap_max_pf));
      if (!delay) {
        slot_upper = std::max(slot_upper, EvalRangeBound(choice.source_boundary_power_w, choice.domain));
      }
    }
    upper = std::max(upper, slot_upper);
  }
  return std::max(upper, kAnchorFloor);
}

auto FloorLog2(std::size_t value) -> unsigned
{
  unsigned result = 0U;
  while (value > 1U) {
    value >>= 1U;
    ++result;
  }
  return result;
}

auto CountChoiceConstraints(const MathHtreeProblem& problem) -> std::size_t
{
  std::size_t constraint_count = 2U;
  for (const auto& slot : problem.slots) {
    constraint_count += 13U;
    constraint_count += slot.choices.size() * 8U;
    for (const auto& choice : slot.choices) {
      if (std::isfinite(choice.max_output_slew_ns)) {
        constraint_count += 1U;
      }
      if (std::isfinite(choice.max_source_cap_pf)) {
        constraint_count += 1U;
      }
    }
  }
  if (problem.slots.size() > 1U) {
    constraint_count += 2U * (problem.slots.size() - 1U);
  }
  return constraint_count;
}

auto CountExposedBoundaryCompatibilityConstraints(const MathHtreeProblem& problem) -> std::size_t
{
  std::size_t constraint_count = 0U;
  for (std::size_t upstream_slot_index = 0U; upstream_slot_index + 1U < problem.slots.size(); ++upstream_slot_index) {
    for (const auto& upstream_choice : problem.slots.at(upstream_slot_index).choices) {
      if (!upstream_choice.sink_has_buffer) {
        continue;
      }
      for (std::size_t downstream_slot_index = upstream_slot_index + 1U; downstream_slot_index < problem.slots.size();
           ++downstream_slot_index) {
        for (const auto& downstream_choice : problem.slots.at(downstream_slot_index).choices) {
          if (!downstream_choice.source_has_buffer || upstream_choice.sink_strength_rank >= downstream_choice.source_strength_rank) {
            continue;
          }
          ++constraint_count;
        }
      }
    }
  }
  return constraint_count;
}

auto CountLevelLegalityConstraints(const MathHtreeProblem& problem) -> std::size_t
{
  std::size_t constraint_count = 0U;
  for (const auto& level : problem.levels) {
    constraint_count += 1U;
    for (std::size_t slot_offset = 0U; slot_offset < level.slot_count; ++slot_offset) {
      const auto& slot = problem.slots.at(level.first_slot_index + slot_offset);
      for (const auto& choice : slot.choices) {
        if (choice.hasAnyBuffer()) {
          ++constraint_count;
        }
      }
    }
    if (level.is_leaf_level && problem.max_fanout > 0U) {
      ++constraint_count;
    }
    if (level.require_terminal_branch_buffer) {
      ++constraint_count;
    }
  }
  if (problem.max_fanout > 1U && !problem.levels.empty()) {
    const unsigned max_bufferless_run = FloorLog2(problem.max_fanout) - 1U;
    const std::size_t window_size = static_cast<std::size_t>(max_bufferless_run) + 1U;
    if (window_size <= problem.levels.size()) {
      constraint_count += problem.levels.size() - window_size + 1U;
    }
  }
  return constraint_count;
}

auto MakeModelStats(const MathHtreeProblem& problem) -> MathHtreeModelStats
{
  MathHtreeModelStats stats;
  stats.continuous_variable_count = problem.slots.size() * 10U;
  for (const auto& slot : problem.slots) {
    stats.binary_variable_count += slot.choices.size();
    stats.continuous_variable_count += slot.choices.size() * 4U;
  }
  stats.binary_variable_count += problem.levels.size();

  stats.constraint_count
      = CountChoiceConstraints(problem) + CountExposedBoundaryCompatibilityConstraints(problem) + CountLevelLegalityConstraints(problem);
  return stats;
}

auto BuildPaths(const ScipMathHtreeSolverOptions& options, const MathHtreeProblem& problem, MathHtreeObjective objective) -> SolverPaths
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string base_name = SanitizeName(problem.name) + "_" + ToString(objective) + "_" + std::to_string(now);
  return SolverPaths{
      .model_path = options.working_directory / (base_name + ".lp"),
      .solution_path = options.working_directory / (base_name + ".sol"),
      .log_path = options.working_directory / (base_name + ".log"),
  };
}

auto EnvPath(const char* name) -> std::filesystem::path
{
  const char* value = std::getenv(name);
  if (value == nullptr || std::string(value).empty()) {
    return {};
  }
  return std::filesystem::path(value);
}

auto IsRegularPath(const std::filesystem::path& path) -> bool
{
  if (path.empty()) {
    return false;
  }
  std::error_code filesystem_error;
  return std::filesystem::exists(path, filesystem_error) && !std::filesystem::is_directory(path, filesystem_error);
}

auto SearchExecutableInPath(const std::string& executable_name) -> std::filesystem::path
{
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return {};
  }
  std::string path_text(path_env);
  std::size_t begin = 0U;
  while (begin <= path_text.size()) {
    const auto end = path_text.find(':', begin);
    const auto token = path_text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!token.empty()) {
      auto candidate = std::filesystem::path(token) / executable_name;
      if (IsRegularPath(candidate)) {
        return candidate;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return {};
}

auto ResolveScipBinaryPath(const ScipMathHtreeSolverOptions& options) -> std::filesystem::path
{
  if (IsRegularPath(options.scip_binary_path)) {
    return options.scip_binary_path;
  }
  auto env_binary = EnvPath("ICTS_SCIP_EXECUTABLE");
  if (IsRegularPath(env_binary)) {
    return env_binary;
  }
  auto path_binary = SearchExecutableInPath("scip");
  if (IsRegularPath(path_binary)) {
    return path_binary;
  }
  std::filesystem::path dev_binary = "/home/liweiguo/download/or-tools_x86_64_Ubuntu-20.04_cpp_v9.14.6206/bin/scip";
  if (IsRegularPath(dev_binary)) {
    return dev_binary;
  }
  return options.scip_binary_path;
}

auto ResolveScipLibraryPath(const ScipMathHtreeSolverOptions& options, const std::filesystem::path& binary_path) -> std::filesystem::path
{
  if (!options.scip_library_path.empty()) {
    return options.scip_library_path;
  }
  auto env_library_path = EnvPath("ICTS_SCIP_LIBRARY_PATH");
  if (!env_library_path.empty()) {
    return env_library_path;
  }
  if (!binary_path.empty()) {
    const auto sibling_lib = binary_path.parent_path().parent_path() / "lib";
    std::error_code filesystem_error;
    if (std::filesystem::is_directory(sibling_lib, filesystem_error)) {
      return sibling_lib;
    }
  }
  std::filesystem::path dev_library_path = "/home/liweiguo/download/or-tools_x86_64_Ubuntu-20.04_cpp_v9.14.6206/lib";
  std::error_code filesystem_error;
  if (std::filesystem::is_directory(dev_library_path, filesystem_error)) {
    return dev_library_path;
  }
  return {};
}

auto ResolveScipOptions(ScipMathHtreeSolverOptions options) -> ScipMathHtreeSolverOptions
{
  options.scip_binary_path = ResolveScipBinaryPath(options);
  options.scip_library_path = ResolveScipLibraryPath(options, options.scip_binary_path);
  return options;
}

auto AppendLinearTerm(std::ostream& output, double coefficient, const std::string& variable_name, bool& has_term) -> void
{
  if (std::abs(coefficient) <= 1e-30) {
    return;
  }
  output << (coefficient >= 0.0 ? " + " : " - ");
  output << std::setprecision(17) << std::abs(coefficient) << ' ' << variable_name;
  has_term = true;
}

auto WriteObjective(std::ostream& output, const MathHtreeProblem& problem, MathHtreeObjective objective) -> void
{
  output << "Minimize\n obj:";
  bool has_term = false;
  for (std::size_t slot_index = 0U; slot_index < problem.slots.size(); ++slot_index) {
    const auto& slot = problem.slots.at(slot_index);
    if (objective == MathHtreeObjective::kMinDelay) {
      AppendLinearTerm(output, slot.delay_weight, VarName("d", slot_index), has_term);
    } else if (objective == MathHtreeObjective::kMinPower) {
      AppendLinearTerm(output, slot.power_weight, VarName("op", slot_index), has_term);
    } else {
      AppendLinearTerm(output, slot.delay_weight / std::max(problem.min_delay_anchor_ns, kAnchorFloor), VarName("d", slot_index), has_term);
      AppendLinearTerm(output, slot.power_weight / std::max(problem.min_power_anchor_w, kAnchorFloor), VarName("op", slot_index), has_term);
    }
  }
  if (!has_term) {
    output << " 0";
  }
  output << '\n';
}

auto SubtractAffineFunction(const MathHtreeAffineFunction& lhs, const MathHtreeAffineFunction& rhs, double rhs_scale)
    -> MathHtreeAffineFunction
{
  return MathHtreeAffineFunction{
      .constant = lhs.constant - rhs_scale * rhs.constant,
      .input_slew_coefficient = lhs.input_slew_coefficient - rhs_scale * rhs.input_slew_coefficient,
      .load_cap_coefficient = lhs.load_cap_coefficient - rhs_scale * rhs.load_cap_coefficient,
  };
}

enum class AffineVariableKind
{
  kEval,
  kActual,
};

auto AppendChoiceAffineTerms(std::ostream& output, const MathHtreeAffineFunction& function, std::size_t slot_index,
                             std::size_t choice_index, AffineVariableKind variable_kind, double scale, bool& has_term) -> void
{
  const auto slew_variable
      = variable_kind == AffineVariableKind::kEval ? VarName("se", slot_index, choice_index) : VarName("si", slot_index, choice_index);
  const auto cap_variable
      = variable_kind == AffineVariableKind::kEval ? VarName("ce", slot_index, choice_index) : VarName("cl", slot_index, choice_index);
  AppendLinearTerm(output, scale * function.input_slew_coefficient, slew_variable, has_term);
  AppendLinearTerm(output, scale * function.load_cap_coefficient, cap_variable, has_term);
  AppendLinearTerm(output, scale * function.constant, VarName("z", slot_index, choice_index), has_term);
}

auto WriteLocalSumEquality(std::ostream& output, const std::string& name, const std::string& total_variable_name,
                           const std::string& local_variable_prefix, std::size_t slot_index, std::size_t choice_count) -> void
{
  output << ' ' << name << ": " << total_variable_name;
  bool has_term = true;
  for (std::size_t choice_index = 0U; choice_index < choice_count; ++choice_index) {
    AppendLinearTerm(output, -1.0, VarName(local_variable_prefix, slot_index, choice_index), has_term);
  }
  output << " = 0\n";
}

auto WriteSelectedContributionRange(std::ostream& output, const std::string& name, const std::string& value_name,
                                    const std::string& selected_name, double lower, double upper) -> void
{
  output << ' ' << name << "_lower: " << value_name << " - " << std::setprecision(17) << lower << ' ' << selected_name << " >= 0\n";
  output << ' ' << name << "_upper: " << value_name << " - " << std::setprecision(17) << upper << ' ' << selected_name << " <= 0\n";
}

auto WriteChoiceAffineSumEquality(std::ostream& output, const std::string& name, const std::string& value_name, const MathHtreeSlot& slot,
                                  std::size_t slot_index, MathHtreeAffineFunction MathHtreeChoice::* function_member,
                                  AffineVariableKind variable_kind) -> void
{
  output << ' ' << name << ": " << value_name;
  bool has_term = true;
  for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
    AppendChoiceAffineTerms(output, slot.choices.at(choice_index).*function_member, slot_index, choice_index, variable_kind, -1.0,
                            has_term);
  }
  output << " = 0\n";
}

auto WriteOwnedPowerEquality(std::ostream& output, const MathHtreeSlot& slot, std::size_t slot_index) -> void
{
  output << " selected_owned_power_" << slot_index << ": " << VarName("op", slot_index);
  bool has_term = true;
  const double source_boundary_ratio = slot.power_weight <= 0.0 ? 0.0 : slot.source_boundary_power_weight / slot.power_weight;
  for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
    const auto& choice = slot.choices.at(choice_index);
    AppendChoiceAffineTerms(output, SubtractAffineFunction(choice.power_w, choice.source_boundary_power_w, source_boundary_ratio),
                            slot_index, choice_index, AffineVariableKind::kEval, -1.0, has_term);
  }
  output << " = 0\n";
}

auto WriteChoiceAffineUpper(std::ostream& output, const std::string& name, const MathHtreeAffineFunction& function, std::size_t slot_index,
                            std::size_t choice_index, AffineVariableKind variable_kind, double upper) -> void
{
  output << ' ' << name << ':';
  bool has_term = false;
  AppendChoiceAffineTerms(output, function, slot_index, choice_index, variable_kind, 1.0, has_term);
  AppendLinearTerm(output, -upper, VarName("z", slot_index, choice_index), has_term);
  if (!has_term) {
    output << " 0";
  }
  output << " <= 0\n";
}

auto WriteExposedBoundaryCompatibilityConstraints(std::ostream& output, const MathHtreeProblem& problem) -> void
{
  for (std::size_t upstream_slot_index = 0U; upstream_slot_index + 1U < problem.slots.size(); ++upstream_slot_index) {
    const auto& upstream_slot = problem.slots.at(upstream_slot_index);
    for (std::size_t upstream_choice_index = 0U; upstream_choice_index < upstream_slot.choices.size(); ++upstream_choice_index) {
      const auto& upstream_choice = upstream_slot.choices.at(upstream_choice_index);
      if (!upstream_choice.sink_has_buffer) {
        continue;
      }
      for (std::size_t downstream_slot_index = upstream_slot_index + 1U; downstream_slot_index < problem.slots.size();
           ++downstream_slot_index) {
        const auto& downstream_slot = problem.slots.at(downstream_slot_index);
        for (std::size_t downstream_choice_index = 0U; downstream_choice_index < downstream_slot.choices.size();
             ++downstream_choice_index) {
          const auto& downstream_choice = downstream_slot.choices.at(downstream_choice_index);
          if (!downstream_choice.source_has_buffer || upstream_choice.sink_strength_rank >= downstream_choice.source_strength_rank) {
            continue;
          }

          output << " exposed_monotonic_choice_" << upstream_slot_index << '_' << upstream_choice_index << '_' << downstream_slot_index
                 << '_' << downstream_choice_index << ": " << VarName("z", upstream_slot_index, upstream_choice_index) << " + "
                 << VarName("z", downstream_slot_index, downstream_choice_index);
          for (std::size_t intermediate_slot_index = upstream_slot_index + 1U; intermediate_slot_index < downstream_slot_index;
               ++intermediate_slot_index) {
            const auto& intermediate_slot = problem.slots.at(intermediate_slot_index);
            for (std::size_t intermediate_choice_index = 0U; intermediate_choice_index < intermediate_slot.choices.size();
                 ++intermediate_choice_index) {
              if (intermediate_slot.choices.at(intermediate_choice_index).sink_has_buffer) {
                output << " - " << VarName("z", intermediate_slot_index, intermediate_choice_index);
              }
            }
          }
          output << " <= 1\n";
        }
      }
    }
  }
}

auto HasBufferChoice(const MathHtreeLevel& level, const MathHtreeProblem& problem) -> bool
{
  for (std::size_t slot_offset = 0U; slot_offset < level.slot_count; ++slot_offset) {
    const auto& slot = problem.slots.at(level.first_slot_index + slot_offset);
    for (const auto& choice : slot.choices) {
      if (choice.hasAnyBuffer()) {
        return true;
      }
    }
  }
  return false;
}

auto HasTerminalBranchChoice(const MathHtreeSlot& slot) -> bool
{
  for (const auto& choice : slot.choices) {
    if (choice.terminal_branch_buffer) {
      return true;
    }
  }
  return false;
}

auto WriteImpossibleBinaryConstraint(std::ostream& output, const std::string& name, const std::string& binary_name) -> void
{
  output << ' ' << name << ": " << binary_name << " = 2\n";
}

auto WriteLevelLegalityConstraints(std::ostream& output, const MathHtreeProblem& problem) -> void
{
  for (std::size_t level_index = 0U; level_index < problem.levels.size(); ++level_index) {
    const auto& level = problem.levels.at(level_index);
    const auto level_buffer_var = LevelBufferVarName(level_index);
    output << " level_buffer_upper_" << level_index << ": " << level_buffer_var;
    bool has_buffer_term = true;
    for (std::size_t slot_offset = 0U; slot_offset < level.slot_count; ++slot_offset) {
      const std::size_t slot_index = level.first_slot_index + slot_offset;
      const auto& slot = problem.slots.at(slot_index);
      for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
        if (slot.choices.at(choice_index).hasAnyBuffer()) {
          AppendLinearTerm(output, -1.0, VarName("z", slot_index, choice_index), has_buffer_term);
        }
      }
    }
    output << " <= 0\n";

    for (std::size_t slot_offset = 0U; slot_offset < level.slot_count; ++slot_offset) {
      const std::size_t slot_index = level.first_slot_index + slot_offset;
      const auto& slot = problem.slots.at(slot_index);
      for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
        if (!slot.choices.at(choice_index).hasAnyBuffer()) {
          continue;
        }
        output << " level_buffer_lower_" << level_index << '_' << slot_index << '_' << choice_index << ": "
               << VarName("z", slot_index, choice_index) << " - " << level_buffer_var << " <= 0\n";
      }
    }

    if (level.is_leaf_level && problem.max_fanout > 0U) {
      if (HasBufferChoice(level, problem)) {
        output << " leaf_level_buffer_required_" << level_index << ": " << level_buffer_var << " = 1\n";
      } else {
        WriteImpossibleBinaryConstraint(output, "leaf_level_buffer_required_" + std::to_string(level_index), level_buffer_var);
      }
    }
    if (level.require_terminal_branch_buffer) {
      const std::size_t last_slot_index = level.lastSlotIndex();
      if (!HasTerminalBranchChoice(problem.slots.at(last_slot_index))) {
        WriteImpossibleBinaryConstraint(output, "terminal_branch_buffer_required_" + std::to_string(level_index), level_buffer_var);
        continue;
      }
      output << " terminal_branch_buffer_required_" << level_index << ':';
      bool has_terminal_term = false;
      for (std::size_t choice_index = 0U; choice_index < problem.slots.at(last_slot_index).choices.size(); ++choice_index) {
        if (problem.slots.at(last_slot_index).choices.at(choice_index).terminal_branch_buffer) {
          AppendLinearTerm(output, 1.0, VarName("z", last_slot_index, choice_index), has_terminal_term);
        }
      }
      output << " = 1\n";
    }
  }

  if (problem.max_fanout <= 1U || problem.levels.empty()) {
    return;
  }
  const unsigned max_bufferless_run = FloorLog2(problem.max_fanout) - 1U;
  const std::size_t window_size = static_cast<std::size_t>(max_bufferless_run) + 1U;
  for (std::size_t first_level_index = 0U; first_level_index + window_size <= problem.levels.size(); ++first_level_index) {
    output << " source_fanout_window_" << first_level_index << ':';
    bool has_term = false;
    for (std::size_t offset = 0U; offset < window_size; ++offset) {
      AppendLinearTerm(output, 1.0, LevelBufferVarName(first_level_index + offset), has_term);
    }
    output << " >= 1\n";
  }
}

auto WriteModelFile(const MathHtreeProblem& problem, MathHtreeObjective objective, const std::filesystem::path& model_path,
                    std::string& failure_reason) -> bool
{
  std::ofstream output(model_path);
  if (!output.is_open()) {
    failure_reason = "cannot_open_lp_model_file";
    return false;
  }

  output.setf(std::ios::fixed, std::ios::floatfield);
  output << std::setprecision(17);

  const double input_slew_upper = ResolveGlobalInputSlewUpper(problem);
  const double load_cap_upper = ResolveGlobalLoadCapUpper(problem);
  const double delay_upper = ResolveGlobalMetricUpper(problem, true);
  const double power_upper = ResolveGlobalMetricUpper(problem, false);

  WriteObjective(output, problem, objective);
  output << "Subject To\n";
  output << " root_input_slew_boundary: " << VarName("s_in", 0U) << " = " << problem.root_input_slew_ns << '\n';
  output << " leaf_load_cap_boundary: " << VarName("c_load", problem.slots.size() - 1U) << " = " << problem.leaf_load_cap_pf << '\n';

  for (std::size_t slot_index = 0U; slot_index < problem.slots.size(); ++slot_index) {
    const auto& slot = problem.slots.at(slot_index);
    output << " choice_sum_" << slot_index << ':';
    bool has_choice_term = false;
    for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
      AppendLinearTerm(output, 1.0, VarName("z", slot_index, choice_index), has_choice_term);
    }
    output << " = 1\n";

    WriteLocalSumEquality(output, VarName("input_slew_disaggregation", slot_index), VarName("s_in", slot_index), "si", slot_index,
                          slot.choices.size());
    WriteLocalSumEquality(output, VarName("load_cap_disaggregation", slot_index), VarName("c_load", slot_index), "cl", slot_index,
                          slot.choices.size());
    WriteLocalSumEquality(output, VarName("eval_slew_disaggregation", slot_index), VarName("s_eval", slot_index), "se", slot_index,
                          slot.choices.size());
    WriteLocalSumEquality(output, VarName("eval_cap_disaggregation", slot_index), VarName("c_eval", slot_index), "ce", slot_index,
                          slot.choices.size());
    output << " input_slew_eval_floor_" << slot_index << ": " << VarName("s_eval", slot_index) << " - " << VarName("s_in", slot_index)
           << " >= 0\n";
    output << " load_cap_eval_floor_" << slot_index << ": " << VarName("c_eval", slot_index) << " - " << VarName("c_load", slot_index)
           << " >= 0\n";
    WriteChoiceAffineSumEquality(output, VarName("selected_output_slew", slot_index), VarName("s_out", slot_index), slot, slot_index,
                                 &MathHtreeChoice::output_slew_ns, AffineVariableKind::kEval);
    WriteChoiceAffineSumEquality(output, VarName("selected_delay", slot_index), VarName("d", slot_index), slot, slot_index,
                                 &MathHtreeChoice::delay_ns, AffineVariableKind::kEval);
    WriteChoiceAffineSumEquality(output, VarName("selected_power", slot_index), VarName("p", slot_index), slot, slot_index,
                                 &MathHtreeChoice::power_w, AffineVariableKind::kEval);
    WriteChoiceAffineSumEquality(output, VarName("selected_source_boundary_power", slot_index), VarName("pb", slot_index), slot, slot_index,
                                 &MathHtreeChoice::source_boundary_power_w, AffineVariableKind::kEval);
    WriteOwnedPowerEquality(output, slot, slot_index);
    WriteChoiceAffineSumEquality(output, VarName("selected_source_cap", slot_index), VarName("c_src", slot_index), slot, slot_index,
                                 &MathHtreeChoice::source_cap_pf, AffineVariableKind::kActual);

    for (std::size_t choice_index = 0U; choice_index < slot.choices.size(); ++choice_index) {
      const auto& choice = slot.choices.at(choice_index);
      const auto choice_name = VarName("z", slot_index, choice_index);
      WriteSelectedContributionRange(output, VarName("selected_input_slew_domain", slot_index, choice_index),
                                     VarName("si", slot_index, choice_index), choice_name, 0.0, choice.domain.input_slew_max_ns);
      WriteSelectedContributionRange(output, VarName("selected_load_cap_domain", slot_index, choice_index),
                                     VarName("cl", slot_index, choice_index), choice_name, 0.0, choice.domain.load_cap_max_pf);
      WriteSelectedContributionRange(output, VarName("selected_eval_slew_domain", slot_index, choice_index),
                                     VarName("se", slot_index, choice_index), choice_name, choice.domain.input_slew_min_ns,
                                     choice.domain.input_slew_max_ns);
      WriteSelectedContributionRange(output, VarName("selected_eval_cap_domain", slot_index, choice_index),
                                     VarName("ce", slot_index, choice_index), choice_name, choice.domain.load_cap_min_pf,
                                     choice.domain.load_cap_max_pf);
      output << " selected_eval_slew_actual_lower_" << slot_index << '_' << choice_index << ": " << VarName("se", slot_index, choice_index)
             << " - " << VarName("si", slot_index, choice_index) << " >= 0\n";
      output << " selected_eval_cap_actual_lower_" << slot_index << '_' << choice_index << ": " << VarName("ce", slot_index, choice_index)
             << " - " << VarName("cl", slot_index, choice_index) << " >= 0\n";
      if (std::isfinite(choice.max_output_slew_ns)) {
        WriteChoiceAffineUpper(output, VarName("selected_output_slew_limit", slot_index, choice_index), choice.output_slew_ns, slot_index,
                               choice_index, AffineVariableKind::kEval, choice.max_output_slew_ns);
      }
      if (std::isfinite(choice.max_source_cap_pf)) {
        WriteChoiceAffineUpper(output, VarName("selected_source_cap_limit", slot_index, choice_index), choice.source_cap_pf, slot_index,
                               choice_index, AffineVariableKind::kActual, choice.max_source_cap_pf);
      }
    }
    if (slot_index + 1U < problem.slots.size()) {
      output << " slew_continuity_" << slot_index << ": " << VarName("s_in", slot_index + 1U) << " - " << VarName("s_out", slot_index)
             << " = 0\n";
      output << " cap_continuity_" << slot_index << ": " << VarName("c_load", slot_index) << " - "
             << problem.slots.at(slot_index).downstream_cap_multiplier << ' ' << VarName("c_src", slot_index + 1U) << " = 0\n";
    }
  }
  WriteExposedBoundaryCompatibilityConstraints(output, problem);
  WriteLevelLegalityConstraints(output, problem);

  output << "Bounds\n";
  for (std::size_t slot_index = 0U; slot_index < problem.slots.size(); ++slot_index) {
    output << " 0 <= " << VarName("s_in", slot_index) << " <= " << input_slew_upper << '\n';
    output << " 0 <= " << VarName("s_eval", slot_index) << " <= " << input_slew_upper << '\n';
    output << " 0 <= " << VarName("s_out", slot_index) << " <= " << input_slew_upper << '\n';
    output << " 0 <= " << VarName("c_load", slot_index) << " <= " << load_cap_upper << '\n';
    output << " 0 <= " << VarName("c_eval", slot_index) << " <= " << load_cap_upper << '\n';
    output << " 0 <= " << VarName("c_src", slot_index) << " <= " << load_cap_upper << '\n';
    output << " 0 <= " << VarName("d", slot_index) << " <= " << delay_upper << '\n';
    output << " 0 <= " << VarName("p", slot_index) << " <= " << power_upper << '\n';
    output << " 0 <= " << VarName("pb", slot_index) << " <= " << power_upper << '\n';
    output << " 0 <= " << VarName("op", slot_index) << " <= " << power_upper << '\n';
    for (std::size_t choice_index = 0U; choice_index < problem.slots.at(slot_index).choices.size(); ++choice_index) {
      output << " 0 <= " << VarName("si", slot_index, choice_index) << " <= " << input_slew_upper << '\n';
      output << " 0 <= " << VarName("cl", slot_index, choice_index) << " <= " << load_cap_upper << '\n';
      output << " 0 <= " << VarName("se", slot_index, choice_index) << " <= " << input_slew_upper << '\n';
      output << " 0 <= " << VarName("ce", slot_index, choice_index) << " <= " << load_cap_upper << '\n';
    }
  }

  output << "Binary\n";
  for (std::size_t slot_index = 0U; slot_index < problem.slots.size(); ++slot_index) {
    for (std::size_t choice_index = 0U; choice_index < problem.slots.at(slot_index).choices.size(); ++choice_index) {
      output << ' ' << VarName("z", slot_index, choice_index) << '\n';
    }
  }
  for (std::size_t level_index = 0U; level_index < problem.levels.size(); ++level_index) {
    output << ' ' << LevelBufferVarName(level_index) << '\n';
  }
  output << "End\n";
  return true;
}

auto BuildScipArguments(const ScipMathHtreeSolverOptions& options, const SolverPaths& paths, double time_limit_ms)
    -> std::vector<std::string>
{
  std::vector<std::string> arguments;
  arguments.emplace_back(options.scip_binary_path.string());
  arguments.emplace_back("-q");
  arguments.emplace_back("-l");
  arguments.emplace_back(paths.log_path.string());
  if (time_limit_ms > 0.0 && std::isfinite(time_limit_ms)) {
    arguments.emplace_back("-c");
    arguments.emplace_back("set limits time " + std::to_string(time_limit_ms / 1000.0));
  }
  arguments.emplace_back("-c");
  arguments.emplace_back("read " + paths.model_path.string());
  arguments.emplace_back("-c");
  arguments.emplace_back("optimize");
  arguments.emplace_back("-c");
  arguments.emplace_back("write solution " + paths.solution_path.string());
  arguments.emplace_back("-c");
  arguments.emplace_back("quit");
  return arguments;
}

auto AppendCurrentEnvironmentValue(std::vector<std::string>& environment, const std::string& name) -> void
{
  const char* value = std::getenv(name.c_str());
  if (value != nullptr) {
    environment.emplace_back(name + "=" + value);
  }
}

auto BuildScipEnvironment(const ScipMathHtreeSolverOptions& options) -> std::vector<std::string>
{
  std::vector<std::string> environment;
  if (!options.scip_library_path.empty()) {
    const char* current_library_path = std::getenv("LD_LIBRARY_PATH");
    std::string library_path = options.scip_library_path.string();
    if (current_library_path != nullptr && std::string(current_library_path).empty()) {
      current_library_path = nullptr;
    }
    if (current_library_path != nullptr) {
      library_path += ':';
      library_path += current_library_path;
    }
    environment.emplace_back("LD_LIBRARY_PATH=" + library_path);
  } else {
    AppendCurrentEnvironmentValue(environment, "LD_LIBRARY_PATH");
  }
  AppendCurrentEnvironmentValue(environment, "PATH");
  AppendCurrentEnvironmentValue(environment, "HOME");
  AppendCurrentEnvironmentValue(environment, "TMPDIR");
  return environment;
}

auto RunScipExecutable(const ScipMathHtreeSolverOptions& options, const SolverPaths& paths, double time_limit_ms) -> int
{
  auto arguments = BuildScipArguments(options, paths, time_limit_ms);
  auto environment = BuildScipEnvironment(options);
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1U);
  for (auto& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  std::vector<char*> envp;
  envp.reserve(environment.size() + 1U);
  for (auto& entry : environment) {
    envp.push_back(entry.data());
  }
  envp.push_back(nullptr);

  pid_t child_pid = 0;
  const int spawn_status = ::posix_spawn(&child_pid, options.scip_binary_path.c_str(), nullptr, nullptr, argv.data(), envp.data());
  if (spawn_status != kCommandSuccess) {
    return -1;
  }

  int wait_status = 0;
  while (::waitpid(child_pid, &wait_status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  return wait_status;
}

auto ParseDoubleAfterColon(const std::string& line, double& value) -> bool
{
  const auto separator = line.find(':');
  if (separator == std::string::npos) {
    return false;
  }
  std::string text = Trim(line.substr(separator + 1U));
  const auto percent_pos = text.find('%');
  if (percent_pos != std::string::npos) {
    text = text.substr(0U, percent_pos);
  }
  if (text == "infinite" || text == "Infinity" || text == "inf") {
    value = std::numeric_limits<double>::infinity();
    return true;
  }
  if (text == "-infinite" || text == "-Infinity" || text == "-inf") {
    value = -std::numeric_limits<double>::infinity();
    return true;
  }
  std::istringstream stream(text);
  stream >> value;
  return !stream.fail();
}

auto ParseScipLogFile(const std::filesystem::path& log_path, MathHtreeSolution& solution) -> void
{
  std::ifstream input(log_path);
  if (!input.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.find("Solving Nodes") != std::string::npos) {
      const auto separator = line.find(':');
      if (separator != std::string::npos) {
        std::istringstream stream(line.substr(separator + 1U));
        std::size_t node_count = 0U;
        if (stream >> node_count) {
          solution.branch_and_bound_node_count = node_count;
        }
      }
      continue;
    }
    if (line.find("Primal Bound") != std::string::npos) {
      double primal_bound = 0.0;
      if (ParseDoubleAfterColon(line, primal_bound)) {
        solution.primal_bound = primal_bound;
        solution.has_solver_incumbent = std::isfinite(primal_bound);
      }
      continue;
    }
    if (line.find("Dual Bound") != std::string::npos) {
      double dual_bound = 0.0;
      if (ParseDoubleAfterColon(line, dual_bound)) {
        solution.dual_bound = dual_bound;
      }
      continue;
    }
    if (line.find("Gap") != std::string::npos) {
      double gap_percent = 0.0;
      if (ParseDoubleAfterColon(line, gap_percent)) {
        solution.optimality_gap = gap_percent / 100.0;
      }
    }
  }
}

auto ParseSolutionFile(const std::filesystem::path& solution_path, MathHtreeSolution& solution, std::map<std::string, double>& values)
    -> bool
{
  std::ifstream input(solution_path);
  if (!input.is_open()) {
    solution.status = MathHtreeSolveStatus::kAbnormal;
    solution.failure_reason = "cannot_open_scip_solution_file";
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with("solution status:")) {
      if (line.find("optimal solution found") != std::string::npos) {
        solution.status = MathHtreeSolveStatus::kOptimal;
      } else if (line.find("timelimit") != std::string::npos || line.find("time limit") != std::string::npos) {
        solution.status = MathHtreeSolveStatus::kTimeout;
      } else if (line.find("infeasible") != std::string::npos) {
        solution.status = MathHtreeSolveStatus::kInfeasible;
      } else if (line.find("unbounded") != std::string::npos) {
        solution.status = MathHtreeSolveStatus::kUnbounded;
      } else {
        solution.status = MathHtreeSolveStatus::kFeasible;
      }
      continue;
    }
    if (line.starts_with("objective value:")) {
      std::istringstream stream(line.substr(std::string("objective value:").size()));
      stream >> solution.objective_value;
      solution.has_solver_incumbent = true;
      continue;
    }

    std::istringstream stream(line);
    std::string variable_name;
    double value = 0.0;
    stream >> variable_name >> value;
    if (!variable_name.empty() && stream) {
      values[variable_name] = value;
    }
  }
  const bool has_variable_assignment = !values.empty();
  if (solution.status == MathHtreeSolveStatus::kNotSolved) {
    solution.status = MathHtreeSolveStatus::kAbnormal;
    solution.failure_reason = "missing_scip_solution_status";
    return false;
  }
  if (!has_variable_assignment && solution.status != MathHtreeSolveStatus::kInfeasible
      && solution.status != MathHtreeSolveStatus::kUnbounded) {
    solution.status = MathHtreeSolveStatus::kAbnormal;
    solution.failure_reason = "missing_scip_variable_assignment";
    return false;
  }
  if (solution.status == MathHtreeSolveStatus::kTimeout && solution.has_solver_incumbent && has_variable_assignment
      && std::isfinite(solution.optimality_gap)) {
    solution.status = MathHtreeSolveStatus::kFeasibleWithGap;
  }
  return solution.hasUsableIntegerSolution();
}

auto ValueOrZero(const std::map<std::string, double>& values, const std::string& variable_name) -> double
{
  const auto it = values.find(variable_name);
  return it == values.end() ? 0.0 : it->second;
}

auto SelectedChoiceIndex(const std::map<std::string, double>& values, std::size_t slot_index, std::size_t choice_count) -> std::size_t
{
  std::size_t selected_index = 0U;
  double selected_value = -1.0;
  for (std::size_t choice_index = 0U; choice_index < choice_count; ++choice_index) {
    const double value = ValueOrZero(values, VarName("z", slot_index, choice_index));
    if (value > selected_value) {
      selected_value = value;
      selected_index = choice_index;
    }
  }
  return selected_index;
}

auto CollectSlotSolutions(const MathHtreeProblem& problem, const std::map<std::string, double>& values, MathHtreeSolution& solution) -> void
{
  solution.slots.reserve(problem.slots.size());
  for (std::size_t slot_index = 0U; slot_index < problem.slots.size(); ++slot_index) {
    MathHtreeSlotSolution slot_solution;
    slot_solution.selected_choice_index = SelectedChoiceIndex(values, slot_index, problem.slots.at(slot_index).choices.size());
    slot_solution.input_slew_ns = ValueOrZero(values, VarName("s_in", slot_index));
    slot_solution.output_slew_ns = ValueOrZero(values, VarName("s_out", slot_index));
    slot_solution.load_cap_pf = ValueOrZero(values, VarName("c_load", slot_index));
    slot_solution.source_cap_pf = ValueOrZero(values, VarName("c_src", slot_index));
    slot_solution.delay_ns = ValueOrZero(values, VarName("d", slot_index));
    slot_solution.power_w = ValueOrZero(values, VarName("p", slot_index));
    slot_solution.source_boundary_power_w = ValueOrZero(values, VarName("pb", slot_index));
    solution.total_delay_ns += problem.slots.at(slot_index).delay_weight * slot_solution.delay_ns;
    solution.total_power_w += problem.slots.at(slot_index).power_weight * slot_solution.power_w
                              - problem.slots.at(slot_index).source_boundary_power_weight * slot_solution.source_boundary_power_w;
    solution.slots.push_back(slot_solution);
  }
}

auto IsUsableAnchorSolution(const MathHtreeSolution& solution) -> bool
{
  if (!solution.hasUsableIntegerSolution()) {
    return false;
  }
  if (solution.status != MathHtreeSolveStatus::kFeasibleWithGap) {
    return true;
  }
  return std::isfinite(solution.optimality_gap) && solution.optimality_gap <= kUsableAnchorGap;
}

auto AppendAnchorGapFailureReason(MathHtreeSolution& solution, const std::string& anchor_name) -> void
{
  std::ostringstream reason;
  reason << anchor_name << "_anchor_unavailable:" << ToString(solution.status);
  if (solution.status == MathHtreeSolveStatus::kFeasibleWithGap) {
    reason << ":gap=" << std::setprecision(6) << solution.optimality_gap;
  } else if (!solution.failure_reason.empty()) {
    reason << ':' << solution.failure_reason;
  } else {
    reason << ":no_detail";
  }
  solution.failure_reason = reason.str();
}

auto MakeStatusSolution(MathHtreeSolveStatus status, std::string failure_reason) -> MathHtreeSolution
{
  MathHtreeSolution solution;
  solution.status = status;
  solution.failure_reason = std::move(failure_reason);
  solution.backend_name = kBackendName;
  return solution;
}

auto RunScip(const MathHtreeProblem& problem, MathHtreeObjective objective, const ScipMathHtreeSolverOptions& options) -> MathHtreeSolution
{
  std::string failure_reason;
  if (!problem.isValid(failure_reason)) {
    return MakeStatusSolution(MathHtreeSolveStatus::kModelInvalid, failure_reason);
  }
  if (objective == MathHtreeObjective::kNormalizedDelayPower
      && (problem.min_delay_anchor_ns <= kAnchorFloor || problem.min_power_anchor_w <= kAnchorFloor)) {
    return MakeStatusSolution(MathHtreeSolveStatus::kModelInvalid, "invalid_normalized_objective_anchor");
  }
  std::error_code filesystem_error;
  if (!std::filesystem::exists(options.scip_binary_path, filesystem_error)) {
    return MakeStatusSolution(MathHtreeSolveStatus::kSolverUnavailable, "missing_scip_binary");
  }

  std::filesystem::create_directories(options.working_directory, filesystem_error);
  if (filesystem_error) {
    return MakeStatusSolution(MathHtreeSolveStatus::kModelInvalid, "cannot_create_scip_working_directory");
  }
  const auto paths = BuildPaths(options, problem, objective);
  const auto model_stats = MakeModelStats(problem);
  if (!WriteModelFile(problem, objective, paths.model_path, failure_reason)) {
    return MakeStatusSolution(MathHtreeSolveStatus::kModelInvalid, failure_reason);
  }

  const auto start_time = std::chrono::steady_clock::now();
  const int command_status = RunScipExecutable(options, paths, problem.solve_time_limit_ms);
  const auto end_time = std::chrono::steady_clock::now();

  MathHtreeSolution solution;
  solution.backend_name = kBackendName;
  solution.binary_variable_count = model_stats.binary_variable_count;
  solution.continuous_variable_count = model_stats.continuous_variable_count;
  solution.variable_count = solution.binary_variable_count + solution.continuous_variable_count;
  solution.constraint_count = model_stats.constraint_count;
  solution.solve_wall_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  solution.min_delay_anchor_ns = problem.min_delay_anchor_ns;
  solution.min_power_anchor_w = problem.min_power_anchor_w;
  if (command_status == -1 || !WIFEXITED(command_status) || WEXITSTATUS(command_status) != kCommandSuccess) {
    solution.status = MathHtreeSolveStatus::kAbnormal;
    solution.failure_reason = "scip_command_failed";
    return solution;
  }

  std::map<std::string, double> values;
  ParseScipLogFile(paths.log_path, solution);
  const bool has_solution = ParseSolutionFile(paths.solution_path, solution, values);
  if (has_solution) {
    CollectSlotSolutions(problem, values, solution);
  }

  if (!options.keep_model_files && solution.hasUsableIntegerSolution()) {
    std::filesystem::remove(paths.model_path, filesystem_error);
    filesystem_error.clear();
    std::filesystem::remove(paths.solution_path, filesystem_error);
    filesystem_error.clear();
    std::filesystem::remove(paths.log_path, filesystem_error);
  }
  return solution;
}

auto MakeTimedProblem(const MathHtreeProblem& problem, double time_limit_ms) -> MathHtreeProblem
{
  auto timed_problem = problem;
  timed_problem.solve_time_limit_ms = time_limit_ms;
  return timed_problem;
}

}  // namespace

ScipMathHtreeSolver::ScipMathHtreeSolver(ScipMathHtreeSolverOptions options) : _options(ResolveScipOptions(std::move(options)))
{
}

auto ScipMathHtreeSolver::solve(const MathHtreeProblem& problem, MathHtreeObjective objective) const -> MathHtreeSolution
{
  return RunScip(problem, objective, _options);
}

auto ScipMathHtreeSolver::solveNormalizedTradeoff(const MathHtreeProblem& problem) const -> MathHtreeSolution
{
  std::string failure_reason;
  if (!problem.isValid(failure_reason)) {
    return MakeStatusSolution(MathHtreeSolveStatus::kModelInvalid, failure_reason);
  }

  MathHtreeProblem anchored_problem = problem;
  auto min_delay_solution = solve(MakeTimedProblem(anchored_problem, _options.anchor_time_limit_ms), MathHtreeObjective::kMinDelay);
  if (!IsUsableAnchorSolution(min_delay_solution) || min_delay_solution.total_delay_ns <= kAnchorFloor) {
    AppendAnchorGapFailureReason(min_delay_solution, "min_delay");
    return min_delay_solution;
  }
  anchored_problem.min_delay_anchor_ns = min_delay_solution.total_delay_ns;

  auto min_power_solution = solve(MakeTimedProblem(anchored_problem, _options.anchor_time_limit_ms), MathHtreeObjective::kMinPower);
  if (!IsUsableAnchorSolution(min_power_solution) || min_power_solution.total_power_w <= kAnchorFloor) {
    AppendAnchorGapFailureReason(min_power_solution, "min_power");
    return min_power_solution;
  }
  anchored_problem.min_power_anchor_w = min_power_solution.total_power_w;

  auto normalized_solution
      = solve(MakeTimedProblem(anchored_problem, _options.normalized_time_limit_ms), MathHtreeObjective::kNormalizedDelayPower);
  normalized_solution.min_delay_anchor_ns = anchored_problem.min_delay_anchor_ns;
  normalized_solution.min_power_anchor_w = anchored_problem.min_power_anchor_w;
  normalized_solution.solve_wall_time_ms += min_delay_solution.solve_wall_time_ms + min_power_solution.solve_wall_time_ms;
  normalized_solution.branch_and_bound_node_count
      += min_delay_solution.branch_and_bound_node_count + min_power_solution.branch_and_bound_node_count;
  normalized_solution.optimality_gap
      = std::max({normalized_solution.optimality_gap, min_delay_solution.optimality_gap, min_power_solution.optimality_gap});
  return normalized_solution;
}

}  // namespace icts::htree::analytical_solver
