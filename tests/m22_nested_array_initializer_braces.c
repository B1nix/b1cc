struct in6_addr {
    union {
        unsigned char s6_addr[16];
        unsigned short s6_addr16[8];
        unsigned int s6_addr32[4];
    };
};

#define IN6ADDR_ANY_INIT { { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } } }
#define IN6ADDR_LOOPBACK_INIT { { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } } }

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

int main(void) {
    if (in6addr_any.s6_addr[0] != 0) return 1;
    if (in6addr_any.s6_addr[15] != 0) return 2;
    if (in6addr_loopback.s6_addr[14] != 0) return 3;
    if (in6addr_loopback.s6_addr[15] != 1) return 4;
    return 42;
}
