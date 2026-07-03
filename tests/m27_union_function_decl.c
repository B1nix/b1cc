union U {
    int x;
};

union U make_u(void);

int main(void) {
    union U u;
    u.x = 42;
    return u.x;
}
