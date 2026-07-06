/* mkstemps() is a BSD/glibc extension; on Linux it needs _DEFAULT_SOURCE.
   Harmless on Darwin, where it is declared unconditionally. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "diagnostics.h"
#include "preprocessor.h"
#include "lexer.h"
#include "parser.h"
#include "ir.h"
#include "backend.h"
#include "elf_writer.h"
#include "macho_writer.h"
#include "builtin_headers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *read_file(const char *path, Arena *arena) {
    FILE *f = fopen(path, "r");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot open %s", path);
        diagnostics_fatal(msg);
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = arena_alloc(arena, sz + 1);
    size_t read_bytes = fread(data, 1, sz, f);
    data[read_bytes] = '\0';
    fclose(f);
    return data;
}

static void write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "w");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot write %s", path);
        diagnostics_fatal(msg);
    }
    fputs(data, f);
    fclose(f);
}

static int exists(const char *path) {
    return access(path, R_OK) == 0;
}

static int has_suffix(const char *path, const char *suffix) {
    size_t plen = strlen(path);
    size_t slen = strlen(suffix);
    if (plen < slen) return 0;
    return strcmp(path + plen - slen, suffix) == 0;
}

static void define_object_macro(Arena *arena, const char *name, const char *body) {
    Macro *m = arena_alloc(arena, sizeof(Macro));
    m->is_function_like = false;
    string_array_init(&m->params);
    m->body = body;
    hashmap_put(&preprocessor_driver_macros, name, m, 0);
}

static const char *shell_quote(const char *s, Arena *arena) {
    StringBuilder out;
    sb_init(&out);
    sb_append_char(&out, '\'');
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] == '\'') {
            sb_append(&out, "'\\''");
        } else {
            sb_append_char(&out, s[i]);
        }
    }
    sb_append_char(&out, '\'');
    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    return res;
}

static void dump_command(const char *title, const char *cmd) {
    fprintf(stdout, "=== %s ===\n", title);
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        diagnostics_fatal("cannot run dump command");
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        fputs(buf, stdout);
    }
    pclose(pipe);
}

/* Compile freestanding runtime sources into a temp directory.
 * Returns an array of .o file paths, or NULL on failure.
 * The caller must free the returned array and clean up the temp files. */
static StringArray compile_freestanding_runtime(const char *target, const char *mcmodel, Arena *arena) {
    StringArray runtime_objects;
    string_array_init(&runtime_objects);

    const char *runtime_files[] = {
        "runtime/divdi3.c",
        "runtime/runtime.c",
    };
    int num_files = sizeof(runtime_files) / sizeof(runtime_files[0]);

    char tmpdir[] = "/tmp/b1cc-rt-XXXXXX";
    char *dir = mkdtemp(tmpdir);
    if (!dir) return runtime_objects;

    for (int i = 0; i < num_files; ++i) {
        FILE *f = fopen(runtime_files[i], "r");
        if (!f) continue;
        fclose(f);

        char tmp_obj[1024];
        snprintf(tmp_obj, sizeof(tmp_obj), "%s/%s.o", dir, runtime_files[i]);
        /* mkdir -p for subdirs */
        char mkdir_cmd[1024];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/runtime", dir);
        system(mkdir_cmd);

        bool is_kernel = (strstr(target, "elf") != NULL);
        bool use_native = (strstr(target, "b1nix") != NULL || is_kernel);

        int rc = -1;
        if (use_native) {
            /* Use b1cc -c to produce native ELF */
            StringBuilder acmd;
            sb_init(&acmd);
            sb_append(&acmd, "./build/b1cc ");
            sb_append(&acmd, runtime_files[i]);
            sb_append(&acmd, " -c --target=");
            sb_append(&acmd, target);
            sb_append(&acmd, " -mcmodel=");
            sb_append(&acmd, mcmodel);
            sb_append(&acmd, " -o ");
            sb_append(&acmd, shell_quote(tmp_obj, arena));
            const char *acmd_str = sb_to_string(&acmd, arena);
            sb_free(&acmd);
            rc = system(acmd_str);
        } else {
            /* Use host cc to compile runtime (avoids Mach-O reloc issues) */
            StringBuilder acmd;
            sb_init(&acmd);
            sb_append(&acmd, "cc -c -std=c23 ");
            sb_append(&acmd, shell_quote(runtime_files[i], arena));
            sb_append(&acmd, " -o ");
            sb_append(&acmd, shell_quote(tmp_obj, arena));
            const char *acmd_str = sb_to_string(&acmd, arena);
            sb_free(&acmd);
            rc = system(acmd_str);
        }

        if (rc == 0) {
            string_array_push(&runtime_objects, arena_strdup(arena, tmp_obj));
        }
    }

    return runtime_objects;
}

