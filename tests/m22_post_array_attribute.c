/* tests/m22_post_array_attribute.c - GNU attribute after an array declarator. */

struct Entry {
    int value;
};

static struct Entry table[2] __attribute__((aligned(64)));

int main(void) {
    table[0].value = 40;
    table[1].value = 2;
    return table[0].value + table[1].value;
}
