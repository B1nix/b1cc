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
#include <sys/stat.h>

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

/* GCC/Clang atomic memory-order constants used by freestanding libc code. */
static const char hdr_builtins[] =
    "#ifndef __ATOMIC_RELAXED\n"
    "#define __ATOMIC_RELAXED 0\n"
    "#define __ATOMIC_CONSUME 1\n"
    "#define __ATOMIC_ACQUIRE 2\n"
    "#define __ATOMIC_RELEASE 3\n"
    "#define __ATOMIC_ACQ_REL 4\n"
    "#define __ATOMIC_SEQ_CST 5\n"
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
    "typedef __builtin_va_list va_list;\n"
    "#define va_start(ap, last) __builtin_va_start(ap, last)\n"
    "#define va_arg(ap, type)   __builtin_va_arg(ap, type)\n"
    "#define va_end(ap)         __builtin_va_end(ap)\n"
    "#define va_copy(dst, src)  __builtin_va_copy(dst, src)\n"
    "#else\n"
    "#include_next <stdarg.h>\n"
    "#endif\n"
    "#endif\n";

/* Minimal host-facing declarations used when b1cc compiles its own sources.
 * Keeping these declarations bundled avoids depending on a host SDK's large
 * implementation headers, which the intentionally small preprocessor cannot
 * consume. They are declarations only; the final host link still supplies the
 * platform libc definitions. */
static const char hdr_stdio[] =
    "#ifndef _STDIO_H\n"
    "#define _STDIO_H\n"
    "#include <stddef.h>\n"
    "typedef struct __b1cc_FILE FILE;\n"
    "extern FILE *stdin;\n"
    "extern FILE *stdout;\n"
    "extern FILE *stderr;\n"
    "#define SEEK_SET 0\n"
    "#define SEEK_CUR 1\n"
    "#define SEEK_END 2\n"
    "#define EOF (-1)\n"
    "FILE *fopen(const char *, const char *);\n"
    "int fclose(FILE *);\n"
    "int fseek(FILE *, long, int);\n"
    "long ftell(FILE *);\n"
    "size_t fread(void *, size_t, size_t, FILE *);\n"
    "size_t fwrite(const void *, size_t, size_t, FILE *);\n"
    "int fputs(const char *, FILE *);\n"
    "int fprintf(FILE *, const char *, ...);\n"
    "int printf(const char *, ...);\n"
    "int snprintf(char *, size_t, const char *, ...);\n"
    "FILE *popen(const char *, const char *);\n"
    "int pclose(FILE *);\n"
    "#endif\n";

static const char hdr_stdlib[] =
    "#ifndef _STDLIB_H\n"
    "#define _STDLIB_H\n"
    "#include <stddef.h>\n"
    "void *malloc(size_t);\n"
    "void *calloc(size_t, size_t);\n"
    "void *realloc(void *, size_t);\n"
    "void free(void *);\n"
    "char *getenv(const char *);\n"
    "int system(const char *);\n"
    "long strtol(const char *, char **, int);\n"
    "unsigned long strtoul(const char *, char **, int);\n"
    "long long strtoll(const char *, char **, int);\n"
    "unsigned long long strtoull(const char *, char **, int);\n"
    "double strtod(const char *, char **);\n"
    "void exit(int);\n"
    "void abort(void);\n"
    "#endif\n";

static const char hdr_string[] =
    "#ifndef _STRING_H\n"
    "#define _STRING_H\n"
    "#include <stddef.h>\n"
    "void *memcpy(void *, const void *, size_t);\n"
    "void *memmove(void *, const void *, size_t);\n"
    "void *memset(void *, int, size_t);\n"
    "int memcmp(const void *, const void *, size_t);\n"
    "size_t strlen(const char *);\n"
    "char *strcpy(char *, const char *);\n"
    "char *strncpy(char *, const char *, size_t);\n"
    "int strcmp(const char *, const char *);\n"
    "int strncmp(const char *, const char *, size_t);\n"
    "char *strchr(const char *, int);\n"
    "char *strrchr(const char *, int);\n"
    "char *strstr(const char *, const char *);\n"
    "char *strcat(char *, const char *);\n"
    "char *strdup(const char *);\n"
    "char *strtok(char *, const char *);\n"
    "#endif\n";

