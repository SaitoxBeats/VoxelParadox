#include "support/crash_handler.hpp"
#include "runtime/app/runtime_app.hpp"

int main() {
  CrashHandler::install();
  return VoxelParadox::runRuntimeApp();
}

