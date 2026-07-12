#include <complex.h>

float _Complex z;
float _Complex identity_float_complex(float _Complex x) { return x; }

int main(void) {
    z = 1.0f + 2.0fi;
    z = identity_float_complex(z);
    if (__real__ z != 1.0f || __imag__ z != 2.0f) return 1;
    if (sizeof(z) != 8) return 2;
    return 42;
}
