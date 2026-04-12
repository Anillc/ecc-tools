#pragma once

#include <optional>

#include "Lib.hh"

namespace python_interface::pydb_test {

inline double export_lib_pin_cap_for_python_pf(double internal_cap_pf)
{
  return internal_cap_pf;
}

inline double export_lib_pin_cap_limit_for_python_pf(std::optional<double> internal_cap_limit_pf, double default_cap_pf)
{
  return internal_cap_limit_pf.value_or(default_cap_pf);
}

inline double export_lib_pin_slew_limit_for_python_ps(std::optional<double> internal_slew_limit_ns, double default_slew_ps)
{
  return internal_slew_limit_ns.has_value() ? internal_slew_limit_ns.value() * 1000.0 : default_slew_ps;
}

inline double resolve_lib_pin_slew_limit_for_python_ps(std::optional<double> internal_slew_limit_ns,
                                                       std::optional<double> default_slew_limit_ns,
                                                       double fallback_slew_ps = 0.0)
{
  if (internal_slew_limit_ns.has_value()) {
    return internal_slew_limit_ns.value() * 1000.0;
  }
  if (default_slew_limit_ns.has_value()) {
    return default_slew_limit_ns.value() * 1000.0;
  }
  return fallback_slew_ps;
}

inline double export_one_dimensional_lut_axis_for_python(double raw_axis_value,
                                                         bool first_var_is_input_transition,
                                                         double time_coeff,
                                                         double cap_coeff)
{
  return raw_axis_value * (first_var_is_input_transition ? time_coeff : cap_coeff);
}

inline double export_lut_time_value_for_python_ps(double raw_table_value, double time_coeff)
{
  return raw_table_value * time_coeff;
}

inline double export_libcell_leakage_for_python_mw(ista::LibCell& lib_cell)
{
  double scalar_cell_leakage_mw = lib_cell.get_cell_leakage_power();
  if (scalar_cell_leakage_mw > 0.0) {
    return scalar_cell_leakage_mw;
  }

  auto leakage_powers = lib_cell.getLeakagePowerList();
  double positive_conditional_sum_mw = 0.0;
  int positive_conditional_count = 0;
  for (auto* leakage_power : leakage_powers) {
    if (leakage_power == nullptr) {
      continue;
    }
    double value_mw = leakage_power->get_value();
    if (value_mw > 0.0) {
      positive_conditional_sum_mw += value_mw;
      ++positive_conditional_count;
    }
  }

  if (positive_conditional_count > 0) {
    return positive_conditional_sum_mw / positive_conditional_count;
  }

  return scalar_cell_leakage_mw;
}

}  // namespace python_interface::pydb_test
