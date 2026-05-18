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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastStaDmpCeff.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief OpenSTA-style DMP effective capacitance and load slew calculation for CTS fast STA.
 */

#include "FastStaDmpCeff.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace icts {
namespace {

constexpr double kOhmPfToNs = 1e-3;
constexpr double kCapDeltaPf = 1e-3;
constexpr double kSmallRdNsPerPf = 1e-5;
constexpr double kEpsilon = 1e-18;
constexpr double kDriverParamTolerance = 0.01;
constexpr double kThresholdTimeTolerance = 0.01;
constexpr std::size_t kFindRootMaxIter = 20U;
constexpr std::size_t kNewtonRaphsonMaxIter = 100U;
constexpr std::size_t kMaxOrder = 3U;

enum class DmpParam : std::size_t
{
  kT0 = 0U,
  kDt = 1U,
  kCeff = 2U
};

enum class DmpFunc : std::size_t
{
  kY20 = 0U,
  kY50 = 1U,
  kIpi = 2U
};

constexpr auto index(DmpParam param) -> std::size_t
{
  return static_cast<std::size_t>(param);
}

constexpr auto index(DmpFunc func) -> std::size_t
{
  return static_cast<std::size_t>(func);
}

struct GateValues
{
  bool valid = false;
  double delay_ns = 0.0;
  double table_slew_ns = 0.0;
  double measured_slew_ns = 0.0;
  double t_vl_ns = 0.0;
};

auto thresholdInRange(double value, double default_value) -> double
{
  return value > 0.0 && value < 1.0 ? value : default_value;
}

auto outputThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double
{
  return thresholdInRange(transition == FastStaTransition::kRise ? cell.output_threshold_rise : cell.output_threshold_fall, 0.5);
}

auto inputThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double
{
  return thresholdInRange(transition == FastStaTransition::kRise ? cell.input_threshold_rise : cell.input_threshold_fall, 0.5);
}

auto slewLowerThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double
{
  return thresholdInRange(transition == FastStaTransition::kRise ? cell.slew_lower_threshold_rise : cell.slew_lower_threshold_fall, 0.3);
}

auto slewUpperThreshold(const FastStaLibertyCell& cell, FastStaTransition transition) -> double
{
  return thresholdInRange(transition == FastStaTransition::kRise ? cell.slew_upper_threshold_rise : cell.slew_upper_threshold_fall, 0.7);
}

auto slewDerate(const FastStaLibertyCell& cell) -> double
{
  return cell.slew_derate_from_library > 0.0 ? cell.slew_derate_from_library : 1.0;
}

auto selectTable(const std::vector<FastStaLibertyTable>& tables, FastStaTransition transition) -> const FastStaLibertyTable*
{
  for (const auto& table : tables) {
    if (table.transition == transition && !table.empty()) {
      return &table;
    }
  }
  for (const auto& table : tables) {
    if (!table.empty()) {
      return &table;
    }
  }
  return nullptr;
}

auto lookupTable(const std::vector<FastStaLibertyTable>& tables, FastStaTransition transition, double input_slew_ns, double output_load_pf)
    -> double
{
  const auto* table = selectTable(tables, transition);
  if (table == nullptr) {
    return 0.0;
  }
  return table->lookup(input_slew_ns, output_load_pf).value_or(0.0);
}

auto gateDelaySlew(const FastStaLibertyCell& cell, FastStaTransition transition, double input_slew_ns, double ceff_pf)
    -> std::pair<double, double>
{
  return {lookupTable(cell.timing_arc.delay_tables, transition, input_slew_ns, ceff_pf),
          lookupTable(cell.timing_arc.slew_tables, transition, input_slew_ns, ceff_pf)};
}

auto gateModelRdNsPerPf(const FastStaLibertyCell& cell, const FastStaPiModel& pi, FastStaTransition transition, double input_slew_ns)
    -> double
{
  const auto cap1_pf = std::max(0.0, pi.near_cap_pf + pi.far_cap_pf);
  const auto cap2_pf = cap1_pf + kCapDeltaPf;
  const auto [delay1_ns, unused_slew1] = gateDelaySlew(cell, transition, input_slew_ns, cap1_pf);
  const auto [delay2_ns, unused_slew2] = gateDelaySlew(cell, transition, input_slew_ns, cap2_pf);
  (void) unused_slew1;
  (void) unused_slew2;
  const auto vth = outputThreshold(cell, transition);
  if (!std::isfinite(delay1_ns) || !std::isfinite(delay2_ns) || vth <= 0.0) {
    return 0.0;
  }
  return -std::log(vth) * std::abs(delay1_ns - delay2_ns) / kCapDeltaPf;
}

auto dmpExp(double x) -> double
{
  if (x < -12.0) {
    return 0.0;
  }
  auto y = 1.0 + x / 4096.0;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  y *= y;
  return y;
}

using RootFunc = std::function<void(double, double&, double&)>;

auto findRoot(const RootFunc& func, double x1, double x2, double x_tolerance, std::size_t max_iter) -> std::optional<double>
{
  if (!std::isfinite(x1) || !std::isfinite(x2) || max_iter == 0U) {
    return std::nullopt;
  }
  if (x1 == x2) {
    return std::nullopt;
  }

  double y1 = 0.0;
  double dy1 = 0.0;
  double y2 = 0.0;
  double dy2 = 0.0;
  func(x1, y1, dy1);
  func(x2, y2, dy2);
  if ((y1 > 0.0 && y2 > 0.0) || (y1 < 0.0 && y2 < 0.0)) {
    return std::nullopt;
  }
  if (y1 == 0.0) {
    return x1;
  }
  if (y2 == 0.0) {
    return x2;
  }
  if (y1 > 0.0) {
    std::swap(x1, x2);
  }

  auto root = (x1 + x2) * 0.5;
  auto dx_prev = std::abs(x2 - x1);
  auto dx = dx_prev;
  double y = 0.0;
  double dy = 0.0;
  func(root, y, dy);
  for (std::size_t iter = 0U; iter < max_iter; ++iter) {
    if (std::abs(dy) <= kEpsilon || (((root - x2) * dy - y) * ((root - x1) * dy - y) > 0.0)
        || (std::abs(2.0 * y) > std::abs(dx_prev * dy))) {
      dx_prev = dx;
      dx = (x2 - x1) * 0.5;
      root = x1 + dx;
    } else {
      dx_prev = dx;
      dx = y / dy;
      root -= dx;
    }
    if (std::abs(dx) <= x_tolerance * std::max(std::abs(root), 1e-12)) {
      return root;
    }

    func(root, y, dy);
    if (y < 0.0) {
      x1 = root;
    } else {
      x2 = root;
    }
  }
  return std::nullopt;
}

class DmpSolver
{
 public:
  DmpSolver(const FastStaLibertyCell& cell, const FastStaPiModel& pi, FastStaTransition transition, double input_slew_ns)
      : _cell(&cell),
        _transition(transition),
        _input_slew_ns(std::max(0.0, input_slew_ns)),
        _near_cap_pf(std::max(0.0, pi.near_cap_pf)),
        _far_cap_pf(std::max(0.0, pi.far_cap_pf)),
        _rpi_ns_per_pf(std::max(0.0, pi.resistance_ohm) * kOhmPfToNs),
        _rd_ns_per_pf(gateModelRdNsPerPf(cell, pi, transition, _input_slew_ns)),
        _output_threshold(outputThreshold(cell, transition)),
        _slew_lower_threshold(slewLowerThreshold(cell, transition)),
        _slew_upper_threshold(slewUpperThreshold(cell, transition)),
        _slew_derate(slewDerate(cell))
  {
  }