static const char hdr_unistd[] =
    "#ifndef _UNISTD_H\n"
    "#define _UNISTD_H\n"
    "#define R_OK 4\n"
    "int close(int);\n"
    "int unlink(const char *);\n"
    "int rmdir(const char *);\n"
    "int access(const char *, int);\n"
    "int mkstemps(char *, int);\n"
    "char *mkdtemp(char *);\n"
    "int mkdir(const char *, int);\n"
    "#endif\n";

/* struct dirent / struct stat must match the *host libc* ABI, because the real
 * opendir/readdir/lstat run at runtime and fill these layouts. A too-small or
 * wrongly-offset stub means a field read at the wrong offset (garbage) and, for
 * stat, a stack overflow when lstat writes more bytes than the struct declares.
 * Layouts are selected by target: __APPLE__ (arm64-darwin host) vs b1nix. */
static const char hdr_dirent[] =
    "#ifndef _DIRENT_H\n"
    "#define _DIRENT_H\n"
    "typedef struct __b1cc_DIR DIR;\n"
    "#ifdef __APPLE__\n"
    /* macOS: sizeof 1048, d_name at offset 21. */
    "struct dirent { long d_ino; long d_seekoff; char d_reclen[2]; char d_namlen[2]; char d_type; char d_name[1024]; };\n"
    "#else\n"
    /* b1nix: d_name at offset 8. */
    "struct dirent { long d_ino; char d_name[256]; char d_type; };\n"
    "#endif\n"
    "DIR *opendir(const char *);\n"
    "struct dirent *readdir(DIR *);\n"
    "int closedir(DIR *);\n"
    "#endif\n";

static const char hdr_sys_stat[] =
    "#ifndef _SYS_STAT_H\n"
    "#define _SYS_STAT_H\n"
    "#ifdef __APPLE__\n"
    /* macOS: sizeof 144, st_mode at offset 4. Read as int; S_ISDIR masks the
     * low 16 bits, so overlapping st_nlink in the upper bits is harmless. */
    "struct stat { int st_dev; int st_mode; char __b1cc_pad[136]; };\n"
    "#else\n"
    /* b1nix: st_mode at offset 16 (see userspace/include/unistd.h). */
    "struct stat { long st_dev; long st_ino; int st_mode; char __b1cc_pad[124]; };\n"
    "#endif\n"
    "int stat(const char *, struct stat *);\n"
    "int lstat(const char *, struct stat *);\n"
    "#define S_ISDIR(mode) (((mode) & 0170000) == 0040000)\n"
    "#endif\n";

static const char hdr_pthread[] =
    "#ifndef _PTHREAD_H\n"
    "#define _PTHREAD_H\n"
    "typedef unsigned long pthread_t;\n"
    "int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);\n"
    "int pthread_join(pthread_t, void **);\n"
    "#endif\n";

static const char hdr_ctype[] =
    "#ifndef _CTYPE_H\n"
    "#define _CTYPE_H\n"
    "#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))\n"
    "#define isalnum(c) (isalpha(c) || ((c) >= '0' && (c) <= '9'))\n"
    "#define isdigit(c) ((c) >= '0' && (c) <= '9')\n"
    "#define toupper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) - 'a' + 'A') : (c))\n"
    "#define tolower(c) (((c) >= 'A' && (c) <= 'Z') ? ((c) - 'A' + 'a') : (c))\n"
    "#endif\n";

