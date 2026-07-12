/*
 * macho_writer.c — Native Mach-O relocatable object writer for b1cc (M15)
 *
 * Strategy:
 *   1. Parse the GNU AS arm64 text produced by backend_arm64.c into
 *      a simple section/symbol/reloc model.
 *   2. Encode arm64 instructions to binary using a minimal table-driven
 *      encoder covering the exact instruction set emitted by b1cc.
 *   3. Lay out Mach-O (MH_OBJECT) container and write bytes.
 */

#include "macho_writer.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>

/* =========================================================================
 * Mach-O constants & structs
 * ========================================================================= */

#define MH_MAGIC_64 0xfeedfacf
#define CPU_TYPE_ARM64 0x0100000c
#define CPU_SUBTYPE_ARM64_ALL 0x00000000
#define MH_OBJECT 0x1
#define MH_SUBSECTIONS_VIA_SYMBOLS 0x2000

struct mach_header_64 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

#define LC_SEGMENT_64 0x19
#define LC_SYMTAB 0x2
#define LC_DYSYMTAB 0xb

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};
#define LC_BUILD_VERSION 0x32

struct build_version_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
};
struct dysymtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
};

struct nlist_64 {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

#define N_TYPE 0x0e
#define N_EXT  0x01
#define N_UNDF 0x0
#define N_SECT 0xe

enum arm64_reloc_type_e {
    ARM64_RELOC_UNSIGNED      = 0,
    ARM64_RELOC_SUBTRACTOR    = 1,
    ARM64_RELOC_BRANCH26      = 2,
    ARM64_RELOC_PAGE21        = 3,
    ARM64_RELOC_PAGEOFF12     = 4,
    ARM64_RELOC_GOT_LOAD_PAGE21 = 5,
    ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6
};

/* =========================================================================
 * Byte buffer & String Table builder (copied from elf_writer structure)
 * ========================================================================= */

typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   cap;
} ByteBuf;

static void bb_init(ByteBuf *b) {
    b->data = NULL;
    b->size = 0;
    b->cap  = 0;
}

static void bb_reserve(ByteBuf *b, size_t extra) {
    size_t need = b->size + extra;
    if (need > b->cap) {
        size_t newcap = (b->cap == 0) ? 256 : b->cap * 2;
        while (newcap < need) newcap *= 2;
        b->data = realloc(b->data, newcap);
        if (!b->data) diagnostics_fatal("macho_writer: out of memory");
        b->cap = newcap;
    }
}

static void bb_write8(ByteBuf *b, uint8_t v) {
    bb_reserve(b, 1);
    b->data[b->size++] = v;
}

static void bb_write16le(ByteBuf *b, uint16_t v) {
    bb_reserve(b, 2);
    b->data[b->size++] = (uint8_t)(v);
    b->data[b->size++] = (uint8_t)(v >> 8);
}

static void bb_write32le(ByteBuf *b, uint32_t v) {
    bb_reserve(b, 4);
    b->data[b->size++] = (uint8_t)(v);
    b->data[b->size++] = (uint8_t)(v >> 8);
    b->data[b->size++] = (uint8_t)(v >> 16);
    b->data[b->size++] = (uint8_t)(v >> 24);
}

static void bb_write64le(ByteBuf *b, uint64_t v) {
    bb_write32le(b, (uint32_t)(v));
    bb_write32le(b, (uint32_t)(v >> 32));
}

static void bb_writebytes(ByteBuf *b, const uint8_t *src, size_t n) {
    bb_reserve(b, n);
    memcpy(b->data + b->size, src, n);
    b->size += n;
}

static void bb_patch32le(ByteBuf *b, size_t off, uint32_t v) {
    b->data[off+0] = (uint8_t)(v);
    b->data[off+1] = (uint8_t)(v >> 8);
    b->data[off+2] = (uint8_t)(v >> 16);
    b->data[off+3] = (uint8_t)(v >> 24);
}

static void bb_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
    b->size = b->cap = 0;
}

static void bb_align(ByteBuf *b, size_t align) {
    if (align <= 1) return;
    while (b->size % align) bb_write8(b, 0);
}

typedef struct {
    ByteBuf buf;
} Strtab;

static void strtab_init(Strtab *st) {
    bb_init(&st->buf);
    bb_write8(&st->buf, 0); /* empty string index 0 */
}

static uint32_t strtab_add(Strtab *st, const char *s) {
    uint32_t off = (uint32_t)st->buf.size;
    size_t n = strlen(s) + 1;
    bb_reserve(&st->buf, n);
    memcpy(st->buf.data + st->buf.size, s, n);
    st->buf.size += n;
    return off;
}

static void strtab_free(Strtab *st) { bb_free(&st->buf); }

/* =========================================================================
 * Mach-O Symbol and Relocation internal model
 * ========================================================================= */

typedef enum {
    SEC_NONE,
    SEC_TEXT,
    SEC_CSTRING,
    SEC_DATA
} SectionId;

typedef struct {
    char     name[256];
    uint32_t name_off;
    uint8_t  sect;       /* 1-based section: 1=__text, 2=__cstring, 3=__data */
    uint64_t value;      /* offset within section */
    int      defined;
    int      is_global;
} MachoSym;

typedef struct {
    char     sym_name[256];
    uint64_t offset;
    uint32_t type;       /* ARM64_RELOC_* */
} MachoReloc;

typedef struct { MachoSym *data; int count; int cap; } SymArr;
typedef struct { MachoReloc *data; int count; int cap; } RelocArr;

static void symarr_push(SymArr *a, const MachoSym *s) {
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->data = realloc(a->data, sizeof(MachoSym) * a->cap);
    }
    a->data[a->count++] = *s;
}

static void relocarr_push(RelocArr *a, const MachoReloc *r) {
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->data = realloc(a->data, sizeof(MachoReloc) * a->cap);
    }
    a->data[a->count++] = *r;
}

typedef struct {
    char label[256];
    size_t patch_off;
} PatchEntry;

typedef struct {
    char label[256];
    int32_t offset;
} LabelEntry;

typedef struct {
    PatchEntry *patches;
    int patch_count;
    int patch_cap;
    LabelEntry *labels;
    int label_count;
    int label_cap;
} EncCtx;

static void ctx_init(EncCtx *c) {
    c->patches = NULL;
    c->patch_count = 0;
    c->patch_cap = 0;
    c->labels = NULL;
    c->label_count = 0;
    c->label_cap = 0;
}

static void ctx_free(EncCtx *c) {
    free(c->patches);
    free(c->labels);
    c->patches = NULL;
    c->labels = NULL;
    c->patch_count = c->patch_cap = 0;
    c->label_count = c->label_cap = 0;
}

static int64_t parse_int(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int64_t)strtoull(s + 2, NULL, 16);
    return strtoll(s, NULL, 10);
}

static void ctx_add_label(EncCtx *c, const char *name, int32_t offset) {
    if (c->label_count >= c->label_cap) {
        c->label_cap = c->label_cap * 2 + 256;
        c->labels = realloc(c->labels, c->label_cap * sizeof(LabelEntry));
    }
    strncpy(c->labels[c->label_count].label, name, 255);
    c->labels[c->label_count].label[255] = 0;
    c->labels[c->label_count].offset = offset;
    c->label_count++;
}

