#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>

#include "Layer.hpp"
#include "VariationParams.hpp"

namespace itf
{

class ProcessCorner {
 public:
  // constructor
  ProcessCorner();
  ~ProcessCorner();

  // getter
  std::string get_technology() const;
  double get_global_temperature() const;
  double get_background_er() const;
  double get_half_node_scale_factor() const;
  bool get_use_si_density() const;
  double get_drop_factor_lateral_spacing() const;
  Layers* get_layers();
  const Layers* get_layers() const;
  VariationParams* get_variation_params();
  const VariationParams* get_variation_params() const;

  // setter
  void set_technology(std::string);
  void set_global_temperature(double);
  void set_background_er(double);
  void set_half_node_scale_factor(double);
  void set_use_si_density(bool);
  void set_drop_factor_lateral_spacing(double);
  void set_layers_map(const std::map<std::string, uint8_t>&);

  // function
  void update_layers_height();
  void show_layers() const;

 private:
  // function
  void update_dielectric_layer_height(LayerDielectric*, LayerDielectric*);
  void update_conductor_layer_height(LayerConductor*, LayerDielectric*);

  // members
  std::string _technology;
  double _global_temperature;
  double _background_er;
  double _half_node_scale_factor;
  bool _use_si_density;
  double _drop_factor_lateral_spacing;
  Layers* _layers;
  VariationParams* _variation_params;
};

} // namespace itf
