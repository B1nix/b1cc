#!/usr/bin/env python3
# tests/elf_dyninfo.py — dump canonical dynamic-linking facts from an ELF64.
#
# Reads purely through the program headers + PT_DYNAMIC, so it works on files
# with NO section headers (which is exactly what b1cc's internal linker emits).
# Used by internal_link_diff.sh to structurally compare b1cc's own PIE/.so
# output against ld.lld's for the same source.
#
# Emits `key=value` lines (sorted, comma-joined sets) for easy diffing:
#   type, tags, needed, soname, imports, exports, jumpslots, globdats,
#   relatives, pltstubs
import struct, sys

DT = {1:'NEEDED',2:'PLTRELSZ',3:'PLTGOT',4:'HASH',5:'STRTAB',6:'SYMTAB',
      7:'RELA',8:'RELASZ',9:'RELAENT',10:'STRSZ',11:'SYMENT',14:'SONAME',
      20:'PLTREL',23:'JMPREL',25:'INIT_ARRAY',27:'INIT_ARRAYSZ'}
R_RELATIVE, R_GLOB_DAT, R_JUMP_SLOT, R_64 = 8, 6, 7, 1

def main(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'\x7fELF' and d[4] == 2, 'not ELF64'
    e_type = struct.unpack_from('<H', d, 16)[0]
    e_phoff = struct.unpack_from('<Q', d, 32)[0]
    e_phentsize = struct.unpack_from('<H', d, 54)[0]
    e_phnum = struct.unpack_from('<H', d, 56)[0]

    loads = []          # (vaddr, off, filesz, flags)
    dyn_off = dyn_sz = None
    for i in range(e_phnum):
        p = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from('<II', d, p)
        p_off, p_vaddr = struct.unpack_from('<QQ', d, p + 8)[0], struct.unpack_from('<Q', d, p + 16)[0]
        p_filesz = struct.unpack_from('<Q', d, p + 32)[0]
        if p_type == 1:                 # PT_LOAD
            loads.append((p_vaddr, p_off, p_filesz, p_flags))
        elif p_type == 2:               # PT_DYNAMIC
            dyn_off, dyn_sz = p_off, p_filesz

    def v2o(vaddr):                     # virtual addr -> file offset
        for va, off, fsz, _ in loads:
            if va <= vaddr < va + fsz:
                return off + (vaddr - va)
        return None

    tags, needed, soname = [], [], None
    d_hash = d_symtab = d_strtab = None
    d_rela = d_relasz = d_jmprel = d_pltrelsz = None
    for o in range(dyn_off, dyn_off + dyn_sz, 16):
        tag, val = struct.unpack_from('<QQ', d, o)
        if tag == 0:                    # DT_NULL
            break
        tags.append(DT.get(tag, str(tag)))
        if tag == 1:   needed.append(val)      # resolved to string below
        elif tag == 4: d_hash = val
        elif tag == 5: d_strtab = val
        elif tag == 6: d_symtab = val
        elif tag == 7: d_rela = val
        elif tag == 8: d_relasz = val
        elif tag == 14: soname = val
        elif tag == 23: d_jmprel = val
        elif tag == 2:  d_pltrelsz = val

    stroff = v2o(d_strtab)
    def dstr(idx):
        e = d.index(b'\0', stroff + idx)
        return d[stroff + idx:e].decode()

    needed = [dstr(x) for x in needed]
    soname = dstr(soname) if soname is not None else ''

    # symbol count from SysV hash nchain (2nd word)
    nsym = 0
    if d_hash is not None:
        ho = v2o(d_hash)
        nchain = struct.unpack_from('<I', d, ho + 4)[0]
        nsym = nchain
    imports, exports = [], []
    symo = v2o(d_symtab)
    syms = []
    for i in range(nsym):
        st_name, st_info, st_other, st_shndx = struct.unpack_from('<IBBH', d, symo + i * 24)
        st_value = struct.unpack_from('<Q', d, symo + i * 24 + 8)[0]
        name = dstr(st_name)
        syms.append(name)
        if i == 0:
            continue
        if st_shndx == 0:               # SHN_UNDEF -> import
            imports.append(name)
        else:
            exports.append(name)

    # dynamic reloc type tallies
    globdats = relatives = 0
    if d_rela is not None and d_relasz:
        ro = v2o(d_rela)
        for i in range(d_relasz // 24):
            _, info, _ = struct.unpack_from('<QQq', d, ro + i * 24)
            t = info & 0xffffffff
            if t == R_GLOB_DAT: globdats += 1
            elif t == R_RELATIVE: relatives += 1
    jumpslots = (d_pltrelsz // 24) if d_pltrelsz else 0

    # count PLT stubs (ff 25 ...) in executable LOAD segments
    pltstubs = 0
    for va, off, fsz, flags in loads:
        if flags & 1:                   # PF_X
            seg = d[off:off + fsz]
            j = 0
            while True:
                j = seg.find(b'\xff\x25', j)
                if j < 0: break
                pltstubs += 1
                j += 2

    def cs(xs): return ','.join(sorted(set(xs)))
    print('type=' + ('DYN' if e_type == 3 else ('EXEC' if e_type == 2 else str(e_type))))
    print('tags=' + cs(tags))
    print('needed=' + cs(needed))
    print('soname=' + soname)
    print('imports=' + cs(imports))
    print('exports=' + cs(exports))
    print('jumpslots=%d' % jumpslots)
    print('globdats=%d' % globdats)
    print('relatives=%d' % relatives)
    print('pltstubs=%d' % pltstubs)

if __name__ == '__main__':
    main(sys.argv[1])
