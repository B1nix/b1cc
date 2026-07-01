struct PackedTail {
    unsigned short a;
    unsigned int b;
} __attribute__((packed));

struct OuterPacked {
    struct {
        unsigned int x;
    } __attribute__((packed)) inner;
};

int main(void) {
    struct PackedTail p;
    struct OuterPacked o;
    p.a = 40;
    p.b = 2;
    o.inner.x = p.a + p.b;
    return o.inner.x;
}
