/* M34: legal const-qualified pointer usage compiles and runs. Adding const
 * (int* -> const int*) is allowed; const-to-const is allowed. */
int main(void) {
    int x = 40;
    const int *cp = &x;      /* adding const: legal */
    const int y = 2;
    const int *cq = &y;      /* const -> const: legal */
    return *cp + *cq;        /* 42 */
}