  auto solve() -> FastStaDmpDriverResult
  {
    if (useCapAlgorithm()) {
      return solveCap();
    }
    if (_near_cap_pf < _far_cap_pf * 1e-3) {
      return solveZeroC2();
    }
    return solvePi();
  }

 private:
  auto useCapAlgorithm() const -> bool
  {
    return _rd_ns_per_pf < kSmallRdNsPerPf || _rpi_ns_per_pf < _rd_ns_per_pf * 1e-3 || _far_cap_pf == 0.0
           || _far_cap_pf < _near_cap_pf * 1e-3 || _rpi_ns_per_pf == 0.0;
  }

  auto makeResult(FastStaDmpAlgorithm algorithm, double ceff_pf, double delay_ns, double slew_ns, bool waveform_valid,
                  double waveform_delay_ns = 0.0) const -> FastStaDmpDriverResult
  {
    return FastStaDmpDriverResult{
        .valid = true,
        .driver_waveform_valid = waveform_valid,
        .algorithm = algorithm,
        .transition = _transition,
        .driver_cell_master = _cell->cell_master,
        .ceff_pf = std::max(0.0, ceff_pf),
        .gate_delay_ns = std::max(0.0, delay_ns),
        .driver_slew_ns = std::max(0.0, slew_ns),
        .driver_waveform_delay_ns = std::max(0.0, waveform_delay_ns),
        .t0_ns = _t0_ns,
        .dt_ns = std::max(0.0, _dt_ns),
        .near_cap_pf = _near_cap_pf,
        .far_cap_pf = _far_cap_pf,
        .rpi_ns_per_pf = _rpi_ns_per_pf,
        .rd_ns_per_pf = _rd_ns_per_pf,
        .input_threshold = inputThreshold(*_cell, _transition),
        .output_threshold = _output_threshold,
        .slew_lower_threshold = _slew_lower_threshold,
        .slew_upper_threshold = _slew_upper_threshold,
        .slew_derate = _slew_derate,
        .pole1_per_ns = _p1_per_ns,
        .pole2_per_ns = _p2_per_ns,
        .zero1_per_ns = _z1_per_ns,
        .k0 = _k0,
        .k1 = _k1,
        .k2 = _k2,
        .k3 = _k3,
        .k4 = _k4,
    };
  }

  auto solveCap() -> FastStaDmpDriverResult
  {
    const auto ceff_pf = _near_cap_pf + _far_cap_pf;
    const auto [delay_ns, slew_ns] = gateDelaySlew(*_cell, _transition, _input_slew_ns, ceff_pf);
    return makeResult(FastStaDmpAlgorithm::kCap, ceff_pf, delay_ns, slew_ns, false);
  }

  auto solvePi() -> FastStaDmpDriverResult
  {
    _algorithm = FastStaDmpAlgorithm::kPi;
    _nr_order = 3U;
    if (!initPi()) {
      return solveCap();
    }

    if (!findDriverParams(_near_cap_pf + _far_cap_pf) && !findDriverParams(_near_cap_pf)) {
      return solveCap();
    }
    _ceff_pf = _x.at(index(DmpParam::kCeff));
    const auto [table_delay_ns, table_slew_ns] = gateDelaySlew(*_cell, _transition, _input_slew_ns, _ceff_pf);
    const auto driver_crossing = findDriverDelaySlew();
    if (!driver_crossing.has_value()) {
      return makeResult(FastStaDmpAlgorithm::kPi, _ceff_pf, table_delay_ns, table_slew_ns, false);
    }
    const auto [waveform_delay_ns, waveform_slew_ns] = *driver_crossing;
    return makeResult(FastStaDmpAlgorithm::kPi, _ceff_pf, table_delay_ns, waveform_slew_ns, true, waveform_delay_ns);
  }

