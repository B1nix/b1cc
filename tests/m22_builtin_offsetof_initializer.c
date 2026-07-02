typedef unsigned short uint16_t;

typedef struct TCCState {
    int warn_none;
    int warn_all;
} TCCState;

typedef struct OptionalHeader {
    int base;
    int checksum;
} OptionalHeader;

typedef struct PEHeader {
    int signature;
    OptionalHeader opthdr;
} PEHeader;

typedef struct FlagDef {
    uint16_t offset;
    uint16_t flags;
    const char *name;
} FlagDef;

static const FlagDef options_W[] = {
    { __builtin_offsetof(TCCState, warn_all), 1, "all" },
    { __builtin_offsetof(PEHeader, opthdr.checksum), 2, "checksum" },
    { 0, 0, ((void *)0) },
};

int main(void) {
    char *bytes = (char *)options_W;
    if (bytes[0] != 4) return 1;
    if (bytes[2] != 1) return 2;
    if (bytes[16] != 8) return 3;
    if (bytes[18] != 2) return 4;
    return 42;
}
