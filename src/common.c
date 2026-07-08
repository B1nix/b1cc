#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// --- Arena Allocator ---

#define ARENA_CHUNK_DEFAULT_CAPACITY (64 * 1024)

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void arena_init(Arena *a) {
    a->head = nullptr;
}

void *arena_alloc(Arena *a, size_t size) {
    size = align_up(size, alignof(max_align_t));

    if (!a->head || a->head->offset + size > a->head->capacity) {
        size_t cap = ARENA_CHUNK_DEFAULT_CAPACITY;
        if (size > cap) {
            cap = size;
        }
        ArenaChunk *chunk = malloc(sizeof(ArenaChunk));
        chunk->bytes = malloc(cap);
        chunk->offset = 0;
        chunk->capacity = cap;
        chunk->next = a->head;
        a->head = chunk;
    }

    void *ptr = &a->head->bytes[a->head->offset];
    a->head->offset = a->head->offset + size;
    return ptr;
}

void arena_free(Arena *a) {
    ArenaChunk *curr = a->head;
    while (curr) {
        ArenaChunk *next = curr->next;
        free(curr->bytes);
        free(curr);
        curr = next;
    }
    a->head = nullptr;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return nullptr;
    size_t len = strlen(s);
    char *res = arena_alloc(a, len + 1);
    memcpy(res, s, len + 1);
    return res;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    if (!s) return nullptr;
    char *res = arena_alloc(a, n + 1);
    memcpy(res, s, n);
    res[n] = '\0';
    return res;
}

// --- String Builder ---

void sb_init(StringBuilder *sb) {
    sb->buf = nullptr;
    sb->len = 0;
    sb->cap = 0;
}

void sb_append(StringBuilder *sb, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    size_t needed = sb->len + slen + 1;
    if (needed > sb->cap) {
        size_t new_cap = needed + 16;
        sb->cap = new_cap;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, slen);
    sb->len = sb->len + slen;
    sb->buf[sb->len] = '\0';
}

void sb_append_n(StringBuilder *sb, const char *s, size_t slen) {
    if (!s || slen == 0) return;
    size_t needed = sb->len + slen + 1;
    if (needed > sb->cap) {
        size_t new_cap = needed + 16;
        sb->cap = new_cap;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, slen);
    sb->len = sb->len + slen;
    sb->buf[sb->len] = '\0';
}

void sb_append_char(StringBuilder *sb, char c) {
    size_t needed = sb->len + 2;
    if (needed > sb->cap) {
        size_t new_cap = needed + 16;
        sb->cap = new_cap;
        sb->buf = realloc(sb->buf, sb->cap);
    }
    sb->buf[sb->len] = c;
    sb->len = sb->len + 1;
    sb->buf[sb->len] = '\0';
}

#ifdef __b1cc__
void sb_appendf(StringBuilder *sb, const char *fmt, long a1, long a2, long a3, long a4, long a5, long a6) {
    char temp[2048];
    int needed = snprintf(temp, sizeof(temp), fmt, a1, a2, a3, a4, a5, a6);
    if (needed > 0) {
        sb_append(sb, temp);
    }
}
#else
void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
    char stack_buf[512];
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    if (needed < 0) return;

    if ((size_t)needed < sizeof(stack_buf)) {
        sb_append(sb, stack_buf);
        return;
    }

    if (sb->len + needed + 1 > sb->cap) {
        sb->cap = sb->cap * 2 + needed + 16;
        sb->buf = realloc(sb->buf, sb->cap);
    }

    va_start(args, fmt);
    vsnprintf(sb->buf + sb->len, needed + 1, fmt, args);
    va_end(args);
    sb->len = sb->len + needed;
}
#endif

char *sb_to_string(StringBuilder *sb, Arena *a) {
    if (!sb->buf) return arena_strdup(a, "");
    return arena_strndup(a, sb->buf, sb->len);
}

void sb_free(StringBuilder *sb) {
    free(sb->buf);
    sb->buf = nullptr;
    sb->len = 0;
    sb->cap = 0;
}

const char *escape_asm_string(const char *s, Arena *arena) {
    StringBuilder sb;
    sb_init(&sb);
    size_t len = strlen(s);
    size_t i = 0;
    while (i < len) {
        if (s[i] == '\\' && i + 1 < len) {
            char next = s[i + 1];
            if (next == '\'' || next == '?') {
                sb_append_char(&sb, next);
            } else {
                sb_append_char(&sb, '\\');
                sb_append_char(&sb, next);
            }
            i += 2;
        } else {
            sb_append_char(&sb, s[i]);
            i++;
        }
    }
    const char *res = sb_to_string(&sb, arena);
    sb_free(&sb);
    return res;
}

// --- Hash Map ---

static unsigned int hash_key(const char *str) {
    unsigned int h = 2166136261U;
    while (*str) {
        h ^= (unsigned char)*str++;
        h *= 16777619;
    }
    return h;
}

static HashMap intern_map;
static int intern_map_initialized = 0;
static void hashmap_pool_free(void) {
    if (intern_map_initialized) {
        if (intern_map.entries) {
            free(intern_map.entries);
            intern_map.entries = nullptr;
        }
        intern_map_initialized = 0;
    }
}

