/* tests/m22_compound_literal_small_struct_assign.c - small struct compound literal assignment. */

struct Ip {
    int value;
};

static struct Ip ip;

int main(void) {
    ip = (struct Ip){42};
    return ip.value;
}