  auto solveZeroC2() -> FastStaDmpDriverResult
  {
    _algorithm = FastStaDmpAlgorithm::kZeroC2;
    _nr_order = 2U;
    _ceff_pf = _far_cap_pf;
    if (!initZeroC2() || !findDriverParams(_far_cap_pf)) {
      const auto [delay_ns, slew_ns] = gateDelaySlew(*_cell, _transition, _input_slew_ns, _far_cap_pf);
      return makeResult(FastStaDmpAlgorithm::kZeroC2, _far_cap_pf, delay_ns, slew_ns, false);
    }

    const auto driver_crossing = findDriverDelaySlew();
    if (!driver_crossing.has_value()) {
      const auto [delay_ns, slew_ns] = gateDelaySlew(*_cell, _transition, _input_slew_ns, _far_cap_pf);
      return makeResult(FastStaDmpAlgorithm::kZeroC2, _far_cap_pf, delay_ns, slew_ns, false);
    }
    const auto [waveform_delay_ns, waveform_slew_ns] = *driver_crossing;
    return makeResult(FastStaDmpAlgorithm::kZeroC2, _far_cap_pf, waveform_delay_ns, waveform_slew_ns, true, waveform_delay_ns);
  }

  auto initPi() -> bool
  {
    if (_near_cap_pf <= 0.0 || _far_cap_pf <= 0.0 || _rpi_ns_per_pf <= 0.0 || _rd_ns_per_pf <= 0.0) {
      return false;
    }
    _z1_per_ns = 1.0 / (_rpi_ns_per_pf * _far_cap_pf);
    _k0 = 1.0 / (_rd_ns_per_pf * _near_cap_pf);
    const auto a = _rpi_ns_per_pf * _rd_ns_per_pf * _far_cap_pf * _near_cap_pf;
    const auto b = _rd_ns_per_pf * (_far_cap_pf + _near_cap_pf) + _rpi_ns_per_pf * _far_cap_pf;
    const auto discriminant = b * b - 4.0 * a;
    if (a <= 0.0 || discriminant < 0.0) {
      return false;
    }
    const auto root = std::sqrt(discriminant);
    _p1_per_ns = (b + root) / (2.0 * a);
    _p2_per_ns = (b - root) / (2.0 * a);
    const auto p1p2 = _p1_per_ns * _p2_per_ns;
    if (std::abs(p1p2) <= kEpsilon || std::abs(_p2_per_ns - _p1_per_ns) <= kEpsilon) {
      return false;
    }

    _k2 = _z1_per_ns / p1p2;
    _k1 = (1.0 - _k2 * (_p1_per_ns + _p2_per_ns)) / p1p2;
    _k4 = (_k1 * _p1_per_ns + _k2) / (_p2_per_ns - _p1_per_ns);
    _k3 = -_k1 - _k4;

    const auto z = (_far_cap_pf + _near_cap_pf) / (_rpi_ns_per_pf * _far_cap_pf * _near_cap_pf);
    _a_coeff = z / p1p2;
    _b_coeff = (z - _p1_per_ns) / (_p1_per_ns * (_p1_per_ns - _p2_per_ns));
    _d_coeff = (z - _p2_per_ns) / (_p2_per_ns * (_p2_per_ns - _p1_per_ns));
    return std::isfinite(_a_coeff) && std::isfinite(_b_coeff) && std::isfinite(_d_coeff);
  }

  auto initZeroC2() -> bool
  {
    if (_far_cap_pf <= 0.0 || _rpi_ns_per_pf <= 0.0 || _rd_ns_per_pf <= 0.0) {
      return false;
    }
    _z1_per_ns = 1.0 / (_rpi_ns_per_pf * _far_cap_pf);
    _p1_per_ns = 1.0 / (_far_cap_pf * (_rd_ns_per_pf + _rpi_ns_per_pf));
    if (std::abs(_z1_per_ns) <= kEpsilon || std::abs(_p1_per_ns) <= kEpsilon) {
      return false;
    }
    _k0 = _p1_per_ns / _z1_per_ns;
    _k2 = 1.0 / _k0;
    _k1 = (_p1_per_ns - _z1_per_ns) / (_p1_per_ns * _p1_per_ns);
    _k3 = -_k1;
    _k4 = 0.0;
    return std::isfinite(_k0) && std::isfinite(_k1) && std::isfinite(_k2) && std::isfinite(_k3);
  }

  auto findDriverParams(double ceff_seed_pf) -> bool
  {
    if (_nr_order == 3U) {
      _x.at(index(DmpParam::kCeff)) = ceff_seed_pf;
    }
    const auto gate_values = gateDelays(ceff_seed_pf);
    if (!gate_values.valid) {
      return false;
    }
    const auto threshold_span = _slew_upper_threshold - _slew_lower_threshold;
    if (threshold_span <= kEpsilon || _rd_ns_per_pf <= 0.0 || ceff_seed_pf <= 0.0) {
      return false;
    }
    const auto dt_ns = gate_values.measured_slew_ns / threshold_span;
    if (dt_ns <= kEpsilon) {
      return false;
    }
    const auto t0_ns = gate_values.delay_ns + std::log(1.0 - _output_threshold) * _rd_ns_per_pf * ceff_seed_pf - _output_threshold * dt_ns;
    _x.at(index(DmpParam::kDt)) = dt_ns;
    _x.at(index(DmpParam::kT0)) = t0_ns;
    if (!newtonRaphson()) {
      return false;
    }
    _t0_ns = _x.at(index(DmpParam::kT0));
    _dt_ns = _x.at(index(DmpParam::kDt));
    if (_nr_order == 3U) {
      _ceff_pf = _x.at(index(DmpParam::kCeff));
    }
    return std::isfinite(_t0_ns) && std::isfinite(_dt_ns) && _dt_ns > 0.0 && _ceff_pf >= 0.0;
  }

