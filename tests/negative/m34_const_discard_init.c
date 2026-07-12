/* M34: initializing a pointer-to-non-const from the address of a const object
 * discards the const qualifier and must be diagnosed. */
int main(void) {
    const int x = 5;
    int *p = &x;
    return *p;
}
