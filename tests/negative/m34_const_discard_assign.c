/* M34: assigning the address of a const object into a pointer-to-non-const
 * discards the const qualifier and must be diagnosed. */
int main(void) {
    const int x = 5;
    int *p;
    p = &x;
    return *p;
}