const char *intern_string_n(Arena *arena, const char *s, size_t len) {
    if (!s) return NULL;
    if (!intern_map_initialized) {
        hashmap_init(&intern_map, 4096);
        intern_map_initialized = 1;
        atexit(hashmap_pool_free);
    }
    char *tmp = malloc(len + 1);
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    HashMapEntry *entry = hashmap_get(&intern_map, tmp);
    if (entry) {
        free(tmp);
        return entry->key;
    }
    char *interned = arena_alloc(arena, len + 1);
    memcpy(interned, tmp, len + 1);
    free(tmp);
    hashmap_put(&intern_map, interned, (void *)interned, 0);
    return interned;
}

const char *intern_string(Arena *arena, const char *s) {
    if (!s) return NULL;
    return intern_string_n(arena, s, strlen(s));
}

void hashmap_init(HashMap *map, int bucket_count) {
    map->bucket_count = bucket_count;
    map->entries = calloc(bucket_count, sizeof(HashMapEntry));
    map->size = 0;
}

void hashmap_free(HashMap *map) {
    if (!map->entries) return;
    free(map->entries);
    map->entries = nullptr;
    map->bucket_count = 0;
    map->size = 0;
}

static void hashmap_resize(HashMap *map, int new_bucket_count) {
    HashMapEntry *old_entries = map->entries;
    int old_count = map->bucket_count;

    map->bucket_count = new_bucket_count;
    map->entries = calloc(new_bucket_count, sizeof(HashMapEntry));
    map->size = 0;

    for (int i = 0; i < old_count; ++i) {
        HashMapEntry *entry = &old_entries[i];
        if (entry->key && entry->key != TOMBSTONE) {
            hashmap_put(map, entry->key, entry->val_ptr, entry->val_int);
        }
    }

    free(old_entries);
}

void hashmap_put(HashMap *map, const char *key, void *val_ptr, long val_int) {
    if (map->size > map->bucket_count * 0.75) {
        hashmap_resize(map, map->bucket_count * 2);
    }

    unsigned int h = hash_key(key) % map->bucket_count;
    int tombstone_idx = -1;

    for (int i = 0; i < map->bucket_count; ++i) {
        int idx = (h + i) % map->bucket_count;
        HashMapEntry *entry = &map->entries[idx];
        if (!entry->key) {
            int dest = (tombstone_idx != -1) ? tombstone_idx : idx;
            map->entries[dest].key = key;
            map->entries[dest].val_ptr = val_ptr;
            map->entries[dest].val_int = val_int;
            map->size++;
            return;
        }
        if (entry->key == TOMBSTONE) {
            if (tombstone_idx == -1) {
                tombstone_idx = idx;
            }
            continue;
        }
        if (strcmp(entry->key, key) == 0) {
            entry->val_ptr = val_ptr;
            entry->val_int = val_int;
            return;
        }
    }

    if (tombstone_idx != -1) {
        map->entries[tombstone_idx].key = key;
        map->entries[tombstone_idx].val_ptr = val_ptr;
        map->entries[tombstone_idx].val_int = val_int;
        map->size++;
    }
}

HashMapEntry *hashmap_get(HashMap *map, const char *key) {
    if (!map->entries) return nullptr;
    unsigned int h = hash_key(key) % map->bucket_count;

    for (int i = 0; i < map->bucket_count; ++i) {
        int idx = (h + i) % map->bucket_count;
        HashMapEntry *entry = &map->entries[idx];
        if (!entry->key) {
            return nullptr;
        }
        if (entry->key == TOMBSTONE) {
            continue;
        }
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
    }
    return nullptr;
}

int hashmap_has(HashMap *map, const char *key) {
    return hashmap_get(map, key) != nullptr;
}

void hashmap_remove(HashMap *map, const char *key) {
    if (!map->entries) return;
    unsigned int h = hash_key(key) % map->bucket_count;

    for (int i = 0; i < map->bucket_count; ++i) {
        int idx = (h + i) % map->bucket_count;
        HashMapEntry *entry = &map->entries[idx];
        if (!entry->key) {
            return;
        }
        if (entry->key == TOMBSTONE) {
            continue;
        }
        if (strcmp(entry->key, key) == 0) {
            entry->key = TOMBSTONE;
            entry->val_ptr = nullptr;
            entry->val_int = 0;
            map->size--;
            return;
        }
    }
}

// --- Basic Dynamic Arrays ---

void string_array_init(StringArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void string_array_push(StringArray *arr, const char *val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(const char *));
    }
    arr->data[arr->count] = val;
    arr->count = arr->count + 1;
}

void string_array_free(StringArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void int_array_init(IntArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void int_array_push(IntArray *arr, int val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(int));
    }
    arr->data[arr->count] = val;
    arr->count = arr->count + 1;
}

void int_array_free(IntArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void long_array_init(LongArray *arr) {
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}

void long_array_push(LongArray *arr, long val) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity * 2 + 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(long));
    }
    arr->data[arr->count] = val;
    arr->count = arr->count + 1;
}

void long_array_free(LongArray *arr) {
    free(arr->data);
    arr->data = nullptr;
    arr->count = 0;
    arr->capacity = 0;
}
