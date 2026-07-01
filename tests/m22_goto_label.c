/* tests/m22_goto_label.c - Basic C labels and goto. */

int main(void) {
    int x = 1;
    goto target;
    x = 99;
target:
    x = x + 41;
    return x;
}

