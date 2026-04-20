#pragma once

#include <memory>
#include <string>

#include "ItfService.hpp"
#include "itfrReader.hpp"

namespace itf
{


// interface between itf and irc
class ItfRead {
 public:
  ItfRead(ItfService*);
  ~ItfRead() = default;

  // getter
  ItfService* get_service() { return _itf_service; }
  const ItfService* get_service() const { return _itf_service; }

  // function
  bool createDb(const std::string&);

 private:
  // callback
  static int technologyCb(itfCallBackType, const char*, itfiUserData);
  static int globalTemperatureCb(itfCallBackType, double, itfiUserData);
  static int backgroundErCb(itfCallBackType, double, itfiUserData);
  static int halfNodeScaleFactorCb(itfCallBackType, double, itfiUserData);
  static int useSiDensityCb(itfCallBackType, int, itfiUserData);
  static int dropFactorLateralSpacingCb(itfCallBackType, double, itfiUserData);
  static int dielectricCb(itfCallBackType, itfiDielectric*, itfiUserData);
  static int conductorCb(itfCallBackType, itfiConductor*, itfiUserData);
  static int viaCb(itfCallBackType, itfiVia*, itfiUserData);
  static int variationParamCb(itfCallBackType, itfiVariationParam*, itfiUserData);

  // parser
  int parse_technology(const char*);
  int parse_globalTemperature(double);
  int parse_backgroundEr(double);
  int parse_halfNodeScaleFactor(double);
  int parse_useSiDensity(int);
  int parse_dropFactorLateralSpacing(double);
  int parse_conductor(const itfiConductor&);
  int parse_dielectric(const itfiDielectric&);
  int parse_via(const itfiVia&);
  int parse_variation_param(const itfiVariationParam&);

  // members
  ItfService* _itf_service;
  std::string _fname;
  std::unique_ptr<ProcessCorner> _process_corner;
};

} // namespace itf
