/* M26: exported function with no main, linked as a shared library via -shared. */
int shared_add(int a, int b) {
    return a + b;
}
