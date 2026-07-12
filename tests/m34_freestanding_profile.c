/* M34: the freestanding profile must expose the bundled standard surface
 * without consulting host libc headers. */
#include <assert.h>
#include <complex.h>
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <iso646.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#if __STDC_HOSTED__ != 0
#error freestanding profile did not define __STDC_HOSTED__ to zero
#endif

int main(void) {
    return (sizeof(size_t) == 8 && sizeof(int32_t) == 4 && CHAR_BIT == 8 &&
            FLT_RADIX == 2 && sizeof(double complex) == 16) ? 0 : 1;
}
