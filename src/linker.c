/*
 * linker.c — b1cc internal static linker (M33). See linker.h.
 *
 * Pipeline: read objects -> parse ELF64 -> resolve symbols (pulling archive
 * members transitively) -> lay out output sections per linker.ld -> apply
 * relocations -> emit ET_EXEC.
 */

#include "linker.h"
#include "diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

/* ---- ELF64 constants ---- */
#define ET_EXEC       2
#define EM_X86_64     62
#define SHT_PROGBITS  1
#define SHT_SYMTAB    2
#define SHT_STRTAB    3
#define SHT_RELA      4
#define SHT_NOBITS    8
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4
#define SHF_WRITE     0x1
#define STB_LOCAL     0
#define STB_GLOBAL    1
#define STB_WEAK      2
#define STT_SECTION   3
#define SHN_UNDEF     0
#define SHN_ABS       0xfff1
#define SHN_COMMON    0xfff2

#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_PLT32     4
#define R_X86_64_GOTPCREL  9
#define R_X86_64_32        10
#define R_X86_64_32S       11
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8
#define R_X86_64_GOTPCRELX     41
#define R_X86_64_REX_GOTPCRELX 42

#define ET_DYN        3
#define PT_LOAD       1
#define PT_DYNAMIC    2
#define PT_GNU_STACK  0x6474e551
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* dynamic tags */
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_SONAME 14
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_INIT_ARRAYSZ 27
#define STT_FUNC 2
#define STT_OBJECT 1
#define STT_FILE 4

/* Output section slots, in linker.ld order. */
enum { OUT_TEXT, OUT_RODATA, OUT_DATA, OUT_INIT_ARRAY, OUT_BSS, OUT_COUNT, OUT_DISCARD = -1 };

/* ---- little-endian readers ---- */
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ---- growable byte buffer ---- */
typedef struct { uint8_t *data; long len; long cap; } Buf;
static void buf_need(Buf *b, long n) {
    if (b->len + n <= b->cap) return;
    long nc = b->cap ? b->cap : 1024;
    while (nc < b->len + n) nc *= 2;
    b->data = (uint8_t *)realloc(b->data, (size_t)nc);
    b->cap = nc;
}
static void buf_put(Buf *b, const void *p, long n) { buf_need(b, n); memcpy(b->data + b->len, p, (size_t)n); b->len += n; }
static void buf_zero(Buf *b, long n) { buf_need(b, n); memset(b->data + b->len, 0, (size_t)n); b->len += n; }
static void buf_put32(Buf *b, uint32_t v) { buf_put(b, &v, 4); }
static void buf_put64(Buf *b, uint64_t v) { buf_put(b, &v, 8); }
static void buf_put16(Buf *b, uint16_t v) { buf_put(b, &v, 2); }

/* ---- internal model ---- */
typedef struct { uint64_t off; uint32_t sym; uint32_t type; int64_t add; } Rela;

typedef struct {
    const char *name;
    uint32_t    type;
    uint64_t    flags;
    uint64_t    size;
    uint64_t    align;
    uint8_t    *data;     /* mutable copy for PROGBITS (NULL for NOBITS) */
    Rela       *rel;
    int         nrel;
    int         out;      /* OUT_* or OUT_DISCARD */
    uint64_t    va;       /* assigned virtual address of this input section */
} Sec;

typedef struct {
    const char *name;
    uint64_t    value;
    uint64_t    size;
    uint16_t    shndx;
    uint8_t     bind;
    uint8_t     type;
} Sym;

typedef struct Obj {
    const char *name;
    const uint8_t *img;
    long        imgsz;
    Sec        *secs;
    int         nsec;
    Sym        *syms;
    int         nsym;
    int         included;
} Obj;

/* A resolved global definition: which object + symbol index. */
typedef struct { Obj *obj; int symidx; } GlobalDef;

typedef struct {
    Obj      **objs;
    int        nobj;
    int        cap;
    HashMap    globals;    /* name -> GlobalDef* */
    Arena     *arena;
    /* archive member cache: parsed lazily and reused if pulled twice */
    uint64_t   init_start; /* linker-defined __init_array_start */
    uint64_t   init_end;
} Linker;

/* map an input section name to an output slot */
static int classify_section(const char *name, uint32_t type, uint64_t flags) {
    if (!(flags & SHF_ALLOC)) return OUT_DISCARD;      /* .comment/.symtab/.rela.* etc. */
    if (type == SHT_NOBITS) return OUT_BSS;
    if (strncmp(name, ".init_array", 11) == 0 || strncmp(name, ".preinit_array", 14) == 0 ||
        strncmp(name, ".fini_array", 11) == 0) return OUT_INIT_ARRAY;
    if (flags & SHF_EXECINSTR) return OUT_TEXT;
    if (flags & SHF_WRITE) return OUT_DATA;
    return OUT_RODATA;                                  /* alloc, read-only, non-exec */
}

