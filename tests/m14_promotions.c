/* tests/m14_promotions.c - Test integer promotions and UAC */

int main(void) {
    char c = 5;
    short s = 10;
    unsigned char uc = 200;
    
    // char and short promote to int
    if (sizeof(c + c) != 4) return 1;
    if (sizeof(c * s) != 4) return 2;
    if (sizeof(uc + uc) != 4) return 3;
    
    unsigned int u = 100;
    
    // UAC converts char and unsigned int to common unsigned int
    if (sizeof(c + u) != 4) return 4;
    
    if (c + c != 10) return 5;
    if (c * s != 50) return 6;
    if (uc + uc != 400) return 7;

    return 0;
}