/* C99 7.9 <iso646.h>: alternative spellings for operators. Pure macros. */
static const char hdr_iso646[] =
    "#ifndef _ISO646_H\n"
    "#define _ISO646_H\n"
    "#define and    &&\n"
    "#define and_eq &=\n"
    "#define bitand &\n"
    "#define bitor  |\n"
    "#define compl  ~\n"
    "#define not    !\n"
    "#define not_eq !=\n"
    "#define or     ||\n"
    "#define or_eq  |=\n"
    "#define xor    ^\n"
    "#define xor_eq ^=\n"
    "#endif\n";

/* C99 7.10 <limits.h>: integer type limits. Values for the b1cc target model
 * (char=8, short=16, int=32, long=64, long long=64). Pure macros. */
static const char hdr_limits[] =
    "#ifndef _LIMITS_H\n"
    "#define _LIMITS_H\n"
    "#define CHAR_BIT   8\n"
    "#define SCHAR_MIN  (-128)\n"
    "#define SCHAR_MAX  127\n"
    "#define UCHAR_MAX  255\n"
    "#define CHAR_MIN   (-128)\n"
    "#define CHAR_MAX   127\n"
    "#define MB_LEN_MAX 4\n"
    "#define SHRT_MIN   (-32768)\n"
    "#define SHRT_MAX   32767\n"
    "#define USHRT_MAX  65535\n"
    "#define INT_MIN    (-2147483647-1)\n"
    "#define INT_MAX    2147483647\n"
    "#define UINT_MAX   4294967295U\n"
    "#define LONG_MIN   (-9223372036854775807L-1)\n"
    "#define LONG_MAX   9223372036854775807L\n"
    "#define ULONG_MAX  18446744073709551615UL\n"
    "#define LLONG_MIN  (-9223372036854775807LL-1)\n"
    "#define LLONG_MAX  9223372036854775807LL\n"
    "#define ULLONG_MAX 18446744073709551615ULL\n"
    "#endif\n";

/* C99 7.7 <float.h>: floating type characteristics for IEEE-754 binary32/64
 * (float, double) and the x87 80-bit long double. Pure macros. */
static const char hdr_float[] =
    "#ifndef _FLOAT_H\n"
    "#define _FLOAT_H\n"
    "#ifdef __b1cc__\n"
    "#define FLT_RADIX      2\n"
    "#define FLT_ROUNDS     1\n"
    "#define FLT_EVAL_METHOD 0\n"
    "#define DECIMAL_DIG    21\n"
    "#define FLT_MANT_DIG   24\n"
    "#define FLT_DIG        6\n"
    "#define FLT_MIN_EXP    (-125)\n"
    "#define FLT_MIN_10_EXP (-37)\n"
    "#define FLT_MAX_EXP    128\n"
    "#define FLT_MAX_10_EXP 38\n"
    "#define FLT_MAX        3.40282347e+38F\n"
    "#define FLT_MIN        1.17549435e-38F\n"
    "#define FLT_EPSILON    1.19209290e-7F\n"
    "#define DBL_MANT_DIG   53\n"
    "#define DBL_DIG        15\n"
    "#define DBL_MIN_EXP    (-1021)\n"
    "#define DBL_MIN_10_EXP (-307)\n"
    "#define DBL_MAX_EXP    1024\n"
    "#define DBL_MAX_10_EXP 308\n"
    "#define DBL_MAX        1.7976931348623157e+308\n"
    "#define DBL_MIN        2.2250738585072014e-308\n"
    "#define DBL_EPSILON    2.2204460492503131e-16\n"
    "#define LDBL_MANT_DIG  64\n"
    "#define LDBL_DIG       18\n"
    "#define LDBL_MIN_EXP   (-16381)\n"
    "#define LDBL_MIN_10_EXP (-4931)\n"
    "#define LDBL_MAX_EXP   16384\n"
    "#define LDBL_MAX_10_EXP 4932\n"
    "#define LDBL_MAX       1.18973149535723176502e+4932L\n"
    "#define LDBL_MIN       3.36210314311209350626e-4932L\n"
    "#define LDBL_EPSILON   1.08420217248550443401e-19L\n"
    "#else\n"
    "#include_next <float.h>\n"
    "#endif\n"
    "#endif\n";