/* ---- ELF64 object parsing ---- */
static Obj *parse_object(Linker *lk, const char *name, const uint8_t *img, long imgsz) {
    if (imgsz < 64 || memcmp(img, "\177ELF", 4) != 0 || img[4] != 2 /*ELFCLASS64*/) {
        diagnostics_fatal("linker: not an ELF64 object");
    }
    Obj *o = (Obj *)arena_alloc(lk->arena, sizeof(Obj));
    memset(o, 0, sizeof(*o));
    o->name = name; o->img = img; o->imgsz = imgsz;

    uint64_t e_shoff = rd64(img + 40);
    uint16_t e_shentsize = rd16(img + 58);
    uint16_t e_shnum = rd16(img + 60);
    uint16_t e_shstrndx = rd16(img + 62);

    const uint8_t *sh = img + e_shoff;
    /* section header string table */
    const uint8_t *shstr_hdr = sh + (uint64_t)e_shstrndx * e_shentsize;
    const char *shstrtab = (const char *)(img + rd64(shstr_hdr + 24));

    o->nsec = e_shnum;
    o->secs = (Sec *)arena_alloc(lk->arena, sizeof(Sec) * (e_shnum ? e_shnum : 1));
    memset(o->secs, 0, sizeof(Sec) * (e_shnum ? e_shnum : 1));

    int symtab_idx = -1;
    /* first pass: read section metadata + data */
    for (int i = 0; i < e_shnum; i++) {
        const uint8_t *shp = sh + (uint64_t)i * e_shentsize;
        Sec *s = &o->secs[i];
        s->name = shstrtab + rd32(shp + 0);
        s->type = rd32(shp + 4);
        s->flags = rd64(shp + 8);
        uint64_t offset = rd64(shp + 24);
        s->size = rd64(shp + 32);
        s->align = rd64(shp + 48);
        if (s->align == 0) s->align = 1;
        s->out = classify_section(s->name, s->type, s->flags);
        if (s->type == SHT_NOBITS) {
            s->data = NULL;
        } else if (s->size) {
            /* mutable copy so relocations can patch in place */
            s->data = (uint8_t *)arena_alloc(lk->arena, (size_t)s->size);
            memcpy(s->data, img + offset, (size_t)s->size);
        }
        if (s->type == SHT_SYMTAB) symtab_idx = i;
    }

    /* symbols */
    if (symtab_idx >= 0) {
        const uint8_t *shp = sh + (uint64_t)symtab_idx * e_shentsize;
        uint64_t off = rd64(shp + 24);
        uint64_t size = rd64(shp + 32);
        uint64_t entsize = rd64(shp + 56); if (!entsize) entsize = 24;
        uint32_t link = rd32(shp + 40);   /* strtab section */
        const uint8_t *strhp = sh + (uint64_t)link * e_shentsize;
        const char *strtab = (const char *)(img + rd64(strhp + 24));
        int n = (int)(size / entsize);
        o->nsym = n;
        o->syms = (Sym *)arena_alloc(lk->arena, sizeof(Sym) * (n ? n : 1));
        for (int i = 0; i < n; i++) {
            const uint8_t *sp = img + off + (uint64_t)i * entsize;
            Sym *y = &o->syms[i];
            y->name = strtab + rd32(sp + 0);
            uint8_t info = sp[4];
            y->bind = info >> 4;
            y->type = info & 0xf;
            y->shndx = rd16(sp + 6);
            y->value = rd64(sp + 8);
            y->size = rd64(sp + 16);
        }
    }

    /* relocations: attach each SHT_RELA to its target section */
    for (int i = 0; i < e_shnum; i++) {
        const uint8_t *shp = sh + (uint64_t)i * e_shentsize;
        if (rd32(shp + 4) != SHT_RELA) continue;
        uint32_t info = rd32(shp + 44);  /* sh_info = target section */
        if (info >= (uint32_t)e_shnum) continue;
        Sec *tgt = &o->secs[info];
        uint64_t off = rd64(shp + 24);
        uint64_t size = rd64(shp + 32);
        uint64_t entsize = rd64(shp + 56); if (!entsize) entsize = 24;
        int n = (int)(size / entsize);
        tgt->rel = (Rela *)arena_alloc(lk->arena, sizeof(Rela) * (n ? n : 1));
        tgt->nrel = n;
        for (int r = 0; r < n; r++) {
            const uint8_t *rp = img + off + (uint64_t)r * entsize;
            uint64_t rinfo = rd64(rp + 8);
            tgt->rel[r].off = rd64(rp + 0);
            tgt->rel[r].type = (uint32_t)(rinfo & 0xffffffff);
            tgt->rel[r].sym = (uint32_t)(rinfo >> 32);
            tgt->rel[r].add = (int64_t)rd64(rp + 16);
        }
    }
    return o;
}

static void include_object(Linker *lk, Obj *o) {
    if (o->included) return;
    o->included = 1;
    if (lk->nobj == lk->cap) {
        lk->cap = lk->cap ? lk->cap * 2 : 16;
        lk->objs = (Obj **)realloc(lk->objs, sizeof(Obj *) * lk->cap);
    }
    lk->objs[lk->nobj++] = o;
    /* register its global definitions */
    for (int i = 0; i < o->nsym; i++) {
        Sym *y = &o->syms[i];
        if (y->bind == STB_LOCAL) continue;
        if (y->shndx == SHN_UNDEF) continue;
        if (!y->name || !y->name[0]) continue;
        HashMapEntry *e = hashmap_get(&lk->globals, y->name);
        if (e && y->bind == STB_GLOBAL) {
            GlobalDef *gd = (GlobalDef *)e->val_ptr;
            /* strong overrides an existing weak */
            if (gd->obj->syms[gd->symidx].bind == STB_WEAK) { gd->obj = o; gd->symidx = i; }
            continue;
        }
        if (e) continue; /* keep first (weak) def */
        GlobalDef *gd = (GlobalDef *)arena_alloc(lk->arena, sizeof(GlobalDef));
        gd->obj = o; gd->symidx = i;
        hashmap_put(&lk->globals, y->name, gd, 0);
    }
}

/* ---- archive (.a) handling ---- */
typedef struct { const char *name; long off; long size; } ArMember;
typedef struct {
    const uint8_t *img; long imgsz;
    /* symbol -> member data offset (System V armap) */
    HashMap symmap;        /* name -> long member header offset (+1) */
    HashMap parsed;        /* member offset -> Obj* (cache) */
    const char *longnames; long longnames_len;
} Archive;

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void archive_load(Linker *lk, Archive *ar, const uint8_t *img, long imgsz) {
    memset(ar, 0, sizeof(*ar));
    ar->img = img; ar->imgsz = imgsz;
    hashmap_init(&ar->symmap, 256);
    hashmap_init(&ar->parsed, 256);
    if (imgsz < 8 || memcmp(img, "!<arch>\n", 8) != 0)
        diagnostics_fatal("linker: bad archive magic");
    long p = 8;
    while (p + 60 <= imgsz) {
        const uint8_t *h = img + p;
        char szbuf[11]; memcpy(szbuf, h + 48, 10); szbuf[10] = 0;
        long msize = strtol(szbuf, NULL, 10);
        long data_off = p + 60;
        char nm[17]; memcpy(nm, h, 16); nm[16] = 0;
        if (strncmp(nm, "/ ", 2) == 0 || (nm[0] == '/' && nm[1] == ' ')) {
            /* System V symbol table: BE count, count offsets, then names */
            const uint8_t *d = img + data_off;
            uint32_t cnt = rd32be(d);
            const uint8_t *offs = d + 4;
            const char *names = (const char *)(offs + (uint64_t)cnt * 4);
            const char *np = names;
            for (uint32_t i = 0; i < cnt; i++) {
                long moff = (long)rd32be(offs + (uint64_t)i * 4);
                const char *interned = arena_strdup(lk->arena, np);
                /* store member header offset (+1 so 0 is "absent") */
                if (!hashmap_get(&ar->symmap, interned))
                    hashmap_put(&ar->symmap, interned, NULL, moff + 1);
                np += strlen(np) + 1;
            }
        } else if (strncmp(nm, "//", 2) == 0) {
            ar->longnames = (const char *)(img + data_off);
            ar->longnames_len = msize;
        }
        p = data_off + msize;
        if (p & 1) p++; /* 2-byte alignment */
    }
}

static Obj *archive_member_at(Linker *lk, Archive *ar, long hdr_off) {
    /* cache keyed by the member header offset (as a decimal string) */
    char key[24]; snprintf(key, sizeof(key), "%ld", hdr_off);
    const char *ikey = arena_strdup(lk->arena, key);
    HashMapEntry *e = hashmap_get(&ar->parsed, ikey);
    if (e && e->val_ptr) return (Obj *)e->val_ptr;

    const uint8_t *h = ar->img + hdr_off;
    char szbuf[11]; memcpy(szbuf, h + 48, 10); szbuf[10] = 0;
    long msize = strtol(szbuf, NULL, 10);
    long data_off = hdr_off + 60;
    /* resolve member name (for diagnostics) */
    char nm[17]; memcpy(nm, h, 16); nm[16] = 0;
    const char *mname = arena_strdup(lk->arena, "archive-member");
    if (nm[0] == '/' && nm[1] >= '0' && nm[1] <= '9' && ar->longnames) {
        long lo = strtol(nm + 1, NULL, 10);
        mname = arena_strdup(lk->arena, ar->longnames + lo);
    }
    Obj *o = parse_object(lk, mname, ar->img + data_off, msize);
    hashmap_put(&ar->parsed, ikey, o, 0);
    return o;
}