static int32_t ctx_get_label(EncCtx *c, const char *name) {
    for (int i = 0; i < c->label_count; i++) {
        if (strcmp(c->labels[i].label, name) == 0) {
            return c->labels[i].offset;
        }
    }
    return -1;
}

static void ctx_add_patch(EncCtx *c, const char *name, size_t patch_off) {
    if (c->patch_count >= c->patch_cap) {
        c->patch_cap = c->patch_cap * 2 + 256;
        c->patches = realloc(c->patches, c->patch_cap * sizeof(PatchEntry));
    }
    strncpy(c->patches[c->patch_count].label, name, 255);
    c->patches[c->patch_count].label[255] = 0;
    c->patches[c->patch_count].patch_off = patch_off;
    c->patch_count++;
}

typedef struct {
    ByteBuf text;
    ByteBuf cstring;
    ByteBuf data;
    RelocArr text_relocs;
    RelocArr data_relocs;
    SymArr syms;
} AsmModel;

static void asm_model_init(AsmModel *m) {
    bb_init(&m->text);
    bb_init(&m->cstring);
    bb_init(&m->data);
    m->text_relocs.data = NULL; m->text_relocs.count = m->text_relocs.cap = 0;
    m->data_relocs.data = NULL; m->data_relocs.count = m->data_relocs.cap = 0;
    m->syms.data = NULL; m->syms.count = m->syms.cap = 0;
}

static void asm_model_free(AsmModel *m) {
    bb_free(&m->text);
    bb_free(&m->cstring);
    bb_free(&m->data);
    free(m->text_relocs.data);
    free(m->data_relocs.data);
    free(m->syms.data);
}

/* =========================================================================
 * Assembly parser and register encoders
 * ========================================================================= */

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

typedef enum {
    REG_X,
    REG_W,
    REG_D,
    REG_S,
    REG_SP,
    REG_NONE
} RegType;

typedef struct {
    RegType type;
    int num;
} Reg;

static Reg parse_reg(const char **p_str) {
    const char *p = *p_str;
    Reg r = {REG_NONE, 0};
    p = skip_ws(p);
    if (strncmp(p, "sp", 2) == 0 && !isalnum(p[2])) {
        r.type = REG_SP;
        r.num = 31;
        p += 2;
    } else if (strncmp(p, "xzr", 3) == 0 && !isalnum(p[3])) {
        r.type = REG_X;
        r.num = 31;
        p += 3;
    } else if (strncmp(p, "wzr", 3) == 0 && !isalnum(p[3])) {
        r.type = REG_W;
        r.num = 31;
        p += 3;
    } else if ((*p == 'x' || *p == 'w' || *p == 'd' || *p == 's') && isdigit(p[1])) {
        char ch = *p;
        p++;
        int val = 0;
        while (isdigit(*p)) {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (ch == 'x') r.type = REG_X;
        else if (ch == 'w') r.type = REG_W;
        else if (ch == 'd') r.type = REG_D;
        else r.type = REG_S;
        r.num = val;
    }
    *p_str = p;
    return r;
}

static int64_t parse_imm(const char **p_str) {
    const char *p = *p_str;
    p = skip_ws(p);
    if (*p == '#') p++;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    int64_t val = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        val = strtoll(p, (char**)&p, 16);
    } else {
        val = strtoll(p, (char**)&p, 10);
    }
    *p_str = p;
    return sign * val;
}

typedef struct {
    Reg base;
    Reg offset_reg;
    int64_t offset_imm;
    char offset_sym[256];
    uint32_t offset_reloc_type;
    int is_pre_index;
    int is_post_index;
    int shift;
} MemOperand;

static void parse_symbol_ref(const char **p_str, char *buf, int maxlen);

static MemOperand parse_mem(const char **p_str) {
    const char *p = *p_str;
    MemOperand m = {{REG_NONE, 0}, {REG_NONE, 0}, 0, "", 0, 0, 0, 0};
    p = skip_ws(p);
    if (*p != '[') return m;
    p++; // skip '['
    
    m.base = parse_reg(&p);
    p = skip_ws(p);
    if (*p == ']') {
        p++;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            p = skip_ws(p);
            if (*p == '#') {
                m.offset_imm = parse_imm(&p);
                m.is_post_index = 1;
            }
        }
        *p_str = p;
        return m;
    }
    
    if (*p == ',') {
        p++;
        p = skip_ws(p);
        if (*p == '#') {
            m.offset_imm = parse_imm(&p);
            p = skip_ws(p);
            if (*p == ']') {
                p++;
                p = skip_ws(p);
                if (*p == '!') {
                    p++;
                    m.is_pre_index = 1;
                }
            }
        } else if (*p == '_' || *p == '.') {
            parse_symbol_ref(&p, m.offset_sym, sizeof(m.offset_sym));
            if (strncmp(p, "@GOTPAGEOFF", 11) == 0) {
                p += 11;
                m.offset_reloc_type = ARM64_RELOC_GOT_LOAD_PAGEOFF12;
            } else if (strncmp(p, "@PAGEOFF", 8) == 0) {
                p += 8;
                m.offset_reloc_type = ARM64_RELOC_PAGEOFF12;
            }
            p = skip_ws(p);
            if (*p == ']') {
                p++;
            }
        } else {
            m.offset_reg = parse_reg(&p);
            p = skip_ws(p);
            if (*p == ',') {
                p++;
                p = skip_ws(p);
                if (strncmp(p, "lsl", 3) == 0) {
                    p += 3;
                    p = skip_ws(p);
                    if (*p == '#') {
                        m.shift = (int)parse_imm(&p);
                    }
                }
            }
            p = skip_ws(p);
            if (*p == ']') {
                p++;
            }
        }
    }
    *p_str = p;
    return m;
}

static void parse_symbol_ref(const char **p_str, char *buf, int maxlen) {
    const char *p = *p_str;
    p = skip_ws(p);
    int len = 0;
    while (*p && *p != ',' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '@' && len < maxlen - 1) {
        buf[len++] = *p++;
    }
    buf[len] = 0;
    *p_str = p;
}

/* =========================================================================
 * Line Encoder for ARM64 Darwin
 * ========================================================================= */

