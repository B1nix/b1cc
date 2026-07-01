typedef int wchar_t;

int main(void) {
    wchar_t upper = L'A';
    wchar_t lower = L'a';
    if (upper != 65) return 1;
    if (lower - upper != 32) return 2;
    if (L'\n' != 10) return 3;
    return 42;
}
