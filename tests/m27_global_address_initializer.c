struct S {
    int a;
    int b;
};

int g[4];
struct S s;
int *gp = &g[2];
int *sp = &s.b;

int main(void) {
    *gp = 20;
    *sp = 22;
    return g[2] + s.b;
}
