#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __b1cc__
#define B1CC_THREAD_LOCAL
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define B1CC_THREAD_LOCAL thread_local
#else
#define B1CC_THREAD_LOCAL _Thread_local
#endif

static_assert(sizeof(long) >= 8, "b1cc assumes a 64-bit or wider host long");
static_assert(sizeof(void *) <= sizeof(long), "b1cc stores pointer-sized values in long slots");

// --- Arena Allocator ---
typedef struct ArenaChunk {
    char *bytes;
    size_t offset;
    size_t capacity;
    struct ArenaChunk *next;
} ArenaChunk;

typedef struct Arena {
    ArenaChunk *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
void arena_free(Arena *a);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);

// --- String Builder ---
typedef struct StringBuilder {
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

void sb_init(StringBuilder *sb);
void sb_append(StringBuilder *sb, const char *s);
void sb_append_char(StringBuilder *sb, char c);
#ifdef __b1cc__
void sb_appendf(StringBuilder *sb, const char *fmt, long a1, long a2, long a3, long a4, long a5, long a6);
#else
void sb_appendf(StringBuilder *sb, const char *fmt, ...);
#endif
char *sb_to_string(StringBuilder *sb, Arena *a);
void sb_free(StringBuilder *sb);
const char *escape_asm_string(const char *s, Arena *arena);

// --- Hash Map ---
typedef struct HashMapEntry {
    const char *key;
    void *val_ptr;
    long val_int;
    struct HashMapEntry *next;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry **buckets;
    int bucket_count;
    int size;
} HashMap;

void hashmap_init(HashMap *map, int bucket_count);
void hashmap_free(HashMap *map);
void hashmap_put(HashMap *map, const char *key, void *val_ptr, long val_int);
HashMapEntry *hashmap_get(HashMap *map, const char *key);
int hashmap_has(HashMap *map, const char *key);
void hashmap_remove(HashMap *map, const char *key);

// --- Basic Dynamic Arrays ---
typedef struct StringArray {
    const char **data;
    int count;
    int capacity;
} StringArray;

void string_array_init(StringArray *arr);
void string_array_push(StringArray *arr, const char *val);
void string_array_free(StringArray *arr);

typedef struct IntArray {
    int *data;
    int count;
    int capacity;
} IntArray;

void int_array_init(IntArray *arr);
void int_array_push(IntArray *arr, int val);
void int_array_free(IntArray *arr);

typedef struct LongArray {
    long *data;
    int count;
    int capacity;
} LongArray;

void long_array_init(LongArray *arr);
void long_array_push(LongArray *arr, long val);
void long_array_free(LongArray *arr);

#endif // COMMON_H