  auto gateDelays(double ceff_pf) const -> GateValues
  {
    const auto [delay_ns, table_slew_ns] = gateDelaySlew(*_cell, _transition, _input_slew_ns, ceff_pf);
    const auto threshold_span = _slew_upper_threshold - _slew_lower_threshold;
    if (!std::isfinite(delay_ns) || !std::isfinite(table_slew_ns) || table_slew_ns <= 0.0 || threshold_span <= kEpsilon) {
      return GateValues{};
    }
    const auto measured_slew_ns = table_slew_ns * _slew_derate;
    return GateValues{.valid = true,
                      .delay_ns = delay_ns,
                      .table_slew_ns = table_slew_ns,
                      .measured_slew_ns = measured_slew_ns,
                      .t_vl_ns = delay_ns - measured_slew_ns * (_output_threshold - _slew_lower_threshold) / threshold_span};
  }

  auto y(double t_ns, double t0_ns, double dt_ns, double cl_pf) const -> std::pair<double, double>
  {
    const auto t1_ns = t_ns - t0_ns;
    if (t1_ns <= 0.0) {
      return {0.0, t1_ns};
    }
    if (dt_ns <= kEpsilon) {
      return {0.0, t1_ns};
    }
    if (t1_ns <= dt_ns) {
      return {y0(t1_ns, cl_pf) / dt_ns, t1_ns};
    }
    return {(y0(t1_ns, cl_pf) - y0(t1_ns - dt_ns, cl_pf)) / dt_ns, t1_ns};
  }

  auto y0(double t_ns, double cl_pf) const -> double
  {
    const auto tau_ns = _rd_ns_per_pf * cl_pf;
    if (tau_ns <= kEpsilon) {
      return t_ns;
    }
    return t_ns - tau_ns * (1.0 - dmpExp(-t_ns / tau_ns));
  }

  auto y0dt(double t_ns, double cl_pf) const -> double
  {
    const auto tau_ns = _rd_ns_per_pf * cl_pf;
    if (tau_ns <= kEpsilon) {
      return 1.0;
    }
    return 1.0 - dmpExp(-t_ns / tau_ns);
  }

  auto y0dcl(double t_ns, double cl_pf) const -> double
  {
    const auto tau_ns = _rd_ns_per_pf * cl_pf;
    if (tau_ns <= kEpsilon || cl_pf <= kEpsilon) {
      return 0.0;
    }
    return _rd_ns_per_pf * ((1.0 + t_ns / tau_ns) * dmpExp(-t_ns / tau_ns) - 1.0);
  }

  auto dy(double t_ns, double t0_ns, double dt_ns, double cl_pf) const -> std::tuple<double, double, double>
  {
    const auto t1_ns = t_ns - t0_ns;
    if (t1_ns <= 0.0 || dt_ns <= kEpsilon) {
      return {0.0, 0.0, 0.0};
    }
    if (t1_ns <= dt_ns) {
      return {-y0dt(t1_ns, cl_pf) / dt_ns, -y0(t1_ns, cl_pf) / (dt_ns * dt_ns), y0dcl(t1_ns, cl_pf) / dt_ns};
    }
    return {-(y0dt(t1_ns, cl_pf) - y0dt(t1_ns - dt_ns, cl_pf)) / dt_ns,
            -(y0(t1_ns, cl_pf) + y0(t1_ns - dt_ns, cl_pf)) / (dt_ns * dt_ns) + y0dt(t1_ns - dt_ns, cl_pf) / dt_ns,
            (y0dcl(t1_ns, cl_pf) - y0dcl(t1_ns - dt_ns, cl_pf)) / dt_ns};
  }

  auto evalDmpEquations() -> bool
  {
    if (_algorithm == FastStaDmpAlgorithm::kPi) {
      return evalPiEquations();
    }
    if (_algorithm == FastStaDmpAlgorithm::kZeroC2) {
      return evalOnePoleEquations();
    }
    return false;
  }

  auto evalPiEquations() -> bool
  {
    const auto t0_ns = _x.at(index(DmpParam::kT0));
    const auto dt_ns = _x.at(index(DmpParam::kDt));
    const auto ceff_pf = _x.at(index(DmpParam::kCeff));
    if (ceff_pf < 0.0 || ceff_pf > _far_cap_pf + _near_cap_pf || dt_ns <= 0.0) {
      return false;
    }

    const auto gate_values = gateDelays(ceff_pf);
    if (!gate_values.valid) {
      return false;
    }
    auto ceff_time_ns = gate_values.measured_slew_ns / (_slew_upper_threshold - _slew_lower_threshold);
    ceff_time_ns = std::min(ceff_time_ns, 1.4 * dt_ns);
    const auto exp_p1_dt = dmpExp(-_p1_per_ns * dt_ns);
    const auto exp_p2_dt = dmpExp(-_p2_per_ns * dt_ns);
    const auto tau_ns = _rd_ns_per_pf * ceff_pf;
    if (tau_ns <= kEpsilon) {
      return false;
    }
    const auto exp_dt_rd_ceff = dmpExp(-dt_ns / tau_ns);
    const auto y50 = y(gate_values.delay_ns, t0_ns, dt_ns, ceff_pf).first;
    const auto y20 = y(gate_values.t_vl_ns, t0_ns, dt_ns, ceff_pf).first;
    _fvec.at(index(DmpFunc::kIpi)) = ipiIceff(dt_ns, ceff_time_ns, ceff_pf);
    _fvec.at(index(DmpFunc::kY50)) = y50 - _output_threshold;
    _fvec.at(index(DmpFunc::kY20)) = y20 - _slew_lower_threshold;
    _fjac.at(index(DmpFunc::kIpi)).at(index(DmpParam::kT0)) = 0.0;
    _fjac.at(index(DmpFunc::kIpi)).at(index(DmpParam::kDt))
        = (-_a_coeff * dt_ns + _b_coeff * dt_ns * exp_p1_dt - (2.0 * _b_coeff / _p1_per_ns) * (1.0 - exp_p1_dt)
           + _d_coeff * dt_ns * exp_p2_dt - (2.0 * _d_coeff / _p2_per_ns) * (1.0 - exp_p2_dt)
           + _rd_ns_per_pf * ceff_pf * (dt_ns + dt_ns * exp_dt_rd_ceff - 2.0 * _rd_ns_per_pf * ceff_pf * (1.0 - exp_dt_rd_ceff)))
          / (_rd_ns_per_pf * dt_ns * dt_ns * dt_ns);
    _fjac.at(index(DmpFunc::kIpi)).at(index(DmpParam::kCeff))
        = (2.0 * _rd_ns_per_pf * ceff_pf - dt_ns - (2.0 * _rd_ns_per_pf * ceff_pf + dt_ns) * exp_dt_rd_ceff) / (dt_ns * dt_ns);

    std::tie(_fjac.at(index(DmpFunc::kY20)).at(index(DmpParam::kT0)), _fjac.at(index(DmpFunc::kY20)).at(index(DmpParam::kDt)),
             _fjac.at(index(DmpFunc::kY20)).at(index(DmpParam::kCeff))) = dy(gate_values.t_vl_ns, t0_ns, dt_ns, ceff_pf);
    std::tie(_fjac.at(index(DmpFunc::kY50)).at(index(DmpParam::kT0)), _fjac.at(index(DmpFunc::kY50)).at(index(DmpParam::kDt)),
             _fjac.at(index(DmpFunc::kY50)).at(index(DmpParam::kCeff))) = dy(gate_values.delay_ns, t0_ns, dt_ns, ceff_pf);
    return allFinite(_fvec) && allFinite(_fjac);
  }

