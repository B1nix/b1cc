/* tests/m17_if_macro_expr.c — #if must expand function-like macros and support full C99 operators */

#define INC(x) x + 1
#define CHOOSE(a, b) a ## b

#if INC(1) == 2 && CHOOSE(4, 2) == 42 && (1 << 3) == 8 && (12 & 4) == 4 && (8 | 2) == 10 && (9 ^ 3) == 10 && +5 == 5 && 17 % 5 == 2
int main(void) {
    return 0;
}
#else
int main(void) {
    return 1;
}
#endif
