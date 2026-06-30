/* tests/m17_preproc_edge.c - Test preprocessor edge cases (recursion & placemarkers) */

#define GLUE(a, b) a ## b
#define CAT(a, b) a ## b
#define EXPAND_CAT(a, b) CAT(a, b)
#define SUFFIX 2

#define x y
#define y x

int main(void) {
    int GLUE(va, ) = 42;
    int GLUE(, lue) = 42;
    
    if (va != 42) return 1;
    if (lue != 42) return 2;
    
    int x = 100;
    if (x != 100) return 3;

    int XSUFFIX = 11;
    int X2 = 22;
    if (CAT(X, SUFFIX) != 11) return 4;
    if (EXPAND_CAT(X, SUFFIX) != 22) return 5;
    
    return 0;
}
