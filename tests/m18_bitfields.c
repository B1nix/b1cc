/* tests/m18_bitfields.c — M18 bitfield packing/unpacking test */
int main(void) {
    struct Flags {
        unsigned int a : 3;
        unsigned int b : 5;
        unsigned int c : 8;
    };

    struct Flags f;
    f.a = 5;
    f.b = 17;
    f.c = 200;

    /* Verify read-back */
    if (f.a != 5) return 1;
    if (f.b != 17) return 2;
    if (f.c != 200) return 3;

    /* Verify values don't bleed into each other */
    f.a = 7;
    if (f.b != 17) return 4;
    if (f.c != 200) return 5;

    f.b = 0;
    if (f.a != 7) return 6;
    if (f.c != 200) return 7;

    return 0;
}
