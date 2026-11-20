#include "uvs.h"

#include <stdlib.h>
#include <string.h>

typedef enum { LANG_AUTO, LANG_PY, LANG_RB, LANG_BRACE, LANG_GENERIC } LangKind;

typedef struct {
    int imports;
    int signatures;
    int docs;
    int comments;
    int fields;
    int bodies;
    int collapse_objects;
    int collapse_control;
    StrList focus;
    LangKind lang;
} KeepOpts;

typedef struct {
    StrList roots;
    StrList exts;
    int all_exts;
    StrList include;
    StrList exclude;
    StrList include_gitignored;
    StrList ignore_files;
    int use_gitignore;
    int hidden;
    int follow_symlinks;
    const char *out_path;
    KeepOpts keep;
    Arena *arena;
} Config;

static void usage(FILE *fp) {
    fprintf(fp,
            "Usage: skeleton_dump [paths ...] [options]\n"
            "\n"
            "Outline dump. Default: imports, signatures, docs, comments, fields;\n"
            "function bodies replaced with ... / { ... }. Every category is a flag.\n"
            "\n"
            "Scan:\n"
            "  --dir PATH, --ext EXTS, --include/--exclude GLOB\n"
            "  --include-gitignored GLOB, --ignore-file PATH\n"
            "  --hidden / --no-hidden, --follow-symlinks, --no-gitignore\n"
            "  --out PATH            File or - (default: -)\n"
            "  --lang {auto,py,rb,c} Unknown extensions only (default: auto)\n"
            "  --focus GLOB          Full source for matching files (repeatable)\n"
            "\n"
            "Keep (default on except --bodies):\n"
            "  --imports / --no-imports\n"
            "  --signatures / --no-signatures\n"
            "  --docs / --no-docs\n"
            "  --comments / --no-comments\n"
            "  --fields / --no-fields\n"
            "  --bodies / --no-bodies\n"
            "  --outline             Preset: no docs/comments/fields/bodies\n"
            "\n"
            "Collapse:\n"
            "  --collapse-objects / --no-collapse-objects  (default: collapse)\n"
            "  --collapse-control / --no-collapse-control  (default: keep if/for)\n"
            "  -h, --help\n");
}

static void cfg_init(Arena *a, Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->arena = a;
    strlist_init(&cfg->roots, a);
    strlist_init(&cfg->exts, a);
    strlist_init(&cfg->include, a);
    strlist_init(&cfg->exclude, a);
    strlist_init(&cfg->include_gitignored, a);
    strlist_init(&cfg->ignore_files, a);
    strlist_init(&cfg->keep.focus, a);
    cfg->all_exts = 1;
    cfg->use_gitignore = 1;
    cfg->hidden = 1;
    cfg->out_path = "-";
    cfg->keep.imports = 1;
    cfg->keep.signatures = 1;
    cfg->keep.docs = 1;
    cfg->keep.comments = 1;
    cfg->keep.fields = 1;
    cfg->keep.bodies = 0;
    cfg->keep.collapse_objects = 1;
    cfg->keep.collapse_control = 0;
    cfg->keep.lang = LANG_AUTO;
}

static void apply_ext_spec(Arena *a, Config *cfg, const char *spec) {
    strlist_init(&cfg->exts, a);
    uvs_parse_exts(a, &cfg->exts, spec);
    cfg->all_exts = 0;
    if (cfg->exts.len == 1 && (strcmp(cfg->exts.items[0], "*") == 0 || strcmp(cfg->exts.items[0], "all") == 0)) {
        cfg->all_exts = 1;
    }
    if (cfg->exts.len == 0) {
        cfg->all_exts = 1;
    }
}

static LangKind parse_lang(const char *s) {
    if (strcmp(s, "auto") == 0) {
        return LANG_AUTO;
    }
    if (strcmp(s, "py") == 0 || strcmp(s, "python") == 0) {
        return LANG_PY;
    }
    if (strcmp(s, "rb") == 0 || strcmp(s, "ruby") == 0) {
        return LANG_RB;
    }
    if (strcmp(s, "c") == 0 || strcmp(s, "js") == 0 || strcmp(s, "ts") == 0 || strcmp(s, "go") == 0 ||
        strcmp(s, "rs") == 0 || strcmp(s, "rust") == 0) {
        return LANG_BRACE;
    }
    fprintf(stderr, "error: unknown --lang %s (auto, py, rb, c)\n", s);
    exit(2);
}

