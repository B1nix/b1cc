/* M34: basic _Complex arithmetic, imaginary literals, and extraction. */
#include <complex.h>
double _Complex a;
double _Complex b;
double _Complex out;

int main(void) {
    double *ap = (double *)&a;
    double *bp = (double *)&b;
    ap[0] = 3.0;
    ap[1] = 4.0;
    bp[0] = 1.0;
    bp[1] = 2.0;
    out = a + b;
    if (__real__ out != 4.0 || __imag__ out != 6.0) return 1;
    out = a * b;
    if (__real__ out != -5.0 || __imag__ out != 10.0) return 2;
    out = a / b;
    if (__real__ out != 2.2 || __imag__ out != -0.4) return 3;
    out = a + 2.0 * I;
    if (__real__ out != 3.0 || __imag__ out != 6.0) return 4;
    if (creal(out) != 3.0 || cimag(out) != 6.0) return 5;
    if ((int)cabs(out) != 6) return 6;
    if (!(carg(out) > 1.0 && carg(out) < 2.0)) return 7;
    out = cproj(out);
    if (creal(out) != 3.0 || cimag(out) != 6.0) return 8;
    out = conj(out);
    if (__real__ out != 3.0 || __imag__ out != -6.0) return 9;
    return 42;
}