/* Does object o have an undefined (non-weak or weak) global not yet resolved? */
static int resolve_pass(Linker *lk, Archive *archives, int narch) {
    int progress = 0;
    for (int oi = 0; oi < lk->nobj; oi++) {
        Obj *o = lk->objs[oi];
        for (int i = 0; i < o->nsym; i++) {
            Sym *y = &o->syms[i];
            if (y->shndx != SHN_UNDEF) continue;
            if (y->bind == STB_LOCAL) continue;
            if (!y->name || !y->name[0]) continue;
            if (hashmap_get(&lk->globals, y->name)) continue;
            /* search archives */
            for (int a = 0; a < narch; a++) {
                HashMapEntry *e = hashmap_get(&archives[a].symmap, y->name);
                if (!e || e->val_int == 0) continue;
                Obj *m = archive_member_at(lk, &archives[a], e->val_int - 1);
                if (!m->included) { include_object(lk, m); progress = 1; }
                break;
            }
        }
    }
    return progress;
}

/* ---- symbol final-address resolution ---- */
/* returns 1 and sets *out if resolvable; 0 if unresolved (caller decides). */
static int sym_addr(Linker *lk, Obj *o, int symidx, uint64_t *out) {
    Sym *y = &o->syms[symidx];
    if (y->bind == STB_LOCAL || y->type == STT_SECTION) {
        /* local: resolve within its own object */
        if (y->shndx == SHN_ABS) { *out = y->value; return 1; }
        if (y->shndx < o->nsec) { *out = o->secs[y->shndx].va + y->value; return 1; }
        return 0;
    }
    /* linker-defined boundary symbols */
    if (strcmp(y->name, "__init_array_start") == 0) { *out = lk->init_start; return 1; }
    if (strcmp(y->name, "__init_array_end") == 0) { *out = lk->init_end; return 1; }
    /* global via resolution map */
    HashMapEntry *e = hashmap_get(&lk->globals, y->name);
    if (e) {
        GlobalDef *gd = (GlobalDef *)e->val_ptr;
        Sym *d = &gd->obj->syms[gd->symidx];
        if (d->shndx == SHN_ABS) { *out = d->value; return 1; }
        if (d->shndx < gd->obj->nsec) { *out = gd->obj->secs[d->shndx].va + d->value; return 1; }
        return 0;
    }
    /* self-defined (e.g. this very symbol has a def elsewhere in o but not global map) */
    if (y->shndx != SHN_UNDEF && y->shndx < o->nsec) { *out = o->secs[y->shndx].va + y->value; return 1; }
    return 0; /* undefined */
}

