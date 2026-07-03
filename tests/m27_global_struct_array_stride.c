struct S {
    int x;
    unsigned short y;
};

static struct S items[3] = {{0, 9}, {0, 9}, {0, 9}};

int main(void) {
    if (items[0].y != 9) {
        return 1;
    }
    if (items[1].y != 9) {
        return 2;
    }
    if (items[2].y != 9) {
        return 3;
    }
    items[1].x = 40;
    items[2].y = 2;
    return items[1].x + items[2].y;
}
