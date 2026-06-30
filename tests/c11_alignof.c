/* tests/c11_alignof.c — Test C11 _Alignof and alignof operators */
struct S {
    char a;
    int b;
};

int main(void) {
    if (_Alignof(char) != 1) return 1;
    if (_Alignof(short) != 2) return 2;
    if (_Alignof(int) != 4) return 3;
    if (_Alignof(long) != 8) return 4;
    if (_Alignof(char *) != 8) return 5;
    if (_Alignof(struct S) != 4) return 6;
    
    char c;
    if (_Alignof(c) != 1) return 7;
    
    return 0;
}
