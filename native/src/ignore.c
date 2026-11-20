#include "uvs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if UVS_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct IgnoreRule {
    const char *base;
    const char *pattern;
    int negated;
    int dir_only;
    int anchored;
} IgnoreRule;

struct IgnoreSet {
    Arena *arena;
    IgnoreRule *rules;
    size_t len;
    size_t cap;
    StrList loaded;
    char *git_root;
    int loaded_global;
    int autoload;
};

static int glob_match(const char *pat, const char *text);

IgnoreSet *ignore_new(Arena *a) {
    IgnoreSet *set = arena_alloc(a, sizeof(IgnoreSet));
    memset(set, 0, sizeof(*set));
    set->arena = a;
    set->autoload = 1;
    strlist_init(&set->loaded, a);
    return set;
}

IgnoreSet *ignore_new_manual(Arena *a) {
    IgnoreSet *set = ignore_new(a);
    set->autoload = 0;
    return set;
}

static void add_rule(IgnoreSet *set, const char *base, const char *pattern, int negated, int dir_only,
                     int anchored) {
    if (set->len + 1 > set->cap) {
        size_t cap = set->cap ? set->cap * 2 : 16;
        IgnoreRule *rules = arena_alloc(set->arena, cap * sizeof(*rules));
        if (set->rules) {
            memcpy(rules, set->rules, set->len * sizeof(*rules));
        }
        set->rules = rules;
        set->cap = cap;
    }
    IgnoreRule *r = &set->rules[set->len++];
    r->base = base;
    r->pattern = pattern;
    r->negated = negated;
    r->dir_only = dir_only;
    r->anchored = anchored;
}

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    if (home && home[0]) {
        return home;
    }
#if UVS_IS_WINDOWS
    home = getenv("USERPROFILE");
    if (home && home[0]) {
        return home;
    }
#endif
    return NULL;
}

static char *expand_user(Arena *a, const char *raw, const char *relative_to) {
    while (*raw == ' ' || *raw == '\t') {
        raw++;
    }
    if (raw[0] == '~' && (raw[1] == '/' || raw[1] == '\\' || raw[1] == '\0')) {
        const char *home = home_dir();
        if (!home) {
            return path_normalize(a, raw);
        }
        if (raw[1] == '\0') {
            return path_normalize(a, home);
        }
        return path_join(a, home, raw + 2);
    }
    if (path_is_absolute(raw)) {
        return path_normalize(a, raw);
    }
    if (relative_to) {
        return path_join(a, relative_to, raw);
    }
    return path_absolute(a, raw);
}

static unsigned char glob_fold(unsigned char c) {
#if UVS_IS_WINDOWS
    if (c >= 'A' && c <= 'Z') {
        return (unsigned char)(c - 'A' + 'a');
    }
#endif
    return c;
}

/* 1 match, 0 no match, -1 malformed class (treat '[' as literal). */
static int class_match(const char **ppos, unsigned char ch) {
    const char *p = *ppos;
    if (*p != '[') {
        return -1;
    }
    p++;
    int neg = 0;
    if (*p == '!' || *p == '^') {
        neg = 1;
        p++;
    }
    int matched = 0;
    int saw = 0;
    unsigned char folded = glob_fold(ch);
    if (*p == ']') {
        matched = folded == glob_fold(']');
        p++;
        saw = 1;
    }
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            unsigned char lo = glob_fold((unsigned char)p[0]);
            unsigned char hi = glob_fold((unsigned char)p[2]);
            if (folded >= lo && folded <= hi) {
                matched = 1;
            }
            p += 3;
            saw = 1;
            continue;
        }
        if (glob_fold((unsigned char)*p) == folded) {
            matched = 1;
        }
        p++;
        saw = 1;
    }
    if (*p != ']' || !saw) {
        return -1;
    }
    p++;
    *ppos = p;
    if (ch == '/') {
        return 0;
    }
    return (neg ? !matched : matched) ? 1 : 0;
}

/* Double-star is special only as a full path component. */
static int is_globstar(const char *pat) {
    if (pat[0] != '*' || pat[1] != '*') {
        return 0;
    }
    return pat[2] == '\0' || pat[2] == '/';
}

