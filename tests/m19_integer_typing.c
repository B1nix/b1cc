/* tests/m19_integer_typing.c - integer-only C99 typing foundation */

int main(void) {
    unsigned char uc = 255;
    unsigned short us = 65535;
    signed char sc = -1;
    short ss = -2;

    if (uc < 0) return 1;
    if (us < 0) return 2;
    if ((uc + sc) != 254) return 3;
    if ((us + ss) != 65533) return 4;

    unsigned int ui = 4000000000;
    long minus_one = -1;
    if ((ui + minus_one) != 3999999999) return 5;
    if ((minus_one < ui) == 0) return 6;

    _Bool b = 2;
    if (b != 1) return 7;
    b = 0;
    if (b != 0) return 8;
    b = -9;
    if (b != 1) return 9;
    if (((_Bool)123) != 1) return 10;
    if (((_Bool)0) != 0) return 11;

    int value = 41;
    int *restrict p = &value;
    *p = *p + 1;
    if (value != 42) return 12;

    return 0;
}
