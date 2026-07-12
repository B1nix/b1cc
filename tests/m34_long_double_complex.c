#include <complex.h>

long double _Complex z;

int main(void) {
    z = 1.0L + 2.0Li;
    if (__real__ z != 1.0L || __imag__ z != 2.0L) return 1;
    if (sizeof(z) != 16) return 2;
    return 42;
}