static int glob_match(const char *pat, const char *text) {
    while (*pat) {
        if (is_globstar(pat)) {
            const char *rest = pat + 2;
            if (*rest == '/') {
                rest++;
            }
            if (*rest == '\0') {
                return 1;
            }
            for (;;) {
                if (glob_match(rest, text)) {
                    return 1;
                }
                if (*text == '\0') {
                    return 0;
                }
                text++;
            }
        }
        if (*pat == '*') {
            pat++;
            if (*pat == '\0') {
                return strchr(text, '/') == NULL;
            }
            for (;;) {
                if (glob_match(pat, text)) {
                    return 1;
                }
                if (*text == '\0' || *text == '/') {
                    return 0;
                }
                text++;
            }
        }
        if (*pat == '?') {
            if (*text == '\0' || *text == '/') {
                return 0;
            }
            pat++;
            text++;
            continue;
        }
        if (*pat == '[') {
            if (*text == '\0' || *text == '/') {
                int cm = class_match(&pat, (unsigned char)(*text ? *text : 0));
                if (cm >= 0) {
                    return 0;
                }
            } else {
                int cm = class_match(&pat, (unsigned char)*text);
                if (cm > 0) {
                    text++;
                    continue;
                }
                if (cm == 0) {
                    return 0;
                }
            }
            /* malformed class: fall through and match a literal '[' */
        }
        if (*pat == '\\' && pat[1]) {
            pat++;
        }
        if (*text == '\0' || glob_fold((unsigned char)*pat) != glob_fold((unsigned char)*text)) {
            return 0;
        }
        pat++;
        text++;
    }
    return *text == '\0';
}

static int unanchored_match(const char *pat, const char *rel) {
    if (glob_match(pat, rel)) {
        return 1;
    }
    for (const char *s = rel; *s; s++) {
        if (*s == '/' && glob_match(pat, s + 1)) {
            return 1;
        }
    }
    return 0;
}

static int rule_matches_rel(const IgnoreRule *rule, const char *rel, int is_dir) {
    int match;
    if (rule->anchored) {
        match = glob_match(rule->pattern, rel);
    } else {
        match = unanchored_match(rule->pattern, rel);
    }
    if (match && rule->dir_only && !is_dir) {
        return 0;
    }
    return match;
}

static char *parse_gitignore_line(Arena *a, const char *raw, int *negated, int *dir_only, int *anchored) {
    *negated = 0;
    *dir_only = 0;
    *anchored = 0;
    while (*raw == ' ' || *raw == '\t') {
        raw++;
    }
    if (*raw == '\0') {
        return NULL;
    }
    if (*raw == '#' ) {
        return NULL;
    }
    size_t n = strlen(raw);
    while (n > 0 && (raw[n - 1] == ' ' || raw[n - 1] == '\t' || raw[n - 1] == '\r')) {
        if (n >= 2 && raw[n - 2] == '\\' && raw[n - 1] != '\r') {
            break;
        }
        n--;
    }
    if (n == 0) {
        return NULL;
    }
    char *line = arena_strndup(a, raw, n);
    size_t llen0 = strlen(line);
    if (llen0 >= 2 && line[llen0 - 2] == '\\' && (line[llen0 - 1] == ' ' || line[llen0 - 1] == '\t')) {
        line[llen0 - 2] = line[llen0 - 1];
        line[llen0 - 1] = 0;
    }
    if (line[0] == '\\' && line[1] == '#') {
        line++;
    }
    if (line[0] == '!') {
        *negated = 1;
        line++;
        if (!line[0]) {
            return NULL;
        }
    } else if (line[0] == '\\' && line[1] == '!') {
        line++;
    }
    size_t llen = strlen(line);
    if (llen > 0 && line[llen - 1] == '/') {
        *dir_only = 1;
        line = arena_strndup(a, line, llen - 1);
    }
    if (line[0] == '/') {
        *anchored = 1;
        line++;
    } else if (strchr(line, '/') != NULL) {
        *anchored = 1;
    }
    if (!line[0]) {
        return NULL;
    }
    return line;
}

static void load_gitignore_file(IgnoreSet *set, const char *gitignore_path, const char *base) {
    if (strlist_contains(&set->loaded, gitignore_path)) {
        return;
    }
    strlist_push(&set->loaded, arena_strdup(set->arena, gitignore_path));
    size_t len = 0;
    char *text = fs_read_all(set->arena, gitignore_path, &len);
    if (!text) {
        return;
    }
    char *p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        char *raw = arena_strndup(set->arena, p, n);
        p = eol ? eol + 1 : p + n;
        int negated = 0;
        int dir_only = 0;
        int anchored = 0;
        char *pattern = parse_gitignore_line(set->arena, raw, &negated, &dir_only, &anchored);
        if (!pattern) {
            continue;
        }
        add_rule(set, base, pattern, negated, dir_only, anchored);
    }
}

static char *find_git_root(Arena *a, const char *start) {
    char *dir = fs_is_dir(start) ? path_absolute(a, start) : path_dirname(a, path_absolute(a, start));
    for (;;) {
        char *dotgit = path_join(a, dir, ".git");
        if (fs_exists(dotgit)) {
            return dir;
        }
        char *parent = path_dirname(a, dir);
        if (path_equal(parent, dir)) {
            return NULL;
        }
        dir = parent;
    }
}