static int arm64_encode_line(const char *line, ByteBuf *text, EncCtx *ctx,
                             size_t fn_start, RelocArr *relocs) {
    const char *p = skip_ws(line);
    if (!*p || *p == '\n' || *p == '#') return 0;
    if (*p == '.') return 0; // directives skipped, handled in main loop

    // Check for label definition
    {
        const char *q = p;
        char lname[256]; int ln = 0;
        while (*q && *q != ':' && *q != ' ' && *q != '\n' && ln < 255)
            lname[ln++] = *q++;
        lname[ln] = 0;
        if (*q == ':') {
            ctx_add_label(ctx, lname, (int32_t)(text->size - fn_start));
            // Patch forward jumps
            for (int i = 0; i < ctx->patch_count; i++) {
                if (strcmp(ctx->patches[i].label, lname) == 0) {
                    size_t off = ctx->patches[i].patch_off;
                    int32_t target = (int32_t)(text->size - fn_start);
                    int32_t disp = target - (int32_t)(off - fn_start);
                    int32_t disp_words = disp / 4;
                    
                    uint32_t inst;
                    memcpy(&inst, &text->data[off], 4);
                    // Check if it is conditional branch b.cond (opcode starts with 0x54)
                    if ((inst & 0xff000000) == 0x54000000) {
                        inst &= 0xff00001f;
                        inst |= ((disp_words & 0x7ffff) << 5);
                    } else {
                        // Unconditional branch b
                        inst &= 0xfc000000;
                        inst |= (disp_words & 0x3ffffff);
                    }
                    bb_patch32le(text, off, inst);
                }
            }
            return 0;
        }
    }

    char mnem[32]; int mn = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && mn < 31)
        mnem[mn++] = *p++;
    mnem[mn] = 0;
    p = skip_ws(p);

    if (strcmp(mnem, "stp") == 0) {
        // Only ever used for: stp x29, x30, [sp, #-16]!
        bb_write32le(text, 0xa9bf7bfd);
        return 1;
    }
    if (strcmp(mnem, "ldp") == 0) {
        // Only ever used for: ldp x29, x30, [sp], #16
        bb_write32le(text, 0xa8c17bfd);
        return 1;
    }
    if (strcmp(mnem, "ret") == 0) {
        bb_write32le(text, 0xd65f03c0);
        return 1;
    }
    if (strcmp(mnem, "blr") == 0) {
        bb_write32le(text, 0xd63f0200);
        return 1;
    }
    if (strcmp(mnem, "fneg") == 0) {
        bb_write32le(text, 0x1e614000);
        return 1;
    }

    if (strcmp(mnem, "neg") == 0) {
        bb_write32le(text, 0xcb0003e0);
        return 1;
    }

    if (strcmp(mnem, "mov") == 0 || strcmp(mnem, "movz") == 0 || strcmp(mnem, "movk") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        int sf = (rd.type == REG_X || rd.type == REG_SP) ? 1 : 0;
        
        if (*p == '#') {
            int64_t imm = parse_imm(&p);
            int hw = 0;
            p = skip_ws(p);
            if (*p == ',') {
                p++;
                p = skip_ws(p);
                if (strncmp(p, "lsl", 3) == 0) {
                    p += 3;
                    p = skip_ws(p);
                    if (*p == '#') {
                        hw = (int)(parse_imm(&p) / 16);
                    }
                }
            }
            int opc = (strcmp(mnem, "movk") == 0) ? 3 : 2;
            uint32_t val = (sf << 31) | (opc << 29) | (0x25 << 23) | (hw << 21) | ((imm & 0xffff) << 5) | rd.num;
            bb_write32le(text, val);
        } else {
            Reg rs = parse_reg(&p);
            if (rs.type == REG_SP || rd.type == REG_SP) {
                // mov x29, sp -> add x29, sp, #0
                uint32_t val = (sf << 31) | 0x11000000 | (rs.num << 5) | rd.num;
                bb_write32le(text, val);
            } else {
                // mov x0, x1 -> orr x0, xzr, x1
                uint32_t val = (sf << 31) | 0x2a000000 | (31 << 5) | (rs.num << 16) | rd.num;
                bb_write32le(text, val);
            }
        }
        return 1;
    }

    if (strcmp(mnem, "add") == 0 || strcmp(mnem, "sub") == 0) {
        int op = (strcmp(mnem, "sub") == 0) ? 1 : 0;
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        int sf = (rd.type == REG_X || rd.type == REG_SP) ? 1 : 0;
        
        if (*p == '#') {
            int64_t imm = parse_imm(&p);
            uint32_t val = (sf << 31) | (op << 30) | 0x11000000 | ((imm & 0xfff) << 10) | (rn.num << 5) | rd.num;
            bb_write32le(text, val);
        } else {
            const char *saved_p = p;
            Reg rm = parse_reg(&p);
            if (rm.type != REG_NONE) {
                int shift_amount = 0;
                p = skip_ws(p);
                if (*p == ',') {
                    p++;
                    p = skip_ws(p);
                    if (strncmp(p, "lsl", 3) == 0) {
                        p += 3;
                        p = skip_ws(p);
                        if (*p == '#') {
                            shift_amount = (int)parse_imm(&p);
                        }
                    }
                }
                uint32_t val = (sf << 31) | (op << 30) | (0x58 << 21) | (rm.num << 16) | (shift_amount << 10) | (rn.num << 5) | rd.num;
                bb_write32le(text, val);
            } else {
                p = saved_p;
                char sym[256];
                parse_symbol_ref(&p, sym, sizeof(sym));
                uint32_t reloc_type = ARM64_RELOC_PAGEOFF12;
                if (strncmp(p, "@GOTPAGEOFF", 11) == 0) {
                    p += 11;
                    reloc_type = ARM64_RELOC_GOT_LOAD_PAGEOFF12;
                } else if (strncmp(p, "@PAGEOFF", 8) == 0) {
                    p += 8;
                }
                MachoReloc rel = {0};
                strncpy(rel.sym_name, sym, sizeof(rel.sym_name) - 1);
                rel.offset = text->size;
                rel.type = reloc_type;
                relocarr_push(relocs, &rel);
                
                uint32_t val = (sf << 31) | (op << 30) | 0x11000000 | (rn.num << 5) | rd.num;
                bb_write32le(text, val);
            }
        }
        return 1;
    }

    if (strcmp(mnem, "cmp") == 0) {
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        int sf = (rn.type == REG_X || rn.type == REG_SP) ? 1 : 0;
        
        if (*p == '#') {
            int64_t imm = parse_imm(&p);
            // cmp x1, #0 -> subs xzr, x1, #0
            uint32_t val = (sf << 31) | (1 << 30) | (1 << 29) | 0x11000000 | ((imm & 0xfff) << 10) | (rn.num << 5) | 31;
            bb_write32le(text, val);
        } else {
            Reg rm = parse_reg(&p);
            // cmp x1, x0 -> subs xzr, x1, x0
            uint32_t val = (sf << 31) | (1 << 30) | (1 << 29) | (0x58 << 21) | (rm.num << 16) | (rn.num << 5) | 31;
            bb_write32le(text, val);
        }
        return 1;
    }

    if (strcmp(mnem, "adrp") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        char sym[256];
        parse_symbol_ref(&p, sym, sizeof(sym));
        
        uint32_t reloc_type = ARM64_RELOC_PAGE21;
        if (strncmp(p, "@GOTPAGE", 8) == 0) {
            p += 8;
            reloc_type = ARM64_RELOC_GOT_LOAD_PAGE21;
        } else if (strncmp(p, "@PAGE", 5) == 0) {
            p += 5;
        }
        
        MachoReloc rel = {0};
        strncpy(rel.sym_name, sym, sizeof(rel.sym_name) - 1);
        rel.offset = text->size;
        rel.type = reloc_type;
        relocarr_push(relocs, &rel);
        
        uint32_t val = 0x90000000 | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "bl") == 0) {
        char sym[256];
        parse_symbol_ref(&p, sym, sizeof(sym));
        
        MachoReloc rel = {0};
        strncpy(rel.sym_name, sym, sizeof(rel.sym_name) - 1);
        rel.offset = text->size;
        rel.type = ARM64_RELOC_BRANCH26;
        relocarr_push(relocs, &rel);
        
        bb_write32le(text, 0x94000000);
        return 1;
    }

    if (strcmp(mnem, "b") == 0 || strncmp(mnem, "b.", 2) == 0) {
        char label[256];
        parse_symbol_ref(&p, label, sizeof(label));
        
        int32_t target = ctx_get_label(ctx, label);
        int32_t disp_words = 0;
        if (target != -1) {
            int32_t disp = target - (int32_t)(text->size - fn_start);
            disp_words = disp / 4;
        } else {
            ctx_add_patch(ctx, label, text->size);
        }
        
        if (strcmp(mnem, "b") == 0) {
            uint32_t val = 0x14000000 | (disp_words & 0x3ffffff);
            bb_write32le(text, val);
        } else {
            // b.cond
            const char *cond_str = mnem + 2;
            int cond = 0;
            if (strcmp(cond_str, "eq") == 0) cond = 0;
            else if (strcmp(cond_str, "ne") == 0) cond = 1;
            else if (strcmp(cond_str, "hs") == 0) cond = 2;
            else if (strcmp(cond_str, "lo") == 0) cond = 3;
            else if (strcmp(cond_str, "ge") == 0) cond = 10;
            else if (strcmp(cond_str, "lt") == 0) cond = 11;
            else if (strcmp(cond_str, "gt") == 0) cond = 12;
            else if (strcmp(cond_str, "le") == 0) cond = 13;
            
            uint32_t val = 0x54000000 | ((disp_words & 0x7ffff) << 5) | cond;
            bb_write32le(text, val);
        }
        return 1;
    }

    if (strcmp(mnem, "cset") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        char cond_str[16];
        int ci = 0;
        while (isalpha(*p) && ci < 15) cond_str[ci++] = *p++;
        cond_str[ci] = 0;
        
        int cond_inverted = 0;
        if (strcmp(cond_str, "eq") == 0) cond_inverted = 1;
        else if (strcmp(cond_str, "ne") == 0) cond_inverted = 0;
        else if (strcmp(cond_str, "lt") == 0) cond_inverted = 10;
        else if (strcmp(cond_str, "ge") == 0) cond_inverted = 11;
        else if (strcmp(cond_str, "le") == 0) cond_inverted = 12; // wait, opposite of le (13) is gt (12)
        else if (strcmp(cond_str, "gt") == 0) cond_inverted = 13; // opposite of gt (12) is le (13)
        else if (strcmp(cond_str, "lo") == 0) cond_inverted = 2;  // opposite of lo (3) is hs (2)
        else if (strcmp(cond_str, "hs") == 0) cond_inverted = 3;  // opposite of hs (2) is lo (3)
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1a9f07e0 | (cond_inverted << 12) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "csel") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        char cond_str[16];
        int ci = 0;
        while (isalpha(*p) && ci < 15) cond_str[ci++] = *p++;
        cond_str[ci] = 0;
        
        int cond = 0;
        if (strcmp(cond_str, "eq") == 0) cond = 0;
        else if (strcmp(cond_str, "ne") == 0) cond = 1;
        else if (strcmp(cond_str, "hs") == 0) cond = 2;
        else if (strcmp(cond_str, "lo") == 0) cond = 3;
        else if (strcmp(cond_str, "ge") == 0) cond = 10;
        else if (strcmp(cond_str, "lt") == 0) cond = 11;
        else if (strcmp(cond_str, "gt") == 0) cond = 12;
        else if (strcmp(cond_str, "le") == 0) cond = 13;
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1a800000 | (rm.num << 16) | (cond << 12) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "and") == 0 || strcmp(mnem, "orr") == 0 || strcmp(mnem, "eor") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t opc = (strcmp(mnem, "and") == 0) ? 0 : ((strcmp(mnem, "orr") == 0) ? 1 : 2);
        uint32_t val = (sf << 31) | (opc << 29) | 0x0a000000 | (rm.num << 16) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "mvn") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        // mvn rd, rm -> orn rd, xzr, rm
        uint32_t val = (sf << 31) | 0x2a2003e0 | (rm.num << 16) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "lsl") == 0 || strcmp(mnem, "lsr") == 0 || strcmp(mnem, "asr") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        int opc = (strcmp(mnem, "lsl") == 0) ? 0 : ((strcmp(mnem, "lsr") == 0) ? 1 : 2);
        uint32_t val = (sf << 31) | 0x1ac02000 | (opc << 10) | (rm.num << 16) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "mul") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1b007c00 | (rm.num << 16) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "sdiv") == 0 || strcmp(mnem, "udiv") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t val = (sf << 31) | (strcmp(mnem, "udiv") == 0 ? 0x1ac00800 : 0x1ac00c00) |
                       (rm.num << 16) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "msub") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        if (*p == ',') p++;
        Reg ra = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1b008000 | (rm.num << 16) | (ra.num << 10) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "sxtb") == 0 || strcmp(mnem, "sxth") == 0 || strcmp(mnem, "sxtw") == 0 ||
        strcmp(mnem, "uxtb") == 0 || strcmp(mnem, "uxth") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        int is_signed = (mnem[0] == 's');
        int width_bits = (strcmp(mnem + 3, "b") == 0) ? 8 : ((strcmp(mnem + 3, "h") == 0) ? 16 : 32);
        
        int opc = is_signed ? 0 : 2;
        int N = sf;
        int immr = 0;
        int imms = width_bits - 1;
        uint32_t val = (sf << 31) | (opc << 29) | (0x26 << 23) | (N << 22) | (immr << 16) | (imms << 10) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "ubfx") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        int64_t offset = parse_imm(&p);
        if (*p == ',') p++;
        int64_t width = parse_imm(&p);
        
        uint32_t val = 0xd3400000 | (offset << 16) | ((offset + width - 1) << 10) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "bfi") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        int64_t offset = parse_imm(&p);
        if (*p == ',') p++;
        int64_t width = parse_imm(&p);
        
        uint32_t val = 0xb3000000 | (((64 - offset) % 64) << 16) | ((width - 1) << 10) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "scvtf") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        
        int sf = (rn.type == REG_X) ? 1 : 0;
        int type = (rd.type == REG_D) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1e220000 | (type << 22) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "fcvtzs") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        
        int sf = (rd.type == REG_X) ? 1 : 0;
        int type = (rn.type == REG_D) ? 1 : 0;
        uint32_t val = (sf << 31) | 0x1e380000 | (type << 22) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "fcvt") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        
        uint32_t val = 0;
        if (rd.type == REG_D && rn.type == REG_S) {
            val = 0x1e22c000 | (rn.num << 5) | rd.num;
        } else {
            val = 0x1e624000 | (rn.num << 5) | rd.num;
        }
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "fadd") == 0 || strcmp(mnem, "fsub") == 0 || strcmp(mnem, "fmul") == 0 || strcmp(mnem, "fdiv") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        
        int type = (rd.type == REG_D) ? 1 : 0;
        int opc = (strcmp(mnem, "fmul") == 0) ? 0 : ((strcmp(mnem, "fdiv") == 0) ? 1 : ((strcmp(mnem, "fadd") == 0) ? 2 : 3));
        uint32_t val = 0x1e200800 | (type << 22) | (opc << 12) | (rm.num << 16) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "fcmp") == 0) {
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        int type = (rn.type == REG_D) ? 1 : 0;
        if (*p == '#') {
            // fcmp d0, #0.0
            uint32_t val = 0x1e202008 | (type << 22) | (rn.num << 5);
            bb_write32le(text, val);
        } else {
            Reg rm = parse_reg(&p);
            uint32_t val = 0x1e202000 | (type << 22) | (rm.num << 16) | (rn.num << 5);
            bb_write32le(text, val);
        }
        return 1;
    }

    if (strcmp(mnem, "fcsel") == 0) {
        Reg rd = parse_reg(&p);
        if (*p == ',') p++;
        Reg rn = parse_reg(&p);
        if (*p == ',') p++;
        Reg rm = parse_reg(&p);
        if (*p == ',') p++;
        p = skip_ws(p);
        
        char cond_str[16];
        int ci = 0;
        while (isalpha(*p) && ci < 15) cond_str[ci++] = *p++;
        cond_str[ci] = 0;
        
        int cond = 0;
        if (strcmp(cond_str, "eq") == 0) cond = 0;
        else if (strcmp(cond_str, "ne") == 0) cond = 1;
        else if (strcmp(cond_str, "hs") == 0) cond = 2;
        else if (strcmp(cond_str, "lo") == 0) cond = 3;
        else if (strcmp(cond_str, "ge") == 0) cond = 10;
        else if (strcmp(cond_str, "lt") == 0) cond = 11;
        else if (strcmp(cond_str, "gt") == 0) cond = 12;
        else if (strcmp(cond_str, "le") == 0) cond = 13;
        
        int type = (rd.type == REG_D) ? 1 : 0;
        uint32_t val = 0x1e200c00 | (type << 22) | (rm.num << 16) | (cond << 12) | (rn.num << 5) | rd.num;
        bb_write32le(text, val);
        return 1;
    }

    if (strcmp(mnem, "ldr") == 0 || strcmp(mnem, "str") == 0 ||
        strcmp(mnem, "ldur") == 0 || strcmp(mnem, "stur") == 0 ||
        strcmp(mnem, "ldrb") == 0 || strcmp(mnem, "strb") == 0 ||
        strcmp(mnem, "ldrh") == 0 || strcmp(mnem, "strh") == 0 ||
        strcmp(mnem, "ldrsb") == 0 || strcmp(mnem, "ldrsh") == 0 || strcmp(mnem, "ldrsw") == 0) {
        int is_load = (mnem[0] == 'l');
        int is_unscaled = (strcmp(mnem, "ldur") == 0 || strcmp(mnem, "stur") == 0);
        Reg rt = parse_reg(&p);
        if (*p == ',') p++;
        MemOperand mem = parse_mem(&p);
        
        int is_fp = (rt.type == REG_D || rt.type == REG_S);
        int scale = (rt.type == REG_D || rt.type == REG_X || rt.type == REG_SP) ? 3 :
                    ((rt.type == REG_S || rt.type == REG_W) ? 2 : 0);
        if (strcmp(mnem, "ldrb") == 0 || strcmp(mnem, "strb") == 0 || strcmp(mnem, "ldrsb") == 0) {
            scale = 0;
        } else if (strcmp(mnem, "ldrh") == 0 || strcmp(mnem, "strh") == 0 || strcmp(mnem, "ldrsh") == 0) {
            scale = 1;
        } else if (strcmp(mnem, "ldrsw") == 0) {
            scale = 2;
        }
        
        if (mem.offset_sym[0]) {
            MachoReloc rel = {0};
            strncpy(rel.sym_name, mem.offset_sym, sizeof(rel.sym_name) - 1);
            rel.offset = text->size;
            rel.type = mem.offset_reloc_type;
            relocarr_push(relocs, &rel);
        }

        if (is_unscaled) {
            int opc = is_load ? 1 : 0;
            int32_t imm9 = (int32_t)mem.offset_imm;
            uint32_t op_prefix = is_fp ? 0x3c000000 : 0x38000000;
            uint32_t val = (scale << 30) | op_prefix | (opc << 22) | ((imm9 & 0x1ff) << 12) | (mem.base.num << 5) | rt.num;
            bb_write32le(text, val);
        } else if (mem.is_pre_index) {
            // str x0, [sp, #-16]!
            int opc = is_load ? 1 : 0;
            int32_t imm9 = (int32_t)mem.offset_imm;
            uint32_t op_prefix = is_fp ? 0x3c000c00 : 0x38000c00;
            uint32_t val = (scale << 30) | op_prefix | (opc << 22) | ((imm9 & 0x1ff) << 12) | (mem.base.num << 5) | rt.num;
            bb_write32le(text, val);
        } else if (mem.is_post_index) {
            // ldr x1, [sp], #16
            int opc = is_load ? 1 : 0;
            int32_t imm9 = (int32_t)mem.offset_imm;
            uint32_t op_prefix = is_fp ? 0x3c000400 : 0x38000400;
            uint32_t val = (scale << 30) | op_prefix | (opc << 22) | ((imm9 & 0x1ff) << 12) | (mem.base.num << 5) | rt.num;
            bb_write32le(text, val);
        } else if (mem.offset_reg.type != REG_NONE) {
            // Register offset: ldr x0, [x1, x2]
            int opc = is_load ? 1 : 0;
            if (strcmp(mnem, "ldrsb") == 0) {
                opc = (rt.type == REG_X) ? 2 : 3;
            } else if (strcmp(mnem, "ldrsh") == 0) {
                opc = (rt.type == REG_X) ? 2 : 3;
            } else if (strcmp(mnem, "ldrsw") == 0) {
                opc = 2;
            }
            int option = 3; // LSL
            int S = (mem.shift > 0) ? 1 : 0;
            uint32_t op_prefix = is_fp ? 0x3c200800 : 0x38200800;
            uint32_t val = (scale << 30) | op_prefix | (opc << 22) | (mem.offset_reg.num << 16) | (option << 13) | (S << 12) | (mem.base.num << 5) | rt.num;
            bb_write32le(text, val);
        } else {
            // Unsigned immediate offset
            int opc = is_load ? 1 : 0;
            if (strcmp(mnem, "ldrsb") == 0) {
                opc = (rt.type == REG_X) ? 2 : 3;
            } else if (strcmp(mnem, "ldrsh") == 0) {
                opc = (rt.type == REG_X) ? 2 : 3;
            } else if (strcmp(mnem, "ldrsw") == 0) {
                opc = 2;
            }
            int64_t val_imm = mem.offset_imm / (1 << scale);
            uint32_t op_prefix = is_fp ? 0x3d000000 : 0x39000000;
            uint32_t val = (scale << 30) | op_prefix | (opc << 22) | ((val_imm & 0xfff) << 10) | (mem.base.num << 5) | rt.num;
            bb_write32le(text, val);
        }
        return 1;
    }

    // Unrecognized instruction
    char msg[512];
    snprintf(msg, sizeof(msg), "macho_writer: unsupported instruction '%s'", mnem);
    diagnostics_fatal(msg);
    return 0;
}

