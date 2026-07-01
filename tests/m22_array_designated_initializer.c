typedef unsigned char u8;

static const u8 table[8] = {
    [1] = 7,
    [4] = 35,
};

int main(void) {
    return table[1] + table[4];
}
