/* tests/c11_anon_struct.c — Test C11 anonymous structures and unions */
struct Container {
    int a;
    union {
        int b;
        char c;
    };
    struct {
        int d;
        int e;
    };
};

int main(void) {
    struct Container s;
    s.a = 10;
    s.b = 20;
    s.d = 30;
    s.e = 40;

    if (s.a != 10) return 1;
    if (s.b != 20) return 2;
    if (s.d != 30) return 3;
    if (s.e != 40) return 4;

    return 0;
}
