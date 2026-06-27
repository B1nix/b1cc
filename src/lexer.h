#ifndef LEXER_H
#define LEXER_H

#include "ast.h"
#include "preprocessor.h"
#include <vector>
#include <string>
#include <map>
#include <set>

namespace Lexer {
  std::vector<Token> lex(const std::string &src, const std::map<std::string, Preprocessor::Macro> &macros = {}, std::set<std::string> active_macros = {});
}

#endif // LEXER_H
