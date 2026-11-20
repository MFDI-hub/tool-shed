#include "uvs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *uvs_rel_slash(Arena *a, const char *rel) {
    char *s = arena_strdup(a, rel ? rel : "");
    for (char *p = s; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return s;
}

static int is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static const char *lstrip(const char *s) {
    while (is_space_char(*s)) {
        s++;
    }
    return s;
}

static int starts_word(const char *s, const char *word) {
    size_t n = strlen(word);
    if (strncmp(s, word, n) != 0) {
        return 0;
    }
    char next = s[n];
    return next == 0 || is_space_char(next) || next == '(' || next == ':' || next == '"' || next == '\'' ||
           next == '<' || next == '{';
}

static const char *skip_word(const char *s) {
    while (*s && !is_space_char(*s) && *s != '(' && *s != ':' && *s != '{' && *s != ',' && *s != ';') {
        s++;
    }
    return s;
}

static int line_indent(const char *s) {
    int n = 0;
    while (*s == ' ' || *s == '\t') {
        n += (*s == '\t') ? 4 : 1;
        s++;
    }
    return n;
}

static int fold_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return *a == 0 && *b == 0;
}

static const char *file_ext(const char *path) {
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) {
        return "";
    }
    return dot + 1;
}

static int ext_is_python(const char *ext) {
    return fold_eq(ext, "py") || fold_eq(ext, "pyi") || fold_eq(ext, "pyw");
}

static int ext_is_ruby(const char *ext) {
    return fold_eq(ext, "rb");
}

static int ext_is_js(const char *ext) {
    return fold_eq(ext, "js") || fold_eq(ext, "jsx") || fold_eq(ext, "ts") || fold_eq(ext, "tsx") || fold_eq(ext, "mjs") ||
           fold_eq(ext, "cjs");
}

static int ext_is_c(const char *ext) {
    return fold_eq(ext, "c") || fold_eq(ext, "h") || fold_eq(ext, "cc") || fold_eq(ext, "cpp") || fold_eq(ext, "cxx") ||
           fold_eq(ext, "hpp") || fold_eq(ext, "hh");
}

static void idx_push(Arena *a, size_t **items, size_t *len, size_t *cap, size_t v) {
    for (size_t i = 0; i < *len; i++) {
        if ((*items)[i] == v) {
            return;
        }
    }
    if (*len + 1 > *cap) {
        size_t ncap = *cap ? *cap * 2 : 4;
        size_t *n = arena_alloc(a, ncap * sizeof(*n));
        if (*items && *len) {
            memcpy(n, *items, *len * sizeof(*n));
        }
        *items = n;
        *cap = ncap;
    }
    (*items)[(*len)++] = v;
}

