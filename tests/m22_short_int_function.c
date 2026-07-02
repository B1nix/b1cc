/* tests/m22_short_int_function.c - TCC-style short int function return types. */

short int narrow_value(const char *s) {
    if (s[0] == 'o') return 40;
    return 1;
}

unsigned short int add_two(unsigned short int x) {
    return x + 2;
}

int main(void) {
    return narrow_value("ok") + add_two(0);
}
