/* Software 64-bit integer division helpers for i386 and other 32-bit targets.
 * These are called by b1cc-generated code when dividing/modulo 64-bit integers
 * on platforms without native 64-bit division instructions. */

typedef long long di_t;
typedef unsigned long long udi_t;

/* Signed 64-bit division: a / b */
di_t __divdi3(di_t a, di_t b) {
    int neg = 0;
    udi_t ua, ub, q;
    if (a < 0) { neg ^= 1; ua = (udi_t)(-a); } else { ua = (udi_t)a; }
    if (b < 0) { neg ^= 1; ub = (udi_t)(-b); } else { ub = (udi_t)b; }
    q = ua / ub;
    return neg ? (di_t)(-q) : (di_t)q;
}

/* Signed 64-bit modulo: a % b */
di_t __moddi3(di_t a, di_t b) {
    int neg = 0;
    udi_t ua, ub, r;
    if (a < 0) { neg = 1; ua = (udi_t)(-a); } else { ua = (udi_t)a; }
    if (b < 0) ub = (udi_t)(-b); else ub = (udi_t)b;
    r = ua % ub;
    return neg ? (di_t)(-r) : (di_t)r;
}

/* Unsigned 64-bit division: a / b */
udi_t __udivdi3(udi_t a, udi_t b) {
    return a / b;
}

/* Unsigned 64-bit modulo: a % b */
udi_t __umoddi3(udi_t a, udi_t b) {
    return a % b;
}