  auto evalOnePoleEquations() -> bool
  {
    auto t0_ns = _x.at(index(DmpParam::kT0));
    auto dt_ns = _x.at(index(DmpParam::kDt));
    const auto gate_values = gateDelays(_ceff_pf);
    if (!gate_values.valid) {
      return false;
    }
    if (dt_ns <= 0.0) {
      dt_ns = std::max(std::abs(gate_values.delay_ns - gate_values.t_vl_ns) / 100.0, 1e-6);
      _x.at(index(DmpParam::kDt)) = dt_ns;
    }
    _fvec.at(index(DmpFunc::kY50)) = y(gate_values.delay_ns, t0_ns, dt_ns, _ceff_pf).first - _output_threshold;
    _fvec.at(index(DmpFunc::kY20)) = y(gate_values.t_vl_ns, t0_ns, dt_ns, _ceff_pf).first - _slew_lower_threshold;

    double unused = 0.0;
    std::tie(_fjac.at(index(DmpFunc::kY20)).at(index(DmpParam::kT0)), _fjac.at(index(DmpFunc::kY20)).at(index(DmpParam::kDt)), unused)
        = dy(gate_values.t_vl_ns, t0_ns, dt_ns, _ceff_pf);
    std::tie(_fjac.at(index(DmpFunc::kY50)).at(index(DmpParam::kT0)), _fjac.at(index(DmpFunc::kY50)).at(index(DmpParam::kDt)), unused)
        = dy(gate_values.delay_ns, t0_ns, dt_ns, _ceff_pf);
    return allFinite(_fvec) && allFinite(_fjac);
  }

  auto ipiIceff(double dt_ns, double ceff_time_ns, double ceff_pf) const -> double
  {
    const auto exp_p1 = dmpExp(-_p1_per_ns * ceff_time_ns);
    const auto exp_p2 = dmpExp(-_p2_per_ns * ceff_time_ns);
    const auto tau_ns = _rd_ns_per_pf * ceff_pf;
    if (tau_ns <= kEpsilon || ceff_time_ns <= kEpsilon || dt_ns <= kEpsilon) {
      return 0.0;
    }
    const auto exp_ceff = dmpExp(-ceff_time_ns / tau_ns);
    const auto ipi = (_a_coeff * ceff_time_ns + (_b_coeff / _p1_per_ns) * (1.0 - exp_p1) + (_d_coeff / _p2_per_ns) * (1.0 - exp_p2))
                     / (_rd_ns_per_pf * ceff_time_ns * dt_ns);
    const auto iceff = (tau_ns * ceff_time_ns - tau_ns * tau_ns * (1.0 - exp_ceff)) / (_rd_ns_per_pf * ceff_time_ns * dt_ns);
    return ipi - iceff;
  }

  auto newtonRaphson() -> bool
  {
    for (std::size_t iter = 0U; iter < kNewtonRaphsonMaxIter; ++iter) {
      if (!evalDmpEquations()) {
        return false;
      }
      for (std::size_t i = 0U; i < _nr_order; ++i) {
        _p.at(i) = -_fvec.at(i);
      }
      if (!luDecomp() || !luSolve()) {
        return false;
      }

      auto all_under_tolerance = true;
      for (std::size_t i = 0U; i < _nr_order; ++i) {
        if (std::abs(_p.at(i)) > std::max(std::abs(_x.at(i)), 1e-12) * kDriverParamTolerance) {
          all_under_tolerance = false;
        }
        _x.at(i) += _p.at(i);
      }
      if (all_under_tolerance) {
        return evalDmpEquations();
      }
    }
    return false;
  }

