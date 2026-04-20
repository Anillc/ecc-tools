#pragma once

namespace itf
{

typedef void* itfiUserData;
  
class itfrSettings {
 public:
  // constructor
  itfrSettings();
  ~itfrSettings();

  // function
  static void reset();

  // members
  itfiUserData user_data;
};

extern itfrSettings* itfSettings;


} // namespace itf
