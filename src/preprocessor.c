#include "preprocessor.h"
#include "diagnostics.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

StringArray preprocessor_driver_include_dirs;
HashMap preprocessor_driver_macros;
int preprocessor_current_line = 1;
const char *preprocessor_current_file = "";
int preprocessor_counter = 0;

void cond_state_array_init(CondStateArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void cond_state_array_push(CondStateArray *arr, const CondState *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(CondState));
    }
    arr->data[arr->count].condition_met = val->condition_met;
    arr->data[arr->count].active = val->active;
    arr->count = arr->count + 1;
}

void cond_state_array_free(CondStateArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

static bool is_active(const CondStateArray *cond_stack) {
    for (int idx = 0; idx < cond_stack->count; ++idx) {
        if (!cond_stack->data[idx].active) {
            return false;
        }
    }
    return true;
}

static void push_cond(CondStateArray *arr, bool condition_met, bool active) {
    CondState cs;
    cs.condition_met = condition_met;
    cs.active = active;
    cond_state_array_push(arr, &cs);
}

static int exists(const char *path) {
    return access(path, R_OK) == 0;
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

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static StringArray get_expr_tokens(const char *s, Arena *arena) {
    StringArray tokens;
    string_array_init(&tokens);
    size_t len = strlen(s);
    size_t i = 0;
    while (i < len) {
        if (is_space(s[i])) {
            i++;
        } else if (is_digit(s[i])) {
            size_t start = i;
            while (i < len && is_digit(s[i])) i++;
            string_array_push(&tokens, arena_strndup(arena, s + start, i - start));
        } else if (i + 1 < len &&
                   ((s[i] == '=' && s[i+1] == '=') ||
                    (s[i] == '!' && s[i+1] == '=') ||
                    (s[i] == '<' && s[i+1] == '=') ||
                    (s[i] == '>' && s[i+1] == '=') ||
                    (s[i] == '&' && s[i+1] == '&') ||
                    (s[i] == '|' && s[i+1] == '|') ||
                    (s[i] == '<' && s[i+1] == '<') ||
                    (s[i] == '>' && s[i+1] == '>'))) {
            string_array_push(&tokens, arena_strndup(arena, s + i, 2));
            i += 2;
        } else if (strchr("+-*/%<>()!~&|^", s[i]) != nullptr) {
            string_array_push(&tokens, arena_strndup(arena, s + i, 1));
            i++;
        } else {
            string_array_push(&tokens, arena_strndup(arena, s + i, 1));
            i++;
        }
    }
    return tokens;
}

typedef struct {
    StringArray tokens;
    int pos;
} ExprParser;

static const char *ep_peek(ExprParser *ep) {
    if (ep->pos >= ep->tokens.count) return "";
    return ep->tokens.data[ep->pos];
}

static const char *ep_take(ExprParser *ep) {
    const char *t = ep_peek(ep);
    ep->pos = ep->pos + 1;
    return t;
}

static long ep_eval_or(ExprParser *ep);

static long ep_primary(ExprParser *ep) {
    const char *t = ep_peek(ep);
    if (strcmp(t, "(") == 0) {
        ep_take(ep);
        long val = ep_eval_or(ep);
        ep_take(ep); // ")"
        return val;
    }
    if (t[0] && is_digit(t[0])) {
        return strtol(ep_take(ep), nullptr, 10);
    }
    if (strcmp(t, "!") == 0) {
        ep_take(ep);
        return !ep_primary(ep);
    }
    if (strcmp(t, "~") == 0) {
        ep_take(ep);
        return ~ep_primary(ep);
    }
    if (strcmp(t, "-") == 0) {
        ep_take(ep);
        return -ep_primary(ep);
    }
    if (strcmp(t, "+") == 0) {
        ep_take(ep);
        return ep_primary(ep);
    }
    return 0;
}

static long ep_mul(ExprParser *ep) {
    long val = ep_primary(ep);
    while (strcmp(ep_peek(ep), "*") == 0 || strcmp(ep_peek(ep), "/") == 0 || strcmp(ep_peek(ep), "%") == 0) {
        const char *op = ep_take(ep);
        long rhs = ep_primary(ep);
        if (strcmp(op, "*") == 0) {
            val *= rhs;
        } else if (strcmp(op, "/") == 0) {
            if (rhs != 0) {
                val /= rhs;
            } else {
                val = 0;
            }
        } else {
            if (rhs != 0) {
                val %= rhs;
            } else {
                val = 0;
            }
        }
    }
    return val;
}

static long ep_add(ExprParser *ep) {
    long val = ep_mul(ep);
    while (strcmp(ep_peek(ep), "+") == 0 || strcmp(ep_peek(ep), "-") == 0) {
        const char *op = ep_take(ep);
        long rhs = ep_mul(ep);
        if (strcmp(op, "+") == 0) {
            val += rhs;
        } else {
            val -= rhs;
        }
    }
    return val;
}

static long ep_shift(ExprParser *ep) {
    long val = ep_add(ep);
    while (strcmp(ep_peek(ep), "<<") == 0 || strcmp(ep_peek(ep), ">>") == 0) {
        const char *op = ep_take(ep);
        long rhs = ep_add(ep);
        if (strcmp(op, "<<") == 0) {
            val <<= rhs;
        } else {
            val >>= rhs;
        }
    }
    return val;
}

static long ep_relational(ExprParser *ep) {
    long val = ep_shift(ep);
    while (strcmp(ep_peek(ep), "<") == 0 || strcmp(ep_peek(ep), ">") == 0 ||
           strcmp(ep_peek(ep), "<=") == 0 || strcmp(ep_peek(ep), ">=") == 0) {
        const char *op = ep_take(ep);
        long rhs = ep_shift(ep);
        if (strcmp(op, "<") == 0) val = val < rhs;
        else if (strcmp(op, ">") == 0) val = val > rhs;
        else if (strcmp(op, "<=") == 0) val = val <= rhs;
        else val = val >= rhs;
    }
    return val;
}

static long ep_equality(ExprParser *ep) {
    long val = ep_relational(ep);
    while (strcmp(ep_peek(ep), "==") == 0 || strcmp(ep_peek(ep), "!=") == 0) {
        const char *op = ep_take(ep);
        long rhs = ep_relational(ep);
        if (strcmp(op, "==") == 0) val = val == rhs;
        else val = val != rhs;
    }
    return val;
}

static long ep_and(ExprParser *ep) {
    long val = ep_equality(ep);
    while (strcmp(ep_peek(ep), "&") == 0) {
        ep_take(ep);
        long rhs = ep_equality(ep);
        val &= rhs;
    }
    return val;
}

static long ep_xor(ExprParser *ep) {
    long val = ep_and(ep);
    while (strcmp(ep_peek(ep), "^") == 0) {
        ep_take(ep);
        long rhs = ep_and(ep);
        val ^= rhs;
    }
    return val;
}

static long ep_or(ExprParser *ep) {
    long val = ep_xor(ep);
    while (strcmp(ep_peek(ep), "|") == 0) {
        ep_take(ep);
        long rhs = ep_xor(ep);
        val |= rhs;
    }
    return val;
}

static long ep_eval_and(ExprParser *ep) {
    long val = ep_or(ep);
    while (strcmp(ep_peek(ep), "&&") == 0) {
        ep_take(ep);
        long rhs = ep_or(ep);
        val = val && rhs;
    }
    return val;
}

static long ep_eval_or(ExprParser *ep) {
    long val = ep_eval_and(ep);
    while (strcmp(ep_peek(ep), "||") == 0) {
        ep_take(ep);
        long rhs = ep_eval_and(ep);
        val = val || rhs;
    }
    return val;
}

static long eval_preproc_expr(const char *expr_str, const HashMap *macros, Arena *arena) {
    // defined() replacement
    StringBuilder sb;
    sb_init(&sb);
    size_t len = strlen(expr_str);
    size_t i = 0;
    while (i < len) {
        if (i + 7 <= len && strncmp(expr_str + i, "defined", 7) == 0 && (i + 7 == len || !is_alnum(expr_str[i+7]))) {
            i += 7;
            while (i < len && is_space(expr_str[i])) i++;
            int has_paren = 0;
            if (i < len && expr_str[i] == '(') {
                has_paren = 1;
                i++;
            }
            while (i < len && is_space(expr_str[i])) i++;
            size_t start = i;
            while (i < len && (is_alnum(expr_str[i]) || expr_str[i] == '_')) i++;
            const char *name = arena_strndup(arena, expr_str + start, i - start);
            while (i < len && is_space(expr_str[i])) i++;
            if (has_paren && i < len && expr_str[i] == ')') {
                i++;
            }
            int exists_macro = hashmap_has((HashMap *)macros, name);
            sb_append(&sb, exists_macro ? "1" : "0");
        } else {
            sb_append_char(&sb, expr_str[i]);
            i++;
        }
    }
    const char *s_defined = sb_to_string(&sb, arena);
    sb_free(&sb);

    TokenArray expanded = lex(s_defined, (HashMap *)macros, nullptr, arena);
    sb_init(&sb);
    for (int tok_i = 0; tok_i < expanded.count; ++tok_i) {
        if (strcmp(expanded.data[tok_i].text, "EOF") != 0) {
            sb_append(&sb, expanded.data[tok_i].text);
            sb_append(&sb, " ");
        }
    }
    token_array_free(&expanded);
    const char *s_expanded = sb_to_string(&sb, arena);
    sb_free(&sb);

    StringBuilder res;
    sb_init(&res);
    len = strlen(s_expanded);
    i = 0;
    while (i < len) {
        if (is_alpha(s_expanded[i])) {
            size_t start = i;
            while (i < len && (is_alnum(s_expanded[i]) || s_expanded[i] == '_')) i++;
            const char *ident = arena_strndup(arena, s_expanded + start, i - start);
            HashMapEntry *me = hashmap_get((HashMap *)macros, ident);
            if (me) {
                Macro *m = (Macro *)me->val_ptr;
                if (!m->is_function_like) {
                    sb_append(&res, m->body);
                } else {
                    sb_append(&res, "0");
                }
            } else if (strcmp(ident, "true") == 0) {
                sb_append(&res, "1");
            } else if (strcmp(ident, "false") == 0) {
                sb_append(&res, "0");
            } else {
                sb_append(&res, "0");
            }
        } else {
            sb_append_char(&res, s_expanded[i]);
            i++;
        }
    }
    const char *res_str = sb_to_string(&res, arena);
    sb_free(&res);

    ExprParser parser;
    parser.tokens = get_expr_tokens(res_str, arena);
    parser.pos = 0;
    long val = ep_eval_or(&parser);
    string_array_free(&parser.tokens);
    return val;
}

static const char *find_include_file(const char *name, bool is_angled, const char *current_file_dir, const StringArray *include_dirs, Arena *arena) {
    char path[1024];
    if (!is_angled) {
        snprintf(path, sizeof(path), "%s/%s", current_file_dir, name);
        if (exists(path)) return arena_strdup(arena, path);
    }
    for (int idx = 0; idx < include_dirs->count; ++idx) {
        snprintf(path, sizeof(path), "%s/%s", include_dirs->data[idx], name);
        if (exists(path)) return arena_strdup(arena, path);
    }
    return nullptr;
}

static const char *strip_comments(const char *src, Arena *arena) {
    size_t len = strlen(src);
    StringBuilder sb;
    sb_init(&sb);
    size_t i = 0;
    bool in_string = false;
    bool in_char = false;
    while (i < len) {
        if (in_string) {
            if (src[i] == '\\' && i + 1 < len) {
                sb_append_char(&sb, src[i]);
                sb_append_char(&sb, src[i + 1]);
                i += 2;
            } else if (src[i] == '"') {
                in_string = false;
                sb_append_char(&sb, src[i]);
                i++;
            } else {
                sb_append_char(&sb, src[i]);
                i++;
            }
        } else if (in_char) {
            if (src[i] == '\\' && i + 1 < len) {
                sb_append_char(&sb, src[i]);
                sb_append_char(&sb, src[i + 1]);
                i += 2;
            } else if (src[i] == '\'') {
                in_char = false;
                sb_append_char(&sb, src[i]);
                i++;
            } else {
                sb_append_char(&sb, src[i]);
                i++;
            }
        } else {
            if (src[i] == '"') {
                in_string = true;
                sb_append_char(&sb, src[i]);
                i++;
            } else if (src[i] == '\'') {
                in_char = true;
                sb_append_char(&sb, src[i]);
                i++;
            } else if (i + 1 < len && src[i] == '/' && src[i + 1] == '/') {
                i += 2;
                sb_append_char(&sb, ' ');
                while (i < len && src[i] != '\n') {
                    i++;
                }
            } else if (i + 1 < len && src[i] == '/' && src[i + 1] == '*') {
                i += 2;
                sb_append_char(&sb, ' ');
                while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) {
                    if (src[i] == '\n') {
                        sb_append_char(&sb, '\n');
                    }
                    i++;
                }
                if (i + 1 < len) {
                    i += 2;
                }
            } else {
                sb_append_char(&sb, src[i]);
                i++;
            }
        }
    }
    const char *res = sb_to_string(&sb, arena);
    sb_free(&sb);
    return res;
}

static const char *join_continuation_lines(const char *src, Arena *arena) {
    size_t len = strlen(src);
    StringBuilder sb;
    sb_init(&sb);
    size_t i = 0;
    while (i < len) {
        if (src[i] == '\\' && i + 1 < len && src[i + 1] == '\n') {
            i += 2;
        } else if (src[i] == '\\' && i + 2 < len && src[i + 1] == '\r' && src[i + 2] == '\n') {
            i += 3;
        } else {
            sb_append_char(&sb, src[i]);
            i++;
        }
    }
    const char *res = sb_to_string(&sb, arena);
    sb_free(&sb);
    return res;
}

/* ── Dedicated preprocessing-token expander ──────────────────────────────────
 *
 * Operates directly on preprocessing tokens instead of round-tripping through
 * the C lexer's built-in macro engine and re-lexing to a fixpoint. A single
 * left-to-right pass with a per-expansion "active set" (the standard blue-paint
 * recursion guard) replaces object-like and function-like macros, applying `#`
 * (stringize), `##` (paste) and `__VA_ARGS__`. Because expansion is not re-fed
 * through the lexer, the old duplicate-expansion artifact (patched with the
 * `s1 -> s1 ->` string hack) cannot occur, so both the hack and the 8-pass loop
 * are gone. */

static Token pp_tok(const char *text) {
    Token t; t.text = text; t.line = 0; t.col = 0; return t;
}

/* Raw tokenize (no macro expansion), dropping only the trailing end-of-stream
 * sentinel. The lexer uses the literal text "EOF" as that sentinel, so an
 * identifier that happens to be named EOF (e.g. the `EOF` macro) appears as an
 * interior "EOF" token and MUST be kept — only the final sentinel is stripped. */
static TokenArray pp_tokenize(const char *s, Arena *arena) {
    TokenArray raw = lex(s, nullptr, nullptr, arena);
    TokenArray out; token_array_init(&out);
    int last = raw.count - 1;
    for (int i = 0; i < raw.count; ++i) {
        if (i == last && strcmp(raw.data[i].text, "EOF") == 0) continue;
        token_array_push(&out, &raw.data[i]);
    }
    token_array_free(&raw);
    return out;
}

/* "quote" a token list into a single string literal (for the # operator). */
static const char *pp_stringize(const TokenArray *toks, Arena *arena) {
    StringBuilder sb; sb_init(&sb);
    sb_append_char(&sb, '"');
    for (int i = 0; i < toks->count; ++i) {
        if (i > 0) sb_append_char(&sb, ' ');
        for (const char *c = toks->data[i].text; *c; ++c) {
            if (*c == '"' || *c == '\\') sb_append_char(&sb, '\\');
            sb_append_char(&sb, *c);
        }
    }
    sb_append_char(&sb, '"');
    const char *r = sb_to_string(&sb, arena);
    sb_free(&sb);
    return r;
}

/* Paste the text of `next` onto the last token already in `out` (## operator). */
static void pp_paste_onto_last(TokenArray *out, const char *next_text, Arena *arena) {
    if (out->count == 0) { Token t = pp_tok(next_text); token_array_push(out, &t); return; }
    const char *prev = out->data[out->count - 1].text;
    char *joined = arena_alloc(arena, strlen(prev) + strlen(next_text) + 1);
    strcpy(joined, prev); strcat(joined, next_text);
    out->data[out->count - 1].text = intern_string(arena, joined);
}

static TokenArray pp_expand_tokens(TokenArray in, HashMap *macros, Arena *arena);

/* Substitute a function-like macro body with the collected argument token lists. */
static TokenArray pp_substitute(const Macro *m, TokenArray *args, int argc, HashMap *macros, Arena *arena) {
    int vararg_idx = -1;
    for (int i = 0; i < m->params.count; ++i)
        if (strcmp(m->params.data[i], "...") == 0) { vararg_idx = i; break; }

    TokenArray body = pp_tokenize(m->body, arena);
    TokenArray out; token_array_init(&out);
    int pending_paste = 0;

    for (int k = 0; k < body.count; ++k) {
        const char *bt = body.data[k].text;

        /* # param  →  stringized argument */
        if (strcmp(bt, "#") == 0 && k + 1 < body.count) {
            const char *pname = body.data[k + 1].text;
            int pidx = -1;
            for (int p = 0; p < m->params.count; ++p)
                if (strcmp(m->params.data[p], pname) == 0) { pidx = p; break; }
            if (strcmp(pname, "__VA_ARGS__") == 0 && vararg_idx >= 0) pidx = vararg_idx;
            if (pidx >= 0) {
                TokenArray joined; token_array_init(&joined);
                for (int a = pidx; a < argc; ++a) {
                    if (a > pidx) { Token c = pp_tok(","); token_array_push(&joined, &c); }
                    for (int t = 0; t < args[a].count; ++t) token_array_push(&joined, &args[a].data[t]);
                    if (pidx != vararg_idx) break;
                }
                Token s = pp_tok(pp_stringize(&joined, arena));
                token_array_free(&joined);
                token_array_push(&out, &s);
                k++;
                continue;
            }
        }

        if (strcmp(bt, "##") == 0) { pending_paste = 1; continue; }

        /* Determine the substitution token list for this body token. */
        int pidx = -1;
        for (int p = 0; p < m->params.count; ++p)
            if (strcmp(m->params.data[p], bt) == 0) { pidx = p; break; }
        if (strcmp(bt, "__VA_ARGS__") == 0 && vararg_idx >= 0) pidx = vararg_idx;

        TokenArray sub; token_array_init(&sub);
        if (pidx >= 0) {
            if (pidx == vararg_idx) {
                for (int a = pidx; a < argc; ++a) {
                    if (a > pidx) { Token c = pp_tok(","); token_array_push(&sub, &c); }
                    for (int t = 0; t < args[a].count; ++t) token_array_push(&sub, &args[a].data[t]);
                }
            } else if (pidx < argc) {
                for (int t = 0; t < args[pidx].count; ++t) token_array_push(&sub, &args[pidx].data[t]);
            }
            /* Argument prescan: a parameter that is NOT an operand of # or ## is
             * fully macro-expanded before substitution (C standard 6.10.3.1).
             * Operands of ## use the raw argument so pasting sees literal tokens. */
            int is_paste_operand = pending_paste ||
                (k + 1 < body.count && strcmp(body.data[k + 1].text, "##") == 0);
            if (!is_paste_operand) {
                TokenArray pre = pp_expand_tokens(sub, macros, arena);
                token_array_free(&sub);
                sub = pre;
            }
        } else {
            Token t = pp_tok(bt);
            token_array_push(&sub, &t);
        }

        for (int t = 0; t < sub.count; ++t) {
            if (pending_paste && t == 0) {
                pp_paste_onto_last(&out, sub.data[t].text, arena);
            } else {
                token_array_push(&out, &sub.data[t]);
            }
        }
        pending_paste = 0;
        token_array_free(&sub);
    }
    token_array_free(&body);
    return out;
}

static int pp_hide_has(StringArray *h, const char *n) {
    if (!h) return 0;
    for (int i = 0; i < h->count; ++i) if (strcmp(h->data[i], n) == 0) return 1;
    return 0;
}
static StringArray *pp_hide_add(StringArray *h, const char *n, Arena *arena) {
    StringArray *r = arena_alloc(arena, sizeof(StringArray));
    string_array_init(r);
    if (h) for (int i = 0; i < h->count; ++i) string_array_push(r, h->data[i]);
    if (!pp_hide_has(r, n)) string_array_push(r, n);
    return r;
}

/* Fully expand a token list using per-token hidesets (the standard blue-paint
 * recursion guard). A macro replacement is spliced back into the front of the
 * remaining stream and re-examined together with the tokens that follow — so an
 * object-like macro that expands to a function-like macro name picks up its
 * `(...)` arguments from the surrounding text (the M21 X-macro alias case). */
static TokenArray pp_expand_tokens(TokenArray in, HashMap *macros, Arena *arena) {
    int rn = in.count;
    const char **rtxt = malloc(sizeof(char *) * (rn > 0 ? rn : 1));
    StringArray **rhide = malloc(sizeof(StringArray *) * (rn > 0 ? rn : 1));
    for (int i = 0; i < rn; ++i) { rtxt[i] = in.data[i].text; rhide[i] = nullptr; }

    TokenArray out; token_array_init(&out);
    int head = 0;
    int steps = 0, step_cap = 100000;   /* safety net against pathological macro sets */

    while (head < rn) {
        if (++steps > step_cap) break;
        const char *name = rtxt[head];
        HashMapEntry *me = macros ? hashmap_get(macros, name) : nullptr;
        if (!me || pp_hide_has(rhide[head], name)) {
            Token t = pp_tok(name); token_array_push(&out, &t); head++; continue;
        }
        Macro *m = (Macro *)me->val_ptr;

        TokenArray repl;
        int end;   /* first index in the remaining stream after the invocation */
        if (!m->is_function_like) {
            TokenArray body = pp_tokenize(m->body, arena);
            TokenArray pasted; token_array_init(&pasted);
            for (int k = 0; k < body.count; ++k) {
                if (strcmp(body.data[k].text, "##") == 0 && pasted.count > 0 && k + 1 < body.count) {
                    pp_paste_onto_last(&pasted, body.data[k + 1].text, arena);
                    k++;
                } else {
                    token_array_push(&pasted, &body.data[k]);
                }
            }
            token_array_free(&body);
            repl = pasted;
            end = head + 1;
        } else {
            if (head + 1 >= rn || strcmp(rtxt[head + 1], "(") != 0) {
                Token t = pp_tok(name); token_array_push(&out, &t); head++; continue;
            }
            int j = head + 2, depth = 0;
            TokenArray args[64]; int argc = 0;
            token_array_init(&args[0]);
            for (; j < rn; ++j) {
                const char *t = rtxt[j];
                Token tk = pp_tok(t);
                if (strcmp(t, "(") == 0) { depth++; token_array_push(&args[argc], &tk); }
                else if (strcmp(t, ")") == 0) {
                    if (depth == 0) { argc++; break; }
                    depth--; token_array_push(&args[argc], &tk);
                } else if (strcmp(t, ",") == 0 && depth == 0) {
                    argc++;
                    if (argc < 64) token_array_init(&args[argc]);
                } else {
                    token_array_push(&args[argc], &tk);
                }
            }
            if (argc == 1 && args[0].count == 0) argc = 0;
            repl = pp_substitute(m, args, argc, macros, arena);
            end = (j < rn) ? j + 1 : rn;   /* through the closing ')' */
        }

        /* Splice: new remaining = repl (hidden for `name`) ++ old remaining[end..]. */
        StringArray *nh = pp_hide_add(rhide[head], name, arena);
        int tail = rn - end;
        int nn = repl.count + tail;
        const char **ntxt = malloc(sizeof(char *) * (nn > 0 ? nn : 1));
        StringArray **nhide = malloc(sizeof(StringArray *) * (nn > 0 ? nn : 1));
        for (int k = 0; k < repl.count; ++k) { ntxt[k] = repl.data[k].text; nhide[k] = nh; }
        for (int k = 0; k < tail; ++k) { ntxt[repl.count + k] = rtxt[end + k]; nhide[repl.count + k] = rhide[end + k]; }
        token_array_free(&repl);
        free(rtxt); free(rhide);
        rtxt = ntxt; rhide = nhide; rn = nn; head = 0;
    }

    free(rtxt); free(rhide);
    return out;
}

static const char *expand_active_line(const char *line, HashMap *macros, Arena *arena) {
    TokenArray toks = pp_tokenize(line, arena);
    TokenArray expanded = pp_expand_tokens(toks, macros, arena);
    token_array_free(&toks);

    StringBuilder sb; sb_init(&sb);
    for (int i = 0; i < expanded.count; ++i) {
        sb_append(&sb, expanded.data[i].text);
        sb_append_char(&sb, ' ');   /* single-space join never accidentally pastes tokens */
    }
    token_array_free(&expanded);
    const char *r = sb_to_string(&sb, arena);
    sb_free(&sb);
    return r;
}

const char *preprocessor_preprocess(const char *raw_src, const char *filepath, StringArray *include_dirs, HashMap *macros, HashMap *included_files, Arena *arena) {
    int parent_line = preprocessor_current_line;
    const char *parent_file = preprocessor_current_file;
    preprocessor_current_line = 1;
    preprocessor_current_file = filepath;

    const char *src = join_continuation_lines(strip_comments(raw_src, arena), arena);
    StringBuilder out;
    sb_init(&out);

    CondStateArray cond_stack;
    cond_state_array_init(&cond_stack);

    #define IS_ACTIVE() is_active(&cond_stack)

    char current_file_dir[512] = ".";
    const char *last_slash1 = strrchr(filepath, '/');
    const char *last_slash2 = strrchr(filepath, '\\');
    const char *last_slash = last_slash1 > last_slash2 ? last_slash1 : last_slash2;
    if (last_slash) {
        size_t len_dir = last_slash - filepath;
        if (len_dir < sizeof(current_file_dir)) {
            memcpy(current_file_dir, filepath, len_dir);
            current_file_dir[len_dir] = '\0';
        }
    }

    size_t src_len = strlen(src);
    size_t i = 0;
    while (i < src_len) {
        // Read line
        size_t line_end = i;
        while (line_end < src_len && src[line_end] != '\n') {
            line_end++;
        }
        size_t next_i = line_end < src_len ? line_end + 1 : src_len;
        const char *line = arena_strndup(arena, src + i, line_end - i);
        size_t line_len = strlen(line);

        size_t first_non_ws = 0;
        while (first_non_ws < line_len && (line[first_non_ws] == ' ' || line[first_non_ws] == '\t')) {
            first_non_ws++;
        }

        if (first_non_ws < line_len && line[first_non_ws] == '#') {
            size_t p = first_non_ws + 1;
            while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
            size_t dir_start = p;
            while (p < line_len && is_alpha(line[p])) p++;
            const char *directive = arena_strndup(arena, line + dir_start, p - dir_start);

            while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;

            if (strcmp(directive, "ifdef") == 0) {
                size_t name_start = p;
                while (p < line_len && (is_alnum(line[p]) || line[p] == '_')) p++;
                const char *name = arena_strndup(arena, line + name_start, p - name_start);
                bool cond = hashmap_has(macros, name);
                bool parent_active = IS_ACTIVE();
                push_cond(&cond_stack, cond, cond && parent_active);
                sb_append(&out, "\n");
            } else if (strcmp(directive, "ifndef") == 0) {
                size_t name_start = p;
                while (p < line_len && (is_alnum(line[p]) || line[p] == '_')) p++;
                const char *name = arena_strndup(arena, line + name_start, p - name_start);
                bool cond = !hashmap_has(macros, name);
                bool parent_active = IS_ACTIVE();
                push_cond(&cond_stack, cond, cond && parent_active);
                sb_append(&out, "\n");
            } else if (strcmp(directive, "if") == 0) {
                bool cond = eval_preproc_expr(line + p, macros, arena) != 0;
                bool parent_active = IS_ACTIVE();
                push_cond(&cond_stack, cond, cond && parent_active);
                sb_append(&out, "\n");
            } else if (strcmp(directive, "elif") == 0) {
                if (cond_stack.count == 0) {
                    diagnostics_fatal("unmatched #elif");
                }
                bool parent_active = true;
                for (int idx = 0; idx + 1 < cond_stack.count; ++idx) {
                    if (!cond_stack.data[idx].active) parent_active = false;
                }
                if (cond_stack.data[cond_stack.count - 1].condition_met) {
                    cond_stack.data[cond_stack.count - 1].active = false;
                } else {
                    bool cond = eval_preproc_expr(line + p, macros, arena) != 0;
                    cond_stack.data[cond_stack.count - 1].active = cond && parent_active;
                    if (cond) {
                        cond_stack.data[cond_stack.count - 1].condition_met = true;
                    }
                }
                sb_append(&out, "\n");
            } else if (strcmp(directive, "else") == 0) {
                if (cond_stack.count == 0) {
                    diagnostics_fatal("unmatched #else");
                }
                bool parent_active = true;
                for (int idx = 0; idx + 1 < cond_stack.count; ++idx) {
                    if (!cond_stack.data[idx].active) parent_active = false;
                }
                cond_stack.data[cond_stack.count - 1].active = !cond_stack.data[cond_stack.count - 1].condition_met && parent_active;
                cond_stack.data[cond_stack.count - 1].condition_met = true;
                sb_append(&out, "\n");
            } else if (strcmp(directive, "endif") == 0) {
                if (cond_stack.count == 0) {
                    diagnostics_fatal("unmatched #endif");
                }
                cond_stack.count = cond_stack.count - 1;
                sb_append(&out, "\n");
            } else if (strcmp(directive, "define") == 0 && IS_ACTIVE()) {
                size_t name_start = p;
                while (p < line_len && (is_alnum(line[p]) || line[p] == '_')) p++;
                const char *name = arena_strndup(arena, line + name_start, p - name_start);

                Macro *m = arena_alloc(arena, sizeof(Macro));
                m->is_function_like = false;
                string_array_init(&m->params);

                if (p < line_len && line[p] == '(') {
                    m->is_function_like = true;
                    p++;
                    while (p < line_len && line[p] != ')') {
                        while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
                        if (p + 3 <= line_len && strncmp(line + p, "...", 3) == 0) {
                            string_array_push(&m->params, "...");
                            p += 3;
                        } else {
                            size_t param_start = p;
                            while (p < line_len && (is_alnum(line[p]) || line[p] == '_')) p++;
                            if (p == param_start) {
                                p++;
                            } else {
                                string_array_push(&m->params, arena_strndup(arena, line + param_start, p - param_start));
                            }
                        }
                        while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
                        if (p < line_len && line[p] == ',') p++;
                    }
                    if (p < line_len && line[p] == ')') p++;
                }

                while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
                const char *body = line + p;
                const char *comment_pos = strstr(body, "//");
                if (comment_pos) {
                    body = arena_strndup(arena, body, comment_pos - body);
                }
                size_t body_len = strlen(body);
                while (body_len > 0 && (body[body_len - 1] == ' ' || body[body_len - 1] == '\t' || body[body_len - 1] == '\r')) {
                    body_len--;
                }
                m->body = arena_strndup(arena, body, body_len);

                hashmap_put(macros, name, m, 0);
                sb_append(&out, "\n");
            } else if (strcmp(directive, "undef") == 0 && IS_ACTIVE()) {
                size_t name_start = p;
                while (p < line_len && (is_alnum(line[p]) || line[p] == '_')) p++;
                const char *name = arena_strndup(arena, line + name_start, p - name_start);
                hashmap_remove(macros, name);
                sb_append(&out, "\n");
            } else if (strcmp(directive, "include") == 0 && IS_ACTIVE()) {
                bool is_angled = false;
                const char *filename = "";
                if (p < line_len && line[p] == '<') {
                    is_angled = true;
                    p++;
                    size_t fn_start = p;
                    while (p < line_len && line[p] != '>') p++;
                    filename = arena_strndup(arena, line + fn_start, p - fn_start);
                } else if (p < line_len && line[p] == '"') {
                    p++;
                    size_t fn_start = p;
                    while (p < line_len && line[p] != '"') p++;
                    filename = arena_strndup(arena, line + fn_start, p - fn_start);
                }

                const char *inc_path = find_include_file(filename, is_angled, current_file_dir, include_dirs, arena);
                if (!inc_path) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "cannot find include file %s", filename);
                    diagnostics_fatal(msg);
                }

                int is_host_system = (strncmp(inc_path, "/usr/include", 12) == 0 ||
                                      strncmp(inc_path, "/Library", 8) == 0 ||
                                      strncmp(inc_path, "/System", 7) == 0 ||
                                      strncmp(inc_path, "/Applications", 13) == 0);

                if (!is_host_system) {
                    FILE *in = fopen(inc_path, "r");
                    if (!in) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "cannot open include file %s", inc_path);
                        diagnostics_fatal(msg);
                    }
                    // Read file contents
                    fseek(in, 0, SEEK_END);
                    long f_size = ftell(in);
                    fseek(in, 0, SEEK_SET);
                    char *inc_content = malloc(f_size + 1);
                    size_t read_bytes = fread(inc_content, 1, f_size, in);
                    inc_content[read_bytes] = '\0';
                    fclose(in);

                    const char *preprocessed = preprocessor_preprocess(inc_content, inc_path, include_dirs, macros, included_files, arena);
                    free(inc_content);
                    sb_append(&out, preprocessed);
                }
                sb_append(&out, "\n");
            } else {
                sb_append(&out, "\n");
            }
        } else {
            if (IS_ACTIVE()) {
                StringBuilder logical;
                sb_init(&logical);
                sb_append(&logical, line);
                int paren_depth = 0;
                for (size_t c_i = 0; line[c_i]; ++c_i) {
                    if (line[c_i] == '(') paren_depth++;
                    else if (line[c_i] == ')' && paren_depth > 0) paren_depth--;
                }
                while (paren_depth > 0 && next_i < src_len) {
                    size_t extra_end = next_i;
                    while (extra_end < src_len && src[extra_end] != '\n') {
                        extra_end++;
                    }
                    const char *extra = arena_strndup(arena, src + next_i, extra_end - next_i);
                    size_t extra_first = 0;
                    while (extra[extra_first] == ' ' || extra[extra_first] == '\t') {
                        extra_first++;
                    }
                    if (extra[extra_first] == '#') {
                        break;
                    }
                    sb_append(&logical, " ");
                    sb_append(&logical, extra);
                    for (size_t c_i = 0; extra[c_i]; ++c_i) {
                        if (extra[c_i] == '(') paren_depth++;
                        else if (extra[c_i] == ')' && paren_depth > 0) paren_depth--;
                    }
                    next_i = extra_end < src_len ? extra_end + 1 : src_len;
                    preprocessor_current_line++;
                }
                const char *logical_line = sb_to_string(&logical, arena);
                sb_free(&logical);
                sb_append(&out, expand_active_line(logical_line, macros, arena));
                sb_append(&out, "\n");
            } else {
                sb_append(&out, "\n");
            }
        }

        preprocessor_current_line++;
        i = next_i;
    }

    cond_state_array_free(&cond_stack);
    const char *res = sb_to_string(&out, arena);
    sb_free(&out);
    preprocessor_current_line = parent_line;
    preprocessor_current_file = parent_file;
    return res;
}