  auto luDecomp() -> bool
  {
    for (std::size_t i = 0U; i < _nr_order; ++i) {
      auto big = 0.0;
      for (std::size_t j = 0U; j < _nr_order; ++j) {
        big = std::max(big, std::abs(_fjac.at(i).at(j)));
      }
      if (big <= kEpsilon) {
        return false;
      }
      _scale.at(i) = 1.0 / big;
    }

    for (std::size_t j = 0U; j < _nr_order; ++j) {
      for (std::size_t i = 0U; i < j; ++i) {
        auto sum = _fjac.at(i).at(j);
        for (std::size_t k = 0U; k < i; ++k) {
          sum -= _fjac.at(i).at(k) * _fjac.at(k).at(j);
        }
        _fjac.at(i).at(j) = sum;
      }

      auto big = 0.0;
      auto imax = j;
      for (std::size_t i = j; i < _nr_order; ++i) {
        auto sum = _fjac.at(i).at(j);
        for (std::size_t k = 0U; k < j; ++k) {
          sum -= _fjac.at(i).at(k) * _fjac.at(k).at(j);
        }
        _fjac.at(i).at(j) = sum;
        const auto scaled = _scale.at(i) * std::abs(sum);
        if (scaled >= big) {
          big = scaled;
          imax = i;
        }
      }
      if (j != imax) {
        for (std::size_t k = 0U; k < _nr_order; ++k) {
          std::swap(_fjac.at(imax).at(k), _fjac.at(j).at(k));
        }
        _scale.at(imax) = _scale.at(j);
      }
      _index.at(j) = imax;
      if (std::abs(_fjac.at(j).at(j)) <= kEpsilon) {
        _fjac.at(j).at(j) = kEpsilon;
      }
      if (j != _nr_order - 1) {
        const auto pivot = 1.0 / _fjac.at(j).at(j);
        for (std::size_t i = j + 1U; i < _nr_order; ++i) {
          _fjac.at(i).at(j) *= pivot;
        }
      }
    }
    return true;
  }

  auto luSolve() -> bool
  {
    auto non_zero = kMaxOrder;
    for (std::size_t i = 0U; i < _nr_order; ++i) {
      const auto iperm = _index.at(i);
      if (iperm >= _nr_order) {
        return false;
      }
      auto sum = _p.at(iperm);
      _p.at(iperm) = _p.at(i);
      if (non_zero != kMaxOrder) {
        for (std::size_t j = non_zero; j < i; ++j) {
          sum -= _fjac.at(i).at(j) * _p.at(j);
        }
      } else if (sum != 0.0) {
        non_zero = i;
      }
      _p.at(i) = sum;
    }
    for (std::size_t i = _nr_order; i-- > 0U;) {
      auto sum = _p.at(i);
      for (std::size_t j = i + 1U; j < _nr_order; ++j) {
        sum -= _fjac.at(i).at(j) * _p.at(j);
      }
      if (std::abs(_fjac.at(i).at(i)) <= kEpsilon) {
        return false;
      }
      _p.at(i) = sum / _fjac.at(i).at(i);
    }
    return allFinite(_p);
  }

  auto findDriverDelaySlew() -> std::optional<std::pair<double, double>>
  {
    const auto upper = voCrossingUpperBound();
    const auto delay = findVoCrossing(_output_threshold, _t0_ns, upper);
    if (!delay.has_value()) {
      return std::nullopt;
    }
    const auto tl = findVoCrossing(_slew_lower_threshold, _t0_ns, *delay);
    const auto th = findVoCrossing(_slew_upper_threshold, *delay, upper);
    if (!tl.has_value() || !th.has_value()) {
      return std::nullopt;
    }
    return std::pair<double, double>{*delay, std::max(0.0, (*th - *tl) / _slew_derate)};
  }

  auto findVoCrossing(double threshold, double lower, double upper) -> std::optional<double>
  {
    if (upper <= lower) {
      upper = lower + std::max(_dt_ns, 1e-6);
    }
    return findRoot(
        [&](double t, double& y_value, double& dy_value) -> void {
          const auto [vo_value, dvo_dt] = vo(t);
          y_value = vo_value - threshold;
          dy_value = dvo_dt;
        },
        lower, upper, kThresholdTimeTolerance, kFindRootMaxIter);
  }

  auto vo(double t_ns) const -> std::pair<double, double>
  {
    const auto t1_ns = t_ns - _t0_ns;
    if (t1_ns <= 0.0 || _dt_ns <= kEpsilon) {
      return {0.0, 0.0};
    }
    if (t1_ns <= _dt_ns) {
      const auto [value, deriv] = v0(t1_ns);
      return {value / _dt_ns, deriv / _dt_ns};
    }
    const auto [value, deriv] = v0(t1_ns);
    const auto [dt_value, dt_deriv] = v0(t1_ns - _dt_ns);
    return {(value - dt_value) / _dt_ns, (deriv - dt_deriv) / _dt_ns};
  }

  auto v0(double t_ns) const -> std::pair<double, double>
  {
    if (_algorithm == FastStaDmpAlgorithm::kPi) {
      const auto exp_p1 = dmpExp(-_p1_per_ns * t_ns);
      const auto exp_p2 = dmpExp(-_p2_per_ns * t_ns);
      return {_k0 * (_k1 + _k2 * t_ns + _k3 * exp_p1 + _k4 * exp_p2), _k0 * (_k2 - _k3 * _p1_per_ns * exp_p1 - _k4 * _p2_per_ns * exp_p2)};
    }
    if (_algorithm == FastStaDmpAlgorithm::kZeroC2) {
      const auto exp_p1 = dmpExp(-_p1_per_ns * t_ns);
      return {_k0 * (_k1 + _k2 * t_ns + _k3 * exp_p1), _k0 * (_k2 - _k3 * _p1_per_ns * exp_p1)};
    }
    return {0.0, 0.0};
  }

  auto voCrossingUpperBound() const -> double
  {
    if (_algorithm == FastStaDmpAlgorithm::kPi) {
      return _t0_ns + _dt_ns + (_far_cap_pf + _near_cap_pf) * (_rd_ns_per_pf + _rpi_ns_per_pf) * 2.0;
    }
    if (_algorithm == FastStaDmpAlgorithm::kZeroC2) {
      return _t0_ns + _dt_ns + _far_cap_pf * (_rd_ns_per_pf + _rpi_ns_per_pf) * 2.0;
    }
    return _t0_ns + _dt_ns;
  }

  template <typename T>
  static auto allFinite(const std::array<T, kMaxOrder>& values) -> bool
  {
    for (const auto& value : values) {
      if (!std::isfinite(value)) {
        return false;
      }
    }
    return true;
  }

