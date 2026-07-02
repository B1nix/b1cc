/* tests/m22_aggregate_call_argument.c - aggregate function call result as aggregate argument. */

struct Ip {
    int value;
};

static struct Ip make_ip(void) {
    struct Ip ip = {42};
    return ip;
}

static int take_ip(struct Ip ip) {
    return ip.value;
}

int main(void) {
    return take_ip(make_ip());
}
