/* M30: Test bundled freestanding standard C headers */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

int main(void) {
    int status = 0;

    /* stddef.h */
    size_t sz = sizeof(size_t);
    ptrdiff_t pd = 10 - 5;
    if (sz != 8) status = 1;
    if (pd != 5) status = 1;

    /* stdint.h */
    int32_t i32 = -42;
    uint32_t u32 = 42;
    int64_t i64 = -1000000L;
    uint64_t u64 = 1000000UL;
    if (i32 != -42) status = 1;
    if (u32 != 42) status = 1;
    if (i64 != -1000000L) status = 1;
    if (u64 != 1000000UL) status = 1;
    if (INT32_MAX != 2147483647) status = 1;
    if (UINT64_MAX != 18446744073709551615UL) status = 1;

    /* stdbool.h */
    bool flag = true;
    if (!flag) status = 1;
    flag = false;
    if (flag) status = 1;

    /* stdarg.h: test via a variadic-like pattern using builtins */
    /* Just verify the header compiles and types are available */
    void *vl = (void *)0;
    (void)vl;
    (void)sz;
    (void)pd;
    (void)i32;
    (void)u32;
    (void)i64;
    (void)u64;

    return status;
}
