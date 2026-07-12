 * i386 instruction encoder and ELF32 writer
 *
 * This covers the exact GNU AS dialect emitted by backend_i386.c. It mirrors
 * the x86_64 encoder above but emits 32-bit encodings and SHT_REL relocations.
 * ========================================================================= */

#define EHDR32_SIZE 52
#define SHDR32_SIZE 40
#define SYM32_SIZE  16

static const char *parse_symbol_name(const char *p, char *out, size_t out_size) {
    size_t n = 0;
    while (*p && *p != ',' && *p != '\n' && *p != ' ' && *p != '\t' && *p != '(' && n + 1 < out_size) {
        out[n++] = *p++;
    }
    out[n] = 0;
    return p;
}

static void i386_emit_modrm_mem(ByteBuf *out, int reg, const Operand *mem, uint8_t op,
                                RelocArr *relocs, int reloc_type) {
    if (op != 0) bb_write8(out, op);

    if (mem->type == OT_SYM) {
        bb_write8(out, modrm_r0(reg & 7, 5));
        ElfReloc rel = {0};
        strncpy(rel.sym_name, mem->sym, sizeof(rel.sym_name) - 1);
        rel.offset = out->size;
        rel.type = reloc_type;
        rel.addend = 0;
        relocarr_push(relocs, &rel);
        bb_write32le(out, 0);
        return;
    }

    if (mem->type == OT_MEM_IDX) {
        int scale_log2 = (mem->scale == 2) ? 1 : (mem->scale == 4) ? 2 : (mem->scale == 8) ? 3 : 0;
        bb_write8(out, (uint8_t)(0x00 | ((reg & 7) << 3) | 0x04));
        bb_write8(out, sib_byte(scale_log2, mem->idx, mem->base));
        return;
    }

    int base = mem->base;
    int rb = base & 7;
    int32_t disp = (int32_t)mem->imm;

    /* GOT-relative: symbol@GOT(%reg) — use R_386_GOT32 relocation */
    if (mem->is_gotpcrel && mem->sym[0]) {
        bb_write8(out, modrm_r0(reg & 7, rb));
        ElfReloc rel = {0};
        strncpy(rel.sym_name, mem->sym, sizeof(rel.sym_name) - 1);
        rel.offset = out->size;
        rel.type = R_386_GOT32;
        rel.addend = -4;
        relocarr_push(relocs, &rel);
        bb_write32le(out, 0);
        return;
    }

    if (disp == 0 && rb != 5) {
        bb_write8(out, modrm_r0(reg & 7, rb));
        if (rb == 4) bb_write8(out, 0x24);
    } else if (disp >= -128 && disp <= 127) {
        bb_write8(out, modrm_r8(reg & 7, rb));
        if (rb == 4) bb_write8(out, 0x24);
        bb_write8(out, (uint8_t)(int8_t)disp);
    } else {
        bb_write8(out, modrm_r32(reg & 7, rb));
        if (rb == 4) bb_write8(out, 0x24);
        bb_write32le(out, (uint32_t)disp);
    }
}

static Operand parse_operand_i386(const char **pp) {
    Operand op = {0};
    op.type = OT_NONE;
    const char *p = skip_ws(*pp);

    if (*p == '$') {
        p++;
        op.type = OT_IMM;
        if (*p == '-' || isdigit((unsigned char)*p)) {
            char buf[64];
            int n = 0;
            if (*p == '-') buf[n++] = *p++;
            while (*p && *p != ',' && *p != '\n' && *p != ' ' && *p != '\t' && n < 63)
                buf[n++] = *p++;
            buf[n] = 0;
            op.imm = parse_int(buf);
        } else {
            p = parse_symbol_name(p, op.sym, sizeof(op.sym));
        }
        *pp = p;
        return op;
    }

    if (*p == '%') {
        char reg[16];
        int n = 0;
        while (*p && *p != ',' && *p != ')' && *p != '\n' && *p != ' ' && *p != '\t' && n < 15)
            reg[n++] = *p++;
        reg[n] = 0;
        op.type = reg_optype(reg);
        op.reg = x64_reg_num(reg);
        *pp = p;
        return op;
    }

    if (*p == '(') {
        p++;
        char base[16]; int nb = 0;
        while (*p && *p != ',' && *p != ')' && nb < 15) base[nb++] = *p++;
        base[nb] = 0;
        op.base = x64_reg_num(base);
        if (*p == ',') {
            p++;
            char idx[16]; int ni = 0;
            while (*p && *p != ',' && *p != ')' && ni < 15) idx[ni++] = *p++;
            idx[ni] = 0;
            op.idx = x64_reg_num(idx);
            if (*p == ',') {
                p++;
                op.scale = (int)strtol(p, (char **)&p, 10);
            }
        }
        if (*p == ')') p++;
        op.type = (op.scale != 0) ? OT_MEM_IDX : OT_MEM_INDIR;
        *pp = p;
        return op;
    }

    if (*p == '-' || isdigit((unsigned char)*p)) {
        char buf[64];
        int n = 0;
        if (*p == '-') buf[n++] = *p++;
        while (isdigit((unsigned char)*p) && n < 63) buf[n++] = *p++;
        buf[n] = 0;
        int64_t disp = parse_int(buf);
        if (*p == '(') {
            p++;
            char base[16]; int nb = 0;
            while (*p && *p != ')' && *p != ',' && nb < 15) base[nb++] = *p++;
            base[nb] = 0;
            if (*p == ')') p++;
            op.type = OT_MEM_BASE;
            op.base = x64_reg_num(base);
            op.imm = disp;
            *pp = p;
            return op;
        }
        op.type = OT_IMM;
        op.imm = disp;
        *pp = p;
        return op;
    }

    if (*p && *p != ',' && *p != '\n') {
        p = parse_symbol_name(p, op.sym, sizeof(op.sym));
        const char *got_suffix = strstr(op.sym, "@GOT");
        if (got_suffix && *p == '(') {
            /* symbol@GOT(%reg) — GOT-relative addressing */
            int sym_len = (int)(got_suffix - op.sym);
            op.sym[sym_len] = 0;
            op.is_gotpcrel = 1;
            p++;
            char base[16]; int nb = 0;
            while (*p && *p != ')' && nb < 15) base[nb++] = *p++;
            base[nb] = 0;
            op.base = x64_reg_num(base);
            if (*p == ')') p++;
            op.type = OT_MEM_BASE;
            op.imm = 0;
        } else if (got_suffix) {
            /* plain symbol@GOT without (reg) — strip suffix, keep as symbol */
            int sym_len = (int)(got_suffix - op.sym);
            op.sym[sym_len] = 0;
            op.type = OT_SYM;
            op.is_gotpcrel = 1;
        } else {
            op.type = OT_SYM;
        }
        *pp = p;
        return op;
    }

    *pp = p;
    return op;
}

