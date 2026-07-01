int main(void) {
    static int probed, has_tls;
    probed = 40;
    has_tls = 2;
    return probed + has_tls;
}
