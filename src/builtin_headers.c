/* mkdtemp() is a BSD/glibc extension; on Linux it needs _DEFAULT_SOURCE.
   Harmless on Darwin, where it is declared unconditionally. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "builtin_headers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *content;
} BuiltinHeader;

static const char hdr_stddef[] =
    "#ifndef _STDDEF_H\n"
    "#define _STDDEF_H\n"
    "#ifdef __b1cc__\n"
    "typedef long ptrdiff_t;\n"
    "typedef unsigned long size_t;\n"
    "typedef long ssize_t;\n"
    "#define NULL ((void *)0)\n"
    "#define offsetof(type, member) ((size_t)&((type *)0)->member)\n"
    "#ifndef __cplusplus\n"
    "typedef int wchar_t;\n"
    "#endif\n"
    "#else\n"
    "#include_next <stddef.h>\n"
    "#endif\n"
    "#endif\n";

static const char hdr_stdint[] =
    "#ifndef _STDINT_H\n"
    "#define _STDINT_H\n"
    "#ifdef __b1cc__\n"
    "typedef signed char int8_t;\n"
    "typedef unsigned char uint8_t;\n"
    "typedef short int16_t;\n"
    "typedef unsigned short uint16_t;\n"
    "typedef int int32_t;\n"
    "typedef unsigned int uint32_t;\n"
    "typedef long int64_t;\n"
    "typedef unsigned long uint64_t;\n"
    "typedef long intptr_t;\n"
    "typedef unsigned long uintptr_t;\n"
    "typedef long intmax_t;\n"
    "typedef unsigned long uintmax_t;\n"
    "#define INT8_MIN (-128)\n"
    "#define INT8_MAX 127\n"
    "#define UINT8_MAX 255\n"
    "#define INT16_MIN (-32768)\n"
    "#define INT16_MAX 32767\n"
    "#define UINT16_MAX 65535\n"
    "#define INT32_MIN (-2147483647-1)\n"
    "#define INT32_MAX 2147483647\n"
    "#define UINT32_MAX 4294967295U\n"
    "#define INT64_MIN (-9223372036854775807L-1)\n"
    "#define INT64_MAX 9223372036854775807L\n"
    "#define UINT64_MAX 18446744073709551615UL\n"
    "#define INTPTR_MIN INT64_MIN\n"
    "#define INTPTR_MAX INT64_MAX\n"
    "#define UINTPTR_MAX UINT64_MAX\n"
    "#define SIZE_MAX UINT64_MAX\n"
    "#define SSIZE_MAX INT64_MAX\n"
    "#define INTMAX_MIN INT64_MIN\n"
    "#define INTMAX_MAX INT64_MAX\n"
    "#define UINTMAX_MAX UINT64_MAX\n"
    "#else\n"
    "#include_next <stdint.h>\n"
    "#endif\n"
    "#endif\n";

static const char hdr_stdbool[] =
    "#ifndef _STDBOOL_H\n"
    "#define _STDBOOL_H\n"
    "#ifdef __b1cc__\n"
    "#define bool _Bool\n"
    "#define true 1\n"
    "#define false 0\n"
    "#define __bool_true_false_are_defined 1\n"
    "#else\n"
    "#include_next <stdbool.h>\n"
    "#endif\n"
    "#endif\n";

static const char hdr_stdarg[] =
    "#ifndef _STDARG_H\n"
    "#define _STDARG_H\n"
    "#ifdef __b1cc__\n"
    "typedef void *__builtin_va_list;\n"
    "#define va_start(ap, last) __builtin_va_start(ap)\n"
    "#define va_arg(ap, type)   __builtin_va_arg(ap, type)\n"
    "#define va_end(ap)         __builtin_va_end(ap)\n"
    "#define va_copy(dst, src)  __builtin_va_copy(dst, src)\n"
    "#else\n"
    "#include_next <stdarg.h>\n"
    "#endif\n"
    "#endif\n";

static const BuiltinHeader builtin_headers[] = {
    { "stddef.h", hdr_stddef },
    { "stdint.h", hdr_stdint },
    { "stdbool.h", hdr_stdbool },
    { "stdarg.h", hdr_stdarg },
};

#define NUM_BUILTIN_HEADERS (sizeof(builtin_headers) / sizeof(builtin_headers[0]))

static void write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(data, f);
    fclose(f);
}

const char *builtin_headers_write_temp_dir(void) {
    char tmpl[] = "/tmp/b1cc-inc-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return NULL;
    for (size_t i = 0; i < NUM_BUILTIN_HEADERS; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, builtin_headers[i].name);
        write_file(path, builtin_headers[i].content);
    }
    return strdup(dir);
}
