int side_effect;

int bump(void) {
    side_effect++;
    return side_effect;
}

int main(void) {
    int x = 0;
    if ((x = 3, x) != 3) return 1;
    if ((0 && bump()) != 0 || side_effect != 0) return 2;
    if ((1 || bump()) != 1 || side_effect != 0) return 3;
    if ((0 ? bump() : 7) != 7 || side_effect != 0) return 4;
    if ((1 ? 8 : bump()) != 8 || side_effect != 0) return 5;
    return 0;
}
