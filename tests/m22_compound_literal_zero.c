struct sigaction {
    int handler;
    int flags;
};

int main(void) {
    (struct sigaction){0};
    return 42;
}