/* C99 7.8 <inttypes.h>: fixed-width format-specifier macros + typedefs. The
 * conversion macros are pure preprocessor text; on the b1cc model int32 uses
 * plain specifiers and int64 uses the `l` length modifier (long is 64-bit). */
static const char hdr_inttypes[] =
    "#ifndef _INTTYPES_H\n"
    "#define _INTTYPES_H\n"
    "#ifdef __b1cc__\n"
    "#include <stdint.h>\n"
    "typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;\n"
    "#define PRId8 \"d\"\n#define PRIi8 \"i\"\n#define PRIu8 \"u\"\n#define PRIx8 \"x\"\n#define PRIX8 \"X\"\n#define PRIo8 \"o\"\n"
    "#define PRId16 \"d\"\n#define PRIi16 \"i\"\n#define PRIu16 \"u\"\n#define PRIx16 \"x\"\n#define PRIX16 \"X\"\n#define PRIo16 \"o\"\n"
    "#define PRId32 \"d\"\n#define PRIi32 \"i\"\n#define PRIu32 \"u\"\n#define PRIx32 \"x\"\n#define PRIX32 \"X\"\n#define PRIo32 \"o\"\n"
    "#define PRId64 \"ld\"\n#define PRIi64 \"li\"\n#define PRIu64 \"lu\"\n#define PRIx64 \"lx\"\n#define PRIX64 \"lX\"\n#define PRIo64 \"lo\"\n"
    "#define PRIdMAX \"ld\"\n#define PRIuMAX \"lu\"\n#define PRIxMAX \"lx\"\n"
    "#define PRIdPTR \"ld\"\n#define PRIuPTR \"lu\"\n#define PRIxPTR \"lx\"\n"
    "#define SCNd32 \"d\"\n#define SCNu32 \"u\"\n#define SCNx32 \"x\"\n"
    "#define SCNd64 \"ld\"\n#define SCNu64 \"lu\"\n#define SCNx64 \"lx\"\n"
    "#else\n"
    "#include_next <inttypes.h>\n"
    "#endif\n"
    "#endif\n";

/* C99 7.13 <setjmp.h>: jmp_buf must be at least as large as the host libc's,
 * because the real setjmp/longjmp run at runtime and fill it. The b1nix header
 * uses jmp_buf[8] (64 bytes); macOS setjmp writes 192 bytes, so on the host an
 * undersized buffer overflows the stack (the same class of bug as struct stat).
 * Size the buffer generously per target. */
static const char hdr_setjmp[] =
    "#ifndef _SETJMP_H\n"
    "#define _SETJMP_H\n"
    "#ifdef __APPLE__\n"
    "typedef long jmp_buf[48];\n"     /* >= macOS 192-byte jmp_buf */
    "#else\n"
    "typedef long jmp_buf[8];\n"      /* b1nix */
    "#endif\n"
    "int setjmp(jmp_buf env);\n"
    "void longjmp(jmp_buf env, int val) __attribute__((noreturn));\n"
    "#endif\n";

/* C99 7.3 <complex.h>: the active compiler supports double-complex arithmetic
 * and the standard imaginary-unit spelling used by the arithmetic subset. */
static const char hdr_complex[] =
    "#ifndef _COMPLEX_H\n"
    "#define _COMPLEX_H\n"
    "#define complex   _Complex\n"
    "#define imaginary _Imaginary\n"
    "#define I          1.0i\n"
    "extern double creal(double _Complex);\n"
    "extern double cimag(double _Complex);\n"
    "extern double cabs(double _Complex);\n"
    "extern double carg(double _Complex);\n"
    "extern double _Complex conj(double _Complex);\n"
    "extern double _Complex cproj(double _Complex);\n"
    "#endif\n";

