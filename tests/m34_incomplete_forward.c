struct Forward;
struct Forward *pointer;
struct Forward { int value; };
int main(void) {
    struct Forward object;
    object.value = 7;
    pointer = &object;
    return pointer->value == 7 ? 0 : 1;
}