static void parse_args(Arena *a, Config *cfg, int argc, char **argv) {
    cfg_init(a, cfg);
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "--no-gitignore") == 0) {
            cfg->use_gitignore = 0;
            continue;
        }
        if (strcmp(arg, "--hidden") == 0) {
            cfg->hidden = 1;
            continue;
        }
        if (strcmp(arg, "--no-hidden") == 0) {
            cfg->hidden = 0;
            continue;
        }
        if (strcmp(arg, "--follow-symlinks") == 0) {
            cfg->follow_symlinks = 1;
            continue;
        }
        if (strcmp(arg, "--imports") == 0) {
            cfg->keep.imports = 1;
            continue;
        }
        if (strcmp(arg, "--no-imports") == 0) {
            cfg->keep.imports = 0;
            continue;
        }
        if (strcmp(arg, "--signatures") == 0) {
            cfg->keep.signatures = 1;
            continue;
        }
        if (strcmp(arg, "--no-signatures") == 0) {
            cfg->keep.signatures = 0;
            continue;
        }
        if (strcmp(arg, "--docs") == 0) {
            cfg->keep.docs = 1;
            continue;
        }
        if (strcmp(arg, "--no-docs") == 0) {
            cfg->keep.docs = 0;
            continue;
        }
        if (strcmp(arg, "--comments") == 0) {
            cfg->keep.comments = 1;
            continue;
        }
        if (strcmp(arg, "--no-comments") == 0) {
            cfg->keep.comments = 0;
            continue;
        }
        if (strcmp(arg, "--fields") == 0) {
            cfg->keep.fields = 1;
            continue;
        }
        if (strcmp(arg, "--no-fields") == 0) {
            cfg->keep.fields = 0;
            continue;
        }
        if (strcmp(arg, "--bodies") == 0) {
            cfg->keep.bodies = 1;
            continue;
        }
        if (strcmp(arg, "--no-bodies") == 0) {
            cfg->keep.bodies = 0;
            continue;
        }
        if (strcmp(arg, "--outline") == 0) {
            cfg->keep.docs = 0;
            cfg->keep.comments = 0;
            cfg->keep.fields = 0;
            cfg->keep.bodies = 0;
            continue;
        }
        if (strcmp(arg, "--collapse-objects") == 0) {
            cfg->keep.collapse_objects = 1;
            continue;
        }
        if (strcmp(arg, "--no-collapse-objects") == 0) {
            cfg->keep.collapse_objects = 0;
            continue;
        }
        if (strcmp(arg, "--collapse-control") == 0) {
            cfg->keep.collapse_control = 1;
            continue;
        }
        if (strcmp(arg, "--no-collapse-control") == 0) {
            cfg->keep.collapse_control = 0;
            continue;
        }
        if (uvs_flag_eq(arg, "--dir")) {
            strlist_push(&cfg->roots, path_absolute(a, uvs_consume_value(&i, argc, argv, "--dir")));
            continue;
        }
        if (uvs_flag_eq(arg, "--ext")) {
            apply_ext_spec(a, cfg, uvs_consume_value(&i, argc, argv, "--ext"));
            continue;
        }
        if (uvs_flag_eq(arg, "--include")) {
            uvs_parse_csv(a, &cfg->include, uvs_consume_value(&i, argc, argv, "--include"));
            continue;
        }
        if (uvs_flag_eq(arg, "--exclude")) {
            uvs_parse_csv(a, &cfg->exclude, uvs_consume_value(&i, argc, argv, "--exclude"));
            continue;
        }
        if (uvs_flag_eq(arg, "--include-gitignored")) {
            uvs_parse_csv(a, &cfg->include_gitignored, uvs_consume_value(&i, argc, argv, "--include-gitignored"));
            continue;
        }
        if (uvs_flag_eq(arg, "--ignore-file")) {
            strlist_push(&cfg->ignore_files, path_absolute(a, uvs_consume_value(&i, argc, argv, "--ignore-file")));
            continue;
        }
        if (uvs_flag_eq(arg, "--out")) {
            cfg->out_path = uvs_consume_value(&i, argc, argv, "--out");
            continue;
        }
        if (uvs_flag_eq(arg, "--lang")) {
            cfg->keep.lang = parse_lang(uvs_consume_value(&i, argc, argv, "--lang"));
            continue;
        }
        if (uvs_flag_eq(arg, "--focus")) {
            uvs_parse_csv(a, &cfg->keep.focus, uvs_consume_value(&i, argc, argv, "--focus"));
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", arg);
            exit(2);
        }
        strlist_push(&cfg->roots, path_absolute(a, arg));
    }
    if (cfg->roots.len == 0) {
        strlist_push(&cfg->roots, path_cwd(a));
    }
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

