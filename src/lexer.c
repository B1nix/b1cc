#include "lexer.h"
#include "preprocessor.h"
#include "diagnostics.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static int is_xdigit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int find_param(const StringArray *params, const char *name) {
    for (int i = 0; i < params->count; ++i) {
        if (strcmp(params->data[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static void push_token(TokenArray *arr, const char *text, int line, int col) {
    Token t;
    t.text = text;
    t.line = line;
    t.col = col;
    token_array_push(arr, &t);
}

TokenArray lex(const char *src, HashMap *macros, HashMap *active_macros, Arena *arena) {
    TokenArray out;
    token_array_init(&out);
    int current_line = 1;
    int current_col = 1;
    size_t i = 0;
    size_t src_len = strlen(src);

    #define CONSUME(count) { \
        size_t _cnt = (count); \
        for (size_t _k = 0; _k < _cnt && i < src_len; ++_k) { \
            if (src[i] == '\n') { \
                current_line++; \
                current_col = 1; \
            } else { \
                current_col++; \
            } \
            i++; \
        } \
    }

    while (i < src_len) {
        char c = src[i];
        int tok_line = current_line;
        int tok_col = current_col;

        if (is_space(c)) {
            CONSUME(1);
        } else if (c == '#') {
            size_t check_i = i + 1;
            while (check_i < src_len && (src[check_i] == ' ' || src[check_i] == '\t')) {
                check_i++;
            }
            if (check_i < src_len && is_digit(src[check_i])) {
                while (i < src_len && src[i] != '\n') {
                    CONSUME(1);
                }
            } else {
                if (i + 1 < src_len && src[i + 1] == '#') {
                    CONSUME(2);
                    push_token(&out, "##", tok_line, tok_col);
                } else {
                    CONSUME(1);
                    push_token(&out, "#", tok_line, tok_col);
                }
            }
        } else if (i + 1 < src_len && src[i] == '/' && src[i + 1] == '/') {
            CONSUME(2);
            while (i < src_len && src[i] != '\n') {
                CONSUME(1);
            }
        } else if (i + 1 < src_len && src[i] == '/' && src[i + 1] == '*') {
            CONSUME(2);
            while (i + 1 < src_len && !(src[i] == '*' && src[i + 1] == '/')) {
                CONSUME(1);
            }
            if (i + 1 == src_len) {
                diagnostics_error(tok_line, tok_col, "unterminated block comment");
            }
            CONSUME(2);
        } else if (is_digit(c) || (c == '.' && i + 1 < src_len && is_digit(src[i + 1]))) {
            size_t start = i;
            int is_float = 0;
            if (c == '0' && i + 1 < src_len && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                CONSUME(2);
                while (i < src_len && is_xdigit(src[i])) {
                    CONSUME(1);
                }
            } else {
                while (i < src_len && is_digit(src[i])) {
                    CONSUME(1);
                }
                /* fractional part */
                if (i < src_len && src[i] == '.') {
                    is_float = 1;
                    CONSUME(1);
                    while (i < src_len && is_digit(src[i])) {
                        CONSUME(1);
                    }
                }
                /* exponent */
                if (i < src_len && (src[i] == 'e' || src[i] == 'E')) {
                    is_float = 1;
                    CONSUME(1);
                    if (i < src_len && (src[i] == '+' || src[i] == '-')) {
                        CONSUME(1);
                    }
                    while (i < src_len && is_digit(src[i])) {
                        CONSUME(1);
                    }
                }
            }
            /* The token text includes the '.'/exponent so the parser can tell a
               floating literal from an integer one; the suffix is kept too so a
               trailing 'f'/'F' marks a float (4-byte) rather than double. */
            size_t suffix_start = i;
            if (is_float) {
                while (i < src_len && (src[i] == 'f' || src[i] == 'F' || src[i] == 'l' || src[i] == 'L')) {
                    CONSUME(1);
                }
            } else {
                while (i < src_len && (src[i] == 'u' || src[i] == 'U' || src[i] == 'l' || src[i] == 'L')) {
                    CONSUME(1);
                }
            }
            (void)suffix_start;
            const char *num_str = arena_strndup(arena, src + start, i - start);
            push_token(&out, num_str, tok_line, tok_col);
        } else if (is_alpha(c)) {
            size_t start = i;
            CONSUME(1);
            while (i < src_len) {
                char d = src[i];
                if (!is_alnum(d) && d != '_')
                    break;
                CONSUME(1);
            }
            const char *ident = arena_strndup(arena, src + start, i - start);
            HashMapEntry *macro_entry = macros ? hashmap_get(macros, ident) : nullptr;
            int is_active = active_macros ? hashmap_has(active_macros, ident) : 0;

            if (macro_entry && !is_active) {
                HashMap next_active;
                hashmap_init(&next_active, 16);
                if (active_macros) {
                    for (int b = 0; b < active_macros->bucket_count; ++b) {
                        HashMapEntry *curr = active_macros->buckets[b];
                        while (curr) {
                            hashmap_put(&next_active, curr->key, curr->val_ptr, curr->val_int);
                            curr = curr->next;
                        }
                    }
                }
                hashmap_put(&next_active, ident, nullptr, 1);

                Macro *m = (Macro *)macro_entry->val_ptr;
                if (!m->is_function_like) {
                    TokenArray body_tokens = lex(m->body, nullptr, nullptr, arena);
                    TokenArray pasted;
                    token_array_init(&pasted);
                    for (int k = 0; k < body_tokens.count; ++k) {
                        if (strcmp(body_tokens.data[k].text, "EOF") == 0) continue;
                        if (strcmp(body_tokens.data[k].text, "##") == 0) {
                            if (pasted.count > 0 && k + 1 < body_tokens.count && strcmp(body_tokens.data[k + 1].text, "EOF") != 0) {
                                size_t len1 = strlen(pasted.data[pasted.count - 1].text);
                                size_t len2 = strlen(body_tokens.data[k + 1].text);
                                char *pasted_text = arena_alloc(arena, len1 + len2 + 1);
                                strcpy(pasted_text, pasted.data[pasted.count - 1].text);
                                strcat(pasted_text, body_tokens.data[k + 1].text);
                                pasted.data[pasted.count - 1].text = pasted_text;
                                k++;
                            }
                        } else {
                            token_array_push(&pasted, &body_tokens.data[k]);
                        }
                    }
                    token_array_free(&body_tokens);

                    StringBuilder concatenated;
                    sb_init(&concatenated);
                    for (int k = 0; k < pasted.count; ++k) {
                        sb_append(&concatenated, pasted.data[k].text);
                        sb_append(&concatenated, " ");
                    }
                    token_array_free(&pasted);

                    const char *concatenated_str = sb_to_string(&concatenated, arena);
                    sb_free(&concatenated);

                    TokenArray macro_tokens = lex(concatenated_str, macros, &next_active, arena);
                    for (int k = 0; k < macro_tokens.count; ++k) {
                        if (strcmp(macro_tokens.data[k].text, "EOF") != 0) {
                            Token tok = macro_tokens.data[k];
                            tok.line = tok_line;
                            tok.col = tok_col;
                            token_array_push(&out, &tok);
                        }
                    }
                    token_array_free(&macro_tokens);
                } else {
                    size_t next_i = i;
                    while (next_i < src_len && is_space(src[next_i])) next_i++;
                    if (next_i < src_len && src[next_i] == '(') {
                        CONSUME(next_i - i);
                        CONSUME(1);

                        StringArray args;
                        string_array_init(&args);
                        StringBuilder current_arg;
                        sb_init(&current_arg);
                        int paren_depth = 0;

                        while (i < src_len) {
                            char next_c = src[i];
                            if (next_c == '(') {
                                paren_depth++;
                                sb_append_char(&current_arg, next_c);
                                CONSUME(1);
                            } else if (next_c == ')') {
                                if (paren_depth == 0) {
                                    string_array_push(&args, sb_to_string(&current_arg, arena));
                                    sb_free(&current_arg);
                                    CONSUME(1);
                                    break;
                                }
                                paren_depth--;
                                sb_append_char(&current_arg, next_c);
                                CONSUME(1);
                            } else if (next_c == ',' && paren_depth == 0) {
                                string_array_push(&args, sb_to_string(&current_arg, arena));
                                sb_free(&current_arg);
                                sb_init(&current_arg);
                                CONSUME(1);
                            } else {
                                sb_append_char(&current_arg, next_c);
                                CONSUME(1);
                            }
                        }

                        // Trim spaces in args
                        for (int arg_idx = 0; arg_idx < args.count; ++arg_idx) {
                            const char *arg = args.data[arg_idx];
                            size_t s_idx = 0;
                            while (arg[s_idx] && is_space(arg[s_idx])) s_idx++;
                            size_t e_idx = strlen(arg);
                            while (e_idx > s_idx && is_space(arg[e_idx - 1])) e_idx--;
                            args.data[arg_idx] = arena_strndup(arena, arg + s_idx, e_idx - s_idx);
                        }

                        TokenArray body_tokens = lex(m->body, nullptr, nullptr, arena);
                        TokenArray expanded;
                        token_array_init(&expanded);

                        for (int k = 0; k < body_tokens.count; ++k) {
                            if (strcmp(body_tokens.data[k].text, "EOF") == 0) continue;

                            // Stringification (#)
                            if (strcmp(body_tokens.data[k].text, "#") == 0) {
                                if (k + 1 < body_tokens.count && strcmp(body_tokens.data[k + 1].text, "EOF") != 0) {
                                    const char *param_name = body_tokens.data[k + 1].text;
                                    if (strcmp(param_name, "__VA_ARGS__") == 0) {
                                        int p_idx = find_param(&m->params, "...");
                                        if (p_idx != -1) {
                                            StringBuilder va_args_str;
                                            sb_init(&va_args_str);
                                            for (int arg_i = p_idx; arg_i < args.count; ++arg_i) {
                                                if (arg_i > p_idx) sb_append(&va_args_str, ", ");
                                                sb_append(&va_args_str, args.data[arg_i]);
                                            }
                                            const char *va_str = sb_to_string(&va_args_str, arena);
                                            sb_free(&va_args_str);

                                            StringBuilder stringified;
                                            sb_init(&stringified);
                                            sb_append_char(&stringified, '"');
                                            for (size_t ch_i = 0; va_str[ch_i]; ++ch_i) {
                                                char ch = va_str[ch_i];
                                                if (ch == '"' || ch == '\\') {
                                                    sb_append_char(&stringified, '\\');
                                                }
                                                sb_append_char(&stringified, ch);
                                            }
                                            sb_append_char(&stringified, '"');

                                            Token tok = body_tokens.data[k];
                                            tok.text = sb_to_string(&stringified, arena);
                                            sb_free(&stringified);
                                            token_array_push(&expanded, &tok);
                                            k++;
                                            continue;
                                        }
                                    } else {
                                        int p_idx = find_param(&m->params, param_name);
                                        if (p_idx != -1) {
                                            const char *arg_val = (p_idx < args.count) ? args.data[p_idx] : "";
                                            StringBuilder stringified;
                                            sb_init(&stringified);
                                            sb_append_char(&stringified, '"');
                                            for (size_t ch_i = 0; arg_val[ch_i]; ++ch_i) {
                                                char ch = arg_val[ch_i];
                                                if (ch == '"' || ch == '\\') {
                                                    sb_append_char(&stringified, '\\');
                                                }
                                                sb_append_char(&stringified, ch);
                                            }
                                            sb_append_char(&stringified, '"');

                                            Token tok = body_tokens.data[k];
                                            tok.text = sb_to_string(&stringified, arena);
                                            sb_free(&stringified);
                                            token_array_push(&expanded, &tok);
                                            k++;
                                            continue;
                                        }
                                    }
                                }
                            }

                            // __VA_ARGS__ substitution
                            if (strcmp(body_tokens.data[k].text, "__VA_ARGS__") == 0) {
                                int p_idx = find_param(&m->params, "...");
                                if (p_idx != -1) {
                                    StringBuilder va_args_str;
                                    sb_init(&va_args_str);
                                    for (int arg_i = p_idx; arg_i < args.count; ++arg_i) {
                                        if (arg_i > p_idx) sb_append(&va_args_str, ", ");
                                        sb_append(&va_args_str, args.data[arg_i]);
                                    }
                                    const char *va_str = sb_to_string(&va_args_str, arena);
                                    sb_free(&va_args_str);

                                    TokenArray arg_tokens = lex(va_str, macros, active_macros, arena);
                                    for (int tok_idx = 0; tok_idx < arg_tokens.count; ++tok_idx) {
                                        if (strcmp(arg_tokens.data[tok_idx].text, "EOF") != 0) {
                                            token_array_push(&expanded, &arg_tokens.data[tok_idx]);
                                        }
                                    }
                                    token_array_free(&arg_tokens);
                                    continue;
                                }
                            }

                            // Parameter substitution
                            int p_idx = find_param(&m->params, body_tokens.data[k].text);
                            if (p_idx != -1) {
                                const char *arg_val = (p_idx < args.count) ? args.data[p_idx] : "";
                                if (arg_val[0] == '\0') {
                                    Token placemarker;
                                    placemarker.text = "";
                                    placemarker.line = body_tokens.data[k].line;
                                    placemarker.col = body_tokens.data[k].col;
                                    token_array_push(&expanded, &placemarker);
                                } else {
                                    TokenArray arg_tokens = lex(arg_val, macros, active_macros, arena);
                                    for (int tok_idx = 0; tok_idx < arg_tokens.count; ++tok_idx) {
                                        if (strcmp(arg_tokens.data[tok_idx].text, "EOF") != 0) {
                                            token_array_push(&expanded, &arg_tokens.data[tok_idx]);
                                        }
                                    }
                                    token_array_free(&arg_tokens);
                                }
                                continue;
                            }

                            token_array_push(&expanded, &body_tokens.data[k]);
                        }
                        token_array_free(&body_tokens);

                        TokenArray pasted;
                        token_array_init(&pasted);
                        for (int k = 0; k < expanded.count; ++k) {
                            if (strcmp(expanded.data[k].text, "##") == 0) {
                                if (pasted.count > 0 && k + 1 < expanded.count) {
                                    const char *left = pasted.data[pasted.count - 1].text;
                                    const char *right = expanded.data[k + 1].text;
                                    char *pasted_text;
                                    if (left[0] == '\0') {
                                        pasted_text = arena_strdup(arena, right);
                                    } else if (right[0] == '\0') {
                                        pasted_text = arena_strdup(arena, left);
                                    } else {
                                        size_t len1 = strlen(left);
                                        size_t len2 = strlen(right);
                                        pasted_text = arena_alloc(arena, len1 + len2 + 1);
                                        strcpy(pasted_text, left);
                                        strcat(pasted_text, right);
                                    }
                                    pasted.data[pasted.count - 1].text = pasted_text;
                                    k++;
                                }
                            } else {
                                token_array_push(&pasted, &expanded.data[k]);
                            }
                        }
                        token_array_free(&expanded);

                        StringBuilder concatenated;
                        sb_init(&concatenated);
                        for (int k = 0; k < pasted.count; ++k) {
                            if (pasted.data[k].text[0] != '\0') {
                                sb_append(&concatenated, pasted.data[k].text);
                                sb_append(&concatenated, " ");
                            }
                        }
                        token_array_free(&pasted);

                        const char *concatenated_str = sb_to_string(&concatenated, arena);
                        sb_free(&concatenated);

                        TokenArray macro_tokens = lex(concatenated_str, macros, &next_active, arena);
                        for (int k = 0; k < macro_tokens.count; ++k) {
                            if (strcmp(macro_tokens.data[k].text, "EOF") != 0) {
                                Token tok = macro_tokens.data[k];
                                tok.line = tok_line;
                                tok.col = tok_col;
                                token_array_push(&out, &tok);
                            }
                        }
                        token_array_free(&macro_tokens);
                        string_array_free(&args);
                    } else {
                        push_token(&out, ident, tok_line, tok_col);
                    }
                }

                hashmap_free(&next_active);
            } else {
                push_token(&out, ident, tok_line, tok_col);
            }
        } else if (c == '"') {
            size_t start = i;
            CONSUME(1);
            while (i < src_len && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < src_len) {
                    CONSUME(2);
                } else {
                    CONSUME(1);
                }
            }
            if (i == src_len) {
                diagnostics_error(tok_line, tok_col, "unterminated string literal");
            }
            CONSUME(1);
            const char *str_val = arena_strndup(arena, src + start, i - start);
            push_token(&out, str_val, tok_line, tok_col);
        } else if (c == '\'') {
            size_t start = i;
            CONSUME(1);
            while (i < src_len && src[i] != '\'') {
                if (src[i] == '\\' && i + 1 < src_len) {
                    CONSUME(2);
                } else {
                    CONSUME(1);
                }
            }
            if (i == src_len) {
                diagnostics_error(tok_line, tok_col, "unterminated character literal");
            }
            CONSUME(1);
            const char *char_val = arena_strndup(arena, src + start, i - start);
            push_token(&out, char_val, tok_line, tok_col);
        } else if (i + 2 < src_len && src[i] == '.' && src[i+1] == '.' && src[i+2] == '.') {
            CONSUME(3);
            push_token(&out, "...", tok_line, tok_col);
        } else if (i + 1 < src_len &&
                   ((src[i] == '=' && src[i + 1] == '=') ||
                    (src[i] == '!' && src[i + 1] == '=') ||
                    (src[i] == '<' && src[i + 1] == '=') ||
                    (src[i] == '>' && src[i + 1] == '=') ||
                    (src[i] == '-' && src[i + 1] == '>') ||
                    (src[i] == '&' && src[i + 1] == '&') ||
                    (src[i] == '|' && src[i + 1] == '|') ||
                    (src[i] == '<' && src[i + 1] == '<') ||
                    (src[i] == '>' && src[i + 1] == '>') ||
                    (src[i] == '+' && src[i + 1] == '+') ||
                    (src[i] == '-' && src[i + 1] == '-') ||
                    (src[i] == '+' && src[i + 1] == '=') ||
                    (src[i] == '-' && src[i + 1] == '=') ||
                    (src[i] == '*' && src[i + 1] == '=') ||
                    (src[i] == '/' && src[i + 1] == '=') ||
                    (src[i] == '%' && src[i + 1] == '=') ||
                    (src[i] == '&' && src[i + 1] == '=') ||
                    (src[i] == '^' && src[i + 1] == '=') ||
                    (src[i] == '|' && src[i + 1] == '='))) {
            const char *text = arena_strndup(arena, src + i, 2);
            CONSUME(2);
            push_token(&out, text, tok_line, tok_col);
        } else if (strchr("{}[](),;=+-*/%<>.&!|^~:?", c) != nullptr) {
            const char *text = arena_strndup(arena, src + i, 1);
            CONSUME(1);
            push_token(&out, text, tok_line, tok_col);
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            diagnostics_error(tok_line, tok_col, msg);
        }
    }

    push_token(&out, "EOF", current_line, current_col);
    return out;
}