/* C99 7.6 <fenv.h>: floating environment interface. The active hosted
 * targets use the platform IEEE environment and expose the standard control
 * and status operations through libc. */
static const char hdr_fenv[] =
    "#ifndef _FENV_H\n"
    "#define _FENV_H\n"
    "typedef unsigned short fexcept_t;\n"
    "#ifdef __aarch64__\n"
    "typedef struct { unsigned long long __fpsr; unsigned long long __fpcr; } fenv_t;\n"
    "#else\n"
    "typedef struct { unsigned long __opaque[4]; } fenv_t;\n"
    "#endif\n"
    "extern int feclearexcept(int);\n"
    "extern int fegetexceptflag(fexcept_t *, int);\n"
    "extern int feraiseexcept(int);\n"
    "extern int fesetexceptflag(const fexcept_t *, int);\n"
    "extern int fetestexcept(int);\n"
    "extern int fegetround(void);\n"
    "extern int fesetround(int);\n"
    "extern int fegetenv(fenv_t *);\n"
    "extern int feholdexcept(fenv_t *);\n"
    "extern int fesetenv(const fenv_t *);\n"
    "extern int feupdateenv(const fenv_t *);\n"
    "#ifdef __aarch64__\n"
    "#define FE_INVALID 0x01\n#define FE_DIVBYZERO 0x02\n#define FE_OVERFLOW 0x04\n#define FE_UNDERFLOW 0x08\n#define FE_INEXACT 0x10\n"
    "#define FE_DOWNWARD 0x00800000\n#define FE_UPWARD 0x00400000\n#define FE_TOWARDZERO 0x00c00000\n"
    "#else\n"
    "#define FE_INVALID 0x01\n#define FE_DIVBYZERO 0x04\n#define FE_OVERFLOW 0x08\n#define FE_UNDERFLOW 0x10\n#define FE_INEXACT 0x20\n"
    "#define FE_DOWNWARD 0x400\n#define FE_UPWARD 0x800\n#define FE_TOWARDZERO 0xc00\n"
    "#endif\n"
    "#define FE_ALL_EXCEPT (FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW|FE_INEXACT)\n"
    "#define FE_TONEAREST 0\n"
    "#endif\n";

/* C99 7.22 <tgmath.h>: type-generic math. b1cc does not implement per-type
 * (float/long double/complex) dispatch, so this maps to the double-precision
 * <math.h> functions, which is a documented minimal subset. */
static const char hdr_tgmath[] =
    "#ifndef _TGMATH_H\n"
    "#define _TGMATH_H\n"
    "#include <math.h>\n"
    "#endif\n";

static const BuiltinHeader builtin_headers[] = {
    { "stddef.h", hdr_stddef },
    { "b1cc_builtins.h", hdr_builtins },
    { "stdint.h", hdr_stdint },
    { "iso646.h", hdr_iso646 },
    { "limits.h", hdr_limits },
    { "float.h", hdr_float },
    { "setjmp.h", hdr_setjmp },
    { "complex.h", hdr_complex },
    { "fenv.h", hdr_fenv },
    { "tgmath.h", hdr_tgmath },
    { "stdbool.h", hdr_stdbool },
    { "stdarg.h", hdr_stdarg },
    { "stdio.h", hdr_stdio },
    { "stdlib.h", hdr_stdlib },
    { "string.h", hdr_string },
    { "unistd.h", hdr_unistd },
    { "dirent.h", hdr_dirent },
    { "sys/stat.h", hdr_sys_stat },
    { "pthread.h", hdr_pthread },
    { "ctype.h", hdr_ctype },
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
        if (strcmp(builtin_headers[i].name, "sys/stat.h") == 0) {
            char subdir[1024];
            snprintf(subdir, sizeof(subdir), "%s/sys", dir);
            mkdir(subdir, 0755);
        }
        snprintf(path, sizeof(path), "%s/%s", dir, builtin_headers[i].name);
        write_file(path, builtin_headers[i].content);
    }
    return strdup(dir);
}
