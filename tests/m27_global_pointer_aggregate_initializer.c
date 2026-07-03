union U {
    int x;
};

union U g;
static union U *ptrs[4] = {&g, (void *)0, &g, 0};

int main(void) {
    g.x = 21;
    if (ptrs[0] != &g) {
        return 1;
    }
    if (ptrs[1] != 0) {
        return 2;
    }
    if (ptrs[2]->x != 21) {
        return 3;
    }
    if (ptrs[3] != 0) {
        return 4;
    }
    return ptrs[0]->x * 2;
}
