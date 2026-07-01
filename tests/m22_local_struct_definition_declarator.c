union sigval {
    int sival_int;
    void *sival_ptr;
};

int main(void) {
    struct k_sigevent {
        int sigev_notify;
        int sigev_signo;
        union sigval sigev_value;
    } sev;
    sev.sigev_signo = 42;
    return sev.sigev_signo;
}
