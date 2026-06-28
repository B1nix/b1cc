#ifndef BACKEND_TARGET_H
#define BACKEND_TARGET_H

#include "ir.h"
#include <string>
#include <vector>

namespace Backend {
  class TargetBackend {
  public:
    virtual ~TargetBackend() = default;
    virtual std::string emit_function(const IR::IrFunction &fn) = 0;
    virtual std::string emit_globals(const std::vector<IR::IrGlobalVar> &globals) = 0;
  };

  TargetBackend* create_arm64_backend();
  TargetBackend* create_x86_64_backend();
  TargetBackend* create_i386_backend();
}

#endif // BACKEND_TARGET_H