static int find_rel(const UvsCodeGraph *g, const char *rel) {
    if (!rel || !rel[0]) {
        return -1;
    }
    for (size_t i = 0; i < g->len; i++) {
        if (path_equal(g->nodes[i].rel, rel) || strcmp(g->nodes[i].rel, rel) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int uvs_codegraph_index(const UvsCodeGraph *g, const char *rel_or_path) {
    if (!g || !rel_or_path || !rel_or_path[0]) {
        return -1;
    }
    char *want = uvs_rel_slash(g->arena, rel_or_path);
    int idx = find_rel(g, want);
    if (idx >= 0) {
        return idx;
    }
    const char *base = path_basename(want);
    int found = -1;
    int n = 0;
    for (size_t i = 0; i < g->len; i++) {
        if (strcmp(path_basename(g->nodes[i].rel), base) == 0) {
            found = (int)i;
            n++;
        }
        if (uvs_glob_path_match(want, g->nodes[i].rel)) {
            return (int)i;
        }
    }
    return n == 1 ? found : -1;
}

static int try_add_edge(Arena *a, UvsCodeGraph *g, size_t from, const char *cand, size_t *out_cap, size_t *in_cap) {
    int to = find_rel(g, cand);
    if (to < 0 || (size_t)to == from) {
        return 0;
    }
    idx_push(a, &g->nodes[from].importees, &g->nodes[from].n_importees, &out_cap[from], (size_t)to);
    idx_push(a, &g->nodes[to].importers, &g->nodes[to].n_importers, &in_cap[to], from);
    return 1;
}

static char *mod_to_path(Arena *a, const char *mod, const char *ext) {
    char *s = arena_strdup(a, mod);
    for (char *p = s; *p; p++) {
        if (*p == '.') {
            *p = '/';
        }
    }
    return arena_sprintf(a, "%s%s", s, ext);
}

static void resolve_python_mod(Arena *a, UvsCodeGraph *g, size_t from, const char *mod, size_t *out_cap, size_t *in_cap) {
    if (!mod || !mod[0] || mod[0] == '.') {
        return;
    }
    try_add_edge(a, g, from, mod_to_path(a, mod, ".py"), out_cap, in_cap);
    try_add_edge(a, g, from, mod_to_path(a, mod, ".pyi"), out_cap, in_cap);
    try_add_edge(a, g, from, mod_to_path(a, mod, "/__init__.py"), out_cap, in_cap);
}

static void take_ident(const char *s, char *out, size_t outn) {
    size_t n = 0;
    while (*s && (isalnum((unsigned char)*s) || *s == '_' || *s == '.')) {
        if (n + 1 < outn) {
            out[n++] = *s;
        }
        s++;
    }
    out[n] = 0;
}

static void parse_python_import(Arena *a, UvsCodeGraph *g, size_t from, const char *s, size_t *out_cap, size_t *in_cap) {
    s = lstrip(s);
    if (starts_word(s, "import")) {
        s += 6;
        while (*s) {
            while (is_space_char(*s) || *s == ',') {
                s++;
            }
            if (!*s || *s == '#') {
                break;
            }
            if (starts_word(s, "as")) {
                s = skip_word(s);
                continue;
            }
            char mod[256];
            take_ident(s, mod, sizeof(mod));
            if (mod[0]) {
                resolve_python_mod(a, g, from, mod, out_cap, in_cap);
            }
            s = skip_word(s);
            while (is_space_char(*s)) {
                s++;
            }
            if (starts_word(s, "as")) {
                s = skip_word(s);
                while (is_space_char(*s)) {
                    s++;
                }
                s = skip_word(s);
            }
        }
        return;
    }
    if (starts_word(s, "from")) {
        s += 4;
        while (is_space_char(*s)) {
            s++;
        }
        if (*s == '.') {
            return;
        }
        char mod[256];
        take_ident(s, mod, sizeof(mod));
        if (mod[0]) {
            resolve_python_mod(a, g, from, mod, out_cap, in_cap);
        }
    }
}

static const char *extract_quoted(const char *s) {
    const char *q = NULL;
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\'') {
            q = p;
        }
    }
    return q;
}

static char *unquote_copy(Arena *a, const char *q) {
    if (!q || (*q != '"' && *q != '\'')) {
        return NULL;
    }
    char end = *q;
    q++;
    const char *e = q;
    while (*e && *e != end) {
        e++;
    }
    if (e == q) {
        return NULL;
    }
    return arena_strndup(a, q, (size_t)(e - q));
}

static void try_js_path(Arena *a, UvsCodeGraph *g, size_t from, const char *dir, const char *spec, size_t *out_cap,
                       size_t *in_cap) {
    if (!spec || spec[0] != '.') {
        return;
    }
    char *joined = path_join(a, dir, spec);
    const char *cands[] = {"", ".js", ".ts", ".tsx", ".jsx", ".mjs", "/index.js", "/index.ts", "/index.tsx", NULL};
    for (int i = 0; cands[i]; i++) {
        char *p = cands[i][0] ? arena_sprintf(a, "%s%s", joined, cands[i]) : joined;
        try_add_edge(a, g, from, p, out_cap, in_cap);
    }
}

static void parse_js_import(Arena *a, UvsCodeGraph *g, size_t from, const char *s, const char *dir, size_t *out_cap,
                            size_t *in_cap) {
    s = lstrip(s);
    if (!(starts_word(s, "import") || starts_word(s, "export") || strstr(s, "require(") || strstr(s, "from "))) {
        return;
    }
    const char *q = extract_quoted(s);
    char *spec = unquote_copy(a, q);
    if (spec) {
        try_js_path(a, g, from, dir, spec, out_cap, in_cap);
    }
}

static void parse_c_include(Arena *a, UvsCodeGraph *g, size_t from, const char *s, const char *dir, size_t *out_cap,
                            size_t *in_cap) {
    s = lstrip(s);
    if (strncmp(s, "#include", 8) != 0) {
        return;
    }
    s += 8;
    while (is_space_char(*s)) {
        s++;
    }
    if (*s != '"') {
        return;
    }
    char *spec = unquote_copy(a, s);
    if (!spec) {
        return;
    }
    char *joined = path_join(a, dir, spec);
    try_add_edge(a, g, from, joined, out_cap, in_cap);
}

static void parse_ruby_require(Arena *a, UvsCodeGraph *g, size_t from, const char *s, const char *dir, size_t *out_cap,
                               size_t *in_cap) {
    s = lstrip(s);
    if (!starts_word(s, "require_relative")) {
        return;
    }
    const char *q = extract_quoted(s);
    char *spec = unquote_copy(a, q);
    if (!spec) {
        return;
    }
    if (!strrchr(path_basename(spec), '.')) {
        spec = arena_sprintf(a, "%s.rb", spec);
    }
    try_add_edge(a, g, from, path_join(a, dir, spec), out_cap, in_cap);
}

static const char *skip_mods(const char *s) {
    for (;;) {
        if (starts_word(s, "export") || starts_word(s, "pub") || starts_word(s, "public") || starts_word(s, "private") ||
            starts_word(s, "protected") || starts_word(s, "async") || starts_word(s, "static") ||
            starts_word(s, "default") || starts_word(s, "export")) {
            s = skip_word(s);
            while (is_space_char(*s)) {
                s++;
            }
            continue;
        }
        break;
    }
    return s;
}

static int is_sig_kw(const char *s) {
    return starts_word(s, "class") || starts_word(s, "def") || starts_word(s, "function") || starts_word(s, "fn") ||
           starts_word(s, "func") || starts_word(s, "struct") || starts_word(s, "module") || starts_word(s, "interface") ||
           starts_word(s, "type") || starts_word(s, "enum");
}

char *uvs_sig_text(Arena *a, const char *stripped) {
    const char *s = skip_mods(stripped);
    if (!is_sig_kw(s)) {
        return NULL;
    }
    const char *end = s;
    while (*end && *end != '(' && *end != '{' && *end != ':' && *end != ';' && *end != '\n') {
        end++;
    }
    while (end > s && is_space_char(end[-1])) {
        end--;
    }
    if (end <= s) {
        return NULL;
    }
    return arena_strndup(a, s, (size_t)(end - s));
}

void uvs_sigs_from_src(Arena *a, const char *src, size_t n, StrList *out) {
    strlist_init(out, a);
    if (!src) {
        return;
    }
    const char *p = src;
    const char *end = src + n;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(a, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        p = eol ? eol + 1 : end;
        if (line_indent(line) != 0) {
            continue;
        }
        const char *stripped = lstrip(line);
        if (!stripped[0] || stripped[0] == '#' || (stripped[0] == '/' && stripped[1] == '/')) {
            continue;
        }
        char *sig = uvs_sig_text(a, stripped);
        if (sig) {
            strlist_push(out, sig);
        }
    }
}

static void scan_file(Arena *a, UvsCodeGraph *g, size_t idx, const char *src, size_t n, size_t *out_cap, size_t *in_cap) {
    UvsCgNode *node = &g->nodes[idx];
    const char *ext = file_ext(node->rel);
    const char *dir = path_dirname(a, node->rel);
    const char *p = src;
    const char *end = src + n;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(a, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        p = eol ? eol + 1 : end;
        const char *stripped = lstrip(line);
        if (!stripped[0] || stripped[0] == '#' || (stripped[0] == '/' && stripped[1] == '/')) {
            continue;
        }
        if (line_indent(line) == 0) {
            char *sig = uvs_sig_text(a, stripped);
            if (sig) {
                strlist_push(&node->sigs, sig);
            }
        }
        if (ext_is_python(ext)) {
            parse_python_import(a, g, idx, stripped, out_cap, in_cap);
        } else if (ext_is_js(ext)) {
            parse_js_import(a, g, idx, stripped, dir, out_cap, in_cap);
        } else if (ext_is_c(ext)) {
            parse_c_include(a, g, idx, stripped, dir, out_cap, in_cap);
        } else if (ext_is_ruby(ext)) {
            parse_ruby_require(a, g, idx, stripped, dir, out_cap, in_cap);
        } else {
            parse_python_import(a, g, idx, stripped, out_cap, in_cap);
            parse_js_import(a, g, idx, stripped, dir, out_cap, in_cap);
            parse_c_include(a, g, idx, stripped, dir, out_cap, in_cap);
            parse_ruby_require(a, g, idx, stripped, dir, out_cap, in_cap);
        }
    }
}

void uvs_codegraph_build(Arena *a, const UvsFileList *files, UvsCodeGraph *out) {
    memset(out, 0, sizeof(*out));
    out->arena = a;
    out->len = files->len;
    if (!out->len) {
        return;
    }
    out->nodes = arena_alloc(a, out->len * sizeof(*out->nodes));
    memset(out->nodes, 0, out->len * sizeof(*out->nodes));
    size_t *out_cap = arena_alloc(a, out->len * sizeof(*out_cap));
    size_t *in_cap = arena_alloc(a, out->len * sizeof(*in_cap));
    memset(out_cap, 0, out->len * sizeof(*out_cap));
    memset(in_cap, 0, out->len * sizeof(*in_cap));
    for (size_t i = 0; i < out->len; i++) {
        out->nodes[i].rel = uvs_rel_slash(a, files->items[i].rel);
        out->nodes[i].abs = files->items[i].abs;
        strlist_init(&out->nodes[i].sigs, a);
    }
    for (size_t i = 0; i < out->len; i++) {
        size_t len = 0;
        char *body = fs_read_all(a, out->nodes[i].abs, &len);
        if (!body) {
            continue;
        }
        scan_file(a, out, i, body, len, out_cap, in_cap);
    }
}
