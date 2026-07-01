typedef struct ArHdr {
    char ar_name[16];
    char ar_fmag[2];
} ArHdr;

static const ArHdr arhdr_init = {
    "/               ",
    "`\n",
};

int main(void) {
    char *bytes = (char *)&arhdr_init;
    if (bytes[0] != '/') return 1;
    if (bytes[15] != ' ') return 2;
    if (bytes[16] != '`') return 3;
    if (bytes[17] != '\n') return 4;
    return 42;
}
