#include "ParasiticXEngine.hpp"

#include <memory>
#include <mutex>

namespace ircx {

ParasiticXEngine* ParasiticXEngine::_instance = nullptr;

ParasiticXEngine* ParasiticXEngine::get_or_create_parasitic_x_engine()
{
  static std::mutex mutex;
  if (_instance == nullptr) {
    std::lock_guard<std::mutex> lock(mutex);
    if (_instance == nullptr) {
      _instance = new ParasiticXEngine();
    }
  }
  return _instance;
}

void ParasiticXEngine::destroyParasiticXEngine()
{
  delete _instance;
}

ParasiticXEngine::ParasiticXEngine() : _rcx(nullptr), _db_adapter(nullptr) {}


}  // namespace ircx