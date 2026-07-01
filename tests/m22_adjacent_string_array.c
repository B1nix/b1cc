/* tests/m22_adjacent_string_array.c - Global char arrays accept adjacent strings. */

static const char words[] = "ab" "\0" "cd";

int match(const char *s) {
    if (s[0] != 'h') return 1;
    if (s[5] != ' ') return 2;
    if (s[6] != 't') return 3;
    return 0;
}

int main(void) {
    if (words[0] != 'a') return 1;
    if (words[1] != 'b') return 2;
    if (words[2] != 0) return 3;
    if (words[3] != 'c') return 4;
    if (words[4] != 'd') return 5;
    if (words[5] != 0) return 6;
    if (match("hello" " " "there") != 0) return 7;
    return 42;
}
