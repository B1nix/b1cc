/* tests/m22_static_local_struct_array_inferred.c - TCC-style local static struct table with inferred length. */

int main(void) {
    static const struct Subsystem {
        const char *name;
        int value;
    } table[] = {
        { "native", 1 },
        { 0, -1 },
    };
    if (table[0].value != 1) return 1;
    return 42;
}