int elf_link_static(const LinkRequest *req, Arena *arena) {
    Linker lk; memset(&lk, 0, sizeof(lk));
    lk.arena = arena;
    hashmap_init(&lk.globals, 1024);

    /* 1. load explicit inputs (crt0 first, then user objects) */
    for (int i = 0; i < req->n_inputs; i++) {
        FILE *f = fopen(req->inputs[i], "rb");
        if (!f) { diagnostics_fatal("linker: cannot open input object"); }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *img = (uint8_t *)arena_alloc(arena, (size_t)sz);
        if (fread(img, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); diagnostics_fatal("linker: short read"); }
        fclose(f);
        Obj *o = parse_object(&lk, req->inputs[i], img, sz);
        include_object(&lk, o);
    }

    /* 2. load archives + resolve transitively */
    Archive *archives = (Archive *)arena_alloc(arena, sizeof(Archive) * (req->n_archives ? req->n_archives : 1));
    for (int i = 0; i < req->n_archives; i++) {
        FILE *f = fopen(req->archives[i], "rb");
        if (!f) { diagnostics_fatal("linker: cannot open archive"); }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *img = (uint8_t *)arena_alloc(arena, (size_t)sz);
        if (fread(img, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); diagnostics_fatal("linker: short read"); }
        fclose(f);
        archive_load(&lk, &archives[i], img, sz);
    }
    while (resolve_pass(&lk, archives, req->n_archives)) { /* to fixpoint */ }

    /* 3. layout: assign VAs per linker.ld order.
     *    output-section alignments (bytes). */
    static const uint64_t out_align[OUT_COUNT] = {
        [OUT_TEXT] = 0x1000, [OUT_RODATA] = 0x1000, [OUT_DATA] = 0x1000,
        [OUT_INIT_ARRAY] = 8, [OUT_BSS] = 0x1000,
    };
    uint64_t out_va[OUT_COUNT], out_size[OUT_COUNT];
    uint64_t va = req->base_va;
    for (int slot = 0; slot < OUT_COUNT; slot++) {
        uint64_t al = out_align[slot];
        va = (va + al - 1) & ~(al - 1);
        out_va[slot] = va;
        /* place each included input section mapped to this slot, in object order */
        for (int oi = 0; oi < lk.nobj; oi++) {
            Obj *o = lk.objs[oi];
            for (int si = 0; si < o->nsec; si++) {
                Sec *s = &o->secs[si];
                if (s->out != slot) continue;
                if (!(s->flags & SHF_ALLOC)) continue;
                uint64_t sal = s->align ? s->align : 1;
                va = (va + sal - 1) & ~(sal - 1);
                s->va = va;
                va += s->size;
            }
        }
        /* COMMON symbols land in .bss */
        if (slot == OUT_BSS) {
            for (int oi = 0; oi < lk.nobj; oi++) {
                Obj *o = lk.objs[oi];
                for (int i = 0; i < o->nsym; i++) {
                    Sym *y = &o->syms[i];
                    if (y->shndx != SHN_COMMON) continue;
                    uint64_t al = y->value ? y->value : 1;   /* COMMON: value = align */
                    va = (va + al - 1) & ~(al - 1);
                    /* rewrite the symbol to an absolute-in-.bss definition */
                    y->value = va; y->shndx = SHN_ABS;
                    va += y->size;
                }
            }
        }
        out_size[slot] = va - out_va[slot];
    }
    lk.init_start = out_va[OUT_INIT_ARRAY];
    lk.init_end = out_va[OUT_INIT_ARRAY] + out_size[OUT_INIT_ARRAY];

    /* 4. apply relocations (patch each section's mutable data copy) */
    for (int oi = 0; oi < lk.nobj; oi++) {
        Obj *o = lk.objs[oi];
        for (int si = 0; si < o->nsec; si++) {
            Sec *s = &o->secs[si];
            if (!s->nrel || !s->data) continue;
            for (int r = 0; r < s->nrel; r++) {
                Rela *rl = &s->rel[r];
                uint64_t S = 0;
                int ok = sym_addr(&lk, o, (int)rl->sym, &S);
                if (!ok) {
                    Sym *y = &o->syms[rl->sym];
                    if (y->bind == STB_WEAK) { S = 0; }   /* weak-undefined -> 0 */
                    else {
                        fprintf(stderr, "b1cc linker: undefined symbol: %s\n", y->name);
                        return 1;
                    }
                }
                uint64_t P = s->va + rl->off;
                int64_t A = rl->add;
                uint8_t *loc = s->data + rl->off;
                switch (rl->type) {
                    case R_X86_64_64: { uint64_t v = S + A; memcpy(loc, &v, 8); break; }
                    case R_X86_64_PC32:
                    case R_X86_64_PLT32: { uint32_t v = (uint32_t)(int32_t)((int64_t)(S + A) - (int64_t)P); memcpy(loc, &v, 4); break; }
                    case R_X86_64_32:  { uint32_t v = (uint32_t)(S + A); memcpy(loc, &v, 4); break; }
                    case R_X86_64_32S: { uint32_t v = (uint32_t)(int32_t)(int64_t)(S + A); memcpy(loc, &v, 4); break; }
                    default:
                        fprintf(stderr, "b1cc linker: unsupported relocation type %u\n", rl->type);
                        return 1;
                }
            }
        }
    }

    /* 5. resolve entry point */
    uint64_t entry = 0;
    {
        HashMapEntry *e = hashmap_get(&lk.globals, req->entry);
        if (!e) { fprintf(stderr, "b1cc linker: entry symbol %s not found\n", req->entry); return 1; }
        GlobalDef *gd = (GlobalDef *)e->val_ptr;
        Sym *d = &gd->obj->syms[gd->symidx];
        entry = gd->obj->secs[d->shndx].va + d->value;
    }

    /* 6. emit ET_EXEC.
     * File layout: [ehdr|phdrs] in page 0; each loadable output section at
     * file offset 0x1000 + (va - base) so p_offset ≡ p_vaddr (mod 4K). */
    /* loadable slots that actually have content/size */
    int load_slots[OUT_COUNT]; int n_load = 0;
    for (int slot = 0; slot < OUT_COUNT; slot++)
        if (out_size[slot] > 0) load_slots[n_load++] = slot;

    int phnum = n_load + 1; /* + PT_GNU_STACK */
    Buf out; memset(&out, 0, sizeof(out));

    /* ELF header (64 bytes) */
    uint8_t ident[16] = {0x7f,'E','L','F',2,1,1,0,0,0,0,0,0,0,0,0};
    buf_put(&out, ident, 16);
    buf_put16(&out, ET_EXEC);           /* e_type */
    buf_put16(&out, EM_X86_64);         /* e_machine */
    buf_put32(&out, 1);                 /* e_version */
    buf_put64(&out, entry);             /* e_entry */
    buf_put64(&out, 64);                /* e_phoff */
    buf_put64(&out, 0);                 /* e_shoff (no section headers) */
    buf_put32(&out, 0);                 /* e_flags */
    buf_put16(&out, 64);                /* e_ehsize */
    buf_put16(&out, 56);                /* e_phentsize */
    buf_put16(&out, (uint16_t)phnum);   /* e_phnum */
    buf_put16(&out, 0);                 /* e_shentsize */
    buf_put16(&out, 0);                 /* e_shnum */
    buf_put16(&out, 0);                 /* e_shstrndx */

    /* program headers */
    for (int k = 0; k < n_load; k++) {
        int slot = load_slots[k];
        uint64_t v = out_va[slot];
        uint64_t foff = 0x1000 + (v - req->base_va);
        uint32_t flags = PF_R;
        if (slot == OUT_TEXT) flags = PF_R | PF_X;
        else if (slot == OUT_DATA || slot == OUT_INIT_ARRAY || slot == OUT_BSS) flags = PF_R | PF_W;
        uint64_t filesz = (slot == OUT_BSS) ? 0 : out_size[slot];
        uint64_t memsz = out_size[slot];
        buf_put32(&out, PT_LOAD);
        buf_put32(&out, flags);
        buf_put64(&out, foff);
        buf_put64(&out, v);
        buf_put64(&out, v);        /* p_paddr */
        buf_put64(&out, filesz);
        buf_put64(&out, memsz);
        buf_put64(&out, 0x1000);   /* p_align */
    }
    /* PT_GNU_STACK (non-exec stack) */
    buf_put32(&out, PT_GNU_STACK);
    buf_put32(&out, PF_R | PF_W);
    buf_put64(&out, 0); buf_put64(&out, 0); buf_put64(&out, 0);
    buf_put64(&out, 0); buf_put64(&out, 0); buf_put64(&out, 0);

    /* section contents at their file offsets */
    for (int k = 0; k < n_load; k++) {
        int slot = load_slots[k];
        if (slot == OUT_BSS) continue; /* NOBITS */
        uint64_t foff = 0x1000 + (out_va[slot] - req->base_va);
        if (out.len < (long)foff) buf_zero(&out, (long)foff - out.len);
        /* write each input section into place at its own va */
        for (int oi = 0; oi < lk.nobj; oi++) {
            Obj *o = lk.objs[oi];
            for (int si = 0; si < o->nsec; si++) {
                Sec *s = &o->secs[si];
                if (s->out != slot || !(s->flags & SHF_ALLOC)) continue;
                if (s->type == SHT_NOBITS || !s->data) continue;
                long dst = (long)(0x1000 + (s->va - req->base_va));
                if (out.len < dst) buf_zero(&out, dst - out.len);
                buf_put(&out, s->data, (long)s->size);
            }
        }
    }

    /* 7. write file */
    FILE *of = fopen(req->out_path, "wb");
    if (!of) { diagnostics_fatal("linker: cannot open output"); }
    fwrite(out.data, 1, (size_t)out.len, of);
    fclose(of);
    free(out.data);
    chmod(req->out_path, 0755);   /* mark executable */
    return 0;
}

/* ============================================================================
 * Dynamic linking (M33): ET_DYN PIE executables and shared objects.
 * Produces .dynsym/.dynstr/.hash/.rela.dyn/.rela.plt/.plt/.got/.got.plt +
 * PT_DYNAMIC that the B1NIX in-kernel eager dynamic linker consumes.
 * ============================================================================ */

