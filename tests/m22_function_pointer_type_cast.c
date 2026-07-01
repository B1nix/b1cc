typedef void (*handler_t)(int);

int main(void) {
    handler_t h = (void (*)(int))0;
    if (h != (void (*)(int))0) return 1;
    return 42;
}