static int ext_is_braces(const char *ext) {
    return fold_eq(ext, "c") || fold_eq(ext, "h") || fold_eq(ext, "cc") || fold_eq(ext, "cpp") || fold_eq(ext, "cxx") ||
           fold_eq(ext, "hpp") || fold_eq(ext, "js") || fold_eq(ext, "jsx") || fold_eq(ext, "ts") ||
           fold_eq(ext, "tsx") || fold_eq(ext, "go") || fold_eq(ext, "rs") || fold_eq(ext, "java") ||
           fold_eq(ext, "cs") || fold_eq(ext, "kt") || fold_eq(ext, "swift") || fold_eq(ext, "m") || fold_eq(ext, "mm");
}

static int shebang_lang(const char *src, size_t n) {
    if (n < 3 || src[0] != '#' || src[1] != '!') {
        return 0;
    }
    size_t i = 0;
    while (i < n && src[i] != '\n' && i < 200) {
        i++;
    }
    char buf[201];
    size_t m = i < 200 ? i : 200;
    memcpy(buf, src, m);
    buf[m] = 0;
    for (char *p = buf; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
    if (strstr(buf, "python")) {
        return LANG_PY;
    }
    if (strstr(buf, "ruby")) {
        return LANG_RB;
    }
    return 0;
}

static LangKind classify(const char *path, const char *src, size_t n, LangKind force) {
    const char *ext = file_ext(path);
    if (ext_is_python(ext)) {
        return LANG_PY;
    }
    if (ext_is_ruby(ext)) {
        return LANG_RB;
    }
    if (ext_is_braces(ext)) {
        return LANG_BRACE;
    }
    if (force != LANG_AUTO) {
        return force;
    }
    if (!ext[0]) {
        int sh = shebang_lang(src, n);
        if (sh) {
            return (LangKind)sh;
        }
    }
    return LANG_GENERIC;
}

static int focus_match(const KeepOpts *k, const char *rel) {
    for (size_t i = 0; i < k->focus.len; i++) {
        if (uvs_glob_path_match(k->focus.items[i], rel)) {
            return 1;
        }
    }
    return 0;
}

static int is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int line_indent(const char *s) {
    int n = 0;
    while (*s == ' ' || *s == '\t') {
        n += (*s == '\t') ? 4 : 1;
        s++;
    }
    return n;
}

static const char *lstrip(const char *s) {
    while (*s == ' ' || *s == '\t') {
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
    return next == 0 || next == ' ' || next == '\t' || next == '(' || next == ':';
}

static int py_is_def(const char *s) {
    if (starts_word(s, "def")) {
        return 1;
    }
    if (starts_word(s, "async")) {
        const char *rest = s + 5;
        while (*rest == ' ' || *rest == '\t') {
            rest++;
        }
        return starts_word(rest, "def");
    }
    return 0;
}

static int py_is_class(const char *s) {
    return starts_word(s, "class");
}

static int py_is_decl(const char *s) {
    return py_is_class(s) || py_is_def(s);
}

static int py_is_import(const char *s) {
    return starts_word(s, "import") || starts_word(s, "from");
}

static int py_is_type_checking(const char *s) {
    if (!starts_word(s, "if")) {
        return 0;
    }
    return strstr(s, "TYPE_CHECKING") != NULL;
}

static int py_is_stmt_kw(const char *s) {
    return starts_word(s, "return") || starts_word(s, "if") || starts_word(s, "for") || starts_word(s, "while") ||
           starts_word(s, "try") || starts_word(s, "with") || starts_word(s, "raise") || starts_word(s, "assert") ||
           starts_word(s, "pass") || starts_word(s, "break") || starts_word(s, "continue") || starts_word(s, "yield") ||
           starts_word(s, "await") || starts_word(s, "del") || starts_word(s, "global") || starts_word(s, "nonlocal") ||
           starts_word(s, "else") || starts_word(s, "elif") || starts_word(s, "except") || starts_word(s, "finally") ||
           starts_word(s, "match") || starts_word(s, "case") || starts_word(s, "async");
}

static int py_is_field(const char *s) {
    if (py_is_stmt_kw(s) || py_is_decl(s) || s[0] == '@' || s[0] == '#') {
        return 0;
    }
    return strchr(s, ':') != NULL || strchr(s, '=') != NULL;
}

static const char *skip_string_prefix(const char *s) {
    while (*s == 'r' || *s == 'R' || *s == 'f' || *s == 'F' || *s == 'b' || *s == 'B' || *s == 'u' || *s == 'U') {
        s++;
    }
    return s;
}

static int py_doc_open(const char *s, char quote[4]) {
    s = skip_string_prefix(s);
    if (strncmp(s, "\"\"\"", 3) == 0) {
        memcpy(quote, "\"\"\"", 4);
        return 1;
    }
    if (strncmp(s, "'''", 3) == 0) {
        memcpy(quote, "'''", 4);
        return 1;
    }
    return 0;
}

static int py_doc_closed(const char *s, const char *quote) {
    const char *a = strstr(s, quote);
    if (!a) {
        return 0;
    }
    return strstr(a + 3, quote) != NULL;
}

static int line_ends_colon(const char *s) {
    int in_s = 0, in_c = 0;
    const char *last = NULL;
    for (const char *p = s; *p; p++) {
        if (in_s) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '"') {
                in_s = 0;
            }
            continue;
        }
        if (in_c) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '\'') {
                in_c = 0;
            }
            continue;
        }
        if (*p == '#') {
            break;
        }
        if (*p == '"') {
            in_s = 1;
            continue;
        }
        if (*p == '\'') {
            in_c = 1;
            continue;
        }
        if (*p != ' ' && *p != '\t' && *p != '\r') {
            last = p;
        }
    }
    return last && *last == ':';
}

