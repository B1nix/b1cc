/* tests/m17_if_macro_expr.c — #if must expand function-like macros */

#define INC(x) x + 1
#define CHOOSE(a, b) a ## b

#if INC(1) == 2 && CHOOSE(4, 2) == 42
int main(void) {
    return 0;
}
#else
int main(void) {
    return 1;
}
#endif
