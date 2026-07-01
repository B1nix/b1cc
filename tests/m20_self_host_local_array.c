/* tests/m20_self_host_local_array.c - Local string array initializer test */

int main(void) {
    char tmp[] = "/tmp/file-XXXXXX.s";
    if (tmp[0] != '/') return 1;
    if (tmp[4] != '/') return 2;
    if (tmp[5] != 'f') return 3;
    if (tmp[17] != 's') return 4;
    if (tmp[18] != 0) return 5;
    return 42;
}
