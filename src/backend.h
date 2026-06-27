#ifndef BACKEND_H
#define BACKEND_H

#include "ir.h"
#include "preprocessor.h"
#include <string>
#include <vector>
#include <map>

namespace Backend {
  std::string compile_asm(const std::string &src, const std::string &target);
}

#endif // BACKEND_H
