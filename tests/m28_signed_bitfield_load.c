struct S {
    signed f0 : 17;
};

static struct S values[3] = {{-259}, {-259}, {-259}};

int main(void) {
    if (values[0].f0 != -259) return 1;
    if (values[1].f0 != -259) return 2;
    if (values[2].f0 != -259) return 3;
    return 42;
}
