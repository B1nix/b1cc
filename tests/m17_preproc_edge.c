/* tests/m17_preproc_edge.c - Test preprocessor edge cases (recursion & placemarkers) */

#define GLUE(a, b) a ## b

#define x y
#define y x

int main(void) {
    int GLUE(va, ) = 42;
    int GLUE(, lue) = 42;
    
    if (va != 42) return 1;
    if (lue != 42) return 2;
    
    int x = 100;
    if (x != 100) return 3;
    
    return 0;
}