static void dump_object(const char *path, int dump_symbols, int dump_sections, int dump_relocs, Arena *arena) {
    const char *q = shell_quote(path, arena);
    char cmd[1024];
    if (dump_symbols) {
        snprintf(cmd, sizeof(cmd), "nm %s 2>&1", q);
        char title[512];
        snprintf(title, sizeof(title), "symbols %s", path);
        dump_command(title, cmd);
    }
    if (dump_sections) {
        snprintf(cmd, sizeof(cmd), "(objdump -h %s 2>/dev/null || otool -l %s 2>&1)", q, q);
        char title[512];
        snprintf(title, sizeof(title), "sections %s", path);
        dump_command(title, cmd);
    }
    if (dump_relocs) {
        snprintf(cmd, sizeof(cmd), "(objdump -r %s 2>/dev/null || otool -r %s 2>&1)", q, q);
        char title[512];
        snprintf(title, sizeof(title), "relocations %s", path);
        dump_command(title, cmd);
    }
}

int main(int argc, char **argv) {
    Arena arena;
    arena_init(&arena);
    diagnostics_filepath = "input.c";
    bool emit_asm = false;
    bool compile_only = false;
    bool preprocess_only = false;
    const char *mcmodel = "small";
    bool dump_ast = false;
    bool dump_ir = false;
    bool dump_symbols = false;
    bool dump_sections = false;
    bool dump_relocs = false;
    bool pic_mode = false;
    bool freestanding = false;
    bool nostdlib = false;

    StringArray inputs;
    string_array_init(&inputs);
    StringArray link_flags;
    string_array_init(&link_flags);
    const char *output = "";
    const char *target = "arm64-darwin";

    string_array_init(&preprocessor_driver_include_dirs);
    hashmap_init(&preprocessor_driver_macros, 64);
    define_object_macro(&arena, "__b1cc__", "1");

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-S") == 0) {
            emit_asm = true;
        } else if (strcmp(arg, "-c") == 0) {
            compile_only = true;
        } else if (strcmp(arg, "-E") == 0) {
            preprocess_only = true;
        } else if (strcmp(arg, "-fdump-ast") == 0) {
            dump_ast = true;
        } else if (strcmp(arg, "-fdump-ir") == 0) {
            dump_ir = true;
        } else if (strcmp(arg, "-fdump-symbols") == 0) {
            dump_symbols = true;
        } else if (strcmp(arg, "-fdump-sections") == 0) {
            dump_sections = true;
        } else if (strcmp(arg, "-fdump-relocs") == 0) {
            dump_relocs = true;
        } else if (strncmp(arg, "--target=", 9) == 0) {
            target = arg + 9;
        } else if (strncmp(arg, "-mcmodel=", 9) == 0) {
            mcmodel = arg + 9;
        } else if (strcmp(arg, "-fPIC") == 0 || strcmp(arg, "-fpic") == 0 || strcmp(arg, "-fpie") == 0 || strcmp(arg, "-fPIE") == 0) {
            pic_mode = true;
        } else if (strcmp(arg, "-fno-pic") == 0 || strcmp(arg, "-fno-pie") == 0 || strcmp(arg, "-fno-PIE") == 0) {
            pic_mode = false;
        } else if (strcmp(arg, "-ffreestanding") == 0) {
            freestanding = true;
        } else if (strcmp(arg, "-nostdlib") == 0) {
            nostdlib = true;
        } else if (strcmp(arg, "-o") == 0) {
            if (++i == argc)
                diagnostics_fatal("-o needs a path");
            output = argv[i];
        } else if (strncmp(arg, "-I", 2) == 0) {
            const char *dir;
            if (strcmp(arg, "-I") == 0) {
                if (++i == argc)
                    diagnostics_fatal("-I needs a directory");
                dir = argv[i];
            } else {
                dir = arg + 2;
            }
            string_array_push(&preprocessor_driver_include_dirs, dir);
        } else if (strncmp(arg, "-D", 2) == 0) {
            const char *def;
            if (strcmp(arg, "-D") == 0) {
                if (++i == argc)
                    diagnostics_fatal("-D needs a definition");
                def = argv[i];
            } else {
                def = arg + 2;
            }
            const char *eq = strchr(def, '=');
            const char *name = def;
            const char *val = "1";
            if (eq) {
                name = arena_strndup(&arena, def, eq - def);
                val = eq + 1;
            }
            define_object_macro(&arena, name, val);
        } else if (strncmp(arg, "-isystem", 8) == 0) {
            /* silently ignore -isystem (b1cc uses its own preprocessor) */
            if (strcmp(arg, "-isystem") == 0) { ++i; } /* skip the path argument */
        } else if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            string_array_push(&link_flags, arg);
        } else {
            string_array_push(&inputs, arg);
        }
    }

    string_array_push(&preprocessor_driver_include_dirs, ".");
    string_array_push(&preprocessor_driver_include_dirs, "userspace/include");
    string_array_push(&preprocessor_driver_include_dirs, "include");

    /* Write bundled freestanding headers to a temp dir and add to include path */
    const char *builtin_inc_dir = builtin_headers_write_temp_dir();
    if (builtin_inc_dir) {
        string_array_push(&preprocessor_driver_include_dirs, builtin_inc_dir);
    }

    string_array_push(&preprocessor_driver_include_dirs, "../b1nix/userspace/include");
    string_array_push(&preprocessor_driver_include_dirs, "/usr/include");
    string_array_push(&preprocessor_driver_include_dirs, "/usr/local/include");

    ir_pic_mode = pic_mode ? 1 : 0;

    if (inputs.count == 0)
        diagnostics_fatal("usage: b1cc [-S] [-c] [-E] [-fdump-ast] [-fdump-ir] input.c ... [-o output]");

    if (strcmp(target, "arm64-darwin") == 0) {
        define_object_macro(&arena, "stdin", "__stdinp");
        define_object_macro(&arena, "stdout", "__stdoutp");
        define_object_macro(&arena, "stderr", "__stderrp");
    }

    const char *cc = "cc";
    const char *cross_as = NULL;
    const char *prefix = "";
    bool is_kernel_target = (strcmp(target, "x86_64-elf") == 0 || strcmp(target, "i686-elf") == 0 ||
                             strcmp(target, "x86_64-unknown-elf") == 0 || strcmp(target, "i686-unknown-elf") == 0);
    if (strcmp(target, "x86_64-b1nix") == 0 || strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0 || is_kernel_target) {
        const char *env_cc = getenv("B1NIX_CC");
        cc = env_cc ? env_cc : "../b1nix/tools/toolchain/bin/b1nix-cc";
        if (!is_kernel_target && !preprocess_only && !emit_asm && !exists(cc))
            diagnostics_fatal("set B1NIX_CC or run from next to ../b1nix");
        
        /* Cross-assembler for .S files: use clang directly, not b1nix-cc
         * (b1nix-cc is a full driver that adds CRT0 + libc on link) */
        if (strcmp(target, "x86_64-b1nix") == 0 || strcmp(target, "x86_64-elf") == 0 || strcmp(target, "x86_64-unknown-elf") == 0) {
            prefix = is_kernel_target ? "" : "B1NIX_ARCH=x86_64 ";
            cross_as = "clang --target=x86_64-unknown-elf -c";
        } else {
            prefix = is_kernel_target ? "" : "B1NIX_ARCH=x86 ";
            cross_as = "clang --target=i686-unknown-elf -c";
        }
    } else if (strcmp(target, "arm64-darwin") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "linking/assembling is not supported for target %s", target);
        diagnostics_fatal(msg);
    }

    if (preprocess_only) {
        StringBuilder prep_out;
        sb_init(&prep_out);
        for (int idx = 0; idx < inputs.count; ++idx) {
            const char *inp = inputs.data[idx];
            
            HashMap macros;
            hashmap_init(&macros, 64);
            for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
                HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
                while (curr) {
                    hashmap_put(&macros, curr->key, curr->val_ptr, curr->val_int);
                    curr = curr->next;
                }
            }

            HashMap included_files;
            hashmap_init(&included_files, 32);
            hashmap_put(&included_files, inp, nullptr, 1);

            const char *prep = preprocessor_preprocess(read_file(inp, &arena), inp, &preprocessor_driver_include_dirs, &macros, &included_files, &arena);
            hashmap_free(&included_files);

            TokenArray toks = lex(prep, nullptr, nullptr, &arena);
            hashmap_free(&macros);

            for (int t_i = 0; t_i < toks.count; ++t_i) {
                if (strcmp(toks.data[t_i].text, "EOF") == 0) continue;
                sb_append(&prep_out, toks.data[t_i].text);
                if (strcmp(toks.data[t_i].text, ";") == 0 || strcmp(toks.data[t_i].text, "}") == 0 || strcmp(toks.data[t_i].text, "{") == 0)
                    sb_append(&prep_out, "\n");
                else
                    sb_append(&prep_out, " ");
            }
            sb_append(&prep_out, "\n");
            token_array_free(&toks);
        }
        
        const char *prep_str = sb_to_string(&prep_out, &arena);
        sb_free(&prep_out);

        if (!output || !output[0]) {
            printf("%s", prep_str);
        } else {
            write_file(output, prep_str);
        }

        // Cleanup
        string_array_free(&inputs);
        string_array_free(&link_flags);
        string_array_free(&preprocessor_driver_include_dirs);
        if (builtin_inc_dir) {
            char rm_cmd[1024];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
            system(rm_cmd);
            free((void *)builtin_inc_dir);
        }
        // clean preprocessor macros params
        for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
            HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
            while (curr) {
                Macro *m = (Macro *)curr->val_ptr;
                string_array_free(&m->params);
                curr = curr->next;
            }
        }
        hashmap_free(&preprocessor_driver_macros);
        arena_free(&arena);
        return 0;
    }

    if (emit_asm) {
        for (int idx = 0; idx < inputs.count; ++idx) {
            const char *inp = inputs.data[idx];
            diagnostics_filepath = inp;
            const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, mcmodel, dump_ast, dump_ir, &arena);
            const char *dest = output;
            if (!dest || !dest[0] || inputs.count > 1) {
                char dest_buf[1024];
                const char *dot = strrchr(inp, '.');
                if (dot) {
                    size_t len_base = dot - inp;
                    snprintf(dest_buf, sizeof(dest_buf), "%.*s.s", (int)len_base, inp);
                } else {
                    snprintf(dest_buf, sizeof(dest_buf), "%s.s", inp);
                }
                dest = arena_strdup(&arena, dest_buf);
            }
            write_file(dest, asm_text);
        }

        // Cleanup
        string_array_free(&inputs);
        string_array_free(&link_flags);
        string_array_free(&preprocessor_driver_include_dirs);
        if (builtin_inc_dir) {
            char rm_cmd[1024];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
            system(rm_cmd);
            free((void *)builtin_inc_dir);
        }
        for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
            HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
            while (curr) {
                Macro *m = (Macro *)curr->val_ptr;
                string_array_free(&m->params);
                curr = curr->next;
            }
        }
        hashmap_free(&preprocessor_driver_macros);
        arena_free(&arena);
        return 0;
    }

    if (compile_only) {
        /* Determine whether to use native ELF/Mach-O writer or host assembler */
        bool use_native_elf = (strcmp(target, "x86_64-b1nix") == 0 ||
                               strcmp(target, "i386-b1nix")   == 0 ||
                               strcmp(target, "x86-b1nix")    == 0 ||
                               strcmp(target, "x86_64-elf")   == 0 ||
                               strcmp(target, "i686-elf")     == 0 ||
                               strcmp(target, "x86_64-unknown-elf") == 0 ||
                               strcmp(target, "i686-unknown-elf")   == 0);
        bool use_native_macho = (strcmp(target, "arm64-darwin") == 0);

        for (int idx = 0; idx < inputs.count; ++idx) {
            const char *inp = inputs.data[idx];
            if (has_suffix(inp, ".o") || has_suffix(inp, ".a")) {
                continue;
            }
            /* .S assembly files: assemble with the system/cross assembler */
            if (has_suffix(inp, ".S") || has_suffix(inp, ".s")) {
                const char *dest_obj = output;
                if (!dest_obj || !dest_obj[0] || inputs.count > 1) {
                    char dest_buf[1024];
                    const char *dot = strrchr(inp, '.');
                    if (dot) {
                        size_t len_base = dot - inp;
                        snprintf(dest_buf, sizeof(dest_buf), "%.*s.o", (int)len_base, inp);
                    } else {
                        snprintf(dest_buf, sizeof(dest_buf), "%s.o", inp);
                    }
                    dest_obj = arena_strdup(&arena, dest_buf);
                }
                StringBuilder cmd;
                sb_init(&cmd);
                if (cross_as) {
                    sb_append(&cmd, cross_as);
                } else {
                    sb_append(&cmd, shell_quote(cc, &arena));
                    sb_append(&cmd, " -c");
                }
                sb_append(&cmd, " ");
                sb_append(&cmd, shell_quote(inp, &arena));
                sb_append(&cmd, " -o ");
                sb_append(&cmd, shell_quote(dest_obj, &arena));
                const char *cmd_str = sb_to_string(&cmd, &arena);
                sb_free(&cmd);
                int rc = system(cmd_str);
                if (rc != 0) {
                    arena_free(&arena);
                    return 1;
                }
                dump_object(dest_obj, dump_symbols, dump_sections, dump_relocs, &arena);
                continue;
            }
            diagnostics_filepath = inp;
            const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, mcmodel, dump_ast, dump_ir, &arena);

            const char *dest_obj = output;
            if (!dest_obj || !dest_obj[0] || inputs.count > 1) {
                char dest_buf[1024];
                const char *dot = strrchr(inp, '.');
                if (dot) {
                    size_t len_base = dot - inp;
                    snprintf(dest_buf, sizeof(dest_buf), "%.*s.o", (int)len_base, inp);
                } else {
                    snprintf(dest_buf, sizeof(dest_buf), "%s.o", inp);
                }
                dest_obj = arena_strdup(&arena, dest_buf);
            }

            if (use_native_elf) {
                /* Native ELF writer path — no external assembler needed */
                ElfObject obj = elf_write_object(asm_text, target, inp, &arena);
                if (!obj.data || obj.size == 0) {
                    diagnostics_fatal("elf_write_object: failed to produce ELF object");
                }
                FILE *of = fopen(dest_obj, "wb");
                if (!of) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "cannot write %s", dest_obj);
                    diagnostics_fatal(msg);
                }
                fwrite(obj.data, 1, obj.size, of);
                fclose(of);
            } else if (use_native_macho) {
                /* Native Mach-O writer path — no external assembler needed */
                MachObject obj = macho_write_object(asm_text, inp, &arena);
                if (!obj.data || obj.size == 0) {
                    diagnostics_fatal("macho_write_object: failed to produce Mach-O object");
                }
                FILE *of = fopen(dest_obj, "wb");
                if (!of) {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "cannot write %s", dest_obj);
                    diagnostics_fatal(msg);
                }
                fwrite(obj.data, 1, obj.size, of);
                fclose(of);
            } else {
                /* Host assembler path */
                char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
                int fd = mkstemps(tmp_asm, 2);
                if (fd < 0) diagnostics_fatal("cannot create temporary file");
                close(fd);
                write_file(tmp_asm, asm_text);

                StringBuilder cmd;
                sb_init(&cmd);
                sb_append(&cmd, prefix);
                sb_append(&cmd, shell_quote(cc, &arena));
                sb_append(&cmd, " -c ");
                sb_append(&cmd, shell_quote(tmp_asm, &arena));
                sb_append(&cmd, " -o ");
                sb_append(&cmd, shell_quote(dest_obj, &arena));

                const char *cmd_str = sb_to_string(&cmd, &arena);
                sb_free(&cmd);

                int rc = system(cmd_str);
                unlink(tmp_asm);
                if (rc != 0) {
                    arena_free(&arena);
                    return 1;
                }
            }
            dump_object(dest_obj, dump_symbols, dump_sections, dump_relocs, &arena);
        }

        // Cleanup
        string_array_free(&inputs);
        string_array_free(&link_flags);
        string_array_free(&preprocessor_driver_include_dirs);
        if (builtin_inc_dir) {
            char rm_cmd[1024];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
            system(rm_cmd);
            free((void *)builtin_inc_dir);
        }
        for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
            HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
            while (curr) {
                Macro *m = (Macro *)curr->val_ptr;
                string_array_free(&m->params);
                curr = curr->next;
            }
        }
        hashmap_free(&preprocessor_driver_macros);
        arena_free(&arena);
        return 0;
    }

    if (inputs.count == 1 && !compile_only && !emit_asm && !preprocess_only) {
        const char *inp = inputs.data[0];
        if (has_suffix(inp, ".o") || has_suffix(inp, ".a")) {
            const char *out_name = (!output || !output[0]) ? "a.out" : output;
            StringBuilder cmd;
            sb_init(&cmd);
            sb_append(&cmd, prefix);
            sb_append(&cmd, shell_quote(cc, &arena));
            sb_append(&cmd, " ");
            sb_append(&cmd, shell_quote(inp, &arena));
            for (int k = 0; k < link_flags.count; ++k) {
                sb_append(&cmd, " ");
                sb_append(&cmd, shell_quote(link_flags.data[k], &arena));
            }
            sb_append(&cmd, " -o ");
            sb_append(&cmd, shell_quote(out_name, &arena));

            const char *cmd_str = sb_to_string(&cmd, &arena);
            sb_free(&cmd);

            int rc = system(cmd_str);
            string_array_free(&inputs);
            string_array_free(&link_flags);
            string_array_free(&preprocessor_driver_include_dirs);
            if (builtin_inc_dir) {
                char rm_cmd[1024];
                snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
                system(rm_cmd);
                free((void *)builtin_inc_dir);
            }
            for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
                HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
                while (curr) {
                    Macro *m = (Macro *)curr->val_ptr;
                    string_array_free(&m->params);
                    curr = curr->next;
                }
            }
            hashmap_free(&preprocessor_driver_macros);
            arena_free(&arena);
            return rc == 0 ? 0 : 1;
        }

        /* .S assembly files: assemble with the system/cross assembler, don't compile through b1cc */
        if (has_suffix(inp, ".S") || has_suffix(inp, ".s")) {
            const char *out_name = (!output || !output[0]) ? "a.out" : output;
            StringBuilder cmd;
            sb_init(&cmd);
            if (cross_as) {
                sb_append(&cmd, cross_as);
            } else {
                sb_append(&cmd, shell_quote(cc, &arena));
                sb_append(&cmd, " -c");
            }
            sb_append(&cmd, " ");
            sb_append(&cmd, shell_quote(inp, &arena));
            sb_append(&cmd, " -o ");
            sb_append(&cmd, shell_quote(out_name, &arena));

            const char *cmd_str = sb_to_string(&cmd, &arena);
            sb_free(&cmd);

            int rc = system(cmd_str);
            string_array_free(&inputs);
            string_array_free(&link_flags);
            string_array_free(&preprocessor_driver_include_dirs);
            if (builtin_inc_dir) {
                char rm_cmd[1024];
                snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
                system(rm_cmd);
                free((void *)builtin_inc_dir);
            }
            for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
                HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
                while (curr) {
                    Macro *m = (Macro *)curr->val_ptr;
                    string_array_free(&m->params);
                    curr = curr->next;
                }
            }
            hashmap_free(&preprocessor_driver_macros);
            arena_free(&arena);
            return rc == 0 ? 0 : 1;
        }

        diagnostics_filepath = inp;
        const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, mcmodel, dump_ast, dump_ir, &arena);
        
        char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
        int fd = mkstemps(tmp_asm, 2);
        if (fd < 0) diagnostics_fatal("cannot create temporary file");
        close(fd);
        write_file(tmp_asm, asm_text);

        const char *out_name = (!output || !output[0]) ? "a.out" : output;
        StringBuilder cmd;
        sb_init(&cmd);
        sb_append(&cmd, prefix);
        sb_append(&cmd, shell_quote(cc, &arena));
        sb_append(&cmd, " ");
        sb_append(&cmd, shell_quote(tmp_asm, &arena));

        /* Link freestanding runtime if requested */
        StringArray runtime_objs;
        string_array_init(&runtime_objs);
        if (freestanding && !nostdlib) {
            runtime_objs = compile_freestanding_runtime(target, mcmodel, &arena);
            for (int k = 0; k < runtime_objs.count; ++k) {
                sb_append(&cmd, " ");
                sb_append(&cmd, shell_quote(runtime_objs.data[k], &arena));
            }
        }

        for (int k = 0; k < link_flags.count; ++k) {
            sb_append(&cmd, " ");
            sb_append(&cmd, shell_quote(link_flags.data[k], &arena));
        }
        sb_append(&cmd, " -o ");
        sb_append(&cmd, shell_quote(out_name, &arena));

        const char *cmd_str = sb_to_string(&cmd, &arena);
        sb_free(&cmd);

        int rc = system(cmd_str);
        unlink(tmp_asm);
        /* Clean up runtime objects */
        for (int k = 0; k < runtime_objs.count; ++k) {
            unlink(runtime_objs.data[k]);
        }
        string_array_free(&runtime_objs);
        string_array_free(&inputs);
        string_array_free(&link_flags);
        string_array_free(&preprocessor_driver_include_dirs);
        for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
            HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
            while (curr) {
                Macro *m = (Macro *)curr->val_ptr;
                string_array_free(&m->params);
                curr = curr->next;
            }
        }
        hashmap_free(&preprocessor_driver_macros);
        arena_free(&arena);
        return rc == 0 ? 0 : 1;
    }

    StringArray temp_objects;
    string_array_init(&temp_objects);
    StringArray link_cmd_args;
    string_array_init(&link_cmd_args);

    bool failed = false;
    for (int idx = 0; idx < inputs.count; ++idx) {
        const char *inp = inputs.data[idx];
        if (has_suffix(inp, ".o") || has_suffix(inp, ".a")) {
            string_array_push(&link_cmd_args, inp);
            continue;
        }
        /* .S assembly files: assemble with the system/cross assembler */
        if (has_suffix(inp, ".S") || has_suffix(inp, ".s")) {
            char tmp_obj[] = "/tmp/b1cc-XXXXXX.o";
            int fd_obj = mkstemps(tmp_obj, 2);
            if (fd_obj < 0) diagnostics_fatal("cannot create temporary file");
            close(fd_obj);

            string_array_push(&temp_objects, arena_strdup(&arena, tmp_obj));
            string_array_push(&link_cmd_args, arena_strdup(&arena, tmp_obj));

            StringBuilder cmd;
            sb_init(&cmd);
            if (cross_as) {
                sb_append(&cmd, cross_as);
            } else {
                sb_append(&cmd, shell_quote(cc, &arena));
                sb_append(&cmd, " -c");
            }
            sb_append(&cmd, " ");
            sb_append(&cmd, shell_quote(inp, &arena));
            sb_append(&cmd, " -o ");
            sb_append(&cmd, shell_quote(tmp_obj, &arena));

            const char *cmd_str = sb_to_string(&cmd, &arena);
            sb_free(&cmd);

            int rc = system(cmd_str);
            if (rc != 0) {
                failed = true;
                break;
            }
            continue;
        }
        diagnostics_filepath = inp;
        const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, mcmodel, dump_ast, dump_ir, &arena);
        
        char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
        int fd_asm = mkstemps(tmp_asm, 2);
        if (fd_asm < 0) diagnostics_fatal("cannot create temporary file");
        close(fd_asm);
        write_file(tmp_asm, asm_text);

        char tmp_obj[] = "/tmp/b1cc-XXXXXX.o";
        int fd_obj = mkstemps(tmp_obj, 2);
        if (fd_obj < 0) diagnostics_fatal("cannot create temporary file");
        close(fd_obj);

        string_array_push(&temp_objects, arena_strdup(&arena, tmp_obj));
        string_array_push(&link_cmd_args, arena_strdup(&arena, tmp_obj));

        StringBuilder cmd;
        sb_init(&cmd);
        sb_append(&cmd, prefix);
        sb_append(&cmd, shell_quote(cc, &arena));
        sb_append(&cmd, " -c ");
        sb_append(&cmd, shell_quote(tmp_asm, &arena));
        sb_append(&cmd, " -o ");
        sb_append(&cmd, shell_quote(tmp_obj, &arena));

        const char *cmd_str = sb_to_string(&cmd, &arena);
        sb_free(&cmd);

        int rc = system(cmd_str);
        unlink(tmp_asm);
        if (rc != 0) {
            failed = true;
            break;
        }
    }

    int final_rc = 0;
    if (failed) {
        for (int k = 0; k < temp_objects.count; ++k) {
            unlink(temp_objects.data[k]);
        }
        final_rc = 1;
    } else {
        const char *out_name = (!output || !output[0]) ? "a.out" : output;
        StringBuilder link_cmd;
        sb_init(&link_cmd);
        sb_append(&link_cmd, prefix);
        sb_append(&link_cmd, shell_quote(cc, &arena));
        for (int k = 0; k < link_cmd_args.count; ++k) {
            sb_append(&link_cmd, " ");
            sb_append(&link_cmd, shell_quote(link_cmd_args.data[k], &arena));
        }

        /* Link freestanding runtime if requested */
        StringArray runtime_objs;
        string_array_init(&runtime_objs);
        if (freestanding && !nostdlib) {
            runtime_objs = compile_freestanding_runtime(target, mcmodel, &arena);
            for (int k = 0; k < runtime_objs.count; ++k) {
                sb_append(&link_cmd, " ");
                sb_append(&link_cmd, shell_quote(runtime_objs.data[k], &arena));
            }
        }

        for (int k = 0; k < link_flags.count; ++k) {
            sb_append(&link_cmd, " ");
            sb_append(&link_cmd, shell_quote(link_flags.data[k], &arena));
        }
        sb_append(&link_cmd, " -o ");
        sb_append(&link_cmd, shell_quote(out_name, &arena));

        const char *link_cmd_str = sb_to_string(&link_cmd, &arena);
        sb_free(&link_cmd);

        int rc = system(link_cmd_str);
        /* Clean up runtime objects */
        for (int k = 0; k < runtime_objs.count; ++k) {
            unlink(runtime_objs.data[k]);
        }
        string_array_free(&runtime_objs);
        for (int k = 0; k < temp_objects.count; ++k) {
            unlink(temp_objects.data[k]);
        }
        final_rc = (rc == 0) ? 0 : 1;
    }

    string_array_free(&temp_objects);
    string_array_free(&link_cmd_args);
    string_array_free(&inputs);
    string_array_free(&link_flags);
    string_array_free(&preprocessor_driver_include_dirs);
    /* Clean up bundled freestanding headers temp dir */
    if (builtin_inc_dir) {
        char rm_cmd[1024];
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", builtin_inc_dir);
        system(rm_cmd);
        free((void *)builtin_inc_dir);
    }
    for (int b = 0; b < preprocessor_driver_macros.bucket_count; ++b) {
        HashMapEntry *curr = preprocessor_driver_macros.buckets[b];
        while (curr) {
            Macro *m = (Macro *)curr->val_ptr;
            string_array_free(&m->params);
            curr = curr->next;
        }
    }
    hashmap_free(&preprocessor_driver_macros);
    arena_free(&arena);
    return final_rc;
}
