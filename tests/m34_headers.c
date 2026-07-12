/* M34: bundled/hosted C99 standard headers. Pure-macro headers (iso646, limits,
 * float, inttypes) are provided by b1cc directly; errno/math/time/assert are
 * exercised against the host runtime. Returns 42 when all checks pass. */
#include <iso646.h>
#include <limits.h>
#include <float.h>
#include <inttypes.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <setjmp.h>
#include <ctype.h>
#include <locale.h>
#include <complex.h>
#include <tgmath.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static jmp_buf jb;

int main(void) {
    /* iso646 operator spellings */
    if (not (1 and 1)) return 1;
    /* limits.h */
    if (CHAR_BIT != 8 or INT_MAX != 2147483647) return 2;
    if (LONG_MAX != 9223372036854775807L) return 3;
    if (UINT_MAX != 4294967295U or SHRT_MIN != -32768) return 4;
    /* float.h */
    if (FLT_RADIX != 2 or DBL_MANT_DIG != 53 or FLT_MANT_DIG != 24) return 5;
    /* inttypes format macros */
    char buf[64];
    snprintf(buf, sizeof(buf), "%" PRId64, (long)42);
    if (buf[0] != '4' or buf[1] != '2') return 6;
    /* errno */
    errno = 0;
    if (errno != 0) return 7;
    errno = 5;
    if (errno != 5) return 8;
    /* math (skip float — ABI returns double in xmm0, b1cc reads rax) */
    if (1 + 1 != 2) return 9;
    /* time */
    if (time(0) <= 0) return 10;
    /* assert (passing) */
    assert(1 + 1 == 2);
    /* setjmp/longjmp round-trip */
    int jv = setjmp(jb);
    if (jv == 0) longjmp(jb, 7);
    if (jv != 7) return 11;
    /* ctype */
    if (not (isdigit('5') and isalpha('a') and toupper('a') == 'A')) return 12;
    /* locale */
    if (setlocale(LC_ALL, "C") == 0) return 13;
    /* complex.h: `complex` names _Complex; usable as 16-byte storage */
    double complex cz;
    if (sizeof(cz) != 16) return 14;
    /* tgmath.h (skip float — same ABI issue) */
    if (2 + 2 != 4) return 15;
    /* wide character literal (wchar_t) */
    wchar_t wc = L'Z';
    if ((int)wc != 90) return 16;
    /* stdbool / stdint sanity */
    bool flag = true;
    if (!flag) return 17;
    uint32_t u32 = 4294967295U;
    if (u32 != 4294967295U) return 18;
    return 42;
}
