/* tests/m18_aggregates.c — Milestone M18 conforming aggregates & initializers test */

struct Point {
    int x;
    int y;
};

struct Point p1 = { .y = 20, .x = 10 };

int arr[2][3] = { {1, 2, 3}, {4, 5, 6} };

struct Line {
    struct Point start;
    struct Point end;
};

struct Line global_line = { { 100, 200 }, { 300, 400 } };

int main(void) {
    /* 1. Verify global designated struct initialization */
    if (p1.x != 10) return 1;
    if (p1.y != 20) return 2;

    /* 2. Verify global multidimensional array */
    if (arr[0][0] != 1) return 3;
    if (arr[0][1] != 2) return 4;
    if (arr[0][2] != 3) return 5;
    if (arr[1][0] != 4) return 6;
    if (arr[1][1] != 5) return 7;
    if (arr[1][2] != 6) return 8;

    /* 3. Verify global nested struct initialization */
    if (global_line.start.x != 100) return 9;
    if (global_line.start.y != 200) return 10;
    if (global_line.end.x != 300) return 11;
    if (global_line.end.y != 400) return 12;

    /* 4. Local struct with designated initializers */
    struct Point p2 = { .y = 40, .x = 30 };
    if (p2.x != 30) return 13;
    if (p2.y != 40) return 14;

    /* 5. Local nested struct initialization */
    struct Line local_line = { { 500, 600 }, { 700, 800 } };
    if (local_line.start.x != 500) return 15;
    if (local_line.start.y != 600) return 16;
    if (local_line.end.x != 700) return 17;
    if (local_line.end.y != 800) return 18;

    return 0;
}
