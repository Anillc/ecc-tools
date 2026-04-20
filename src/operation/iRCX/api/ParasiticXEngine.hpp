#pragma once

namespace ircx {

class RCX;
class ParasiticXDBAdapter;

// Database Adapter
class ParasiticXEngine {
 public:
  // Get or create singleton instance
  static ParasiticXEngine* get_or_create_parasitic_x_engine();

  // Destroy singleton instance
  static void destroyParasiticXEngine();

  // getter
  [[nodiscard]] RCX* get_rcx() const { return _rcx; }
  [[nodiscard]] ParasiticXDBAdapter* get_db_adapter() const { return _db_adapter; }

  // setter
  void set_rcx(RCX* rcx) { _rcx = rcx; }
  void set_db_adapter(ParasiticXDBAdapter* db_adapter) { _db_adapter = db_adapter; }

  // Disallow copy and move
  ParasiticXEngine(const ParasiticXEngine&) = delete;
  void operator=(const ParasiticXEngine&) = delete;
  ParasiticXEngine(ParasiticXEngine&&) = delete;
  void operator=(ParasiticXEngine&&) = delete;

 private:
  // Private constructor for singleton
  ParasiticXEngine();
  ~ParasiticXEngine() = default;

  // Singleton instance
  static ParasiticXEngine* _instance;

  // members
  RCX* _rcx;
  ParasiticXDBAdapter* _db_adapter;
};

}  // namespace ircx
