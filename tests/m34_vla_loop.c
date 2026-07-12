/* M34: variable-length array with a run-time bound, re-created each loop
 * iteration; verifies per-iteration allocation and indexing are correct. */
int main(void) {
    int total = 0;
    int k = 0;
    while (k < 3) {
        int n = k + 2;          /* 2, 3, 4 */
        int a[n];               /* VLA */
        int i = 0;
        while (i < n) { a[i] = i; i = i + 1; }
        total = total + a[n - 1]; /* 1 + 2 + 3 = 6 */
        k = k + 1;
    }
    if (total != 6) return 1;
    return 42;
}