  static auto allFinite(const std::array<std::array<double, kMaxOrder>, kMaxOrder>& values) -> bool
  {
    for (const auto& row : values) {
      for (const auto value : row) {
        if (!std::isfinite(value)) {
          return false;
        }
      }
    }
    return true;
  }

  const FastStaLibertyCell* _cell = nullptr;
  FastStaTransition _transition = FastStaTransition::kRise;
  double _input_slew_ns = 0.0;
  double _near_cap_pf = 0.0;
  double _far_cap_pf = 0.0;
  double _rpi_ns_per_pf = 0.0;
  double _rd_ns_per_pf = 0.0;
  double _output_threshold = 0.5;
  double _slew_lower_threshold = 0.3;
  double _slew_upper_threshold = 0.7;
  double _slew_derate = 1.0;
  FastStaDmpAlgorithm _algorithm = FastStaDmpAlgorithm::kCap;
  std::size_t _nr_order = 1U;
  double _t0_ns = 0.0;
  double _dt_ns = 0.0;
  double _ceff_pf = 0.0;
  std::array<double, kMaxOrder> _x{};
  std::array<double, kMaxOrder> _fvec{};
  std::array<std::array<double, kMaxOrder>, kMaxOrder> _fjac{};
  std::array<double, kMaxOrder> _scale{};
  std::array<double, kMaxOrder> _p{};
  std::array<std::size_t, kMaxOrder> _index{};
  double _p1_per_ns = 0.0;
  double _p2_per_ns = 0.0;
  double _z1_per_ns = 0.0;
  double _k0 = 0.0;
  double _k1 = 0.0;
  double _k2 = 0.0;
  double _k3 = 0.0;
  double _k4 = 0.0;
  double _a_coeff = 0.0;
  double _b_coeff = 0.0;
  double _d_coeff = 0.0;
};

auto vl0(const FastStaDmpDriverResult& state, double t_ns, double p3_per_ns) -> std::pair<double, double>
{
  if (state.algorithm == FastStaDmpAlgorithm::kPi) {
    const auto denom1 = state.pole1_per_ns - p3_per_ns;
    const auto denom2 = state.pole2_per_ns - p3_per_ns;
    if (std::abs(denom1) <= kEpsilon || std::abs(denom2) <= kEpsilon || std::abs(p3_per_ns) <= kEpsilon) {
      return {0.0, 0.0};
    }
    const auto d1 = state.k0 * (state.k1 - state.k2 / p3_per_ns);
    const auto d3 = -p3_per_ns * state.k0 * state.k3 / denom1;
    const auto d4 = -p3_per_ns * state.k0 * state.k4 / denom2;
    const auto d5 = state.k0 * (state.k2 / p3_per_ns - state.k1 + p3_per_ns * state.k3 / denom1 + p3_per_ns * state.k4 / denom2);
    const auto exp_p1 = dmpExp(-state.pole1_per_ns * t_ns);
    const auto exp_p2 = dmpExp(-state.pole2_per_ns * t_ns);
    const auto exp_p3 = dmpExp(-p3_per_ns * t_ns);
    return {d1 + t_ns + d3 * exp_p1 + d4 * exp_p2 + d5 * exp_p3,
            1.0 - d3 * state.pole1_per_ns * exp_p1 - d4 * state.pole2_per_ns * exp_p2 - d5 * p3_per_ns * exp_p3};
  }
  if (state.algorithm == FastStaDmpAlgorithm::kZeroC2) {
    const auto denom1 = state.pole1_per_ns - p3_per_ns;
    if (std::abs(denom1) <= kEpsilon || std::abs(p3_per_ns) <= kEpsilon) {
      return {0.0, 0.0};
    }
    const auto d1 = state.k0 * (state.k1 - state.k2 / p3_per_ns);
    const auto d3 = -p3_per_ns * state.k0 * state.k3 / denom1;
    const auto d5 = state.k0 * (state.k2 / p3_per_ns - state.k1 + p3_per_ns * state.k3 / denom1);
    const auto exp_p1 = dmpExp(-state.pole1_per_ns * t_ns);
    const auto exp_p3 = dmpExp(-p3_per_ns * t_ns);
    return {d1 + t_ns + d3 * exp_p1 + d5 * exp_p3, 1.0 - d3 * state.pole1_per_ns * exp_p1 - d5 * p3_per_ns * exp_p3};
  }
  return {0.0, 0.0};
}

auto vl(const FastStaDmpDriverResult& state, double t_ns, double p3_per_ns) -> std::pair<double, double>
{
  const auto t1_ns = t_ns - state.t0_ns;
  if (t1_ns <= 0.0 || state.dt_ns <= kEpsilon) {
    return {0.0, 0.0};
  }
  if (t1_ns <= state.dt_ns) {
    const auto [value, deriv] = vl0(state, t1_ns, p3_per_ns);
    return {value / state.dt_ns, deriv / state.dt_ns};
  }
  const auto [value, deriv] = vl0(state, t1_ns, p3_per_ns);
  const auto [dt_value, dt_deriv] = vl0(state, t1_ns - state.dt_ns, p3_per_ns);
  return {(value - dt_value) / state.dt_ns, (deriv - dt_deriv) / state.dt_ns};
}

auto findVlCrossing(const FastStaDmpDriverResult& state, double p3_per_ns, double threshold, double lower, double upper)
    -> std::optional<double>
{
  if (upper <= lower) {
    upper = lower + std::max(state.dt_ns, 1e-6);
  }
  return findRoot(
      [&](double t, double& y_value, double& dy_value) -> void {
        const auto [vl_value, dvl_dt] = vl(state, t, p3_per_ns);
        y_value = vl_value - threshold;
        dy_value = dvl_dt;
      },
      lower, upper, kThresholdTimeTolerance, kFindRootMaxIter);
}

auto vlCrossingUpperBound(const FastStaDmpDriverResult& state, double elmore_delay_ns) -> double
{
  if (state.algorithm == FastStaDmpAlgorithm::kPi) {
    return state.t0_ns + state.dt_ns + (state.near_cap_pf + state.far_cap_pf) * (state.rd_ns_per_pf + state.rpi_ns_per_pf) * 2.0
           + elmore_delay_ns * 2.0;
  }
  if (state.algorithm == FastStaDmpAlgorithm::kZeroC2) {
    return state.t0_ns + state.dt_ns + state.far_cap_pf * (state.rd_ns_per_pf + state.rpi_ns_per_pf) * 2.0 + elmore_delay_ns * 2.0;
  }
  return state.t0_ns + state.dt_ns + elmore_delay_ns * 2.0;
}

auto applyThresholdAdjust(const FastStaDmpDriverResult& driver_timing, const FastStaLibertyCell* load_cell, double& wire_delay_ns,
                          double& load_slew_ns) -> void
{
  if (load_cell == nullptr) {
    return;
  }
  const auto load_vth = inputThreshold(*load_cell, driver_timing.transition);
  const auto load_lower = slewLowerThreshold(*load_cell, driver_timing.transition);
  const auto load_upper = slewUpperThreshold(*load_cell, driver_timing.transition);
  const auto load_derate = slewDerate(*load_cell);
  const auto driver_span = driver_timing.slew_upper_threshold - driver_timing.slew_lower_threshold;
  const auto load_span = load_upper - load_lower;
  if (driver_span <= kEpsilon || load_span <= kEpsilon) {
    return;
  }

  const auto delay_delta = load_slew_ns * ((load_vth - driver_timing.output_threshold) / driver_span);
  wire_delay_ns += driver_timing.transition == FastStaTransition::kRise ? delay_delta : -delay_delta;
  load_slew_ns *= (load_span / load_derate) / (driver_span / driver_timing.slew_derate);
  wire_delay_ns = std::max(0.0, wire_delay_ns);
  load_slew_ns = std::max(0.0, load_slew_ns);
}

}  // namespace

