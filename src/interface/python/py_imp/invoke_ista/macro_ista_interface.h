/**
 * @file   PyPlaceDB.h
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#ifndef _DREAMPLACE_PLACE_IO_PYPLACEDB_H
#define _DREAMPLACE_PLACE_IO_PYPLACEDB_H

//#include <pybind11/stl.h>
// #include <pybind11/numpy.h>
// #include <pybind11/stl_bind.h>

#include <sstream>

#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "Pin.hh"
#include "TimingEngine.hh"
#include "TimingIDBAdapter.hh"
#include "idm.h"
#include "PowerEngine.hh"

//#include <boost/timer/timer.hpp>
namespace python_interface {
/// database for python
struct MacroISTAInterface
{
  // MacroISTAInterface() {}
  MacroISTAInterface(idm::DataManager* db, std::vector<std::string> lib_files, std::string sdc_file)
  {
    _db = db;
    _lib_files = lib_files;
    _sdc_file = sdc_file;
  }
  void initTimingEngine();
  void initPaths();
  idm::DataManager* _db;
  std::string _sdc_file;
  std::vector<std::string> _lib_files;
  ista::TimingEngine* _timing_engine;

  idb::IdbBuilder* _idb_builder;
};
}  // namespace python_interface

#endif
