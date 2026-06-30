#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>

const char *diagnostics_filepath = nullptr;

void diagnostics_error(int line, int col, const char *msg) {
    fprintf(stderr, "%s:%d:%d: error: %s\n", diagnostics_filepath, line, col, msg);
    exit(1);
}

void diagnostics_fatal(const char *msg) {
    fprintf(stderr, "b1cc: error: %s\n", msg);
    exit(1);
}
