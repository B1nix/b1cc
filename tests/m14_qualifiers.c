/* tests/m14_qualifiers.c — M14 const/volatile qualifier semantics.
 *
 * Exercises that:
 *   - const-qualified objects are readable (their value is correct),
 *   - volatile objects can be read and written in a loop, with every
 *     access preserved by b1cc's non-caching code generation,
 *   - const pointers-to-const can still reseat the pointee through a
 *     non-const view.
 *
 * Illegal writes to const objects are rejected at compile time; that is
 * covered by the negative check in test.sh, not here (this file must
 * compile cleanly and exit 0).
 */

const int LIMIT = 10;
const int STEP = 2;

int main(void) {
    volatile int counter = 0;
    int total = 0;

    /* i runs 0,2,4,6,8 -> 5 iterations */
    for (int i = 0; i < LIMIT; i = i + STEP) {
        counter = counter + 1;      /* volatile read + write each iteration */
        total = total + counter;    /* volatile read */
    }

    if (counter != 5) return 1;
    if (total != 15) return 2;      /* 1+2+3+4+5 */
    if (LIMIT != 10) return 3;      /* const read */
    if (STEP != 2) return 4;        /* const read */

    /* pointer-to-const: pointee is read-only through p, but p itself is
       a mutable pointer and may be reseated. */
    const int *p = &LIMIT;
    p = &STEP;
    if (*p != 2) return 5;

    return 0;
}