/* =========================================================================
 * Assembly parser main function
 * ========================================================================= */

static void parse_asm_arm64(const char *asm_text, AsmModel *m) {
    SectionId cur_sec = SEC_NONE;
    const char *p = asm_text;
    EncCtx ctx; ctx_init(&ctx);
    size_t fn_start = 0;
    int in_function = 0;
    char pending_globl[256] = "";

    while (*p) {
        const char *line_start = p;
        const char *line_end   = p;
        while (*line_end && *line_end != '\n') line_end++;

        char line[1024];
        int linelen = (int)(line_end - line_start);
        if (linelen > 1023) linelen = 1023;
        memcpy(line, line_start, linelen);
        line[linelen] = 0;
        p = (*line_end == '\n') ? line_end + 1 : line_end;

        const char *lp = skip_ws(line);
        if (!*lp || *lp == '#') continue;

        if (strcmp(lp, ".text") == 0) { cur_sec = SEC_TEXT; continue; }
        if (strcmp(lp, ".data") == 0) { cur_sec = SEC_DATA; continue; }
        if (strcmp(lp, ".cstring") == 0) { cur_sec = SEC_CSTRING; continue; }

        if (strncmp(lp, ".globl ", 7) == 0 || strncmp(lp, ".global ", 8) == 0) {
            const char *s = lp + (lp[6] == ' ' ? 7 : 8);
            s = skip_ws(s);
            strncpy(pending_globl, s, 255);
            continue;
        }

        if (strncmp(lp, ".p2align ", 9) == 0) {
            const char *s = skip_ws(lp + 9);
            int align = (int)strtol(s, NULL, 10);
            ByteBuf *sec_buf = (cur_sec == SEC_TEXT) ? &m->text :
                               ((cur_sec == SEC_DATA) ? &m->data : &m->cstring);
            bb_align(sec_buf, 1 << align);
            continue;
        }

        if (*lp == '.') {
            if (strncmp(lp, ".file ", 6) == 0 || strncmp(lp, ".loc ", 5) == 0) {
                // skip debug directives for Mach-O stub
                continue;
            }
        }

        if (*lp == '.' || isalpha(*lp) || *lp == '_') {
            const char *q = lp;
            char lname[256]; int ln = 0;
            while (*q && *q != ':' && *q != ' ' && *q != '\t' && ln < 255) lname[ln++] = *q++;
            lname[ln] = 0;
            if (*q == ':') {
                if (cur_sec == SEC_TEXT) {
                    if (lname[0] != '.' && lname[0] != 'L') {
                        // Function start
                        if (in_function) {
                            ctx_free(&ctx);
                            ctx_init(&ctx);
                        }
                        in_function = 1;
                        fn_start = m->text.size;
                        
                        MachoSym sym = {0};
                        strncpy(sym.name, lname, 255);
                        sym.sect = 1; // __text
                        sym.value = (uint64_t)fn_start;
                        sym.defined = 1;
                        sym.is_global = (strcmp(lname, pending_globl) == 0);
                        symarr_push(&m->syms, &sym);
                    } else {
                        // Local label in text
                        if (in_function) {
                            ctx_add_label(&ctx, lname, (int32_t)(m->text.size - fn_start));
                            // Patch forward jumps
                            for (int i = 0; i < ctx.patch_count; i++) {
                                if (strcmp(ctx.patches[i].label, lname) == 0) {
                                    size_t off = ctx.patches[i].patch_off;
                                    int32_t target = (int32_t)(m->text.size - fn_start);
                                    int32_t disp = target - (int32_t)(off - fn_start);
                                    int32_t disp_words = disp / 4;
                                    
                                    uint32_t inst;
                                    memcpy(&inst, &m->text.data[off], 4);
                                    if ((inst & 0xff000000) == 0x54000000) {
                                        inst &= 0xff00001f;
                                        inst |= ((disp_words & 0x7ffff) << 5);
                                    } else {
                                        inst &= 0xfc000000;
                                        inst |= (disp_words & 0x3ffffff);
                                    }
                                    bb_patch32le(&m->text, off, inst);
                                }
                            }
                        }
                    }
                } else if (cur_sec == SEC_DATA || cur_sec == SEC_CSTRING) {
                    ByteBuf *sec_buf = (cur_sec == SEC_DATA) ? &m->data : &m->cstring;
                    MachoSym sym = {0};
                    strncpy(sym.name, lname, 255);
                    sym.sect = (cur_sec == SEC_DATA) ? 3 : 2;
                    sym.value = (uint64_t)sec_buf->size;
                    sym.defined = 1;
                    sym.is_global = (strcmp(lname, pending_globl) == 0);
                    symarr_push(&m->syms, &sym);
                }
                continue;
            }
        }

        // Directives inside sections
        if (*lp == '.') {
            ByteBuf *sec_buf = (cur_sec == SEC_DATA) ? &m->data :
                               ((cur_sec == SEC_CSTRING) ? &m->cstring : NULL);
            if (sec_buf) {
                if (strncmp(lp, ".byte ", 6) == 0) {
                    int64_t v = parse_int(skip_ws(lp + 6));
                    bb_write8(sec_buf, (uint8_t)(v & 0xFF));
                } else if (strncmp(lp, ".short ", 7) == 0) {
                    int64_t v = parse_int(skip_ws(lp + 7));
                    bb_write16le(sec_buf, (uint16_t)(v & 0xFFFF));
                } else if (strncmp(lp, ".long ", 6) == 0) {
                    int64_t v = parse_int(skip_ws(lp + 6));
                    bb_write32le(sec_buf, (uint32_t)v);
                } else if (strncmp(lp, ".quad ", 6) == 0) {
                    const char *val_str = skip_ws(lp + 6);
                    if (*val_str && (val_str[0] == '_' || val_str[0] == '.' || (val_str[0] >= 'a' && val_str[0] <= 'z') || (val_str[0] >= 'A' && val_str[0] <= 'Z'))) {
                        char sym_name[256];
                        int s_len = 0;
                        while (val_str[s_len] && val_str[s_len] != ' ' && val_str[s_len] != '\t' && val_str[s_len] != '\n' && s_len < 255) {
                            sym_name[s_len] = val_str[s_len];
                            s_len++;
                        }
                        sym_name[s_len] = '\0';
                        
                        MachoReloc r = {0};
                        strncpy(r.sym_name, sym_name, 255);
                        r.offset = sec_buf->size;
                        r.type = ARM64_RELOC_UNSIGNED;
                        relocarr_push(&m->data_relocs, &r);
                        
                        bb_write64le(sec_buf, 0);
                    } else {
                        int64_t v = parse_int(val_str);
                        bb_write64le(sec_buf, (uint64_t)v);
                    }
                } else if (strncmp(lp, ".zero ", 6) == 0) {
                    int64_t n = parse_int(skip_ws(lp + 6));
                    for (int64_t i = 0; i < n; i++) bb_write8(sec_buf, 0);
                } else if (strncmp(lp, ".asciz ", 7) == 0) {
                    const char *s = skip_ws(lp + 7);
                    if (*s == '"') s++;
                    while (*s && *s != '"') {
                        if (*s == '\\' && *(s+1)) {
                            s++;
                            switch (*s) {
                            case 'n':  bb_write8(sec_buf, '\n'); break;
                            case 't':  bb_write8(sec_buf, '\t'); break;
                            case 'r':  bb_write8(sec_buf, '\r'); break;
                            case '0':  bb_write8(sec_buf, '\0'); break;
                            case '\\': bb_write8(sec_buf, '\\'); break;
                            case '"':  bb_write8(sec_buf, '"'); break;
                            }
                        } else {
                            bb_write8(sec_buf, (uint8_t)*s);
                        }
                        s++;
                    }
                    bb_write8(sec_buf, 0); // nul terminator
                }
            }
            continue;
        }

        // Instruction line
        if (cur_sec == SEC_TEXT && in_function) {
            arm64_encode_line(line, &m->text, &ctx, fn_start, &m->text_relocs);
        }
    }
    
    if (in_function) {
        ctx_free(&ctx);
    }
}

