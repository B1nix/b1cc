/* tests/c23_bool.c - Test C23 bool, true, and false keywords */

#if true
constexpr int PREPROC_TRUE = 1;
#else
constexpr int PREPROC_TRUE = 0;
#endif

#if false
constexpr int PREPROC_FALSE = 1;
#else
constexpr int PREPROC_FALSE = 0;
#endif

bool invert(bool value) {
    return !value;
}

int main(void) {
    bool a = true;
    bool b = false;
    bool c = 42;

    if (!PREPROC_TRUE) return 1;
    if (PREPROC_FALSE) return 2;
    if (a != 1) return 3;
    if (b != 0) return 4;
    if (c != true) return 5;
    if (invert(false) != true) return 6;
    if (((bool)0) != false) return 7;
    if (((bool)123) != true) return 8;
    if (sizeof(bool) != 1) return 9;
    if (alignof(bool) != 1) return 10;

    for (bool keep_going = true; keep_going; keep_going = false) {
        if (!keep_going) return 11;
    }

    return 0;
}