static char *resolve_git_dir(Arena *a, const char *git_root) {
    char *dotgit = path_join(a, git_root, ".git");
    if (fs_is_dir(dotgit)) {
        return dotgit;
    }
    if (!fs_is_file(dotgit)) {
        return NULL;
    }
    size_t len = 0;
    char *text = fs_read_all(a, dotgit, &len);
    if (!text) {
        return NULL;
    }
    const char *key = "gitdir:";
    size_t klen = strlen(key);
    char *p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) {
            n--;
        }
        size_t lead = 0;
        while (lead < n && (p[lead] == ' ' || p[lead] == '\t')) {
            lead++;
        }
        if (n - lead > klen && strncmp(p + lead, key, klen) == 0) {
            const char *val = p + lead + klen;
            size_t vn = n - lead - klen;
            while (vn > 0 && (*val == ' ' || *val == '\t')) {
                val++;
                vn--;
            }
            char *raw = arena_strndup(a, val, vn);
            if (path_is_absolute(raw)) {
                return path_normalize(a, raw);
            }
            return path_join(a, path_dirname(a, dotgit), raw);
        }
        p = eol ? eol + 1 : p + n;
    }
    return NULL;
}

static char *read_excludes_file_from_config(Arena *a, const char *config_path) {
    size_t len = 0;
    char *text = fs_read_all(a, config_path, &len);
    if (!text) {
        return NULL;
    }
    int in_core = 0;
    char *p = text;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) {
            n--;
        }
        size_t lead = 0;
        while (lead < n && (p[lead] == ' ' || p[lead] == '\t')) {
            lead++;
        }
        if (n > lead && p[lead] == '[') {
            in_core = 0;
            if (n - lead >= 6) {
                char buf[16];
                size_t copy = n - lead < 15 ? n - lead : 15;
                memcpy(buf, p + lead, copy);
                buf[copy] = 0;
                for (size_t i = 0; buf[i]; i++) {
                    buf[i] = (char)tolower((unsigned char)buf[i]);
                }
                if (strncmp(buf, "[core]", 6) == 0) {
                    in_core = 1;
                }
            }
        } else if (in_core && n > lead) {
            char *line = arena_strndup(a, p + lead, n - lead);
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char *key = line;
                while (*key == ' ' || *key == '\t') {
                    key++;
                }
                char *kend = key + strlen(key);
                while (kend > key && (kend[-1] == ' ' || kend[-1] == '\t')) {
                    *--kend = 0;
                }
                for (char *c = key; *c; c++) {
                    *c = (char)tolower((unsigned char)*c);
                }
                if (strcmp(key, "excludesfile") == 0) {
                    char *val = eq + 1;
                    while (*val == ' ' || *val == '\t' || *val == '"') {
                        val++;
                    }
                    size_t vn = strlen(val);
                    while (vn > 0 && (val[vn - 1] == ' ' || val[vn - 1] == '\t' || val[vn - 1] == '"')) {
                        vn--;
                    }
                    val = arena_strndup(a, val, vn);
                    return expand_user(a, val, path_dirname(a, config_path));
                }
            }
        }
        p = eol ? eol + 1 : p + n;
    }
    return NULL;
}

static char *default_excludes_file(Arena *a) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        return path_join(a, xdg, "git/ignore");
    }
    const char *home = home_dir();
    if (!home) {
        return NULL;
    }
    return path_join(a, home, ".config/git/ignore");
}

static void load_global_and_exclude(IgnoreSet *set, const char *git_root) {
    if (set->loaded_global) {
        return;
    }
    set->loaded_global = 1;
    char *excludes = NULL;
    const char *home = home_dir();
    const char *gcfg = getenv("GIT_CONFIG_GLOBAL");
    if (gcfg && gcfg[0]) {
        excludes = read_excludes_file_from_config(set->arena, gcfg);
    } else if (home) {
        excludes = read_excludes_file_from_config(set->arena, path_join(set->arena, home, ".gitconfig"));
        if (!excludes) {
            const char *xdg = getenv("XDG_CONFIG_HOME");
            char *xdg_cfg = xdg && xdg[0] ? path_join(set->arena, xdg, "git/config")
                                          : path_join(set->arena, home, ".config/git/config");
            excludes = read_excludes_file_from_config(set->arena, xdg_cfg);
        }
    }
    char *git_dir = resolve_git_dir(set->arena, git_root);
    if (!excludes && git_dir) {
        excludes = read_excludes_file_from_config(set->arena, path_join(set->arena, git_dir, "config"));
    }
    if (!excludes) {
        excludes = default_excludes_file(set->arena);
    }
    if (excludes && fs_is_file(excludes)) {
        load_gitignore_file(set, excludes, git_root);
    }
    if (git_dir) {
        char *info = path_join(set->arena, git_dir, "info/exclude");
        if (fs_is_file(info)) {
            load_gitignore_file(set, info, git_root);
        }
    }
}