static void i386_emit_group1(ByteBuf *text, uint8_t op_rr, int slash, Operand src, Operand dst) {
    if (src.type == OT_IMM && dst.type == OT_REG32) {
        if (src.imm >= -128 && src.imm <= 127) {
            bb_write8(text, 0x83);
            bb_write8(text, modrm_rr(slash, dst.reg));
            bb_write8(text, (uint8_t)(int8_t)src.imm);
        } else {
            bb_write8(text, 0x81);
            bb_write8(text, modrm_rr(slash, dst.reg));
            bb_write32le(text, (uint32_t)(int32_t)src.imm);
        }
    } else if (src.type == OT_REG32 && dst.type == OT_REG32) {
        bb_write8(text, op_rr);
        bb_write8(text, modrm_rr(src.reg, dst.reg));
    }
}

static int i386_encode_line(const char *line, ByteBuf *text, EncCtx *ctx,
                            size_t fn_start, RelocArr *relocs) {
    const char *p = skip_ws(line);
    if (!*p || *p == '\n' || *p == '#') return 0;
    if (*p == '.') return 0;

    {
        const char *q = p;
        char lname[256]; int ln = 0;
        while (*q && *q != ':' && *q != ' ' && *q != '\n' && ln < 255)
            lname[ln++] = *q++;
        lname[ln] = 0;
        if (*q == ':') {
            ctx_add_label(ctx, lname, (int32_t)(text->size - fn_start));
            for (int i = 0; i < ctx->patch_count; i++) {
                if (strcmp(ctx->patches[i].label, lname) == 0) {
                    size_t off = ctx->patches[i].patch_off;
                    int32_t target = (int32_t)(text->size - fn_start);
                    int32_t disp = target - (int32_t)(off - fn_start + 4);
                    bb_patch32le(text, off, (uint32_t)disp);
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

    if (strcmp(mnem, "pushl") == 0) {
        Operand op = parse_operand_i386(&p);
        if (op.type == OT_REG32) {
            bb_write8(text, (uint8_t)(0x50 | (op.reg & 7)));
        } else if (op.type == OT_IMM && op.sym[0]) {
            bb_write8(text, 0x68);
            ElfReloc rel = {0};
            strncpy(rel.sym_name, op.sym, sizeof(rel.sym_name) - 1);
            rel.offset = text->size;
            rel.type = R_386_32;
            relocarr_push(relocs, &rel);
            bb_write32le(text, 0);
        } else if (op.type == OT_IMM) {
            if (op.imm >= -128 && op.imm <= 127) {
                bb_write8(text, 0x6A);
                bb_write8(text, (uint8_t)(int8_t)op.imm);
            } else {
                bb_write8(text, 0x68);
                bb_write32le(text, (uint32_t)(int32_t)op.imm);
            }
        }
        return 1;
    }

    if (strcmp(mnem, "popl") == 0) {
        Operand op = parse_operand_i386(&p);
        bb_write8(text, (uint8_t)(0x58 | (op.reg & 7)));
        return 1;
    }

    if (strcmp(mnem, "leave") == 0) { bb_write8(text, 0xC9); return 1; }
    if (strcmp(mnem, "ret") == 0) {
        p = skip_ws(p);
        if (*p == '$') {
            Operand imm = parse_operand_i386(&p);
            bb_write8(text, 0xC2);
            bb_write16le(text, (uint16_t)imm.imm);
        } else {
            bb_write8(text, 0xC3);
        }
        return 1;
    }
    if (strcmp(mnem, "cltd") == 0) { bb_write8(text, 0x99); return 1; }

    if (strcmp(mnem, "movl") == 0 || strcmp(mnem, "movw") == 0 || strcmp(mnem, "movb") == 0) {
        int is_word = strcmp(mnem, "movw") == 0;
        int is_byte = strcmp(mnem, "movb") == 0;
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        if (is_word) bb_write8(text, 0x66);

        if (src.type == OT_IMM && dst.type == OT_REG32 && !is_byte && !is_word) {
            bb_write8(text, (uint8_t)(0xB8 | (dst.reg & 7)));
            if (src.sym[0]) {
                ElfReloc rel = {0};
                strncpy(rel.sym_name, src.sym, sizeof(rel.sym_name) - 1);
                rel.offset = text->size;
                rel.type = R_386_32;
                relocarr_push(relocs, &rel);
                bb_write32le(text, 0);
            } else {
                bb_write32le(text, (uint32_t)(int32_t)src.imm);
            }
            return 1;
        }
        if ((src.type == OT_REG32 || src.type == OT_REG16 || src.type == OT_REG8) &&
            (dst.type == OT_REG32 || dst.type == OT_REG16 || dst.type == OT_REG8)) {
            bb_write8(text, is_byte ? 0x88 : 0x89);
            bb_write8(text, modrm_rr(src.reg, dst.reg));
            return 1;
        }
        if ((src.type == OT_REG32 || src.type == OT_REG16 || src.type == OT_REG8) &&
            (dst.type == OT_MEM_BASE || dst.type == OT_MEM_INDIR || dst.type == OT_MEM_IDX || dst.type == OT_SYM)) {
            i386_emit_modrm_mem(text, src.reg, &dst, is_byte ? 0x88 : 0x89, relocs, R_386_32);
            return 1;
        }
        if ((src.type == OT_MEM_BASE || src.type == OT_MEM_INDIR || src.type == OT_MEM_IDX || src.type == OT_SYM) &&
            (dst.type == OT_REG32 || dst.type == OT_REG16 || dst.type == OT_REG8)) {
            i386_emit_modrm_mem(text, dst.reg, &src, is_byte ? 0x8A : 0x8B, relocs, R_386_32);
            return 1;
        }
        diagnostics_fatal("elf_writer: unsupported i386 mov form");
    }

    if (strcmp(mnem, "movsbl") == 0 || strcmp(mnem, "movswl") == 0 || strcmp(mnem, "movzbl") == 0) {
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        if (strcmp(mnem, "movswl") == 0) {
            bb_write8(text, 0x0F); bb_write8(text, 0xBF);
        } else {
            bb_write8(text, 0x0F); bb_write8(text, strcmp(mnem, "movzbl") == 0 ? 0xB6 : 0xBE);
        }
        if (src.type == OT_REG8 || src.type == OT_REG16 || src.type == OT_REG32) {
            bb_write8(text, modrm_rr(dst.reg, src.reg));
        } else {
            i386_emit_modrm_mem(text, dst.reg, &src, 0, relocs, R_386_32);
        }
        return 1;
    }

    if (strcmp(mnem, "addl") == 0 || strcmp(mnem, "subl") == 0 ||
        strcmp(mnem, "andl") == 0 || strcmp(mnem, "orl") == 0 || strcmp(mnem, "xorl") == 0 ||
        strcmp(mnem, "cmpl") == 0) {
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        int slash = strcmp(mnem, "addl") == 0 ? 0 : strcmp(mnem, "orl") == 0 ? 1 :
                    strcmp(mnem, "andl") == 0 ? 4 : strcmp(mnem, "subl") == 0 ? 5 :
                    strcmp(mnem, "xorl") == 0 ? 6 : 7;
        uint8_t op_rr = strcmp(mnem, "addl") == 0 ? 0x01 : strcmp(mnem, "orl") == 0 ? 0x09 :
                        strcmp(mnem, "andl") == 0 ? 0x21 : strcmp(mnem, "subl") == 0 ? 0x29 :
                        strcmp(mnem, "xorl") == 0 ? 0x31 : 0x39;
        i386_emit_group1(text, op_rr, slash, src, dst);
        return 1;
    }

    if (strcmp(mnem, "imull") == 0) {
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        bb_write8(text, 0x0F); bb_write8(text, 0xAF); bb_write8(text, modrm_rr(dst.reg, src.reg));
        return 1;
    }
    if (strcmp(mnem, "idivl") == 0) {
        Operand op = parse_operand_i386(&p);
        bb_write8(text, 0xF7); bb_write8(text, modrm_rr(7, op.reg));
        return 1;
    }
    if (strcmp(mnem, "notl") == 0 || strcmp(mnem, "negl") == 0) {
        Operand op = parse_operand_i386(&p);
        bb_write8(text, 0xF7); bb_write8(text, modrm_rr(strcmp(mnem, "notl") == 0 ? 2 : 3, op.reg));
        return 1;
    }
    if (strcmp(mnem, "shll") == 0 || strcmp(mnem, "sarl") == 0 || strcmp(mnem, "shrl") == 0) {
        int slash = strcmp(mnem, "shll") == 0 ? 4 : strcmp(mnem, "shrl") == 0 ? 5 : 7;
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        if (src.type == OT_IMM) {
            bb_write8(text, src.imm == 1 ? 0xD1 : 0xC1);
            bb_write8(text, modrm_rr(slash, dst.reg));
            if (src.imm != 1) bb_write8(text, (uint8_t)src.imm);
        } else {
            bb_write8(text, 0xD3);
            bb_write8(text, modrm_rr(slash, dst.reg));
        }
        return 1;
    }

    if (strncmp(mnem, "set", 3) == 0) {
        uint8_t op2;
        if      (strcmp(mnem+3, "e")   == 0) op2 = 0x94;
        else if (strcmp(mnem+3, "ne")  == 0) op2 = 0x95;
        else if (strcmp(mnem+3, "l")   == 0) op2 = 0x9C;
        else if (strcmp(mnem+3, "g")   == 0) op2 = 0x9F;
        else if (strcmp(mnem+3, "le")  == 0) op2 = 0x9E;
        else if (strcmp(mnem+3, "ge")  == 0) op2 = 0x9D;
        else if (strcmp(mnem+3, "b")   == 0) op2 = 0x92;
        else if (strcmp(mnem+3, "a")   == 0) op2 = 0x97;
        else if (strcmp(mnem+3, "be")  == 0) op2 = 0x96;
        else                                  op2 = 0x93;
        Operand op = parse_operand_i386(&p);
        bb_write8(text, 0x0F); bb_write8(text, op2); bb_write8(text, modrm_rr(0, op.reg));
        return 1;
    }

    if (strcmp(mnem, "leal") == 0) {
        Operand src = parse_operand_i386(&p);
        if (*p == ',') p++;
        Operand dst = parse_operand_i386(&p);
        i386_emit_modrm_mem(text, dst.reg, &src, 0x8D, relocs, R_386_32);
        return 1;
    }

    if (strcmp(mnem, "call") == 0) {
        if (*p == '*') {
            p++;
            Operand op = parse_operand_i386(&p);
            bb_write8(text, 0xFF); bb_write8(text, modrm_rr(2, op.reg));
        } else {
            char sym[256];
            parse_symbol_name(p, sym, sizeof(sym));
            bb_write8(text, 0xE8);
            ElfReloc rel = {0};
            int use_plt = 0;
            const char *plt = strstr(sym, "@PLT");
            if (plt) {
                int sym_len = (int)(plt - sym);
                strncpy(rel.sym_name, sym, sym_len > 255 ? 255 : (size_t)sym_len);
                rel.sym_name[sym_len] = 0;
                use_plt = 1;
            } else {
                strncpy(rel.sym_name, sym, sizeof(rel.sym_name) - 1);
            }
            rel.offset = text->size;
            rel.type = use_plt ? R_386_PLT32 : R_386_PC32;
            relocarr_push(relocs, &rel);
            bb_write32le(text, 0);
        }
        return 1;
    }

    if (strcmp(mnem, "je") == 0 || strcmp(mnem, "jmp") == 0) {
        char label[64]; int nl = 0;
        while (*p && *p != '\n' && *p != ' ' && *p != '\t' && nl < 63) label[nl++] = *p++;
        label[nl] = 0;
        int is_je = strcmp(mnem, "je") == 0;
        int32_t target_off = ctx_find_label(ctx, label);
        if (is_je) { bb_write8(text, 0x0F); bb_write8(text, 0x84); }
        else bb_write8(text, 0xE9);
        if (target_off >= 0) {
            int32_t here = (int32_t)(text->size - fn_start + 4);
            bb_write32le(text, (uint32_t)(target_off - here));
        } else {
            ctx_add_patch(ctx, text->size, label);
            bb_write32le(text, 0);
        }
        return 1;
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "elf_writer: unsupported i386 instruction %s", mnem);
    diagnostics_fatal(msg);
    return 0;
}

static void parse_asm_i386(const char *asm_text, const char *src_path, AsmModel *m) {
    (void)src_path;
    SectionId cur_sec = SEC_NONE;
    const char *p = asm_text;
    EncCtx ctx; ctx_init(&ctx);
    size_t fn_start = 0;
    int in_function = 0;
    char pending_globl[256] = "";
    char pending_type_func[256] = "";

    while (*p) {
        const char *line_start = p;
        const char *line_end = p;
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
        if (strcmp(lp, ".section .rodata") == 0 || strcmp(lp, ".rodata") == 0 ||
            strncmp(lp, ".section\t.rodata", 16) == 0 || strncmp(lp, ".section .rodata", 16) == 0) {
            cur_sec = SEC_RODATA; continue;
        }
        if (strcmp(lp, ".bss") == 0) { cur_sec = SEC_BSS; continue; }
        if (strncmp(lp, ".globl ", 7) == 0 || strncmp(lp, ".global ", 8) == 0) {
            const char *s = lp + (lp[6] == ' ' ? 7 : 8);
            strncpy(pending_globl, skip_ws(s), sizeof(pending_globl) - 1);
            continue;
        }
        if (strncmp(lp, ".type ", 6) == 0) {
            const char *s = skip_ws(lp + 6);
            char tname[256]; int tn = 0;
            while (*s && *s != ',' && tn < 255) tname[tn++] = *s++;
            tname[tn] = 0;
            if (strstr(s, "@function")) strncpy(pending_type_func, tname, sizeof(pending_type_func) - 1);
            continue;
        }

        if (*lp == '.') {
            const char *q = lp;
            char lname[256]; int ln = 0;
            while (*q && *q != ':' && *q != ' ' && *q != '\t' && ln < 255) lname[ln++] = *q++;
            lname[ln] = 0;
            if (*q == ':') {
                if (in_function && cur_sec == SEC_TEXT) {
                    ctx_add_label(&ctx, lname, (int32_t)(m->text.size - fn_start));
                    for (int i = 0; i < ctx.patch_count; i++) {
                        if (strcmp(ctx.patches[i].label, lname) == 0) {
                            size_t off = ctx.patches[i].patch_off;
                            int32_t target = (int32_t)(m->text.size - fn_start);
                            int32_t disp = target - (int32_t)(off - fn_start + 4);
                            bb_patch32le(&m->text, off, (uint32_t)disp);
                        }
                    }
                } else if (cur_sec == SEC_DATA || cur_sec == SEC_RODATA) {
                    ByteBuf *sec_buf = (cur_sec == SEC_DATA) ? &m->data : &m->rodata;
                    ElfSym sym = {0};
                    strncpy(sym.name, lname, sizeof(sym.name) - 1);
                    sym.shndx = (cur_sec == SEC_DATA) ? 3 : 4;
                    sym.value = (uint64_t)sec_buf->size;
                    sym.bind = (strlen(pending_globl) > 0 && strcmp(lname, pending_globl) == 0) ? STB_GLOBAL : STB_LOCAL;
                    sym.type = STT_OBJECT;
                    sym.defined = 1;
                    symarr_push(&m->syms, &sym);
                }
                continue;
            }
        }

        if (*lp == '.') {
            if (strncmp(lp, ".file ", 6) == 0) {
                const char *s = skip_ws(lp + 6);
                int id = (int)strtol(s, (char**)&s, 10);
                s = skip_ws(s);
                char path[256] = "";
                if (*s == '"') {
                    s++;
                    int pi = 0;
                    while (*s && *s != '"' && pi < 255) path[pi++] = *s++;
                    path[pi] = 0;
                }
                if (m->file_count == m->file_cap) {
                    m->file_cap = m->file_cap ? m->file_cap * 2 : 4;
                    m->files = realloc(m->files, sizeof(FileEntry) * m->file_cap);
                }
                m->files[m->file_count].id = id;
                strncpy(m->files[m->file_count].path, path, 255);
                m->file_count++;
            } else if (strncmp(lp, ".loc ", 5) == 0) {
                const char *s = skip_ws(lp + 5);
                int file_id = (int)strtol(s, (char**)&s, 10);
                s = skip_ws(s);
                int line = (int)strtol(s, (char**)&s, 10);
                s = skip_ws(s);
                int col = (int)strtol(s, (char**)&s, 10);
                if (cur_sec == SEC_TEXT) {
                    if (m->loc_count == m->loc_cap) {
                        m->loc_cap = m->loc_cap ? m->loc_cap * 2 : 32;
                        m->locs = realloc(m->locs, sizeof(LocEntry) * m->loc_cap);
                    }
                    m->locs[m->loc_count].file_id = file_id;
                    m->locs[m->loc_count].line = line;
                    m->locs[m->loc_count].col = col;
                    m->locs[m->loc_count].offset = m->text.size;
                    m->loc_count++;
                }
            } else if (cur_sec == SEC_DATA || cur_sec == SEC_RODATA) {
                ByteBuf *sec_buf = (cur_sec == SEC_DATA) ? &m->data : &m->rodata;
                if (strncmp(lp, ".byte ", 6) == 0) bb_write8(sec_buf, (uint8_t)(parse_int(skip_ws(lp + 6)) & 0xff));
                else if (strncmp(lp, ".short ", 7) == 0) bb_write16le(sec_buf, (uint16_t)(parse_int(skip_ws(lp + 7)) & 0xffff));
                else if (strncmp(lp, ".long ", 6) == 0) bb_write32le(sec_buf, (uint32_t)(int32_t)parse_int(skip_ws(lp + 6)));
                else if (strncmp(lp, ".quad ", 6) == 0) bb_write64le(sec_buf, (uint64_t)parse_int(skip_ws(lp + 6)));
                else if (strncmp(lp, ".zero ", 6) == 0) {
                    int64_t n = parse_int(skip_ws(lp + 6));
                    for (int64_t i = 0; i < n; i++) bb_write8(sec_buf, 0);
                } else if (strncmp(lp, ".asciz ", 7) == 0) {
                    const char *s = skip_ws(lp + 7);
                    if (*s == '"') s++;
                    while (*s && *s != '"') {
                        if (*s == '\\' && *(s + 1)) {
                            s++;
                            switch (*s) {
                            case 'n': bb_write8(sec_buf, '\n'); break;
                            case 't': bb_write8(sec_buf, '\t'); break;
                            case 'r': bb_write8(sec_buf, '\r'); break;
                            case '0': bb_write8(sec_buf, '\0'); break;
                            case '\\': bb_write8(sec_buf, '\\'); break;
                            case '"': bb_write8(sec_buf, '"'); break;
                            default: bb_write8(sec_buf, *s); break;
                            }
                        } else {
                            bb_write8(sec_buf, (uint8_t)*s);
                        }
                        s++;
                    }
                    bb_write8(sec_buf, 0);
                } else if (strncmp(lp, ".p2align ", 9) == 0) {
                    bb_align(sec_buf, (size_t)1 << (int)parse_int(skip_ws(lp + 9)));
                }
            } else if (cur_sec == SEC_BSS && strncmp(lp, ".zero ", 6) == 0) {
                m->bss_size += (size_t)parse_int(skip_ws(lp + 6));
            }
            continue;
        }

        const char *q = lp;
        char lname[256]; int ln = 0;
        while (*q && *q != ':' && *q != ' ' && *q != '\t' && ln < 255) lname[ln++] = *q++;
        lname[ln] = 0;
        if (*q == ':') {
            if (strcmp(lname, pending_type_func) == 0) {
                if (in_function) { ctx_free(&ctx); ctx_init(&ctx); }
                in_function = 1;
                fn_start = m->text.size;
                ElfSym sym = {0};
                strncpy(sym.name, lname, sizeof(sym.name) - 1);
                sym.value = (uint64_t)m->text.size;
                sym.bind = (strlen(pending_globl) > 0 && strcmp(lname, pending_globl) == 0) ? STB_GLOBAL : STB_LOCAL;
                sym.type = STT_FUNC;
                sym.defined = 1;
                symarr_push(&m->syms, &sym);
                pending_type_func[0] = 0;
            } else if (in_function && cur_sec == SEC_TEXT) {
                i386_encode_line(line, &m->text, &ctx, fn_start, &m->text_relocs);
            } else if (cur_sec == SEC_DATA || cur_sec == SEC_RODATA) {
                ByteBuf *sec_buf = (cur_sec == SEC_DATA) ? &m->data : &m->rodata;
                ElfSym sym = {0};
                strncpy(sym.name, lname, sizeof(sym.name) - 1);
                sym.shndx = (cur_sec == SEC_DATA) ? 3 : 4;
                sym.value = (uint64_t)sec_buf->size;
                sym.bind = (strlen(pending_globl) > 0 && strcmp(lname, pending_globl) == 0) ? STB_GLOBAL : STB_LOCAL;
                sym.type = STT_OBJECT;
                sym.defined = 1;
                symarr_push(&m->syms, &sym);
            }
            continue;
        }

        if (cur_sec == SEC_TEXT && in_function) {
            i386_encode_line(line, &m->text, &ctx, fn_start, &m->text_relocs);
        }
    }

    if (in_function) {
        for (int i = m->syms.count - 1; i >= 0; i--) {
            if (m->syms.data[i].type == STT_FUNC && m->syms.data[i].size == 0) {
                m->syms.data[i].size = (uint32_t)(m->text.size - m->syms.data[i].value);
                break;
            }
        }
        ctx_free(&ctx);
    }
}

static int sym_index32(AsmModel *m, const char *name, int symtab_info) {
    int sym_num = 1;
    for (int j = 0; j < m->syms.count; j++) {
        if (m->syms.data[j].bind != STB_LOCAL) continue;
        if (strcmp(m->syms.data[j].name, name) == 0) return sym_num;
        sym_num++;
    }
    sym_num = symtab_info;
    for (int j = 0; j < m->syms.count; j++) {
        if (m->syms.data[j].bind != STB_GLOBAL) continue;
        if (strcmp(m->syms.data[j].name, name) == 0) return sym_num;
        sym_num++;
    }
    return 0;
}

static void write_elf32_object(AsmModel *m, ByteBuf *out) {
    int shidx_text = 1;
    int next_shidx = 2;
    int shidx_data = (m->data.size > 0) ? next_shidx++ : -1;
    int shidx_rodata = (m->rodata.size > 0) ? next_shidx++ : -1;
    int shidx_bss = (m->bss_size > 0) ? next_shidx++ : -1;
    int shidx_rel = (m->text_relocs.count > 0) ? next_shidx++ : -1;
    int shidx_symtab = next_shidx++;
    int shidx_strtab = next_shidx++;
    int shidx_shstr = next_shidx++;
    int num_sections = next_shidx;

    for (int i = 0; i < m->syms.count; i++) {
        ElfSym *s = &m->syms.data[i];
        if (!s->defined) continue;
        if (s->type == STT_FUNC) s->shndx = (uint32_t)shidx_text;
        else if (s->type == STT_OBJECT) {
            if (s->shndx == 3) s->shndx = (shidx_data >= 0) ? (uint32_t)shidx_data : SHN_UNDEF;
            else if (s->shndx == 4) s->shndx = (shidx_rodata >= 0) ? (uint32_t)shidx_rodata : SHN_UNDEF;
        }
    }

    Strtab strtab;
    strtab_init(&strtab);

    Strtab shstrtab;
    strtab_init(&shstrtab);
    uint32_t sh_null_name   = 0;
    uint32_t sh_text_name   = strtab_add(&shstrtab, ".text");
    uint32_t sh_data_name   = (shidx_data >= 0) ? strtab_add(&shstrtab, ".data") : 0;
    uint32_t sh_rodata_name = (shidx_rodata >= 0) ? strtab_add(&shstrtab, ".rodata") : 0;
    uint32_t sh_bss_name    = (shidx_bss >= 0) ? strtab_add(&shstrtab, ".bss") : 0;
    uint32_t sh_rel_name    = (shidx_rel >= 0) ? strtab_add(&shstrtab, ".rel.text") : 0;
    uint32_t sh_symtab_name = strtab_add(&shstrtab, ".symtab");
    uint32_t sh_strtab_name = strtab_add(&shstrtab, ".strtab");
    uint32_t sh_shstr_name  = strtab_add(&shstrtab, ".shstrtab");

    /* Build simple symtab: null + defined locals + globals */
    ByteBuf symtab_buf;
    bb_init(&symtab_buf);
    for (int i = 0; i < SYM32_SIZE; i++) bb_write8(&symtab_buf, 0); /* null */
    int symtab_info = 1;

    for (int i = 0; i < m->syms.count; i++) {
        ElfSym *s = &m->syms.data[i];
        if (s->bind != STB_LOCAL) continue;
        uint32_t no = strtab_add(&strtab, s->name);
        bb_write32le(&symtab_buf, no);           /* st_name */
        bb_write32le(&symtab_buf, (uint32_t)s->value); /* st_value */
        bb_write32le(&symtab_buf, s->size);      /* st_size */
        bb_write8(&symtab_buf, (uint8_t)((s->bind << 4) | (s->type & 0xF)));
        bb_write8(&symtab_buf, STV_DEFAULT);
        bb_write16le(&symtab_buf, (uint16_t)(s->defined ? s->shndx : SHN_UNDEF));
        symtab_info++;
    }
    for (int i = 0; i < m->syms.count; i++) {
        ElfSym *s = &m->syms.data[i];
        if (s->bind != STB_GLOBAL) continue;
        uint32_t no = strtab_add(&strtab, s->name);
        bb_write32le(&symtab_buf, no);
        bb_write32le(&symtab_buf, (uint32_t)s->value);
        bb_write32le(&symtab_buf, s->size);
        bb_write8(&symtab_buf, (uint8_t)((s->bind << 4) | (s->type & 0xF)));
        bb_write8(&symtab_buf, STV_DEFAULT);
        bb_write16le(&symtab_buf, (uint16_t)(s->defined ? s->shndx : SHN_UNDEF));
    }

    for (int i = 0; i < m->text_relocs.count; i++) {
        const char *rname = m->text_relocs.data[i].sym_name;
        int found = 0;
        for (int j = 0; j < m->syms.count; j++) {
            if (strcmp(m->syms.data[j].name, rname) == 0) { found = 1; break; }
        }
        if (!found) {
            uint32_t no = strtab_add(&strtab, rname);
            bb_write32le(&symtab_buf, no);
            bb_write32le(&symtab_buf, 0);
            bb_write32le(&symtab_buf, 0);
            bb_write8(&symtab_buf, (uint8_t)((STB_GLOBAL << 4) | STT_NOTYPE));
            bb_write8(&symtab_buf, STV_DEFAULT);
            bb_write16le(&symtab_buf, SHN_UNDEF);
            ElfSym us = {0};
            strncpy(us.name, rname, sizeof(us.name) - 1);
            us.bind = STB_GLOBAL;
            us.type = STT_NOTYPE;
            us.defined = 0;
            symarr_push(&m->syms, &us);
        }
    }

    ByteBuf rel_buf;
    bb_init(&rel_buf);
    for (int i = 0; i < m->text_relocs.count; i++) {
        ElfReloc *r = &m->text_relocs.data[i];
        int sym_idx = sym_index32(m, r->sym_name, symtab_info);
        bb_write32le(&rel_buf, (uint32_t)r->offset);
        bb_write32le(&rel_buf, ((uint32_t)sym_idx << 8) | (uint32_t)(r->type & 0xff));
    }

    size_t off = EHDR32_SIZE;
    size_t text_off = off; off += m->text.size;
    off = (off + 3) & ~(size_t)3;
    size_t data_off = off; if (shidx_data >= 0) off += m->data.size;
    off = (off + 3) & ~(size_t)3;
    size_t rodata_off = off; if (shidx_rodata >= 0) off += m->rodata.size;
    off = (off + 3) & ~(size_t)3;
    size_t rel_off = off; if (shidx_rel >= 0) off += rel_buf.size;
    off = (off + 3) & ~(size_t)3;
    size_t symtab_off  = off; off += symtab_buf.size;
    size_t strtab_off  = off; off += strtab.buf.size;
    size_t shstr_off   = off; off += shstrtab.buf.size;
    off = (off + 3) & ~(size_t)3;
    size_t shdr_off    = off;

    /* ELF32 header */
    bb_write8(out, ELF_MAG0); bb_write8(out, ELF_MAG1);
    bb_write8(out, ELF_MAG2); bb_write8(out, ELF_MAG3);
    bb_write8(out, ELFCLASS32);
    bb_write8(out, ELFDATA2LSB);
    bb_write8(out, EV_CURRENT);
    bb_write8(out, ELFOSABI_NONE);
    for (int i = 0; i < 8; i++) bb_write8(out, 0);
    bb_write16le(out, ET_REL);
    bb_write16le(out, EM_386);
    bb_write32le(out, EV_CURRENT);
    bb_write32le(out, 0);                  /* e_entry */
    bb_write32le(out, 0);                  /* e_phoff */
    bb_write32le(out, (uint32_t)shdr_off); /* e_shoff */
    bb_write32le(out, 0);                  /* e_flags */
    bb_write16le(out, EHDR32_SIZE);
    bb_write16le(out, 32);                 /* e_phentsize */
    bb_write16le(out, 0);                  /* e_phnum */
    bb_write16le(out, SHDR32_SIZE);
    bb_write16le(out, (uint16_t)num_sections);
    bb_write16le(out, (uint16_t)shidx_shstr);

    /* Write section data */
    bb_writebytes(out, m->text.data, m->text.size);
    while (out->size < data_off) bb_write8(out, 0);
    if (shidx_data >= 0) bb_writebytes(out, m->data.data, m->data.size);
    while (out->size < rodata_off) bb_write8(out, 0);
    if (shidx_rodata >= 0) bb_writebytes(out, m->rodata.data, m->rodata.size);
    while (out->size < rel_off) bb_write8(out, 0);
    if (shidx_rel >= 0) bb_writebytes(out, rel_buf.data, rel_buf.size);
    while (out->size < symtab_off) bb_write8(out, 0);
    bb_writebytes(out, symtab_buf.data, symtab_buf.size);
    bb_writebytes(out, strtab.buf.data, strtab.buf.size);
    bb_writebytes(out, shstrtab.buf.data, shstrtab.buf.size);
    while (out->size < shdr_off) bb_write8(out, 0);

    elf_write_shdr32(out, sh_null_name, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    elf_write_shdr32(out, sh_text_name, SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR, 0, (uint32_t)text_off, (uint32_t)m->text.size, 0, 0, 4, 0);
    if (shidx_data >= 0)
        elf_write_shdr32(out, sh_data_name, SHT_PROGBITS, SHF_ALLOC|SHF_WRITE, 0, (uint32_t)data_off, (uint32_t)m->data.size, 0, 0, 4, 0);
    if (shidx_rodata >= 0)
        elf_write_shdr32(out, sh_rodata_name, SHT_PROGBITS, SHF_ALLOC, 0, (uint32_t)rodata_off, (uint32_t)m->rodata.size, 0, 0, 4, 0);
    if (shidx_bss >= 0)
        elf_write_shdr32(out, sh_bss_name, SHT_NOBITS, SHF_ALLOC|SHF_WRITE, 0, 0, (uint32_t)m->bss_size, 0, 0, 4, 0);
    if (shidx_rel >= 0)
        elf_write_shdr32(out, sh_rel_name, SHT_REL, 0, 0, (uint32_t)rel_off, (uint32_t)rel_buf.size, (uint32_t)shidx_symtab, (uint32_t)shidx_text, 4, 8);
    elf_write_shdr32(out, sh_symtab_name, SHT_SYMTAB, 0, 0, (uint32_t)symtab_off, (uint32_t)symtab_buf.size, (uint32_t)shidx_strtab, (uint32_t)symtab_info, 4, SYM32_SIZE);
    elf_write_shdr32(out, sh_strtab_name, SHT_STRTAB, 0, 0, (uint32_t)strtab_off, (uint32_t)strtab.buf.size, 0, 0, 1, 0);
    elf_write_shdr32(out, sh_shstr_name, SHT_STRTAB, 0, 0, (uint32_t)shstr_off, (uint32_t)shstrtab.buf.size, 0, 0, 1, 0);

    bb_free(&symtab_buf);
    bb_free(&rel_buf);
    strtab_free(&strtab);
    strtab_free(&shstrtab);
}

/* =========================================================================