auto FastStaDmpCeff::calcDriverTiming(const FastStaLibertyCell& driver_cell, const FastStaPiModel& pi, FastStaTransition transition,
                                      double input_slew_ns) -> FastStaDmpDriverResult
{
  DmpSolver solver(driver_cell, pi, transition, input_slew_ns);
  return solver.solve();
}

auto FastStaDmpCeff::calcLoadDelaySlew(const FastStaDmpDriverResult& driver_timing, double elmore_delay_ns,
                                       const FastStaLibertyCell* load_cell) -> FastStaDmpLoadResult
{
  const auto elmore_ns = std::max(0.0, elmore_delay_ns);
  auto wire_delay_ns = elmore_ns;
  auto load_slew_ns = std::max(0.0, driver_timing.driver_slew_ns);

  if (driver_timing.valid && driver_timing.driver_waveform_valid && elmore_ns > 0.0
      && elmore_ns >= std::max(0.0, driver_timing.driver_slew_ns) * 1e-3) {
    const auto p3_per_ns = 1.0 / elmore_ns;
    const auto upper = vlCrossingUpperBound(driver_timing, elmore_ns);
    const auto load_delay = findVlCrossing(driver_timing, p3_per_ns, driver_timing.output_threshold, driver_timing.t0_ns, upper);
    if (load_delay.has_value()) {
      const auto tl = findVlCrossing(driver_timing, p3_per_ns, driver_timing.slew_lower_threshold, driver_timing.t0_ns, *load_delay);
      const auto th = findVlCrossing(driver_timing, p3_per_ns, driver_timing.slew_upper_threshold, *load_delay, upper);
      if (tl.has_value() && th.has_value()) {
        wire_delay_ns = *load_delay - driver_timing.driver_waveform_delay_ns;
        load_slew_ns = (*th - *tl) / driver_timing.slew_derate;
        if (wire_delay_ns < 0.0) {
          wire_delay_ns = elmore_ns;
        }
        load_slew_ns = std::max(load_slew_ns, driver_timing.driver_slew_ns);
      }
    }
  }

  applyThresholdAdjust(driver_timing, load_cell, wire_delay_ns, load_slew_ns);
  return FastStaDmpLoadResult{.valid = true, .wire_delay_ns = std::max(0.0, wire_delay_ns), .load_slew_ns = std::max(0.0, load_slew_ns)};
}

auto FastStaDmpCeff::calcInputPortDelaySlew(double input_slew_ns, double elmore_delay_ns, FastStaTransition transition,
                                            const FastStaLibertyCell* load_cell) -> FastStaDmpLoadResult
{
  const auto elmore_ns = std::max(0.0, elmore_delay_ns);
  const auto vth = load_cell != nullptr ? inputThreshold(*load_cell, transition) : 0.5;
  const auto vl_threshold = load_cell != nullptr ? slewLowerThreshold(*load_cell, transition) : 0.2;
  const auto vh_threshold = load_cell != nullptr ? slewUpperThreshold(*load_cell, transition) : 0.8;
  const auto derate = load_cell != nullptr ? slewDerate(*load_cell) : 1.0;
  auto wire_delay_ns = 0.0;
  auto load_slew_ns = std::max(0.0, input_slew_ns);
  if (elmore_ns > 0.0 && vth > 0.0 && vth < 1.0 && vl_threshold > 0.0 && vh_threshold > vl_threshold && vh_threshold < 1.0) {
    wire_delay_ns = -elmore_ns * std::log(1.0 - vth);
    load_slew_ns += elmore_ns * std::log((1.0 - vl_threshold) / (1.0 - vh_threshold)) / derate;
  }
  return FastStaDmpLoadResult{.valid = true, .wire_delay_ns = std::max(0.0, wire_delay_ns), .load_slew_ns = std::max(0.0, load_slew_ns)};
}

}  // namespace icts
