/* tests/m17_preprocessor_full.c — full C99 preprocessor test */

#define CONCAT_OBJ 0x2 ## A  /* 0x2A = 42 */

#define GLUE(a, b) a ## b

#define STR(x) #x

#define SUM(...) sum_helper(__VA_ARGS__)

int sum_helper(int x, int y, int z) {
    return x + y + z;
}

#if 0
int value = 1;
#elif 1
int value = CONCAT_OBJ;
#else
int value = 2;
#endif

int strcmp(char *s1, char *s2) {
    int i = 0;
    while (s1[i] != 0 && s2[i] != 0) {
        if (s1[i] != s2[i]) return 1;
        i++;
    }
    return s1[i] != s2[i];
}

int GLUE(ma, in)(void) {
    if (value != 42) {
        return 1;
    }
    if (strcmp(STR(hello world), "hello world") != 0) {
        return 2;
    }
    if (SUM(10, 20, 30) != 60) {
        return 3;
    }
    return 0;
}
