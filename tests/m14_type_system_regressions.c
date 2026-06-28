/* tests/m14_type_system_regressions.c — real checks for M14 typing/declarators */

int main(void) {
    unsigned int x = 0;
    if (x - 1 <= 0) return 1;

    int arr[3];
    if (sizeof(arr) != 12) return 2;
    if (sizeof(arr[0]) != 4) return 3;

    int value = 42;
    int *ptrs[3];
    ptrs[0] = &value;
    if (*(ptrs[0]) != 42) return 4;
    if (sizeof(ptrs) != 24) return 5;
    if (sizeof(ptrs[0]) != 8) return 6;
    if (sizeof(*(ptrs[0])) != 4) return 7;

    struct HasArray {
        int values[3];
        char tail;
    };
    struct Other {
        int pad;
        int values;
    };

    struct HasArray h = { { 10, 20, 30 }, 4 };
    if (h.values[0] != 10) return 8;
    if (h.values[1] != 20) return 9;
    if (h.values[2] != 30) return 10;
    h.values[1] = 42;
    if (h.values[1] != 42) return 11;
    if (h.tail != 4) return 12;

    struct HasArray *hp = &h;
    hp->values[2] = 7;
    if (hp->values[2] != 7) return 13;
    hp->tail = 9;
    if (h.tail != 9) return 14;

    struct Other o = { 100, 5 };
    if (o.values != 5) return 15;
    if (h.values[0] != 10) return 16;

    return 0;
}
