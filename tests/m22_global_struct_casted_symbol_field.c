/* tests/m22_global_struct_casted_symbol_field.c - casted global array symbol in struct initializer. */

struct File {
    const char *path;
    const char *data;
    long size;
};

static const unsigned char blob[] = {1, 2, 3};

static const struct File files[] = {
    {"/blob", (const char *)blob, sizeof(blob)},
};

int main(void) {
    return 42;
}