static void emit_line(Buf *out, const char *line) {
    buf_append_str(out, line);
    buf_append_char(out, '\n');
}

static void emit_ellipsis(Buf *out, int indent) {
    for (int i = 0; i < indent + 4; i++) {
        buf_append_char(out, ' ');
    }
    buf_append_str(out, "...\n");
}

static int rb_is_decl(const char *s) {
    return starts_word(s, "class") || starts_word(s, "module") || starts_word(s, "def");
}

static int rb_is_import(const char *s) {
    return starts_word(s, "require") || starts_word(s, "require_relative");
}

static void skeleton_python(Buf *out, const char *src, size_t n, const KeepOpts *k) {
    typedef struct {
        int indent;
        int is_class;
        int saw_doc;
        int saw_ellipsis;
    } PyScope;
    PyScope stack[64];
    int depth = 0;
    int collecting_sig = 0;
    int pending_class = 0;
    int pending_emit = 0;
    int sig_indent = 0;
    int in_doc = 0;
    int skip_doc = 0;
    char doc_quote[4] = {0};
    const char *p = src;
    const char *end = src + n;

    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(out->arena, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        const char *stripped = lstrip(line);
        int indent = line_indent(line);
        p = eol ? eol + 1 : end;

        if (in_doc) {
            if (!skip_doc) {
                emit_line(out, line);
            }
            if (strstr(stripped, doc_quote)) {
                in_doc = 0;
                if (!skip_doc && depth > 0 && !stack[depth - 1].is_class) {
                    stack[depth - 1].saw_doc = 1;
                    if (!k->bodies && !stack[depth - 1].saw_ellipsis) {
                        emit_ellipsis(out, stack[depth - 1].indent);
                        stack[depth - 1].saw_ellipsis = 1;
                    }
                }
            }
            continue;
        }
        if (!stripped[0]) {
            continue;
        }

        while (depth > 0 && indent <= stack[depth - 1].indent) {
            depth--;
        }
        PyScope *scope = depth > 0 ? &stack[depth - 1] : NULL;
        int in_def = scope && !scope->is_class;

        if (collecting_sig) {
            if (pending_emit) {
                emit_line(out, line);
            }
            if (line_ends_colon(stripped)) {
                collecting_sig = 0;
                if (depth < 64) {
                    stack[depth].indent = sig_indent;
                    stack[depth].is_class = pending_class;
                    stack[depth].saw_doc = 0;
                    stack[depth].saw_ellipsis = k->bodies ? 1 : 0;
                    depth++;
                }
            }
            continue;
        }

        char quote[4];
        if (py_doc_open(stripped, quote)) {
            int keep_doc = k->docs && (!in_def || (scope && !scope->saw_doc));
            skip_doc = !keep_doc;
            if (keep_doc) {
                emit_line(out, line);
            }
            if (!py_doc_closed(stripped, quote)) {
                in_doc = 1;
                memcpy(doc_quote, quote, 4);
            } else if (keep_doc && in_def && scope) {
                scope->saw_doc = 1;
                if (!k->bodies && !scope->saw_ellipsis) {
                    emit_ellipsis(out, scope->indent);
                    scope->saw_ellipsis = 1;
                }
            }
            continue;
        }

        if (in_def) {
            if (k->bodies) {
                emit_line(out, line);
            } else if (!scope->saw_ellipsis) {
                emit_ellipsis(out, scope->indent);
                scope->saw_ellipsis = 1;
            }
            continue;
        }

        if (stripped[0] == '#') {
            if (k->comments) {
                emit_line(out, line);
            }
            continue;
        }

        int is_imp = py_is_import(stripped);
        int is_decl = py_is_decl(stripped);
        int is_tc = py_is_type_checking(stripped);
        int is_field = (scope && scope->is_class && py_is_field(stripped)) ||
                       (!scope && py_is_field(stripped) &&
                        (stripped[0] == '_' || (stripped[0] >= 'A' && stripped[0] <= 'Z') ||
                         starts_word(stripped, "type") || strchr(stripped, ':')));
        int is_deco = stripped[0] == '@';

        int emit = 0;
        if (is_imp) {
            emit = k->imports;
        } else if (is_decl || is_deco) {
            emit = k->signatures;
        } else if (is_tc) {
            emit = k->imports || k->signatures;
        } else if (is_field) {
            emit = k->fields;
        }

        if (emit) {
            emit_line(out, line);
        }
        if (is_decl || is_tc) {
            pending_emit = emit;
            if (line_ends_colon(stripped)) {
                if (depth < 64) {
                    stack[depth].indent = indent;
                    stack[depth].is_class = py_is_class(stripped) || is_tc;
                    stack[depth].saw_doc = 0;
                    stack[depth].saw_ellipsis = k->bodies ? 1 : 0;
                    depth++;
                }
            } else {
                collecting_sig = 1;
                pending_class = py_is_class(stripped) || is_tc;
                sig_indent = indent;
            }
        }
    }
}

