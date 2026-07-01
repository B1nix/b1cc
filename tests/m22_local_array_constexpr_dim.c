typedef unsigned char u8;

int main(void) {
    u8 prebuf[64 + 32 + 1];
    char left[2], right[1 + 2];
    int n = 3;
    char *argv[n + 1];
    prebuf[96] = 40;
    right[2] = 2;
    argv[0] = (char *)0;
    return prebuf[96] + right[2];
}
