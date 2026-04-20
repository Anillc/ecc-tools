#pragma once

#include <vector>
#include <string>

namespace itf
{

// variation parameter term 
class itfiVPT {
 public:
  // operator
  bool operator==(const itfiVPT&) const;

  // function
  void clear();

  // members
  std::string layer;
  std::string type;
  float coeff;
};

class itfiVariationParam{
 public:
  // operator
  bool operator==(const itfiVariationParam&) const;
  
  // function
  void clear();

  void add_term(const itfiVPT&);
  void add_term(const char*, const char*, float);

  // members
  std::string param_name;
  std::vector<itfiVPT> term_list;
};

///////////// inline ////////////////

inline bool
itfiVPT::operator==(const itfiVPT& rhs) const
{
  if (this == &rhs) return true;

  return layer == rhs.layer
    && type == rhs.type
    && coeff == rhs.coeff
  ;
}

inline void
itfiVPT::clear()
{
  layer.clear();
  type.clear();
  coeff = 0;
}

inline bool
itfiVariationParam::operator==(const itfiVariationParam& rhs) const
{
  if (this == &rhs) return true;

  return param_name == rhs.param_name
    && term_list == rhs.term_list
  ;
}

inline void
itfiVariationParam::clear()
{
  param_name.clear();
  term_list.clear();
}

inline void
itfiVariationParam::add_term(const itfiVPT& t)
{
  term_list.push_back(t);
}

inline void
itfiVariationParam::add_term(
  const char* layer, const char* type, float coeff
) {
  itfiVPT t;
  t.layer = layer ? layer : "";
  t.type = type ? type : "";
  t.coeff = coeff;
  add_term(t);
}

} // namespace itf