static uint32_t elf_sysv_hash(const char *name) {
    uint32_t h = 0, g;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h = (h << 4) + *p;
        g = h & 0xf0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* one dynamic symbol (import or, for .so, export) */
typedef struct { const char *name; uint8_t bind; uint8_t type; uint64_t value; uint64_t size; uint16_t shndx; } DynSym;
/* a GOT slot: import -> GLOB_DAT(dynidx); internal -> RELATIVE(target) */
typedef struct { int is_import; int dynidx; uint64_t target; } GotSlot;
/* a deferred fix-up of a rel32 site once .plt/.got VAs are known */
typedef struct { uint8_t *loc; uint64_t P; int kind; int slot; int64_t add; } Fixup;
enum { FIX_PLT, FIX_GOT };

int elf_link(const LinkRequest *req, Arena *arena) {
    if (req->mode == LINK_STATIC_EXE) return elf_link_static(req, arena);

    Linker lk; memset(&lk, 0, sizeof(lk));
    lk.arena = arena;
    hashmap_init(&lk.globals, 1024);

    /* 1. load explicit inputs only (no archive folding: libc is a shared dep) */
    for (int i = 0; i < req->n_inputs; i++) {
        FILE *f = fopen(req->inputs[i], "rb");
        if (!f) { diagnostics_fatal("linker: cannot open input object"); }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *img = (uint8_t *)arena_alloc(arena, (size_t)sz);
        if (fread(img, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); diagnostics_fatal("linker: short read"); }
        fclose(f);
        include_object(&lk, parse_object(&lk, req->inputs[i], img, sz));
    }

    /* 2. lay out allocatable input sections (base 0 for PIE/shared). Order:
     *    RX: .text ; R: .rodata ; RW: .data .init_array .bss  (dynamic metadata
     *    sections are appended per-region below). */
    static const uint64_t out_align[OUT_COUNT] = {
        [OUT_TEXT] = 0x1000, [OUT_RODATA] = 0x1000, [OUT_DATA] = 0x1000,
        [OUT_INIT_ARRAY] = 8, [OUT_BSS] = 0x1000,
    };
    uint64_t out_va[OUT_COUNT], out_size[OUT_COUNT];
    uint64_t va = req->base_va; /* 0 */
    for (int slot = 0; slot < OUT_COUNT; slot++) {
        uint64_t al = out_align[slot];
        va = (va + al - 1) & ~(al - 1);
        out_va[slot] = va;
        for (int oi = 0; oi < lk.nobj; oi++) {
            Obj *o = lk.objs[oi];
            for (int si = 0; si < o->nsec; si++) {
                Sec *s = &o->secs[si];
                if (s->out != slot || !(s->flags & SHF_ALLOC)) continue;
                uint64_t sal = s->align ? s->align : 1;
                va = (va + sal - 1) & ~(sal - 1);
                s->va = va; va += s->size;
            }
        }
        if (slot == OUT_BSS) {
            for (int oi = 0; oi < lk.nobj; oi++) {
                Obj *o = lk.objs[oi];
                for (int i = 0; i < o->nsym; i++) {
                    Sym *y = &o->syms[i];
                    if (y->shndx != SHN_COMMON) continue;
                    uint64_t al2 = y->value ? y->value : 1;
                    va = (va + al2 - 1) & ~(al2 - 1);
                    y->value = va; y->shndx = SHN_ABS; va += y->size;
                }
            }
        }
        out_size[slot] = va - out_va[slot];
    }
    lk.init_start = out_va[OUT_INIT_ARRAY];
    lk.init_end = out_va[OUT_INIT_ARRAY] + out_size[OUT_INIT_ARRAY];

    /* 3. walk relocations: classify, build dynsym/PLT/GOT + dynamic relocs,
     *    patch what we can now and defer PLT/GOT rel32s until their VAs exist. */
    DynSym *dsyms = NULL; int ndsym = 0, dcap = 0;  /* index 0 filled later as null */
    HashMap dynidx; hashmap_init(&dynidx, 256);      /* name -> dynidx+1 */
    GotSlot *gots = NULL; int ngot = 0, gcap = 0;
    HashMap gotimp; hashmap_init(&gotimp, 256);      /* import name -> got index+1 */
    int *plts = NULL; int nplt = 0, pcap = 0;        /* plt index -> dynidx */
    HashMap pltmap; hashmap_init(&pltmap, 256);      /* import func name -> plt index+1 */
    /* dynamic relocations we emit into .rela.dyn (RELATIVE / GLOB_DAT / R64) */
    typedef struct { uint64_t off; uint32_t type; uint32_t sym; int64_t add; } DynRela;
    DynRela *rdyn = NULL; int nrdyn = 0, rdcap = 0;
    Fixup *fixups = NULL; int nfix = 0, fxcap = 0;

    #define PUSH(arr, n, cap, val) do { if ((n) == (cap)) { (cap) = (cap) ? (cap)*2 : 16; (arr) = realloc((arr), sizeof(*(arr))*(cap)); } (arr)[(n)++] = (val); } while (0)

    /* get-or-add a dynamic symbol (import), returns dynidx */
    #define GET_DYNIDX_INTO(out, nm, bnd, ty) do { \
        HashMapEntry *_e = hashmap_get(&dynidx, (nm)); int _idx; \
        if (_e) _idx = (int)_e->val_int - 1; else { \
            DynSym _d; _d.name = (nm); _d.bind = (bnd); _d.type = (ty); _d.value = 0; _d.size = 0; _d.shndx = SHN_UNDEF; \
            PUSH(dsyms, ndsym, dcap, _d); _idx = ndsym - 1; \
            hashmap_put(&dynidx, (nm), NULL, _idx + 1); } \
        (out) = _idx; \
    } while (0)

    for (int oi = 0; oi < lk.nobj; oi++) {
        Obj *o = lk.objs[oi];
        for (int si = 0; si < o->nsec; si++) {
            Sec *s = &o->secs[si];
            if (!s->nrel || !s->data) continue;
            /* Only allocatable sections contribute to the loaded image. Debug
             * sections (.debug_*), .comment, etc. carry their own relocations
             * (e.g. R_X86_64_64 -> .text in .debug_line) but are discarded; their
             * unassigned s->va is 0, so processing them here would emit dynamic
             * relocations at bogus offsets that collide with real segments. */
            if (!(s->flags & SHF_ALLOC)) continue;
            for (int r = 0; r < s->nrel; r++) {
                Rela *rl = &s->rel[r];
                Sym *y = &o->syms[rl->sym];
                uint64_t S = 0; int internal = sym_addr(&lk, o, (int)rl->sym, &S);
                uint64_t P = s->va + rl->off;
                uint8_t *loc = s->data + rl->off;
                int64_t A = rl->add;
                uint8_t bnd = y->bind, ty = y->type;
                switch (rl->type) {
                case R_X86_64_PC32: {
                    uint32_t v = (uint32_t)(int32_t)((int64_t)(S + A) - (int64_t)P);
                    memcpy(loc, &v, 4); break; }
                case R_X86_64_PLT32: {
                    if (internal) { uint32_t v = (uint32_t)(int32_t)((int64_t)(S + A) - (int64_t)P); memcpy(loc, &v, 4); }
                    else {
                        HashMapEntry *e = hashmap_get(&pltmap, y->name);
                        int pi;
                        if (e) pi = (int)e->val_int - 1;
                        else { int di; GET_DYNIDX_INTO(di, y->name, bnd, ty ? ty : STT_FUNC); PUSH(plts, nplt, pcap, di); pi = nplt - 1; hashmap_put(&pltmap, y->name, NULL, pi + 1); }
                        Fixup fx; fx.loc = loc; fx.P = P; fx.kind = FIX_PLT; fx.slot = pi; fx.add = A; PUSH(fixups, nfix, fxcap, fx);
                    }
                    break; }
                case R_X86_64_GOTPCREL:
                case R_X86_64_GOTPCRELX:
                case R_X86_64_REX_GOTPCRELX: {
                    int gi;
                    if (internal) { GotSlot g; g.is_import = 0; g.dynidx = 0; g.target = S; PUSH(gots, ngot, gcap, g); gi = ngot - 1; }
                    else {
                        HashMapEntry *e = hashmap_get(&gotimp, y->name);
                        if (e) gi = (int)e->val_int - 1;
                    else { int di; GET_DYNIDX_INTO(di, y->name, bnd, ty ? ty : STT_OBJECT); GotSlot g; g.is_import = 1; g.dynidx = di; g.target = 0; PUSH(gots, ngot, gcap, g); gi = ngot - 1; hashmap_put(&gotimp, y->name, NULL, gi + 1); }
                    }
                    Fixup fx; fx.loc = loc; fx.P = P; fx.kind = FIX_GOT; fx.slot = gi; fx.add = A; PUSH(fixups, nfix, fxcap, fx);
                    break; }
                case R_X86_64_64: {
                    if (internal) { DynRela dr; dr.off = P; dr.type = R_X86_64_RELATIVE; dr.sym = 0; dr.add = (int64_t)(S + A); PUSH(rdyn, nrdyn, rdcap, dr); uint64_t z = 0; memcpy(loc, &z, 8); }
                    else { int di; GET_DYNIDX_INTO(di, y->name, bnd, ty); DynRela dr; dr.off = P; dr.type = R_X86_64_64; dr.sym = (uint32_t)di; dr.add = A; PUSH(rdyn, nrdyn, rdcap, dr); uint64_t z = 0; memcpy(loc, &z, 8); }
                    break; }
                default:
                    fprintf(stderr, "b1cc linker (dynamic): unsupported relocation %u for %s\n", rl->type, y->name);
                    return 1;
                }
            }
        }
    }

    /* 3b. For a shared object, export every defined non-local global into
     *     .dynsym so dependents can bind against it. Append AFTER all imports so
     *     the import dynidx values referenced by .rela.dyn/.rela.plt stay stable.
     *     st_value is filled post-layout (step 5b) since the RW overlap fix below
     *     can still shift .data/.bss VAs. */
    typedef struct { int dynidx; Obj *obj; int sym; } ExportSrc;
    ExportSrc *exps = NULL; int nexp = 0, ecap = 0;
    if (req->mode == LINK_SHARED) {
        for (int oi = 0; oi < lk.nobj; oi++) {
            Obj *o = lk.objs[oi];
            for (int i = 0; i < o->nsym; i++) {
                Sym *y = &o->syms[i];
                if (y->bind == STB_LOCAL) continue;
                if (y->shndx == SHN_UNDEF) continue;
                if (y->type == STT_SECTION || y->type == STT_FILE) continue;
                if (!y->name || !y->name[0]) continue;
                /* only export the canonical definition (the one in lk.globals) */
                HashMapEntry *ge = hashmap_get(&lk.globals, y->name);
                if (!ge) continue;
                GlobalDef *gd = (GlobalDef *)ge->val_ptr;
                if (gd->obj != o || gd->symidx != i) continue; /* not canonical def */
                if (hashmap_get(&dynidx, y->name)) continue;   /* already emitted */
                DynSym d; d.name = y->name; d.bind = y->bind;
                d.type = y->type ? y->type : STT_FUNC;
                d.value = 0; d.size = y->size; d.shndx = 1; /* defined; value filled in 5b */
                PUSH(dsyms, ndsym, dcap, d);
                hashmap_put(&dynidx, y->name, NULL, ndsym); /* dynidx+1 */
                ExportSrc es; es.dynidx = ndsym - 1; es.obj = o; es.sym = i; PUSH(exps, nexp, ecap, es);
            }
        }
    }

    /* 4. lay out synthesized sections.
     *    RX tail: .plt (16 bytes/entry).  RW tail: .got (8/slot) + .got.plt
     *    (8/plt) + .dynamic.  R region: .hash .dynsym .dynstr .rela.dyn .rela.plt */
    uint64_t plt_va = (out_va[OUT_TEXT] + out_size[OUT_TEXT] + 15) & ~15ull;
    uint64_t plt_sz = (uint64_t)nplt * 16;

    /* R region after both RX tail (plt) and input .rodata (page aligned).
     * Must not overlap with input .rodata sections that were already placed
     * in the initial layout (step 2). */
    uint64_t r_off_after_plt = (plt_va + plt_sz + 0xfff) & ~0xfffull;
    uint64_t r_off_after_rodata = (out_va[OUT_RODATA] + out_size[OUT_RODATA] + 0xfff) & ~0xfffull;
    uint64_t r_off = r_off_after_plt > r_off_after_rodata ? r_off_after_plt : r_off_after_rodata;
    /* dynsym: index 0 = null, then ndsym imports */
    int total_dsym = ndsym + 1;
    uint64_t hash_va = r_off;
    /* SysV hash: nbucket, nchain, buckets[], chain[] */
    uint32_t nbucket = 1; while (nbucket * 2 < (uint32_t)total_dsym) nbucket <<= 1; if (!nbucket) nbucket = 1;
    uint64_t hash_sz = (uint64_t)(2 + nbucket + total_dsym) * 4;
    uint64_t dynsym_va = (hash_va + hash_sz + 7) & ~7ull;
    uint64_t dynsym_sz = (uint64_t)total_dsym * 24;
    uint64_t dynstr_va = dynsym_va + dynsym_sz;
    /* dynstr: build now (need size). leading NUL, then each dynsym name, then NEEDED/soname */
    Buf dynstr; memset(&dynstr, 0, sizeof(dynstr)); buf_zero(&dynstr, 1);
    uint32_t *dsym_stroff = (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * (total_dsym ? total_dsym : 1));
    dsym_stroff[0] = 0;
    for (int i = 0; i < ndsym; i++) { dsym_stroff[i + 1] = (uint32_t)dynstr.len; buf_put(&dynstr, dsyms[i].name, (long)strlen(dsyms[i].name) + 1); }
    uint32_t needed_stroff[8]; int nneeded = req->n_needed < 8 ? req->n_needed : 8;
    for (int i = 0; i < nneeded; i++) { needed_stroff[i] = (uint32_t)dynstr.len; buf_put(&dynstr, req->needed[i], (long)strlen(req->needed[i]) + 1); }
    uint32_t soname_stroff = 0;
    if (req->mode == LINK_SHARED && req->soname) { soname_stroff = (uint32_t)dynstr.len; buf_put(&dynstr, req->soname, (long)strlen(req->soname) + 1); }
    uint64_t dynstr_sz = (uint64_t)dynstr.len;
    uint64_t reladyn_va = (dynstr_va + dynstr_sz + 7) & ~7ull;
    uint64_t reladyn_sz = (uint64_t)nrdyn * 24;
    uint64_t relaplt_va = reladyn_va + reladyn_sz;
    uint64_t relaplt_sz = (uint64_t)nplt * 24;

    /* RW layout. The initial layout (step 2) placed .data/.init_array/.bss at
     * low VAs (base 0), before we knew the R region extent. Shift the input RW
     * sections above the R region FIRST, then place the dynamic RW metadata
     * (.got/.got.plt/.dynamic) clear of BOTH the R region and the shifted input
     * RW — otherwise .data and .dynamic can land on the same page. */
    uint64_t r_region_end = relaplt_va + relaplt_sz;
    if (out_va[OUT_DATA] < r_region_end) {
        uint64_t delta = ((r_region_end - out_va[OUT_DATA]) + 0xfff) & ~0xfffull;
        for (int slot = OUT_DATA; slot < OUT_COUNT; slot++) {
            out_va[slot] += delta;
            for (int oi = 0; oi < lk.nobj; oi++) {
                Obj *o = lk.objs[oi];
                for (int si = 0; si < o->nsec; si++) {
                    Sec *s = &o->secs[si];
                    if (s->out == slot && (s->flags & SHF_ALLOC))
                        s->va += delta;
                }
            }
        }
        lk.init_start = out_va[OUT_INIT_ARRAY];
        lk.init_end = out_va[OUT_INIT_ARRAY] + out_size[OUT_INIT_ARRAY];
    }

    /* Dynamic RW metadata goes above both the R region and the input RW
     * sections (incl. .bss). It is file-backed and emitted last in the RW
     * segment; .bss's memsz falls within the segment's filesz range. */
    uint64_t input_rw_end = out_va[OUT_BSS] + out_size[OUT_BSS];
    uint64_t rw_meta_floor = r_region_end > input_rw_end ? r_region_end : input_rw_end;
    uint64_t rw_meta = (rw_meta_floor + 0xfff) & ~0xfffull;
    uint64_t got_va = rw_meta;
    uint64_t gotplt_va = got_va + (uint64_t)ngot * 8;
    uint64_t dynamic_va = gotplt_va + (uint64_t)nplt * 8;

    /* 5b. now that section VAs are final (post RW-shift), fill exported
     *     symbol values so .dynsym advertises correct addresses. */
    for (int i = 0; i < nexp; i++) {
        uint64_t sv = 0;
        if (sym_addr(&lk, exps[i].obj, exps[i].sym, &sv))
            dsyms[exps[i].dynidx].value = sv;
    }

    /* dynsym indices carry final GOT/PLT slot addresses; assign now */
    /* 5. apply deferred rel32 fixups */
    for (int i = 0; i < nfix; i++) {
        Fixup *fx = &fixups[i];
        uint64_t slot_va = (fx->kind == FIX_PLT) ? (plt_va + (uint64_t)fx->slot * 16)
                                                 : (got_va + (uint64_t)fx->slot * 8);
        uint32_t v = (uint32_t)(int32_t)((int64_t)slot_va + fx->add - (int64_t)fx->P);
        memcpy(fx->loc, &v, 4);
    }

    /* 6. resolve entry */
    uint64_t entry = 0;
    if (req->mode == LINK_PIE) {
        HashMapEntry *e = hashmap_get(&lk.globals, req->entry);
        if (!e) { fprintf(stderr, "b1cc linker: entry %s not found\n", req->entry); return 1; }
        GlobalDef *gd = (GlobalDef *)e->val_ptr;
        Sym *d = &gd->obj->syms[gd->symidx];
        entry = gd->obj->secs[d->shndx].va + d->value;
    }

    /* 7. build byte images of the synthesized sections */
    /* .plt: each entry = jmp *gotplt_slot(%rip) ; pad to 16 */
    Buf pltb; memset(&pltb, 0, sizeof(pltb));
    for (int i = 0; i < nplt; i++) {
        uint64_t entv = plt_va + (uint64_t)i * 16;
        uint64_t slotv = gotplt_va + (uint64_t)i * 8;
        uint8_t stub[16]; memset(stub, 0x90, sizeof(stub));
        stub[0] = 0xff; stub[1] = 0x25;
        uint32_t disp = (uint32_t)(int32_t)((int64_t)slotv - (int64_t)(entv + 6));
        memcpy(stub + 2, &disp, 4);
        buf_put(&pltb, stub, 16);
    }
    /* .hash */
    Buf hashb; memset(&hashb, 0, sizeof(hashb));
    buf_put32(&hashb, nbucket);
    buf_put32(&hashb, (uint32_t)total_dsym);
    uint32_t *buckets = (uint32_t *)calloc(nbucket, 4);
    uint32_t *chain = (uint32_t *)calloc(total_dsym ? total_dsym : 1, 4);
    for (int i = 1; i < total_dsym; i++) {
        uint32_t h = elf_sysv_hash(dsyms[i - 1].name) % nbucket;
        chain[i] = buckets[h]; buckets[h] = (uint32_t)i;
    }
    for (uint32_t i = 0; i < nbucket; i++) buf_put32(&hashb, buckets[i]);
    for (int i = 0; i < total_dsym; i++) buf_put32(&hashb, chain[i]);
    free(buckets); free(chain);
    /* .dynsym */
    Buf dsymb; memset(&dsymb, 0, sizeof(dsymb));
    { uint8_t z[24]; memset(z, 0, 24); buf_put(&dsymb, z, 24); }         /* null */
    for (int i = 0; i < ndsym; i++) {
        uint8_t e[24]; memset(e, 0, 24);
        uint32_t nm = dsym_stroff[i + 1]; memcpy(e, &nm, 4);
        e[4] = (uint8_t)((dsyms[i].bind << 4) | (dsyms[i].type & 0xf));
        uint16_t shn = dsyms[i].shndx; memcpy(e + 6, &shn, 2);
        uint64_t val = dsyms[i].value; memcpy(e + 8, &val, 8);
        uint64_t sz = dsyms[i].size; memcpy(e + 16, &sz, 8);
        buf_put(&dsymb, e, 24);
    }
    /* .rela.dyn */
    Buf reladynb; memset(&reladynb, 0, sizeof(reladynb));
    for (int i = 0; i < nrdyn; i++) {
        buf_put64(&reladynb, rdyn[i].off);
        buf_put64(&reladynb, ((uint64_t)(rdyn[i].sym + 1 - 1) << 32) | rdyn[i].type); /* sym already dynidx */
        buf_put64(&reladynb, (uint64_t)rdyn[i].add);
    }
    /* GLOB_DAT for import GOT slots + RELATIVE for internal GOT slots go in .rela.dyn too */
    /* (append) */
    for (int i = 0; i < ngot; i++) {
        uint64_t slotv = got_va + (uint64_t)i * 8;
        if (gots[i].is_import) { buf_put64(&reladynb, slotv); buf_put64(&reladynb, ((uint64_t)(gots[i].dynidx + 1) << 32) | R_X86_64_GLOB_DAT); buf_put64(&reladynb, 0); }
        else { buf_put64(&reladynb, slotv); buf_put64(&reladynb, R_X86_64_RELATIVE); buf_put64(&reladynb, gots[i].target); }
    }
    uint64_t reladyn_total = (uint64_t)reladynb.len;
    /* .rela.plt: JUMP_SLOT for each plt entry */
    Buf relapltb; memset(&relapltb, 0, sizeof(relapltb));
    for (int i = 0; i < nplt; i++) {
        uint64_t slotv = gotplt_va + (uint64_t)i * 8;
        buf_put64(&relapltb, slotv);
        buf_put64(&relapltb, ((uint64_t)(plts[i] + 1) << 32) | R_X86_64_JUMP_SLOT);
        buf_put64(&relapltb, 0);
    }
    /* fix reladyn_sz/relaplt_va to actual (GOT GLOB_DAT/RELATIVE were appended) */
    reladyn_sz = reladyn_total;
    relaplt_va = reladyn_va + reladyn_sz;
    relaplt_sz = (uint64_t)relapltb.len;

    /* 8. .dynamic */
    Buf dynb; memset(&dynb, 0, sizeof(dynb));
    #define DYN(tag, val) do { buf_put64(&dynb, (uint64_t)(tag)); buf_put64(&dynb, (uint64_t)(val)); } while (0)
    for (int i = 0; i < nneeded; i++) DYN(DT_NEEDED, needed_stroff[i]);
    if (req->mode == LINK_SHARED && req->soname) DYN(DT_SONAME, soname_stroff);
    DYN(DT_HASH, hash_va);
    DYN(DT_STRTAB, dynstr_va);
    DYN(DT_SYMTAB, dynsym_va);
    DYN(DT_STRSZ, dynstr_sz);
    DYN(DT_SYMENT, 24);
    if (reladyn_sz) { DYN(DT_RELA, reladyn_va); DYN(DT_RELASZ, reladyn_sz); DYN(DT_RELAENT, 24); }
    if (relaplt_sz) { DYN(DT_JMPREL, relaplt_va); DYN(DT_PLTRELSZ, relaplt_sz); DYN(DT_PLTREL, 7); DYN(DT_PLTGOT, gotplt_va); }
    if (out_size[OUT_INIT_ARRAY]) { DYN(DT_INIT_ARRAY, out_va[OUT_INIT_ARRAY]); DYN(DT_INIT_ARRAYSZ, out_size[OUT_INIT_ARRAY]); }
    DYN(DT_NULL, 0);
    uint64_t dynamic_sz = (uint64_t)dynb.len;

    /* 9. emit ET_DYN. File layout mirrors the static path: headers in page 0,
     *    each region at file offset 0x1000 + vaddr. */
    uint64_t rw_end = dynamic_va + dynamic_sz;
    uint64_t bss_va = out_va[OUT_BSS], bss_memsz = out_size[OUT_BSS];

    /* program headers: RX, R, RW(file part), DYNAMIC, (bss folded into RW memsz) */
    Buf out; memset(&out, 0, sizeof(out));
    int phnum = 5; /* RX, R, RW, DYNAMIC, GNU_STACK */

    uint8_t ident[16] = {0x7f,'E','L','F',2,1,1,0,0,0,0,0,0,0,0,0};
    buf_put(&out, ident, 16);
    buf_put16(&out, ET_DYN);
    buf_put16(&out, EM_X86_64);
    buf_put32(&out, 1);
    buf_put64(&out, entry);
    buf_put64(&out, 64);
    buf_put64(&out, 0);
    buf_put32(&out, 0);
    buf_put16(&out, 64);
    buf_put16(&out, 56);
    buf_put16(&out, (uint16_t)phnum);
    buf_put16(&out, 0);
    buf_put16(&out, 0);
    buf_put16(&out, 0);

    #define PH(type, flags, foff, vad, filesz, memsz, align) do { \
        buf_put32(&out, (type)); buf_put32(&out, (flags)); \
        buf_put64(&out, (foff)); buf_put64(&out, (vad)); buf_put64(&out, (vad)); \
        buf_put64(&out, (filesz)); buf_put64(&out, (memsz)); buf_put64(&out, (align)); } while (0)

    uint64_t rx_start = out_va[OUT_TEXT];
    uint64_t rx_end = plt_va + plt_sz;
    /* R segment covers input .rodata through the synthesized dynamic metadata.
     * Start at min(.rodata VA, synthesized hash VA) to avoid gaps. */
    uint64_t r_start = out_va[OUT_RODATA] < r_off ? out_va[OUT_RODATA] : r_off;
    uint64_t r_end = relaplt_va + relaplt_sz;
    /* RW starts after the R region to avoid overlap. */
    uint64_t rw_start = r_end > out_va[OUT_DATA] ? r_end : out_va[OUT_DATA];
    rw_start = (rw_start + 0xfff) & ~0xfffull;  /* page-align */
    uint64_t rw_file_end = rw_end;                     /* dynamic sits last, file-backed */
    PH(PT_LOAD, PF_R | PF_X, 0x1000 + rx_start, rx_start, rx_end - rx_start, rx_end - rx_start, 0x1000);
    PH(PT_LOAD, PF_R,        0x1000 + r_start,  r_start,  r_end - r_start,   r_end - r_start,   0x1000);
    PH(PT_LOAD, PF_R | PF_W, 0x1000 + rw_start, rw_start, rw_file_end - rw_start, (bss_va + bss_memsz > rw_file_end ? bss_va + bss_memsz : rw_file_end) - rw_start, 0x1000);
    PH(PT_DYNAMIC, PF_R | PF_W, 0x1000 + dynamic_va, dynamic_va, dynamic_sz, dynamic_sz, 8);
    PH(PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0, 0);

    /* helper: write bytes at file offset 0x1000 + vaddr (forward-only) */
    #define EMIT_AT(vaddr, ptr, nbytes) do { long _o = (long)(0x1000 + (vaddr)); if (out.len < _o) buf_zero(&out, _o - out.len); buf_put(&out, (ptr), (long)(nbytes)); } while (0)

    /* Collect all emit targets, sort by VA, emit in order to avoid
     * forward-only EMIT_AT clobbering.  Input sections + synthesized. */
    typedef struct { uint64_t va; const uint8_t *data; uint64_t size; } EmitTarget;
    EmitTarget *targets = NULL; int ntarget = 0, tcap = 0;
    #define PUSH_ET(v, d, s) do { if (ntarget == tcap) { tcap = tcap ? tcap*2 : 64; targets = realloc(targets, sizeof(EmitTarget)*tcap); } targets[ntarget++] = (EmitTarget){(v),(d),(s)}; } while (0)

    for (int slot = 0; slot < OUT_COUNT; slot++) {
        if (slot == OUT_BSS) continue;
        for (int oi = 0; oi < lk.nobj; oi++) {
            Obj *o = lk.objs[oi];
            for (int si = 0; si < o->nsec; si++) {
                Sec *s = &o->secs[si];
                if (s->out != slot || !(s->flags & SHF_ALLOC) || s->type == SHT_NOBITS || !s->data) continue;
                PUSH_ET(s->va, s->data, s->size);
            }
        }
    }
    if (plt_sz) PUSH_ET(plt_va, pltb.data, pltb.len);
    if (hashb.len) PUSH_ET(hash_va, hashb.data, hashb.len);
    PUSH_ET(dynsym_va, dsymb.data, dsymb.len);
    PUSH_ET(dynstr_va, dynstr.data, dynstr.len);
    if (reladynb.len) PUSH_ET(reladyn_va, reladynb.data, reladynb.len);
    if (relapltb.len) PUSH_ET(relaplt_va, relapltb.data, relapltb.len);
    { uint64_t gz = (uint64_t)(ngot + nplt) * 8; if (gz) { uint8_t *zeros = (uint8_t *)arena_alloc(arena, (size_t)gz); memset(zeros, 0, (size_t)gz); PUSH_ET(got_va, zeros, gz); } }
    PUSH_ET(dynamic_va, dynb.data, dynb.len);

    /* Sort emit targets by VA */
    for (int i = 1; i < ntarget; i++) {
        EmitTarget key = targets[i];
        int j = i - 1;
        while (j >= 0 && targets[j].va > key.va) { targets[j+1] = targets[j]; j--; }
        targets[j+1] = key;
    }
    for (int i = 0; i < ntarget; i++) EMIT_AT(targets[i].va, targets[i].data, targets[i].size);
    free(targets);

    FILE *of = fopen(req->out_path, "wb");
    if (!of) { diagnostics_fatal("linker: cannot open output"); }
    fwrite(out.data, 1, (size_t)out.len, of);
    fclose(of);
    free(out.data);
    chmod(req->out_path, req->mode == LINK_SHARED ? 0644 : 0755);
    return 0;
}
