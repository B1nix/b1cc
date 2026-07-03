int g = 0xA1E1C5A3;
unsigned char u = 8;
int *p = &g;

int ret(void) {
    return *p;
}

int main(void) {
    int x = (u = ret());
    g = x;
    if (u != 0xA3) return 1;
    if (g != 0xA3) return 2;
    return 42;
}
