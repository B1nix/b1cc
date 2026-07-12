#include <complex.h>

double _Imaginary z;
double _Imaginary q;
double _Imaginary identity_imag(double _Imaginary x) { return x; }

int main(void) {
    if (sizeof(z) != sizeof(double)) return 1;
    z = 2.0i;
    if (__imag__ z != 2.0) return 2;
    q = 3.0i;
    double _Imaginary got = identity_imag(z);
    if (__imag__ got != 2.0) return 3;
    double _Imaginary sum = z + q;
    if (__imag__ sum != 5.0) return 3;
    if ((double)(__real__ (z * q)) != -6.0) return 4;
    return 42;
}
