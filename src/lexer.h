#ifndef LEXER_H
#define LEXER_H

#include "ast.h"
#include "common.h"

TokenArray lex(const char *src, HashMap *macros, StringArray *active_macros, Arena *arena);

#endif // LEXER_H
