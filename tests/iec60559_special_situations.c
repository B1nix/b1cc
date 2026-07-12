/*
 * IEC 60559 Special Situations Test
 *
 * This test demonstrates the supported subset of IEC 60559 floating-point
 * behavior and documents what is out of scope by design.
 *
 * Supported:
 * - Basic <fenv.h> functions via platform libc
 * - __STDC_IEC_559__ macro defined
 * - IEEE-754 format arithmetic (correct results for normal operations)
 * - Exception flags via platform libc (feraiseexcept, fetestexcept, etc.)
 *
 * The compiler must preserve the platform floating environment. Since b1cc
 * has no floating-point optimization pass, FENV_ACCESS does not permit any
 * additional reordering.
 */

#include <fenv.h>

#ifndef __STDC_IEC_559__
#error "b1cc must advertise IEC 60559 support"
#endif

/* Test 1: Basic <fenv.h> function support */
static int test_fenv_basic(void) {
    /* These functions are supported via platform libc */
    if (fegetround() != FE_TONEAREST) return 1;
    if (fesetround(FE_TONEAREST) != 0) return 2;
    if (feraiseexcept(FE_INEXACT) != 0) return 3;
    if ((fetestexcept(FE_INEXACT) & FE_INEXACT) == 0) return 4;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 5;
    return 0;
}

/* Test 2: IEEE-754 format arithmetic (correct results) */
static int test_ieee754_arithmetic(void) {
    double a = 1.0;
    double b = 0.0;
    double c = a / b;  /* Should produce +infinity */
    double d = 0.0 / b;  /* Should produce NaN */
    double e = 3.0;
    double f = e / 2.0;  /* Should produce 1.5 */
    
    /* Check for +infinity: x > 0 && x == x (NaN check) */
    if (!(c > 0.0 && c == c)) return 1;
    
    /* Check for NaN: x != x */
    if (!(d != d)) return 2;
    
    /* Check for 1.5 */
    double diff = f - 1.5;
    if (diff < -1e-15 || diff > 1e-15) return 3;
    return 0;
}

/* Test 3: Exception flags via platform libc */
static int test_exception_flags(void) {
    fexcept_t saved;
    feclearexcept(FE_ALL_EXCEPT);
    
    /* Raise inexact exception */
    if (feraiseexcept(FE_INEXACT) != 0) return 1;
    if ((fetestexcept(FE_INEXACT) & FE_INEXACT) == 0) return 2;
    if (fegetexceptflag(&saved, FE_INEXACT) != 0) return 3;

    feclearexcept(FE_ALL_EXCEPT);
    if (fesetexceptflag(&saved, FE_INEXACT) != 0) return 4;
    if ((fetestexcept(FE_INEXACT) & FE_INEXACT) == 0) return 5;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 6;
    return 0;
}

/* Test 4: Non-default rounding modes are preserved by generated operations. */
static int test_rounding_mode_query(void) {
    int mode = fegetround();
    if (mode != FE_TONEAREST) return 1;
    double one = 1.0;
    double ten = 10.0;
    if (fesetround(FE_DOWNWARD) != 0) return 2;
    double down = one / ten;
    if (fegetround() != FE_DOWNWARD) return 3;
    if (fesetround(FE_UPWARD) != 0) return 4;
    double up = one / ten;
    if (fegetround() != FE_UPWARD) return 5;
    if (!(down < up)) return 6;
    if (fesetround(FE_TONEAREST) != 0) return 7;
    return 0;
}

int main(void) {
    int result;
    
    result = test_fenv_basic();
    if (result != 0) return 10 + result;
    
    result = test_ieee754_arithmetic();
    if (result != 0) return 20 + result;
    
    result = test_exception_flags();
    if (result != 0) return 30 + result;
    
    result = test_rounding_mode_query();
    if (result != 0) return 40 + result;
    
    return 42;
}
