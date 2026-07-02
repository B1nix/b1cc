/* tests/m22_array_range_designator.c - GNU array range designators. */

typedef struct Pair {
    int x;
    int y;
} Pair;

static const Pair table[8] = {
    [0 ... 3] = { 10, 1 },
    [7] = { 40, 2 },
};

int main(void) {
    char *bytes = (char *)table;
    if (bytes[0] != 10) return 1;
    if (bytes[4] != 1) return 2;
    if (bytes[24] != 10) return 3;
    if (bytes[28] != 1) return 4;
    if (bytes[32] != 0) return 5;
    return bytes[56] + bytes[60];
}
