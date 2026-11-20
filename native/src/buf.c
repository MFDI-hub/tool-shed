#include "uvs.h"

#include <stdlib.h>
#include <string.h>

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t cap;
    size_t used;
    char data[];
} ArenaChunk;

struct Arena {
    ArenaChunk *first;
    ArenaChunk *current;
};

static size_t align_up(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

static ArenaChunk *chunk_new(size_t cap) {
    ArenaChunk *chunk = malloc(sizeof(ArenaChunk) + cap);
    if (!chunk) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    chunk->next = NULL;
    chunk->cap = cap;
    chunk->used = 0;
    return chunk;
}

Arena *arena_new(void) {
    Arena *a = calloc(1, sizeof(Arena));
    if (!a) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    a->first = chunk_new(64 * 1024);
    a->current = a->first;
    return a;
}

void arena_reset(Arena *a) {
    ArenaChunk *chunk = a->first->next;
    while (chunk) {
        ArenaChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    a->first->next = NULL;
    a->first->used = 0;
    a->current = a->first;
}

void arena_free(Arena *a) {
    if (!a) {
        return;
    }
    ArenaChunk *chunk = a->first;
    while (chunk) {
        ArenaChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    free(a);
}

void *arena_alloc(Arena *a, size_t n) {
    if (n == 0) {
        n = 1;
    }
    n = align_up(n);
    if (a->current->used + n > a->current->cap) {
        size_t cap = a->current->cap * 2;
        if (cap < n + 64) {
            cap = n + 64 * 1024;
        }
        ArenaChunk *chunk = chunk_new(cap);
        a->current->next = chunk;
        a->current = chunk;
    }
    void *p = a->current->data + a->current->used;
    a->current->used += n;
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    return arena_strndup(a, s, s ? strlen(s) : 0);
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *out = arena_alloc(a, n + 1);
    if (s && n) {
        memcpy(out, s, n);
    }
    out[n] = 0;
    return out;
}

char *arena_sprintf(Arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) {
        va_end(ap);
        return arena_strdup(a, "");
    }
    char *out = arena_alloc(a, (size_t)n + 1);
    vsnprintf(out, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return out;
}

void strlist_init(StrList *list, Arena *arena) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
    list->arena = arena;
}

void strlist_push(StrList *list, const char *s) {
    if (list->len + 1 > list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 8;
        const char **items = arena_alloc(list->arena, cap * sizeof(*items));
        if (list->items) {
            memcpy(items, list->items, list->len * sizeof(*items));
        }
        list->items = items;
        list->cap = cap;
    }
    list->items[list->len++] = s;
}

static int cmp_cstr(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

void strlist_sort(StrList *list) {
    if (list->len > 1) {
        qsort(list->items, list->len, sizeof(*list->items), cmp_cstr);
    }
}

int strlist_contains(const StrList *list, const char *s) {
    for (size_t i = 0; i < list->len; i++) {
        if (path_equal(list->items[i], s)) {
            return 1;
        }
    }
    return 0;
}

void strlist_copy(Arena *a, StrList *dst, const StrList *src) {
    strlist_init(dst, a);
    if (!src) {
        return;
    }
    for (size_t i = 0; i < src->len; i++) {
        strlist_push(dst, arena_strdup(a, src->items[i]));
    }
}

void buf_init(Buf *buf, Arena *arena) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->arena = arena;
}

static void buf_reserve(Buf *buf, size_t extra) {
    if (buf->len + extra + 1 <= buf->cap) {
        return;
    }
    size_t cap = buf->cap ? buf->cap : 256;
    while (buf->len + extra + 1 > cap) {
        cap *= 2;
    }
    char *data = arena_alloc(buf->arena, cap);
    if (buf->data) {
        memcpy(data, buf->data, buf->len);
    }
    buf->data = data;
    buf->cap = cap;
}

void buf_append(Buf *buf, const char *s, size_t n) {
    if (!s || n == 0) {
        buf_reserve(buf, 0);
        if (buf->data) {
            buf->data[buf->len] = 0;
        }
        return;
    }
    buf_reserve(buf, n);
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
    buf->data[buf->len] = 0;
}

void buf_append_str(Buf *buf, const char *s) {
    if (s) {
        buf_append(buf, s, strlen(s));
    }
}

void buf_append_char(Buf *buf, char c) {
    buf_append(buf, &c, 1);
}

void buf_append_fmt(Buf *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, aq);
    va_end(aq);
    if (n < 0) {
        va_end(ap);
        return;
    }
    buf_reserve(buf, (size_t)n);
    vsnprintf(buf->data + buf->len, (size_t)n + 1, fmt, ap);
    buf->len += (size_t)n;
    va_end(ap);
}

void buf_null_terminate(Buf *buf) {
    buf_reserve(buf, 0);
    if (buf->data) {
        buf->data[buf->len] = 0;
    } else {
        buf_append(buf, "", 0);
    }
}

int uvs_utf8_valid(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        size_t need = 0;
        uint32_t minp = 0;
        uint32_t cp = 0;
        if (c <= 0x7F) {
            i++;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            need = 1;
            minp = 0x80;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            minp = 0x800;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            minp = 0x10000;
            cp = c & 0x07;
        } else {
            return 0;
        }
        if (i + need >= n) {
            return 0;
        }
        for (size_t k = 1; k <= need; k++) {
            unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) {
                return 0;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < minp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return 0;
        }
        i += need + 1;
    }
    return 1;
}

int uvs_has_nul(const char *s, size_t n, size_t probe) {
    size_t lim = n < probe ? n : probe;
    return memchr(s, 0, lim) != NULL;
}

int uvs_is_non_text(const char *s, size_t n) {
    return uvs_has_nul(s, n, UVS_TEXT_PROBE) || !uvs_utf8_valid(s, n);
}
