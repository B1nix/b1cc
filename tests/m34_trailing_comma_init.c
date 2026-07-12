/* M34: trailing commas in initializer lists must be accepted. */

int main(void) {
    /* Trailing comma in array init */
    int arr[4] = { 1, 2, 3, 4, };
    if (arr[0] != 1) return 1;
    if (arr[1] != 2) return 2;
    if (arr[2] != 3) return 3;
    if (arr[3] != 4) return 4;

    /* Trailing comma in struct init */
    struct { int x; int y; int z; } s = { 10, 20, 30, };
    if (s.x != 10) return 5;
    if (s.y != 20) return 6;
    if (s.z != 30) return 7;

    /* Trailing comma in nested init */
    int grid[2][2] = { { 1, 2, }, { 3, 4, }, };
    if (grid[0][0] != 1) return 8;
    if (grid[0][1] != 2) return 9;
    if (grid[1][0] != 3) return 10;
    if (grid[1][1] != 4) return 11;

    /* Trailing comma in designated init */
    int darr[3] = { [0] = 5, [2] = 7, };
    if (darr[0] != 5) return 12;
    if (darr[1] != 0) return 13;
    if (darr[2] != 7) return 14;

    return 42;
}