/* =========================================================================
 * Public API & Mach-O Builder
 * ========================================================================= */

MachObject macho_write_object(const char *asm_text, const char *src_path, Arena *arena) {
    (void)src_path;
    AsmModel m;
    asm_model_init(&m);
    parse_asm_arm64(asm_text, &m);

    // Build string table & symbol tables classified by type
    Strtab strtab;
    strtab_init(&strtab);

    // Sort/Classify symbols
    // SymArr has locals, globals, and undefineds.
    // In Mach-O, relocations referencing undefined symbols reference them by symbol table index.
    // So we must add any symbol referenced by text relocations that is not yet in our symbol array!
    for (int i = 0; i < m.text_relocs.count; i++) {
        const char *rname = m.text_relocs.data[i].sym_name;
        bool found = false;
        for (int j = 0; j < m.syms.count; j++) {
            if (strcmp(m.syms.data[j].name, rname) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            MachoSym sym = {0};
            strncpy(sym.name, rname, 255);
            sym.sect = 0;
            sym.defined = 0;
            sym.is_global = 1;
            symarr_push(&m.syms, &sym);
        }
    }
    for (int i = 0; i < m.data_relocs.count; i++) {
        const char *rname = m.data_relocs.data[i].sym_name;
        bool found = false;
        for (int j = 0; j < m.syms.count; j++) {
            if (strcmp(m.syms.data[j].name, rname) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            MachoSym sym = {0};
            strncpy(sym.name, rname, 255);
            sym.sect = 0;
            sym.defined = 0;
            sym.is_global = 1;
            symarr_push(&m.syms, &sym);
        }
    }

    // Separate symbols into local, global defined, and undefined
    SymArr locals = {NULL, 0, 0};
    SymArr globals = {NULL, 0, 0};
    SymArr undefs = {NULL, 0, 0};
    
    for (int i = 0; i < m.syms.count; i++) {
        MachoSym *s = &m.syms.data[i];
        if (!s->is_global && s->defined) {
            symarr_push(&locals, s);
        } else if (s->is_global && s->defined) {
            symarr_push(&globals, s);
        } else {
            symarr_push(&undefs, s);
        }
    }

    // Merge into final sorted array
    SymArr sorted_syms = {NULL, 0, 0};
    for (int i = 0; i < locals.count; i++) symarr_push(&sorted_syms, &locals.data[i]);
    for (int i = 0; i < globals.count; i++) symarr_push(&sorted_syms, &globals.data[i]);
    for (int i = 0; i < undefs.count; i++) symarr_push(&sorted_syms, &undefs.data[i]);

    // Populate name offsets in final symbols
    for (int i = 0; i < sorted_syms.count; i++) {
        sorted_syms.data[i].name_off = strtab_add(&strtab, sorted_syms.data[i].name);
    }

    // Align sections
    bb_align(&m.text, 8);
    bb_align(&m.cstring, 8);
    bb_align(&m.data, 8);

    // Compute layout offsets
    uint32_t header_size = sizeof(struct mach_header_64);
    uint32_t num_cmds = 4;
    uint32_t lc_segment_size = sizeof(struct segment_command_64) + 3 * sizeof(struct section_64);
    uint32_t lc_build_version_size = 24;
    uint32_t lc_symtab_size = sizeof(struct symtab_command);
    uint32_t lc_dysymtab_size = sizeof(struct dysymtab_command);
    uint32_t sizeofcmds = lc_segment_size + lc_build_version_size + lc_symtab_size + lc_dysymtab_size;
    
    uint32_t current_offset = header_size + sizeofcmds;
    
    uint32_t text_offset = current_offset;
    current_offset += m.text.size;
    
    uint32_t cstring_offset = current_offset;
    current_offset += m.cstring.size;
    
    uint32_t data_offset = current_offset;
    current_offset += m.data.size;
    
    uint32_t reloc_offset = current_offset;
    current_offset += m.text_relocs.count * 8; // 8 bytes per relocation_info
    
    uint32_t data_reloc_offset = current_offset;
    current_offset += m.data_relocs.count * 8; // 8 bytes per relocation_info
    
    uint32_t sym_offset = current_offset;
    current_offset += sorted_syms.count * sizeof(struct nlist_64);
    
    uint32_t str_offset = current_offset;
    current_offset += strtab.buf.size;

    // Build output buffer
    ByteBuf out;
    bb_init(&out);

    // Mach-O Header
    struct mach_header_64 hdr;
    hdr.magic = MH_MAGIC_64;
    hdr.cputype = CPU_TYPE_ARM64;
    hdr.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    hdr.filetype = MH_OBJECT;
    hdr.ncmds = num_cmds;
    hdr.sizeofcmds = sizeofcmds;
    hdr.flags = MH_SUBSECTIONS_VIA_SYMBOLS;
    hdr.reserved = 0;
    bb_writebytes(&out, (const uint8_t *)&hdr, sizeof(hdr));

    // Command 1: LC_SEGMENT_64
    struct segment_command_64 seg;
    seg.cmd = LC_SEGMENT_64;
    seg.cmdsize = lc_segment_size;
    memset(seg.segname, 0, 16); // Empty segment name for MH_OBJECT
    seg.vmaddr = 0;
    seg.vmsize = m.text.size + m.cstring.size + m.data.size;
    seg.fileoff = text_offset;
    seg.filesize = m.text.size + m.cstring.size + m.data.size;
    seg.maxprot = 7;
    seg.initprot = 7;
    seg.nsects = 3;
    seg.flags = 0;
    bb_writebytes(&out, (const uint8_t *)&seg, sizeof(seg));

    // Section 1: __text
    struct section_64 s_text;
    strncpy(s_text.sectname, "__text", 15);
    strncpy(s_text.segname, "__TEXT", 15);
    s_text.addr = 0;
    s_text.size = m.text.size;
    s_text.offset = text_offset;
    s_text.align = 3; // 2^3 = 8
    s_text.reloff = m.text_relocs.count > 0 ? reloc_offset : 0;
    s_text.nreloc = m.text_relocs.count;
    s_text.flags = 0x80000400; // S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_COSTRUCTIONS
    s_text.reserved1 = 0;
    s_text.reserved2 = 0;
    s_text.reserved3 = 0;
    bb_writebytes(&out, (const uint8_t *)&s_text, sizeof(s_text));

    // Section 2: __cstring
    struct section_64 s_cstr;
    strncpy(s_cstr.sectname, "__cstring", 15);
    strncpy(s_cstr.segname, "__TEXT", 15);
    s_cstr.addr = m.text.size;
    s_cstr.size = m.cstring.size;
    s_cstr.offset = cstring_offset;
    s_cstr.align = 0; // 2^0 = 1
    s_cstr.reloff = 0;
    s_cstr.nreloc = 0;
    s_cstr.flags = 0x00000002; // S_CSTRING_LITERALS
    s_cstr.reserved1 = 0;
    s_cstr.reserved2 = 0;
    s_cstr.reserved3 = 0;
    bb_writebytes(&out, (const uint8_t *)&s_cstr, sizeof(s_cstr));

    // Section 3: __data
    struct section_64 s_data;
    strncpy(s_data.sectname, "__data", 15);
    strncpy(s_data.segname, "__DATA", 15);
    s_data.addr = m.text.size + m.cstring.size;
    s_data.size = m.data.size;
    s_data.offset = data_offset;
    s_data.align = 3; // 2^3 = 8
    s_data.reloff = m.data_relocs.count > 0 ? data_reloc_offset : 0;
    s_data.nreloc = m.data_relocs.count;
    s_data.flags = 0x00000000; // S_REGULAR
    s_data.reserved1 = 0;
    s_data.reserved2 = 0;
    s_data.reserved3 = 0;
    bb_writebytes(&out, (const uint8_t *)&s_data, sizeof(s_data));

    // Command 1.5: LC_BUILD_VERSION
    struct build_version_command bvc;
    bvc.cmd = LC_BUILD_VERSION;
    bvc.cmdsize = 24;
    bvc.platform = 1; // macOS
    bvc.minos = 14 << 16; // 14.0.0
    bvc.sdk = 14 << 16;
    bvc.ntools = 0;
    bb_writebytes(&out, (const uint8_t *)&bvc, sizeof(bvc));

    // Command 2: LC_SYMTAB
    struct symtab_command symcmd;
    symcmd.cmd = LC_SYMTAB;
    symcmd.cmdsize = lc_symtab_size;
    symcmd.symoff = sym_offset;
    symcmd.nsyms = sorted_syms.count;
    symcmd.stroff = str_offset;
    symcmd.strsize = strtab.buf.size;
    bb_writebytes(&out, (const uint8_t *)&symcmd, sizeof(symcmd));

    // Command 3: LC_DYSYMTAB
    struct dysymtab_command dsym;
    memset(&dsym, 0, sizeof(dsym));
    dsym.cmd = LC_DYSYMTAB;
    dsym.cmdsize = lc_dysymtab_size;
    dsym.ilocalsym = 0;
    dsym.nlocalsym = locals.count;
    dsym.iextdefsym = locals.count;
    dsym.nextdefsym = globals.count;
    dsym.iundefsym = locals.count + globals.count;
    dsym.nundefsym = undefs.count;
    bb_writebytes(&out, (const uint8_t *)&dsym, sizeof(dsym));

    // Write sections data
    bb_writebytes(&out, m.text.data, m.text.size);
    bb_writebytes(&out, m.cstring.data, m.cstring.size);
    bb_writebytes(&out, m.data.data, m.data.size);

    // Write relocations
    for (int i = 0; i < m.text_relocs.count; i++) {
        MachoReloc *r = &m.text_relocs.data[i];
        
        // Find index of referenced symbol in sorted table
        int sym_idx = -1;
        for (int j = 0; j < sorted_syms.count; j++) {
            if (strcmp(sorted_syms.data[j].name, r->sym_name) == 0) {
                sym_idx = j;
                break;
            }
        }
        if (sym_idx == -1) {
            diagnostics_fatal("macho_writer: unresolved relocation symbol");
        }
        
        uint32_t pcrel = (r->type == ARM64_RELOC_BRANCH26 ||
                          r->type == ARM64_RELOC_PAGE21 ||
                          r->type == ARM64_RELOC_GOT_LOAD_PAGE21) ? 1 : 0;
        uint32_t length = 2; // always 4 bytes (long/32-bit offset target size)
        uint32_t r_extern = 1;
        
        uint32_t r_info = (sym_idx & 0xffffff) | (pcrel << 24) | (length << 25) | (r_extern << 27) | (r->type << 28);
        
        bb_write32le(&out, (uint32_t)r->offset);
        bb_write32le(&out, r_info);
    }

    // Write data relocations
    for (int i = 0; i < m.data_relocs.count; i++) {
        MachoReloc *r = &m.data_relocs.data[i];
        int sym_idx = -1;
        for (int j = 0; j < sorted_syms.count; j++) {
            if (strcmp(sorted_syms.data[j].name, r->sym_name) == 0) {
                sym_idx = j;
                break;
            }
        }
        if (sym_idx == -1) {
            diagnostics_fatal("macho_writer: unresolved data relocation symbol");
        }
        
        uint32_t pcrel = 0;
        uint32_t length = 3; // 8 bytes (2^3 = 8)
        uint32_t r_extern = 1;
        uint32_t r_info = (sym_idx & 0xffffff) | (pcrel << 24) | (length << 25) | (r_extern << 27) | (r->type << 28);
        
        bb_write32le(&out, (uint32_t)r->offset);
        bb_write32le(&out, r_info);
    }

    // Write symbol table
    for (int i = 0; i < sorted_syms.count; i++) {
        MachoSym *s = &sorted_syms.data[i];
        struct nlist_64 ns;
        ns.n_strx = s->name_off;
        ns.n_desc = 0;
        
        if (s->defined) {
            ns.n_type = N_SECT | (s->is_global ? N_EXT : 0);
            ns.n_sect = s->sect;
            ns.n_value = s->value;
            // Add segment offsets to values if needed (for Mach-O, section addresses are absolute)
            if (s->sect == 2) {
                ns.n_value += m.text.size;
            } else if (s->sect == 3) {
                ns.n_value += m.text.size + m.cstring.size;
            }
        } else {
            ns.n_type = N_UNDF | N_EXT;
            ns.n_sect = 0;
            ns.n_value = 0;
        }
        
        bb_write32le(&out, ns.n_strx);
        bb_write8(&out, ns.n_type);
        bb_write8(&out, ns.n_sect);
        bb_reserve(&out, 2);
        out.data[out.size++] = (uint8_t)ns.n_desc;
        out.data[out.size++] = (uint8_t)(ns.n_desc >> 8);
        bb_write64le(&out, ns.n_value);
    }

    // Write string table
    bb_writebytes(&out, strtab.buf.data, strtab.buf.size);

    // Copy to arena
    uint8_t *result = arena_alloc(arena, out.size);
    memcpy(result, out.data, out.size);
    size_t sz = out.size;

    // Cleanup
    bb_free(&out);
    strtab_free(&strtab);
    free(locals.data);
    free(globals.data);
    free(undefs.data);
    free(sorted_syms.data);
    asm_model_free(&m);

    MachObject obj = { result, sz };
    return obj;
}
