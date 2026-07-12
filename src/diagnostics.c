#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *diagnostics_filepath = nullptr;

static const char *diagnostics_category(const char *msg) {
    if (strstr(msg, "expected ") || strstr(msg, "unexpected ") ||
        strstr(msg, "unterminated ")) {
        return "syntax";
    }
    if (strstr(msg, "constant expression") || strstr(msg, "division by zero") ||
        strstr(msg, "shift count")) {
        return "constant-expression";
    }
    if (strstr(msg, "#") || strstr(msg, "include file")) {
        return "preprocessor";
    }
    if (strstr(msg, "lvalue") || strstr(msg, "const-qualified") ||
        strstr(msg, "undeclared") || strstr(msg, "redefinition") ||
        strstr(msg, "duplicate") || strstr(msg, "incompatible") ||
        strstr(msg, "initializer") ||
        strstr(msg, "unknown struct tag")) {
        return "constraint";
    }
    return "semantic";
}

void diagnostics_error(int line, int col, const char *msg) {
    fprintf(stderr, "%s:%d:%d: error: [%s] %s\n", diagnostics_filepath, line, col,
            diagnostics_category(msg), msg);
    exit(1);
}

void diagnostics_fatal(const char *msg) {
    fprintf(stderr, "b1cc: error: %s\n", msg);
    exit(1);
}
