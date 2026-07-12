#include <fenv.h>

#ifndef __STDC_IEC_559__
#error "b1cc active targets must advertise IEC 60559 support"
#endif

int main(void) {
    fenv_t env;
    if (fegetround() != FE_TONEAREST) return 1;
    if (fesetround(FE_TONEAREST) != 0) return 2;
    if (feraiseexcept(FE_INEXACT) != 0) return 3;
    if ((fetestexcept(FE_INEXACT) & FE_INEXACT) == 0) return 4;
    if (feclearexcept(FE_ALL_EXCEPT) != 0) return 5;
    if (fegetenv(&env) != 0) return 6;
    if (fesetenv(&env) != 0) return 7;
    return 42;
}
