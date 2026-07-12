/* M34: complex values through a function parameter and return value. */
double _Complex add_complex(double _Complex x, double _Complex y) {
    return x + y;
}

int main(void) {
    double _Complex a;
    double _Complex b;
    double *ap = (double *)&a;
    double *bp = (double *)&b;
    ap[0] = 3.0;
    ap[1] = 4.0;
    bp[0] = 1.0;
    bp[1] = 2.0;
    a = add_complex(a, b);
    if (__real__ a != 4.0 || __imag__ a != 6.0) return 1;
    return 42;
}
