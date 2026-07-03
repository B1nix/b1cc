int main(void) {
    int arr[3];
    int i;
    i = 1;
    for (arr[i] = 40; arr[i] < 42; arr[i]++) {
    }

    short s[2];
    s[0] = 1;
    s[1] = 20;
    s[0]++;
    if (s[1] != 20) {
        return 1;
    }
    return arr[1];
}