void ignore_load_for_path(IgnoreSet *set, const char *abs_path) {
    char *start = fs_is_dir(abs_path) ? path_absolute(set->arena, abs_path)
                                      : path_dirname(set->arena, path_absolute(set->arena, abs_path));
    char *root = find_git_root(set->arena, start);
    if (root) {
        if (!set->git_root) {
            set->git_root = root;
        }
        load_global_and_exclude(set, root);
    } else {
        root = start;
    }
    StrList stack;
    strlist_init(&stack, set->arena);
    char *dir = start;
    for (;;) {
        strlist_push(&stack, dir);
        if (path_equal(dir, root)) {
            break;
        }
        char *parent = path_dirname(set->arena, dir);
        if (path_equal(parent, dir)) {
            break;
        }
        dir = parent;
    }
    for (size_t i = stack.len; i > 0; i--) {
        char *gi = path_join(set->arena, stack.items[i - 1], ".gitignore");
        if (fs_is_file(gi)) {
            load_gitignore_file(set, gi, stack.items[i - 1]);
        }
    }
}

static int last_match(IgnoreSet *set, const char *abs_path, int is_dir) {
    char *norm = path_absolute(set->arena, abs_path);
    int ignored = -1;
    for (size_t i = 0; i < set->len; i++) {
        const IgnoreRule *rule = &set->rules[i];
        if (!path_is_within(norm, rule->base)) {
            continue;
        }
        char *rel = path_relative(set->arena, norm, rule->base);
        if (strcmp(rel, ".") == 0) {
            continue;
        }
        if (rule_matches_rel(rule, rel, is_dir)) {
            ignored = !rule->negated;
        }
    }
    return ignored;
}

static int prefix_is_scan_root_or_above(const char *prefix_abs, const char *force_root) {
    if (!force_root) {
        return 0;
    }
    if (path_equal(prefix_abs, force_root)) {
        return 1;
    }
    return path_is_within(force_root, prefix_abs);
}

int ignore_is_ignored(IgnoreSet *set, const char *abs_path, int is_dir, const char *force_root) {
    if (path_has_component(abs_path, ".git")) {
        return 1;
    }
    if (set->autoload) {
        ignore_load_for_path(set, abs_path);
    }

    char *norm = path_absolute(set->arena, abs_path);
    char *anchor = set->git_root ? set->git_root : (force_root ? path_absolute(set->arena, force_root) : path_dirname(set->arena, norm));
    char *force_abs = force_root ? path_absolute(set->arena, force_root) : NULL;

    /* Git: a child cannot be re-included if a parent directory is excluded. */
    size_t nlen = strlen(norm);
    for (size_t i = 0; i < nlen; i++) {
        if (norm[i] != '/') {
            continue;
        }
        if (i == 0) {
            continue;
        }
        char *prefix = arena_strndup(set->arena, norm, i);
        if (!path_is_within(prefix, anchor) || path_equal(prefix, anchor)) {
            continue;
        }
        if (prefix_is_scan_root_or_above(prefix, force_abs)) {
            continue;
        }
        int match = last_match(set, prefix, 1);
        if (match == 1) {
            return 1;
        }
    }

    if (force_abs && path_equal(norm, force_abs)) {
        return 0;
    }
    int match = last_match(set, norm, is_dir);
    return match == 1;
}

void ignore_load_file(IgnoreSet *set, const char *path, const char *base) {
    if (!path || !base) {
        return;
    }
    load_gitignore_file(set, path_absolute(set->arena, path), path_absolute(set->arena, base));
}

int uvs_glob_match(const char *pattern, const char *text) {
    if (!pattern || !text) {
        return 0;
    }
    return glob_match(pattern, text);
}

int uvs_glob_path_match(const char *pattern, const char *rel) {
    if (!pattern || !rel) {
        return 0;
    }
    while (*pattern == ' ' || *pattern == '\t') {
        pattern++;
    }
    int anchored = 0;
    if (pattern[0] == '/') {
        pattern++;
        anchored = 1;
    } else if (strchr(pattern, '/') != NULL) {
        anchored = 1;
    }
    if (anchored) {
        return glob_match(pattern, rel);
    }
    return unanchored_match(pattern, rel);
}