static void skeleton_ruby(Buf *out, const char *src, size_t n, const KeepOpts *k) {
    typedef struct {
        int indent;
        int is_class;
        int saw_ellipsis;
    } RbScope;
    RbScope stack[64];
    int depth = 0;
    const char *p = src;
    const char *end = src + n;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(out->arena, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        const char *stripped = lstrip(line);
        int indent = line_indent(line);
        p = eol ? eol + 1 : end;
        if (!stripped[0]) {
            continue;
        }
        while (depth > 0 && indent <= stack[depth - 1].indent) {
            depth--;
        }
        RbScope *scope = depth > 0 ? &stack[depth - 1] : NULL;
        int in_def = scope && !scope->is_class;
        if (in_def) {
            if (k->bodies) {
                emit_line(out, line);
            } else if (!scope->saw_ellipsis) {
                emit_ellipsis(out, scope->indent);
                scope->saw_ellipsis = 1;
            }
            continue;
        }
        if (stripped[0] == '#') {
            if (k->comments) {
                emit_line(out, line);
            }
            continue;
        }
        int emit = 0;
        if (rb_is_import(stripped)) {
            emit = k->imports;
        } else if (rb_is_decl(stripped)) {
            emit = k->signatures;
        }
        if (emit) {
            emit_line(out, line);
        }
        if (rb_is_decl(stripped) && depth < 64) {
            stack[depth].indent = indent;
            stack[depth].is_class = starts_word(stripped, "class") || starts_word(stripped, "module");
            stack[depth].saw_ellipsis = k->bodies ? 1 : 0;
            depth++;
        }
    }
}

