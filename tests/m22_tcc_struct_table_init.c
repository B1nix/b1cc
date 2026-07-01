typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint64_t;

enum {
    TOK_ASM_clc = 1,
};

#define OPT_EA 0x80
#define O(o) ((uint64_t)((((o) & 0xff00) == 0x0f00) ? ((((o) >> 8) & ~0xff) | ((o) & 0xff)) : (o)))

typedef struct ASMInstr {
    uint16_t sym;
    uint16_t opcode;
    uint16_t instr_type;
    uint8_t nb_ops;
    uint8_t op_type[3];
} ASMInstr;

static const ASMInstr asm_instrs[] = {
    { TOK_ASM_clc, O(0xf30f1e), 7, 1, { OPT_EA } },
    { 0, },
};

int main(void) {
    char *bytes = (char *)asm_instrs;
    if (bytes[2] != 0x1e) return 1;
    if ((bytes[3] & 255) != 0xf3) return 2;
    if ((bytes[7] & 255) != OPT_EA) return 3;
    if (bytes[16] != 0) return 4;
    return 42;
}
