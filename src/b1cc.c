#include "diagnostics.h"
#include "preprocessor.h"
#include "lexer.h"
#include "parser.h"
#include "ir.h"
#include "backend.h"
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
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
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

    int emit_asm = 0;
    int compile_only = 0;
    int preprocess_only = 0;
    int dump_ast = 0;
    int dump_ir = 0;
    int dump_symbols = 0;
    int dump_sections = 0;
    int dump_relocs = 0;

    StringArray inputs;
    string_array_init(&inputs);
    StringArray link_flags;
    string_array_init(&link_flags);
    const char *output = "";
    const char *target = "arm64-darwin";

    string_array_init(&preprocessor_driver_include_dirs);
    hashmap_init(&preprocessor_driver_macros, 64);
    string_array_push(&preprocessor_driver_include_dirs, ".");
    string_array_push(&preprocessor_driver_include_dirs, "../b1nix/userspace/include");
    string_array_push(&preprocessor_driver_include_dirs, "/usr/include");
    string_array_push(&preprocessor_driver_include_dirs, "/usr/local/include");

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-S") == 0) {
            emit_asm = 1;
        } else if (strcmp(arg, "-c") == 0) {
            compile_only = 1;
        } else if (strcmp(arg, "-E") == 0) {
            preprocess_only = 1;
        } else if (strcmp(arg, "-fdump-ast") == 0) {
            dump_ast = 1;
        } else if (strcmp(arg, "-fdump-ir") == 0) {
            dump_ir = 1;
        } else if (strcmp(arg, "-fdump-symbols") == 0) {
            dump_symbols = 1;
        } else if (strcmp(arg, "-fdump-sections") == 0) {
            dump_sections = 1;
        } else if (strcmp(arg, "-fdump-relocs") == 0) {
            dump_relocs = 1;
        } else if (strncmp(arg, "--target=", 9) == 0) {
            target = arg + 9;
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
            Macro *m = arena_alloc(&arena, sizeof(Macro));
            m->is_function_like = 0;
            string_array_init(&m->params);
            m->body = val;
            hashmap_put(&preprocessor_driver_macros, name, m, 0);
        } else if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            string_array_push(&link_flags, arg);
        } else {
            string_array_push(&inputs, arg);
        }
    }

    if (inputs.count == 0)
        diagnostics_fatal("usage: b1cc [-S] [-c] [-E] [-fdump-ast] [-fdump-ir] input.c ... [-o output]");

    if (strcmp(target, "arm64-darwin") == 0) {
        Macro *m_in = arena_alloc(&arena, sizeof(Macro));
        m_in->is_function_like = 0;
        string_array_init(&m_in->params);
        m_in->body = "__stdinp";
        hashmap_put(&preprocessor_driver_macros, "stdin", m_in, 0);

        Macro *m_out = arena_alloc(&arena, sizeof(Macro));
        m_out->is_function_like = 0;
        string_array_init(&m_out->params);
        m_out->body = "__stdoutp";
        hashmap_put(&preprocessor_driver_macros, "stdout", m_out, 0);

        Macro *m_err = arena_alloc(&arena, sizeof(Macro));
        m_err->is_function_like = 0;
        string_array_init(&m_err->params);
        m_err->body = "__stderrp";
        hashmap_put(&preprocessor_driver_macros, "stderr", m_err, 0);
    }

    const char *cc = "cc";
    const char *prefix = "";
    if (strcmp(target, "x86_64-b1nix") == 0 || strcmp(target, "i386-b1nix") == 0 || strcmp(target, "x86-b1nix") == 0) {
        const char *env_cc = getenv("B1NIX_CC");
        cc = env_cc ? env_cc : "../b1nix/tools/toolchain/bin/b1nix-cc";
        if (!preprocess_only && !emit_asm && !exists(cc))
            diagnostics_fatal("set B1NIX_CC or run from next to ../b1nix");
        
        if (strcmp(target, "x86_64-b1nix") == 0) {
            prefix = "B1NIX_ARCH=x86_64 ";
        } else {
            prefix = "B1NIX_ARCH=x86 ";
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

            TokenArray toks = lex(prep, &macros, nullptr, &arena);
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
            const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, dump_ast, dump_ir, &arena);
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
        for (int idx = 0; idx < inputs.count; ++idx) {
            const char *inp = inputs.data[idx];
            size_t inp_len = strlen(inp);
            if ((inp_len >= 2 && strcmp(inp + inp_len - 2, ".o") == 0) ||
                (inp_len >= 2 && strcmp(inp + inp_len - 2, ".a") == 0)) {
                continue;
            }
            diagnostics_filepath = inp;
            const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, dump_ast, dump_ir, &arena);
            
            char tmp_asm[] = "/tmp/b1cc-XXXXXX.s";
            int fd = mkstemps(tmp_asm, 2);
            if (fd < 0) diagnostics_fatal("cannot create temporary file");
            close(fd);
            write_file(tmp_asm, asm_text);

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
            dump_object(dest_obj, dump_symbols, dump_sections, dump_relocs, &arena);
        }

        // Cleanup
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
        return 0;
    }

    if (inputs.count == 1 && !compile_only && !emit_asm && !preprocess_only) {
        const char *inp = inputs.data[0];
        size_t inp_len = strlen(inp);
        if ((inp_len >= 2 && strcmp(inp + inp_len - 2, ".o") == 0) ||
            (inp_len >= 2 && strcmp(inp + inp_len - 2, ".a") == 0)) {
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
        const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, dump_ast, dump_ir, &arena);
        
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

    int failed = 0;
    for (int idx = 0; idx < inputs.count; ++idx) {
        const char *inp = inputs.data[idx];
        size_t inp_len = strlen(inp);
        if ((inp_len >= 2 && strcmp(inp + inp_len - 2, ".o") == 0) ||
            (inp_len >= 2 && strcmp(inp + inp_len - 2, ".a") == 0)) {
            string_array_push(&link_cmd_args, inp);
            continue;
        }
        diagnostics_filepath = inp;
        const char *asm_text = backend_compile_asm(read_file(inp, &arena), target, dump_ast, dump_ir, &arena);
        
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
            failed = 1;
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
        for (int k = 0; k < link_flags.count; ++k) {
            sb_append(&link_cmd, " ");
            sb_append(&link_cmd, shell_quote(link_flags.data[k], &arena));
        }
        sb_append(&link_cmd, " -o ");
        sb_append(&link_cmd, shell_quote(out_name, &arena));

        const char *link_cmd_str = sb_to_string(&link_cmd, &arena);
        sb_free(&link_cmd);

        int rc = system(link_cmd_str);
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