static const char *bol_of(const char *start, const char *p) {
    while (p > start && p[-1] != '\n') {
        p--;
    }
    return p;
}

static int ident_eq_ci(const char *s, size_t n, const char *word) {
    size_t w = strlen(word);
    if (n != w) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)s[i];
        unsigned char cb = (unsigned char)word[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static int line_has_kw(const char *bol, const char *limit, const char *word) {
    const char *p = bol;
    while (p < limit && is_space_char(*p)) {
        p++;
    }
    while (p < limit && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) {
        const char *w = p;
        while (p < limit && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')) {
            p++;
        }
        if (ident_eq_ci(w, (size_t)(p - w), word)) {
            return 1;
        }
        while (p < limit && is_space_char(*p)) {
            p++;
        }
    }
    return 0;
}

static int stmt_has_type_kw(const char *start, const char *brace) {
    const char *bol = bol_of(start, brace);
    return line_has_kw(bol, brace, "type") || line_has_kw(bol, brace, "interface") || line_has_kw(bol, brace, "enum");
}

static int call_is_control(const char *start, const char *rparen) {
    const char *p = rparen;
    int depth = 0;
    while (p > start) {
        p--;
        if (*p == ')') {
            depth++;
        } else if (*p == '(') {
            if (depth == 0) {
                break;
            }
            depth--;
        }
    }
    while (p > start && is_space_char(p[-1])) {
        p--;
    }
    const char *end = p;
    while (p > start && ((p[-1] >= 'a' && p[-1] <= 'z') || (p[-1] >= 'A' && p[-1] <= 'Z') || p[-1] == '_')) {
        p--;
    }
    return ident_eq_ci(p, (size_t)(end - p), "if") || ident_eq_ci(p, (size_t)(end - p), "for") ||
           ident_eq_ci(p, (size_t)(end - p), "while") || ident_eq_ci(p, (size_t)(end - p), "switch") ||
           ident_eq_ci(p, (size_t)(end - p), "catch") || ident_eq_ci(p, (size_t)(end - p), "synchronized");
}

static int looks_body_open(const char *start, const char *brace, const KeepOpts *k) {
    const char *p = brace;
    while (p > start && is_space_char(p[-1])) {
        p--;
    }
    if (p > start && p[-1] == ')') {
        if (call_is_control(start, p - 1)) {
            return k->collapse_control;
        }
        return !k->bodies;
    }
    if (p - start >= 2 && p[-1] == '>' && p[-2] == '=') {
        return k->collapse_objects;
    }
    if (p > start && p[-1] == '=') {
        if (stmt_has_type_kw(start, brace)) {
            return 0;
        }
        return k->collapse_objects;
    }
    return 0;
}

static void skip_body(const char *s, size_t n, size_t *i, int *in_s, int *in_c, int *in_sl, int *in_ml) {
    int inner = 1;
    while (*i < n && inner > 0) {
        char ch = s[*i];
        char next = (*i + 1 < n) ? s[*i + 1] : 0;
        if (*in_sl) {
            if (ch == '\n') {
                *in_sl = 0;
            }
            (*i)++;
            continue;
        }
        if (*in_ml) {
            if (ch == '*' && next == '/') {
                *in_ml = 0;
                *i += 2;
                continue;
            }
            (*i)++;
            continue;
        }
        if (*in_s) {
            if (ch == '\\' && *i + 1 < n) {
                *i += 2;
                continue;
            }
            if (ch == '"') {
                *in_s = 0;
            }
            (*i)++;
            continue;
        }
        if (*in_c) {
            if (ch == '\\' && *i + 1 < n) {
                *i += 2;
                continue;
            }
            if (ch == '\'') {
                *in_c = 0;
            }
            (*i)++;
            continue;
        }
        if (ch == '/' && next == '/') {
            *in_sl = 1;
            *i += 2;
            continue;
        }
        if (ch == '/' && next == '*') {
            *in_ml = 1;
            *i += 2;
            continue;
        }
        if (ch == '"') {
            *in_s = 1;
            (*i)++;
            continue;
        }
        if (ch == '\'') {
            *in_c = 1;
            (*i)++;
            continue;
        }
        if (ch == '{') {
            inner++;
        } else if (ch == '}') {
            inner--;
        }
        (*i)++;
    }
}

static int brace_import_line(const char *s) {
    const char *t = lstrip(s);
    return starts_word(t, "import") || starts_word(t, "package") || starts_word(t, "use") || starts_word(t, "using") ||
           strncmp(t, "#include", 8) == 0;
}

static int brace_sig_line(const char *s) {
    return strstr(s, "{ ... }") != NULL && strchr(s, ')');
}

static void skeleton_braces(Buf *out, const char *s, size_t n, const KeepOpts *k) {
    Buf raw;
    buf_init(&raw, out->arena);
    size_t i = 0;
    int in_s = 0, in_c = 0, in_sl = 0, in_ml = 0;
    while (i < n) {
        char ch = s[i];
        char next = (i + 1 < n) ? s[i + 1] : 0;
        if (in_sl) {
            buf_append_char(&raw, ch);
            if (ch == '\n') {
                in_sl = 0;
            }
            i++;
            continue;
        }
        if (in_ml) {
            buf_append_char(&raw, ch);
            if (ch == '*' && next == '/') {
                buf_append_char(&raw, next);
                in_ml = 0;
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        if (in_s) {
            buf_append_char(&raw, ch);
            if (ch == '\\' && i + 1 < n) {
                buf_append_char(&raw, s[i + 1]);
                i += 2;
                continue;
            }
            if (ch == '"') {
                in_s = 0;
            }
            i++;
            continue;
        }
        if (in_c) {
            buf_append_char(&raw, ch);
            if (ch == '\\' && i + 1 < n) {
                buf_append_char(&raw, s[i + 1]);
                i += 2;
                continue;
            }
            if (ch == '\'') {
                in_c = 0;
            }
            i++;
            continue;
        }
        if (ch == '/' && next == '/') {
            if (!k->comments) {
                while (i < n && s[i] != '\n') {
                    i++;
                }
                continue;
            }
            in_sl = 1;
            buf_append_char(&raw, ch);
            i++;
            continue;
        }
        if (ch == '/' && next == '*') {
            int is_doc = (i + 2 < n && s[i + 2] == '*');
            if ((is_doc && !k->docs) || !k->comments) {
                i += 2;
                while (i < n) {
                    if (s[i] == '*' && i + 1 < n && s[i + 1] == '/') {
                        i += 2;
                        break;
                    }
                    i++;
                }
                continue;
            }
            in_ml = 1;
            buf_append_char(&raw, ch);
            i++;
            continue;
        }
        if (ch == '"') {
            in_s = 1;
            buf_append_char(&raw, ch);
            i++;
            continue;
        }
        if (ch == '\'') {
            in_c = 1;
            buf_append_char(&raw, ch);
            i++;
            continue;
        }
        if (ch == '{' && looks_body_open(s, s + i, k)) {
            buf_append_str(&raw, "{ ... }");
            i++;
            skip_body(s, n, &i, &in_s, &in_c, &in_sl, &in_ml);
            continue;
        }
        buf_append_char(&raw, ch);
        i++;
    }
    buf_null_terminate(&raw);
    const char *p = raw.data ? raw.data : "";
    const char *end = p + raw.len;
    int in_type_body = 0;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(out->arena, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        p = eol ? eol + 1 : end;
        const char *stripped = lstrip(line);
        if (!stripped[0]) {
            continue;
        }
        int indent = line_indent(line);
        if (indent == 0) {
            in_type_body = stmt_has_type_kw(stripped, stripped + strlen(stripped)) && strchr(stripped, '{') &&
                           !strstr(stripped, "{ ... }");
        }
        if ((stripped[0] == '/' && stripped[1] == '/') || (stripped[0] == '/' && stripped[1] == '*')) {
            if (!k->comments) {
                continue;
            }
        }
        if (brace_import_line(stripped) && !k->imports) {
            continue;
        }
        if (brace_sig_line(stripped) && !k->signatures) {
            continue;
        }
        if (!k->fields && in_type_body && indent > 0) {
            continue;
        }
        emit_line(out, line);
    }
}

static void skeleton_generic(Buf *out, const char *src, size_t n, const KeepOpts *k) {
    const char *p = src;
    const char *end = src + n;
    int in_block = 0;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t ln = eol ? (size_t)(eol - p) : (size_t)(end - p);
        char *line = arena_strndup(out->arena, p, ln);
        if (ln && line[ln - 1] == '\r') {
            line[ln - 1] = 0;
        }
        p = eol ? eol + 1 : end;
        int indent = line_indent(line);
        const char *stripped = lstrip(line);
        if (!stripped[0]) {
            continue;
        }
        if (stripped[0] == '#' && !k->comments) {
            continue;
        }
        if (indent == 0) {
            in_block = 0;
            if (k->signatures || k->imports) {
                emit_line(out, line);
            }
        } else if (k->bodies) {
            emit_line(out, line);
        } else if (!in_block && k->signatures) {
            in_block = 1;
            buf_append_str(out, "    ...\n");
        }
    }
}

static void skeleton_file(Buf *out, const char *path, const char *src, size_t n, const KeepOpts *k) {
    LangKind kind = classify(path, src, n, k->lang);
    if (kind == LANG_PY) {
        skeleton_python(out, src, n, k);
    } else if (kind == LANG_RB) {
        skeleton_ruby(out, src, n, k);
    } else if (kind == LANG_BRACE) {
        skeleton_braces(out, src, n, k);
    } else {
        skeleton_generic(out, src, n, k);
    }
}

static int write_out(const char *path, const char *data, size_t len) {
    if (!path || strcmp(path, "-") == 0) {
        fwrite(data, 1, len, stdout);
        return 1;
    }
    if (!fs_mkdirs_parent(path) || !fs_write_all(path, data, len)) {
        fprintf(stderr, "error: could not write %s\n", path);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    char **utf8_argv = uvs_cmdline_argv(&argc);
    if (utf8_argv) {
        argv = utf8_argv;
    }
    Arena *a = arena_new();
    Config cfg;
    parse_args(a, &cfg, argc, argv);

    UvsDiscoverOpts opts;
    uvs_discover_opts_init(&opts);
    opts.roots = &cfg.roots;
    opts.exts = &cfg.exts;
    opts.all_exts = cfg.all_exts;
    opts.include = &cfg.include;
    opts.exclude = &cfg.exclude;
    opts.include_gitignored = &cfg.include_gitignored;
    opts.ignore_files = &cfg.ignore_files;
    opts.use_gitignore = cfg.use_gitignore;
    opts.hidden = cfg.hidden;
    opts.follow_symlinks = cfg.follow_symlinks;
    opts.skip_secrets = 0;
    opts.skip_non_text = 1;

    UvsFileList files;
    uvs_discover(a, &opts, NULL, NULL, &files);

    Buf buf;
    buf_init(&buf, a);
    buf_append_fmt(&buf, "# skeleton dump\n# files: %zu\n", files.len);
    for (size_t i = 0; i < files.len; i++) {
        size_t len = 0;
        char *body = fs_read_all(a, files.items[i].abs, &len);
        if (!body) {
            continue;
        }
        buf_append_str(&buf, "\n");
        buf_append_str(&buf, "================================================================================\n");
        buf_append_fmt(&buf, "FILE: %s\n", files.items[i].rel);
        buf_append_str(&buf, "================================================================================\n");
        if (focus_match(&cfg.keep, files.items[i].rel)) {
            buf_append(&buf, body, len);
            if (len && body[len - 1] != '\n') {
                buf_append_char(&buf, '\n');
            }
        } else {
            skeleton_file(&buf, files.items[i].abs, body, len, &cfg.keep);
        }
    }
    buf_null_terminate(&buf);
    int ok = write_out(cfg.out_path, buf.data ? buf.data : "", buf.len);
    if (ok && cfg.out_path && strcmp(cfg.out_path, "-") != 0) {
        fprintf(stdout, "wrote skeleton of %zu file(s)\n", files.len);
    }
    arena_free(a);
    return ok ? 0 : 1;
}
