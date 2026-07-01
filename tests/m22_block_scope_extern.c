int helper(void) {
    return 42;
}

int main(void) {
    extern int helper(void);
    return helper();
}
