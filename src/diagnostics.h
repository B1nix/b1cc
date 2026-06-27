#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <string>
#include <iostream>
#include <cstdlib>

namespace Diagnostics {
  inline std::string filepath = "input.c";

  [[noreturn]] inline void error(int line, int col, const std::string &msg) {
    std::cerr << filepath << ":" << line << ":" << col << ": error: " << msg << "\n";
    std::exit(1);
  }

  [[noreturn]] inline void fatal(const std::string &msg) {
    std::cerr << "b1cc: error: " << msg << "\n";
    std::exit(1);
  }
}

#endif // DIAGNOSTICS_H
