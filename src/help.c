// Markdown help and dialogue window.
// One page is parsed at a time. The wheel scrolls the page. Shift scrolls faster
// and shows a colored pad combo on each link. Those pads light the same colors.
// Press that combo while Shift is held to
// open the link. The page does not jump to the link. The back stack stores the
// path, the scroll, and the link that was opened.
#include "help.h"
#include "font.h"
#include "image.h"
#include "md4c.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELP_TEXT_MAX (64 * 1024)
#define HINT_RIM 3
#define HINT_BOX_W (FONT_S_W + HINT_RIM * 2)
#define HINT_BOX_H (FONT_S_H + HINT_RIM * 2)
#define HELP_PAD_N 6
#define HELP_PARA 1
#define HELP_HEAD 2
#define HELP_ITEM 3
#define HELP_CODE 4
#define HELP_IMG 5

struct span {
    char *text;
    int n, cap;
    int strong, link, code;
};

struct block {
    int kind;
    int heading;
    int indent;
    struct span *spans;
    int nspans, cspans;
    char alt[96];
    uint32_t *native;
    uint32_t *scaled;
    int iw, ih;
    int y, h, draw_w, draw_h;
};

struct link {
    char rel[UI_HELP_PATH];
    int y, bottom;
};

struct frag {
    int x, y, w, h;
    int link, strong, heading, code_block, code_inline;
    const char *text;
    int len;
};

struct doc {
    struct block *blocks;
    int nblocks, cblocks;
    struct link *links;
    int nlinks, clinks;
    struct frag *frags;
    int nfrags, cfrags;
    int content_h;
    int col_w;
    int bar;
    int laid_w, laid_h;
};

struct builder {
    struct doc *doc;
    struct ui_help *help;
    const char *page;
    int leaf;
    int in_li;
    int in_img;
    int in_html;
    int strong;
    int code;
    int link;
    char alt[96];
    int alt_n;
    char img_rel[UI_HELP_PATH];
    int img_ok;
    int failed;
};

static int ends_with_ci(const char *s, const char *suf) {
    size_t n, m, i;
    if (!s || !suf) return 0;
    n = strlen(s);
    m = strlen(suf);
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        unsigned char a = (unsigned char)s[n - m + i];
        unsigned char b = (unsigned char)suf[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static void trim_copy(char *s) {
    char *a = s;
    size_t n;
    while (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static int collapse(const char *in, char *out, int cap) {
    const char *parts[48];
    int lens[48];
    int n = 0, used = 0, i;
    const char *p;
    if (!in || !out || cap < 2) return -1;
    p = in;
    while (*p) {
        const char *s;
        int len;
        while (*p == '/') p++;
        if (!*p) break;
        s = p;
        while (*p && *p != '/') p++;
        len = (int)(p - s);
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (n == 0) return -1;
            n--;
            continue;
        }
        if (n >= 48) return -1;
        parts[n] = s;
        lens[n] = len;
        n++;
    }
    if (n == 0) return -1;
    for (i = 0; i < n; i++) {
        if (used + lens[i] + (i ? 1 : 0) + 1 > cap) return -1;
        if (i) out[used++] = '/';
        memcpy(out + used, parts[i], (size_t)lens[i]);
        used += lens[i];
        out[used] = 0;
    }
    return 0;
}

// href is relative to the current page. The result stays inside the help folder
// and has the asked-for extension. 0 on success.
static int resolve_href(const char *page, const char *href_in, char *out, int cap, int image) {
    char href[UI_HELP_PATH];
    char joined[UI_HELP_PATH];
    const char *slash;
    char *hash;
    int wr, i;
    if (!href_in || !out || cap < 2) return -1;
    if (strlen(href_in) >= sizeof href) return -1;
    memcpy(href, href_in, strlen(href_in) + 1);
    trim_copy(href);
    hash = strchr(href, '#');
    if (hash) *hash = 0;
    trim_copy(href);
    if (!href[0] || href[0] == '/' || href[0] == '\\') return -1;
    if (strchr(href, '\\') || strchr(href, ':')) return -1;
    for (i = 0; href[i]; i++)
        if ((unsigned char)href[i] < 32) return -1;
    slash = page ? strrchr(page, '/') : 0;
    if (slash) wr = snprintf(joined, sizeof joined, "%.*s/%s", (int)(slash - page), page, href);
    else wr = snprintf(joined, sizeof joined, "%s", href);
    if (wr < 0 || wr >= (int)sizeof joined) return -1;
    if (collapse(joined, out, cap) != 0) return -1;
    if (image) {
        if (!ends_with_ci(out, ".png") && !ends_with_ci(out, ".ppm")) return -1;
    } else if (!ends_with_ci(out, ".md")) {
        return -1;
    }
    return 0;
}

static int under_root(const char *root, const char *file) {
    size_t n;
    if (!root || !file || !root[0]) return 0;
    n = strlen(root);
    if (strncmp(file, root, n) != 0) return 0;
    return file[n] == '/';
}

static int rooted(const struct ui_help *h, const char *rel, char *out, int cap) {
    int wr;
    if (!h->canon[0] || !rel || !rel[0] || rel[0] == '/' || strchr(rel, '\\')) return -1;
    wr = snprintf(out, (size_t)cap, "%s/%s", h->canon, rel);
    if (wr < 0 || wr >= cap) return -1;
    return 0;
}

static const struct ui_help_file *lookup_file(const struct ui_help *h, const char *rel) {
    int i;
    if (!h || !h->files || !rel || !rel[0]) return 0;
    for (i = 0; i < h->nfiles; i++) {
        if (h->files[i].path && strcmp(h->files[i].path, rel) == 0) return &h->files[i];
    }
    return 0;
}

// 0 and the canonical path when the file exists inside the help folder.
static int canon_file(const struct ui_help *h, const char *rel, char *out, int cap) {
    char full[PATH_MAX];
    char got[PATH_MAX];
    if (rooted(h, rel, full, (int)sizeof full) != 0) return -1;
    if (!realpath(full, got)) return -1;
    if (!under_root(h->canon, got)) return -1;
    if ((int)strlen(got) >= cap) return -1;
    memcpy(out, got, strlen(got) + 1);
    return 0;
}

// A link is registered only when that page is actually available.
static int page_exists(const struct ui_help *h, const char *rel) {
    char path[PATH_MAX];
    if (!h || !rel || !rel[0]) return 0;
    if (h->files) return lookup_file(h, rel) != 0;
    return canon_file(h, rel, path, (int)sizeof path) == 0;
}

static void span_free(struct span *s) { free(s->text); s->text = 0; s->n = s->cap = 0; }

static void block_free(struct block *b) {
    int i;
    for (i = 0; i < b->nspans; i++) span_free(&b->spans[i]);
    free(b->spans);
    if (b->scaled && b->scaled != b->native) free(b->scaled);
    free(b->native);
    b->spans = 0;
    b->scaled = b->native = 0;
}

static void doc_free(struct doc *d) {
    int i;
    if (!d) return;
    for (i = 0; i < d->nblocks; i++) block_free(&d->blocks[i]);
    free(d->blocks);
    free(d->links);
    free(d->frags);
    free(d);
}

static int block_has_text(const struct block *b) {
    int i, k;
    for (i = 0; i < b->nspans; i++) {
        if (b->kind == HELP_CODE && b->spans[i].n > 0) return 1;
        for (k = 0; k < b->spans[i].n; k++) {
            char c = b->spans[i].text[k];
            if (c != ' ' && c != '\n' && c != '\t') return 1;
        }
    }
    return 0;
}

static int span_add(struct block *b, int strong, int link, int code, const char *s, int n) {
    struct span *sp;
    if (n <= 0) return 0;
    if (b->nspans > 0) {
        sp = &b->spans[b->nspans - 1];
        if (sp->strong == strong && sp->link == link && sp->code == code) {
            if (sp->n + n + 1 > sp->cap) {
                int cap = sp->cap > 0 ? sp->cap : 16;
                char *text;
                while (cap < sp->n + n + 1) cap *= 2;
                text = realloc(sp->text, (size_t)cap);
                if (!text) return -1;
                sp->text = text;
                sp->cap = cap;
            }
            memcpy(sp->text + sp->n, s, (size_t)n);
            sp->n += n;
            sp->text[sp->n] = 0;
            return 0;
        }
    }
    if (b->nspans + 1 > b->cspans) {
        int cap = b->cspans > 0 ? b->cspans * 2 : 4;
        struct span *spans = realloc(b->spans, (size_t)cap * sizeof *spans);
        if (!spans) return -1;
        b->spans = spans;
        b->cspans = cap;
    }
    sp = &b->spans[b->nspans++];
    memset(sp, 0, sizeof *sp);
    sp->strong = strong;
    sp->link = link;
    sp->code = code;
    sp->cap = n + 1;
    sp->text = malloc((size_t)sp->cap);
    if (!sp->text) return -1;
    memcpy(sp->text, s, (size_t)n);
    sp->n = n;
    sp->text[n] = 0;
    return 0;
}

static struct block *add_block(struct builder *b, int kind) {
    struct block *bl;
    int cap;
    if (b->failed) return 0;
    if (b->doc->nblocks + 1 > b->doc->cblocks) {
        cap = b->doc->cblocks > 0 ? b->doc->cblocks * 2 : 8;
        struct block *blocks = realloc(b->doc->blocks, (size_t)cap * sizeof *blocks);
        if (!blocks) {
            b->failed = 1;
            return 0;
        }
        b->doc->blocks = blocks;
        b->doc->cblocks = cap;
    }
    bl = &b->doc->blocks[b->doc->nblocks++];
    memset(bl, 0, sizeof *bl);
    bl->kind = kind;
    return bl;
}

static void end_leaf(struct builder *b) {
    struct block *bl;
    if (b->leaf < 0) return;
    bl = &b->doc->blocks[b->leaf];
    if (bl->kind != HELP_IMG && !block_has_text(bl) && b->leaf == b->doc->nblocks - 1) {
        block_free(bl);
        b->doc->nblocks--;
    }
    b->leaf = -1;
}

static void start_leaf(struct builder *b, int kind) {
    struct block *bl = add_block(b, kind);
    if (!bl) return;
    b->leaf = b->doc->nblocks - 1;
    if (kind == HELP_ITEM) {
        int indent = b->in_li * 12;
        if (indent > 36) indent = 36;
        bl->indent = indent;
    }
    if (kind == HELP_HEAD) bl->heading = 1;
}

static int decode_entity(const char *s, int n, char *out) {
    int v = 0, i;
    if (n < 3 || s[0] != '&' || s[n - 1] != ';') return 0;
    if (n == 5 && memcmp(s, "&amp;", 5) == 0) { *out = '&'; return 1; }
    if (n == 4 && memcmp(s, "&lt;", 4) == 0) { *out = '<'; return 1; }
    if (n == 4 && memcmp(s, "&gt;", 4) == 0) { *out = '>'; return 1; }
    if (n == 6 && memcmp(s, "&quot;", 6) == 0) { *out = '"'; return 1; }
    if (n == 6 && memcmp(s, "&apos;", 6) == 0) { *out = '\''; return 1; }
    if (s[1] != '#') return 0;
    if (s[2] == 'x' || s[2] == 'X') {
        for (i = 3; i < n - 1; i++) {
            unsigned char c = (unsigned char)s[i];
            v *= 16;
            if (c >= '0' && c <= '9') v += c - '0';
            else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
            else return 0;
        }
    } else {
        for (i = 2; i < n - 1; i++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            v = v * 10 + (s[i] - '0');
        }
    }
    if (v >= 32 && v <= 126) { *out = (char)v; return 1; }
    return 0;
}

static int add_chars(struct builder *b, const char *s, int n) {
    struct block *bl;
    if (b->failed || b->in_html || n <= 0) return 0;
    if (b->in_img) {
        int i;
        for (i = 0; i < n && b->alt_n + 1 < (int)sizeof b->alt; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == '\n' || c == '\t') c = ' ';
            if (c < 32 || c > 126) c = '?';
            b->alt[b->alt_n++] = (char)c;
        }
        b->alt[b->alt_n] = 0;
        return 0;
    }
    if (b->leaf < 0) start_leaf(b, b->in_li ? HELP_ITEM : HELP_PARA);
    if (b->leaf < 0) return -1;
    bl = &b->doc->blocks[b->leaf];
    return span_add(bl, b->strong > 0, b->link, b->code > 0, s, n);
}

static void finish_image(struct builder *b) {
    struct block *bl;
    int kind = HELP_PARA;
    int was_item = 0, was_head = 0;
    b->in_img--;
    if (b->in_img < 0) b->in_img = 0;
    if (b->leaf >= 0) {
        kind = b->doc->blocks[b->leaf].kind;
        was_item = kind == HELP_ITEM;
        was_head = kind == HELP_HEAD;
        end_leaf(b);
    }
    bl = add_block(b, HELP_IMG);
    if (bl) {
        snprintf(bl->alt, sizeof bl->alt, "%s", b->alt);
        if (b->img_ok) {
            char path[PATH_MAX];
            uint32_t *px = 0;
            int w = 0, h = 0;
            int got = -1;
            if (b->help->files) {
                const struct ui_help_file *f = lookup_file(b->help, b->img_rel);
                if (f) got = image_decode_mem(f->data, f->size, &px, &w, &h);
            } else if (canon_file(b->help, b->img_rel, path, (int)sizeof path) == 0) {
                got = image_decode(path, &px, &w, &h);
            }
            if (got == 0) {
                bl->native = px;
                bl->iw = w;
                bl->ih = h;
            }
        }
    }
    if (was_item && b->in_li) start_leaf(b, HELP_ITEM);
    else if (was_head) start_leaf(b, HELP_HEAD);
    else if (!b->in_li && kind == HELP_PARA) start_leaf(b, HELP_PARA);
}

static int enter_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    struct builder *b = userdata;
    (void)detail;
    if (b->failed) return -1;
    if (type == MD_BLOCK_HTML) b->in_html++;
    else if (type == MD_BLOCK_LI) {
        end_leaf(b);
        b->in_li++;
        start_leaf(b, HELP_ITEM);
    } else if (type == MD_BLOCK_H) {
        end_leaf(b);
        start_leaf(b, HELP_HEAD);
    } else if (type == MD_BLOCK_CODE) {
        end_leaf(b);
        start_leaf(b, HELP_CODE);
    } else if (type == MD_BLOCK_P && !b->in_li && !b->in_html) {
        end_leaf(b);
        start_leaf(b, HELP_PARA);
    } else if (type == MD_BLOCK_P && b->in_li && b->leaf >= 0 && block_has_text(&b->doc->blocks[b->leaf])) {
        if (add_chars(b, "\n", 1) != 0) return -1;
    }
    return 0;
}

static int leave_block(MD_BLOCKTYPE type, void *detail, void *userdata) {
    struct builder *b = userdata;
    (void)detail;
    if (type == MD_BLOCK_HTML) {
        if (b->in_html) b->in_html--;
    } else if (type == MD_BLOCK_LI) {
        end_leaf(b);
        if (b->in_li) b->in_li--;
    } else if (type == MD_BLOCK_H || type == MD_BLOCK_CODE) {
        end_leaf(b);
    } else if (type == MD_BLOCK_P && !b->in_li) {
        end_leaf(b);
    }
    return b->failed ? -1 : 0;
}

static int attr_copy(char *out, int cap, const MD_ATTRIBUTE *a) {
    unsigned i = 0;
    int n = 0;
    if (!out || cap < 1) return -1;
    out[0] = 0;
    if (!a || !a->text || a->size == 0) return 0;
    if (!a->substr_offsets) {
        int take = (int)a->size;
        if (take >= cap) take = cap - 1;
        memcpy(out, a->text, (size_t)take);
        out[take] = 0;
        return 0;
    }
    while (a->substr_offsets[i] < a->size) {
        unsigned b0 = a->substr_offsets[i];
        unsigned e0 = a->substr_offsets[i + 1];
        MD_TEXTTYPE t = a->substr_types ? a->substr_types[i] : MD_TEXT_NORMAL;
        const char *s = a->text + b0;
        int len = (int)(e0 - b0);
        char one;
        i++;
        if (len < 0) break;
        if (t == MD_TEXT_ENTITY && decode_entity(s, len, &one)) {
            if (n + 1 >= cap) break;
            out[n++] = one;
            continue;
        }
        if (t == MD_TEXT_NULLCHAR) continue;
        if (n + len >= cap) len = cap - 1 - n;
        if (len > 0) {
            memcpy(out + n, s, (size_t)len);
            n += len;
        }
        if (n >= cap - 1) break;
    }
    out[n] = 0;
    return 0;
}

static int enter_span(MD_SPANTYPE type, void *detail, void *userdata) {
    struct builder *b = userdata;
    if (b->failed) return -1;
    if (type == MD_SPAN_STRONG) b->strong++;
    else if (type == MD_SPAN_CODE) b->code++;
    else if (type == MD_SPAN_A && !b->in_img && !b->in_html) {
        MD_SPAN_A_DETAIL *a = detail;
        char href[UI_HELP_PATH];
        char rel[UI_HELP_PATH];
        b->link = -1;
        if (attr_copy(href, (int)sizeof href, &a->href) == 0 &&
            resolve_href(b->page, href, rel, (int)sizeof rel, 0) == 0 &&
            page_exists(b->help, rel)) {
            struct link *L;
            int cap;
            if (b->doc->nlinks + 1 > b->doc->clinks) {
                cap = b->doc->clinks > 0 ? b->doc->clinks * 2 : 4;
                struct link *links = realloc(b->doc->links, (size_t)cap * sizeof *links);
                if (!links) {
                    b->failed = 1;
                    return -1;
                }
                b->doc->links = links;
                b->doc->clinks = cap;
            }
            L = &b->doc->links[b->doc->nlinks];
            memset(L, 0, sizeof *L);
            L->y = -1;
            L->bottom = -1;
            snprintf(L->rel, sizeof L->rel, "%s", rel);
            b->link = b->doc->nlinks++;
        }
    } else if (type == MD_SPAN_IMG && !b->in_img && !b->in_html) {
        MD_SPAN_IMG_DETAIL *im = detail;
        char src[UI_HELP_PATH];
        b->in_img++;
        b->alt_n = 0;
        b->alt[0] = 0;
        b->img_ok = 0;
        b->img_rel[0] = 0;
        if (attr_copy(src, (int)sizeof src, &im->src) == 0 &&
            resolve_href(b->page, src, b->img_rel, (int)sizeof b->img_rel, 1) == 0)
            b->img_ok = 1;
    }
    return 0;
}

static int leave_span(MD_SPANTYPE type, void *detail, void *userdata) {
    struct builder *b = userdata;
    (void)detail;
    if (type == MD_SPAN_STRONG && b->strong) b->strong--;
    else if (type == MD_SPAN_CODE && b->code) b->code--;
    else if (type == MD_SPAN_A) b->link = -1;
    else if (type == MD_SPAN_IMG && b->in_img) finish_image(b);
    return b->failed ? -1 : 0;
}

static int on_text(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata) {
    struct builder *b = userdata;
    char buf[256];
    int n = 0;
    MD_SIZE i;
    if (b->failed) return -1;
    if (b->in_html || type == MD_TEXT_HTML) return 0;
    if (type == MD_TEXT_BR) return add_chars(b, "\n", 1);
    if (type == MD_TEXT_SOFTBR) return add_chars(b, " ", 1);
    if (type == MD_TEXT_NULLCHAR) return add_chars(b, "?", 1);
    if (type == MD_TEXT_ENTITY) {
        char one;
        if (decode_entity(text, (int)size, &one)) return add_chars(b, &one, 1);
        return 0;
    }
    for (i = 0; i < size; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') {
            if (n && add_chars(b, buf, n) != 0) return -1;
            n = 0;
            if (add_chars(b, "\n", 1) != 0) return -1;
            continue;
        }
        if (c == '\t') c = ' ';
        if (c < 32 || c > 126) c = '?';
        buf[n++] = (char)c;
        if (n == (int)sizeof buf) {
            if (add_chars(b, buf, n) != 0) return -1;
            n = 0;
        }
    }
    if (n && add_chars(b, buf, n) != 0) return -1;
    return 0;
}

static struct doc *parse_page(struct ui_help *h, const char *page, const char *text, int len) {
    struct builder b;
    MD_PARSER parser;
    struct doc *doc = calloc(1, sizeof *doc);
    if (!doc) return 0;
    memset(&b, 0, sizeof b);
    b.doc = doc;
    b.help = h;
    b.page = page;
    b.leaf = -1;
    b.link = -1;
    memset(&parser, 0, sizeof parser);
    parser.flags = MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_NOHTML;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = on_text;
    if (md_parse(text, (MD_SIZE)len, &parser, &b) != 0 || b.failed) {
        doc_free(doc);
        return 0;
    }
    end_leaf(&b);
    return doc;
}

static char *read_capped(const char *path, int *out_n) {
    FILE *f = fopen(path, "rb");
    char *buf;
    int n;
    if (!f) return 0;
    buf = malloc(HELP_TEXT_MAX + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    n = (int)fread(buf, 1, HELP_TEXT_MAX, f);
    fclose(f);
    if (n < 0) n = 0;
    buf[n] = 0;
    *out_n = n;
    return buf;
}

static int load_page(struct ui_help *h, const char *rel) {
    char path[PATH_MAX];
    char *text = 0;
    const char *view = 0;
    int n = 0;
    struct doc *doc;
    if (h->files) {
        const struct ui_help_file *f = lookup_file(h, rel);
        if (!f || !f->data) return -1;
        view = (const char *)f->data;
        n = f->size;
        if (n > HELP_TEXT_MAX) n = HELP_TEXT_MAX;
    } else {
        if (canon_file(h, rel, path, (int)sizeof path) != 0) return -1;
        text = read_capped(path, &n);
        if (!text) return -1;
        view = text;
    }
    doc = parse_page(h, rel, view, n);
    free(text);
    if (!doc) return -1;
    doc_free(h->doc);
    h->doc = doc;
    snprintf(h->page, sizeof h->page, "%s", rel);
    return 0;
}

static int emit_frag(struct doc *d, int x, int y, const char *s, int n, int link, int strong, int heading,
                     int code_block, int code_inline) {
    struct frag *f;
    int cap;
    if (n <= 0) return 0;
    if (d->nfrags + 1 > d->cfrags) {
        cap = d->cfrags > 0 ? d->cfrags * 2 : 16;
        struct frag *frags = realloc(d->frags, (size_t)cap * sizeof *frags);
        if (!frags) return -1;
        d->frags = frags;
        d->cfrags = cap;
    }
    f = &d->frags[d->nfrags++];
    memset(f, 0, sizeof *f);
    f->x = x;
    f->y = y;
    f->w = n * FONT_S_W;
    f->h = FONT_S_H;
    f->link = link;
    f->strong = strong;
    f->heading = heading;
    f->code_block = code_block;
    f->code_inline = code_inline;
    f->text = s;
    f->len = n;
    if (link >= 0 && link < d->nlinks) {
        if (d->links[link].y < 0) d->links[link].y = y;
        if (y + FONT_S_H > d->links[link].bottom) d->links[link].bottom = y + FONT_S_H;
    }
    return 0;
}

static void help_next_line(int *line_y, int *x, int x0) {
    *line_y += FONT_S_H;
    *x = x0;
}

static int layout_spans(struct doc *d, struct block *bl, int col_w, int *y) {
    int x0 = bl->indent;
    int x, line_y, code_block, i, bullet;
    if (x0 > col_w - FONT_S_W) x0 = 0;
    x = x0;
    line_y = *y;
    bullet = 0;
    code_block = bl->kind == HELP_CODE;
    for (i = 0; i < bl->nspans; i++) {
        struct span *sp = &bl->spans[i];
        int at = 0;
        while (at < sp->n) {
            int j, room, fit, take;
            if (sp->text[at] == '\n') {
                help_next_line(&line_y, &x, x0);
                at++;
                continue;
            }
            j = at;
            while (j < sp->n && sp->text[j] != ' ' && sp->text[j] != '\n') j++;
            if (j == at) {
                while (at < sp->n && sp->text[at] == ' ') {
                    if (x > x0 && x + FONT_S_W > col_w) break;
                    if (x == x0) {
                        at++;
                        continue;
                    }
                    if (!bullet && bl->kind == HELP_ITEM && x0 >= FONT_S_W * 2) {
                        bullet = 1;
                        if (emit_frag(d, 0, line_y, "- ", 2, -1, 0, 0, 0, 0) != 0) return -1;
                    }
                    if (emit_frag(d, x, line_y, sp->text + at, 1, sp->link, sp->strong, bl->heading, code_block, sp->code) != 0)
                        return -1;
                    x += FONT_S_W;
                    at++;
                }
                if (at < sp->n && sp->text[at] == ' ') help_next_line(&line_y, &x, x0);
                continue;
            }
            room = col_w - x;
            if (x > x0 && (j - at) * FONT_S_W > room) {
                help_next_line(&line_y, &x, x0);
                continue;
            }
            fit = (col_w - x) / FONT_S_W;
            if (fit < 1) {
                if (x == x0) return 0;
                help_next_line(&line_y, &x, x0);
                continue;
            }
            take = j - at;
            if (take > fit) take = fit;
            if (!bullet && bl->kind == HELP_ITEM && x0 >= FONT_S_W * 2) {
                bullet = 1;
                if (emit_frag(d, 0, line_y, "- ", 2, -1, 0, 0, 0, 0) != 0) return -1;
            }
            if (emit_frag(d, x, line_y, sp->text + at, take, sp->link, sp->strong, bl->heading, code_block, sp->code) != 0)
                return -1;
            x += take * FONT_S_W;
            at += take;
        }
    }
    *y = line_y + FONT_S_H;
    return 0;
}

static void place_image(struct block *b, int col_w) {
    int dw, dh;
    if (b->scaled && b->scaled != b->native) free(b->scaled);
    b->scaled = 0;
    b->draw_w = col_w > 0 ? col_w : 1;
    b->draw_h = FONT_S_H + 8;
    if (!b->native || b->iw < 1 || b->ih < 1 || col_w < 1) return;
    dw = b->iw;
    dh = b->ih;
    if (dw > col_w) {
        dh = dh * col_w / dw;
        if (dh < 1) dh = 1;
        dw = col_w;
    }
    b->draw_w = dw;
    b->draw_h = dh;
    if (dw == b->iw && dh == b->ih) {
        b->scaled = b->native;
        return;
    }
    b->scaled = image_scale(b->native, b->iw, b->ih, dw, dh);
    if (!b->scaled) {
        b->draw_w = col_w;
        b->draw_h = FONT_S_H + 8;
    }
}

static int layout_doc(struct doc *d, int col_w, int view_h) {
    int y = 0, i;
    (void)view_h;
    d->nfrags = 0;
    d->col_w = col_w;
    for (i = 0; i < d->nlinks; i++) {
        d->links[i].y = -1;
        d->links[i].bottom = -1;
    }
    for (i = 0; i < d->nblocks; i++) {
        struct block *b = &d->blocks[i];
        if (i > 0) y += 4;
        b->y = y;
        if (b->kind == HELP_IMG) {
            place_image(b, col_w);
            b->h = b->draw_h;
            y += b->h;
        } else {
            int start = y;
            if (layout_spans(d, b, col_w, &y) != 0) return -1;
            if (y == start) y += FONT_S_H;
            b->h = y - start;
        }
    }
    d->content_h = y + 2;
    return 0;
}

static void ensure_layout(struct doc *d, int body_w, int body_h) {
    if (!d || body_w < FONT_S_W || body_h < 1) return;
    if (d->laid_w == body_w && d->laid_h == body_h) return;
    if (layout_doc(d, body_w, body_h) != 0) return;
    d->bar = 0;
    if (d->content_h > body_h && body_w > FONT_S_W + 6) {
        if (layout_doc(d, body_w - 6, body_h) != 0) return;
        d->bar = 1;
    }
    d->laid_w = body_w;
    d->laid_h = body_h;
}

static int max_scroll(const struct doc *d, int body_h) {
    if (!d || body_h < 1 || d->content_h <= body_h) return 0;
    return d->content_h - body_h;
}

static int usable_link(const struct doc *d, int i) {
    return d && i >= 0 && i < d->nlinks && d->links[i].y >= 0;
}

static void clamp_view(struct ui_help *h, int body_h) {
    struct doc *d = h->doc;
    int top = max_scroll(d, body_h);
    if (h->scroll < 0) h->scroll = 0;
    if (h->scroll > top) h->scroll = top;
    if (!usable_link(d, h->link)) h->link = -1;
}

static void scroll_page(struct ui_help *h, const struct doc *d, int body_h, int dy) {
    int top;
    if (!d || body_h < 1 || dy == 0) return;
    top = max_scroll(d, body_h);
    h->scroll += dy;
    if (h->scroll < 0) h->scroll = 0;
    if (h->scroll > top) h->scroll = top;
}

// Pads 1 through 6: red, green, blue, yellow, teal, purple. Each channel is full
// so the pad LED, which takes half of 0..255, still lights at its brightest.
static const uint32_t PAD_COLOR[HELP_PAD_N] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0x00ffcc, 0xcc00ff};

struct help_hint {
    int link;
    int n;
    int a, b;
};

struct hint_pair {
    int a, b, dist, seq;
};

static int pad_manhattan(int a, int b) {
    int dc = ((a - 1) % 4) - ((b - 1) % 4);
    int dr = ((a - 1) / 4) - ((b - 1) / 4);
    if (dc < 0) dc = -dc;
    if (dr < 0) dr = -dr;
    return dc + dr;
}

static int usable_ids(const struct doc *d, int *ids, int cap) {
    int i, n = 0;
    if (!d) return 0;
    for (i = 0; i < d->nlinks && n < cap; i++)
        if (usable_link(d, i)) ids[n++] = i;
    return n;
}

static void sort_pairs(struct hint_pair *p, int n) {
    int i, j;
    for (i = 1; i < n; i++) {
        struct hint_pair key = p[i];
        j = i;
        while (j > 0 && (p[j - 1].dist > key.dist || (p[j - 1].dist == key.dist && p[j - 1].seq > key.seq))) {
            p[j] = p[j - 1];
            j--;
        }
        p[j] = key;
    }
}

// Up to six links use pads 1..n, one press each. Past that, each link gets an
// ordered pair from those same six pads, shortest hop first.
static int assign_hints(const struct doc *d, struct help_hint *out, int cap) {
    int ids[240];
    struct hint_pair pairs[HELP_PAD_N * HELP_PAD_N];
    int n, i, a, b, pc;
    n = usable_ids(d, ids, 240);
    if (n > cap) n = cap;
    if (n <= HELP_PAD_N) {
        for (i = 0; i < n; i++) {
            out[i].link = ids[i];
            out[i].n = 1;
            out[i].a = i + 1;
            out[i].b = 0;
        }
        return n;
    }
    pc = 0;
    for (a = 0; a < HELP_PAD_N; a++) {
        for (b = 0; b < HELP_PAD_N; b++) {
            if (a == b) continue;
            pairs[pc].a = a + 1;
            pairs[pc].b = b + 1;
            pairs[pc].dist = pad_manhattan(a + 1, b + 1);
            pairs[pc].seq = a * 16 + b;
            pc++;
        }
    }
    sort_pairs(pairs, pc);
    if (n > pc) n = pc;
    for (i = 0; i < n; i++) {
        out[i].link = ids[i];
        out[i].n = 2;
        out[i].a = pairs[i].a;
        out[i].b = pairs[i].b;
    }
    return n;
}

static void follow(struct ui_help *h, int link);

static int open_pad(struct ui_help *h, struct doc *d, int pad) {
    struct help_hint hints[240];
    int n, i, link;
    n = assign_hints(d, hints, 240);
    if (n < 1) return 0;
    if (hints[0].n == 1) {
        for (i = 0; i < n; i++) {
            if (hints[i].a != pad) continue;
            h->link = hints[i].link;
            follow(h, hints[i].link);
            return 1;
        }
        return 0;
    }
    if (!h->hint_pad) {
        for (i = 0; i < n; i++) {
            if (hints[i].a == pad) {
                h->hint_pad = pad;
                return 0;
            }
        }
        return 0;
    }
    link = -1;
    for (i = 0; i < n; i++) {
        if (hints[i].a == h->hint_pad && hints[i].b == pad) link = hints[i].link;
    }
    h->hint_pad = 0;
    if (link >= 0) {
        h->link = link;
        follow(h, link);
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (hints[i].a == pad) {
            h->hint_pad = pad;
            return 0;
        }
    }
    return 0;
}

static void take_pads(struct ui_help *h, struct doc *d, const struct input *in) {
    int i;
    if (!in->shift) {
        h->hint_pad = 0;
        return;
    }
    for (i = 0; i < 16; i++) {
        if (in->pad_hit[i] < 0) continue;
        if (open_pad(h, d, i + 1)) return;
        d = h->doc;
        if (!d) return;
    }
}

static uint32_t shade(uint32_t c, int num, int den) {
    int ch[3], i;
    if (den <= 0) return c & 0x00ffffff;
    ch[0] = (int)((c >> 16) & 255);
    ch[1] = (int)((c >> 8) & 255);
    ch[2] = (int)(c & 255);
    for (i = 0; i < 3; i++) {
        ch[i] = ch[i] * num / den;
        if (ch[i] < 0) ch[i] = 0;
        if (ch[i] > 255) ch[i] = 255;
    }
    return ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
}

static void fill_clip(struct ui *u, int x, int y, int w, int h, uint32_t c, int x0, int y0, int x1, int y1) {
    int xa = x < x0 ? x0 : x;
    int ya = y < y0 ? y0 : y;
    int xb = x + w > x1 ? x1 : x + w;
    int yb = y + h > y1 ? y1 : y + h;
    struct ui_paint p;
    if (xa >= xb || ya >= yb) return;
    p.fill_set = 1;
    p.fill = c;
    p.border_set = 0;
    ui_box(u, xa, ya, xb - xa, yb - ya, p);
}

static void draw_clip(struct ui *u, int x, int y, const char *s, int n, uint32_t color, int x0, int y0, int x1, int y1) {
    int i;
    if (y < y0 || y + FONT_S_H > y1) return;
    for (i = 0; i < n; i++) {
        int gx = x + i * FONT_S_W;
        int ch, row, col;
        const unsigned char *g;
        if (gx < x0 || gx + FONT_S_W > x1) continue;
        ch = s ? (unsigned char)s[i] : '?';
        if (ch < 32 || ch > 126) ch = '?';
        g = FONT_S[ch - 32];
        for (row = 0; row < FONT_S_H; row++)
            for (col = 0; col < FONT_S_W; col++) {
                int sx, sy;
                if (!(g[row] & (0x80 >> col))) continue;
                sx = gx + col;
                sy = y + row;
                if (sx < 0 || sy < 0 || sx >= UI_W || sy >= UI_H) continue;
                u->px[sy * UI_W + sx] = color & 0x00ffffff;
            }
    }
}

static void blit_clip(struct ui *u, int dx, int dy, int dw, int dh, const uint32_t *px, int x0, int y0, int x1, int y1) {
    int y, x;
    for (y = 0; y < dh; y++) {
        int sy = dy + y;
        if (sy < y0 || sy >= y1 || sy < 0 || sy >= UI_H) continue;
        for (x = 0; x < dw; x++) {
            int sx = dx + x;
            if (sx < x0 || sx >= x1 || sx < 0 || sx >= UI_W) continue;
            u->px[sy * UI_W + sx] = px[y * dw + x] & 0x00ffffff;
        }
    }
}

static void scroll_bar(struct ui *u, int x, int y, int w, int h, int view_y, int view_h, int content_h, uint32_t color) {
    int bw = 4, bx, thumb_h, travel, ty;
    uint32_t track;
    if (content_h <= view_h || h < 4 || w < 8) return;
    bx = x + w - bw;
    track = shade(color, 1, 3);
    fill_clip(u, bx, y, bw, h, track, x, y, x + w, y + h);
    thumb_h = h * view_h / content_h;
    if (thumb_h < 8) thumb_h = h < 8 ? h : 8;
    if (thumb_h > h) thumb_h = h;
    travel = h - thumb_h;
    ty = y;
    if (content_h > view_h && travel > 0) ty = y + travel * view_y / (content_h - view_h);
    fill_clip(u, bx, ty, bw, thumb_h, color, x, y, x + w, y + h);
}

static void go_back(struct ui_help *h) {
    char rel[UI_HELP_PATH];
    int scroll, link;
    if (h->nstack <= 0) return;
    snprintf(rel, sizeof rel, "%s", h->stack_page[h->nstack - 1]);
    scroll = h->stack_scroll[h->nstack - 1];
    link = h->stack_link[h->nstack - 1];
    if (load_page(h, rel) != 0) return;
    h->nstack--;
    h->scroll = scroll;
    h->link = link;
    h->hint_pad = 0;
}

static void go_home(struct ui_help *h) {
    h->hint_pad = 0;
    if (h->nstack == 0 && h->scroll == 0 && h->link < 0 && strcmp(h->page, h->start) == 0) return;
    if (load_page(h, h->start) != 0) return;
    h->nstack = 0;
    h->scroll = 0;
    h->link = -1;
    h->hint_pad = 0;
}

static void follow(struct ui_help *h, int link) {
    struct doc *d = h->doc;
    char rel[UI_HELP_PATH];
    char prev[UI_HELP_PATH];
    int prev_scroll, prev_link;
    if (!d || link < 0 || link >= d->nlinks) return;
    if (!d->links[link].rel[0] || strcmp(d->links[link].rel, h->page) == 0) return;
    snprintf(rel, sizeof rel, "%s", d->links[link].rel);
    snprintf(prev, sizeof prev, "%s", h->page);
    prev_scroll = h->scroll;
    prev_link = h->link;
    if (load_page(h, rel) != 0) return;
    if (h->nstack >= UI_HELP_STACK) {
        memmove(h->stack_page[0], h->stack_page[1], (UI_HELP_STACK - 1) * UI_HELP_PATH);
        memmove(h->stack_scroll, h->stack_scroll + 1, (UI_HELP_STACK - 1) * sizeof h->stack_scroll[0]);
        memmove(h->stack_link, h->stack_link + 1, (UI_HELP_STACK - 1) * sizeof h->stack_link[0]);
        h->nstack = UI_HELP_STACK - 1;
    }
    snprintf(h->stack_page[h->nstack], UI_HELP_PATH, "%s", prev);
    h->stack_scroll[h->nstack] = prev_scroll;
    h->stack_link[h->nstack] = prev_link;
    h->nstack++;
    h->scroll = 0;
    h->link = -1;
    h->hint_pad = 0;
}

static const char *label_or_empty(const char *s) { return s ? s : ""; }

static int first_frag(const struct doc *d, int frag_i) {
    int link, i;
    link = d->frags[frag_i].link;
    if (link < 0) return 0;
    for (i = 0; i < frag_i; i++)
        if (d->frags[i].link == link) return 0;
    return 1;
}

static uint32_t pad_color(int pad) {
    if (pad < 1 || pad > HELP_PAD_N) return 0xffffff;
    return PAD_COLOR[pad - 1];
}

static void draw_pad_box(struct ui *u, int x, int y, int pad, uint32_t fill, int x0, int y0, int x1, int y1) {
    char digit[2];
    fill_clip(u, x, y, HINT_BOX_W, HINT_BOX_H, pad_color(pad), x0, y0, x1, y1);
    fill_clip(u, x + HINT_RIM, y + HINT_RIM, FONT_S_W, FONT_S_H, fill, x0, y0, x1, y1);
    digit[0] = (char)('0' + pad);
    digit[1] = 0;
    draw_clip(u, x + HINT_RIM, y + HINT_RIM, digit, 1, 0xffffff, x0, y0, x1, y1);
}

// The combo sits on the link. The digit lines up with the first glyph, and the
// colored rim hangs a few pixels past it. A later link does not cover an earlier one.
static void draw_hint(struct ui *u, int sx, int sy, const struct help_hint *hint, struct ui_help_style st, int x0,
                      int y0, int x1, int y1) {
    int gap = 4;
    int bx = sx - HINT_RIM;
    int by = sy - HINT_RIM;
    draw_pad_box(u, bx, by, hint->a, st.fill, x0, y0, x1, y1);
    if (hint->n != 2) return;
    draw_clip(u, bx + HINT_BOX_W + 2, by + HINT_RIM, "-", 1, 0xffffff, x0, y0, x1, y1);
    draw_pad_box(u, bx + HINT_BOX_W + gap + FONT_S_W, by, hint->b, st.fill, x0, y0, x1, y1);
}

static void draw_page(struct ui *u, int x, int y, int w, int h, struct ui_help *hp, struct ui_help_style st, int hints) {
    struct doc *d = hp->doc;
    int soft_h = 24;
    int body_x, body_y, body_w, body_h;
    int view_y = 0;
    int i;
    struct help_hint hint_tab[240];
    int nhints = 0;
    const char *labels[3];
    struct ui_soft_style soft;
    struct ui_paint bg;
    if (w < 8 || h < soft_h + 4) return;
    bg.fill_set = 1;
    bg.fill = st.fill;
    bg.border_set = 0;
    ui_box(u, x, y, w, h, bg);
    labels[0] = label_or_empty(st.back);
    labels[1] = label_or_empty(st.home);
    labels[2] = label_or_empty(st.close);
    memset(&soft, 0, sizeof soft);
    soft.fill = st.border ? st.border : (uint32_t)UI_BOX;
    soft.text = st.text;
    soft.active_text = st.fill;
    soft.highlight = st.highlight;
    soft.gap = 4;
    soft.feedback = UI_SOFT_FLASH;
    soft.flash_ms = 120;
    ui_soft_keys(u, x, y, w, soft_h, labels, 0, soft, 0, &hp->keys);
    body_x = x + 4;
    body_y = y + soft_h + 4;
    body_w = w - 8;
    body_h = h - soft_h - 8;
    if (!d || body_w < FONT_S_W || body_h < FONT_S_H) {
        draw_clip(u, body_x, body_y, "This help page could not be opened.", 35, st.text, body_x, body_y, body_x + body_w, body_y + body_h);
        return;
    }
    view_y = hp->scroll;
    if (hints) nhints = assign_hints(d, hint_tab, 240);
    if (d->content_h > body_h && view_y > d->content_h - body_h) view_y = d->content_h - body_h;
    if (view_y < 0) view_y = 0;
    for (i = 0; i < d->nblocks; i++) {
        struct block *b = &d->blocks[i];
        int sy;
        if (b->kind != HELP_CODE && b->kind != HELP_IMG) continue;
        sy = body_y + b->y - view_y;
        if (b->kind == HELP_CODE)
            fill_clip(u, body_x, sy, d->col_w, b->h, st.code_fill, body_x, body_y, body_x + d->col_w, body_y + body_h);
        else if (b->scaled)
            blit_clip(u, body_x, sy, b->draw_w, b->draw_h, b->scaled, body_x, body_y, body_x + d->col_w, body_y + body_h);
        else {
            const char *alt = b->alt[0] ? b->alt : "missing";
            fill_clip(u, body_x, sy, d->col_w, b->draw_h, shade(st.border ? st.border : st.text, 1, 2), body_x, body_y, body_x + d->col_w, body_y + body_h);
            draw_clip(u, body_x + 4, sy + 4, alt, (int)strlen(alt), st.text, body_x, body_y, body_x + d->col_w, body_y + body_h);
        }
    }
    for (i = 0; i < d->nfrags; i++) {
        struct frag *f = &d->frags[i];
        int sy = body_y + f->y - view_y;
        int sx = body_x + f->x;
        uint32_t color = st.text;
        if (f->code_inline) {
            fill_clip(u, sx, sy, f->w, f->h, st.code_fill, body_x, body_y, body_x + d->col_w, body_y + body_h);
            color = st.code_text;
        }
        if (f->strong || f->heading) color = st.heading;
        if (f->link >= 0) color = st.link;
        else if (f->code_block) color = st.code_text;
        draw_clip(u, sx, sy, f->text, f->len, color, body_x, body_y, body_x + d->col_w, body_y + body_h);
    }
    if (hints) {
        for (i = 0; i < d->nfrags; i++) {
            struct frag *f = &d->frags[i];
            int k;
            if (f->link < 0 || !first_frag(d, i)) continue;
            for (k = 0; k < nhints; k++) {
                if (hint_tab[k].link != f->link) continue;
                draw_hint(u, body_x + f->x, body_y + f->y - view_y, &hint_tab[k], st, body_x, body_y, body_x + d->col_w,
                          body_y + body_h);
                break;
            }
        }
    }
    if (d->bar)
        scroll_bar(u, body_x, body_y, body_w, body_h, view_y, body_h, d->content_h, st.highlight);
}

struct ui_help_style ui_help_style_default(void) {
    struct ui_help_style st;
    memset(&st, 0, sizeof st);
    st.fill = UI_BG;
    st.text = UI_TEXT;
    st.heading = UI_HL;
    st.link = 0x9ecbff;
    st.code_fill = 0x242830;
    st.code_text = 0xd8d8d8;
    st.border = 0x505868;
    st.highlight = UI_HL;
    st.back = "Back";
    st.home = "Home";
    st.close = "Close";
    return st;
}

static void reset_help(struct ui_help *h) {
    doc_free(h->doc);
    h->doc = 0;
    h->files = 0;
    h->nfiles = 0;
    h->ready = 0;
    h->closed = 0;
    h->nstack = 0;
    h->scroll = 0;
    h->link = -1;
    h->hint_pad = 0;
    h->page[0] = 0;
    h->start[0] = 0;
    h->canon[0] = 0;
    memset(&h->keys, 0, sizeof h->keys);
}

int ui_help_open(struct ui_help *h, const char *root, const char *start) {
    char canon[PATH_MAX];
    char rel[UI_HELP_PATH];
    if (!h || !root || !root[0] || !start || !start[0]) return -1;
    reset_help(h);
    if (!realpath(root, canon) || strlen(canon) >= sizeof h->canon) return -1;
    memcpy(h->canon, canon, strlen(canon) + 1);
    if (resolve_href("", start, rel, (int)sizeof rel, 0) != 0) return -1;
    snprintf(h->start, sizeof h->start, "%s", rel);
    if (load_page(h, rel) != 0) return -1;
    h->ready = 1;
    return 0;
}

int ui_help_open_files(struct ui_help *h, const struct ui_help_file *files, int n, const char *start) {
    char rel[UI_HELP_PATH];
    if (!h || !files || n < 1 || !start || !start[0]) return -1;
    reset_help(h);
    h->files = files;
    h->nfiles = n;
    if (resolve_href("", start, rel, (int)sizeof rel, 0) != 0) return -1;
    if (!lookup_file(h, rel)) return -1;
    snprintf(h->start, sizeof h->start, "%s", rel);
    if (load_page(h, rel) != 0) return -1;
    h->ready = 1;
    return 0;
}

void ui_help_close(struct ui_help *h) {
    if (!h) return;
    doc_free(h->doc);
    h->doc = 0;
    h->ready = 0;
    h->closed = 1;
    h->nstack = 0;
}

const char *ui_help_page(const struct ui_help *h) {
    if (!h || !h->page[0]) return "";
    return h->page;
}

int ui_help_scroll(const struct ui_help *h) { return h ? h->scroll : 0; }

int ui_help_link(const struct ui_help *h) { return h ? h->link : -1; }

static void led_pad(struct ui *u, int index, uint32_t c) {
    int r = (int)((c >> 17) & 0x7f);
    int g = (int)((c >> 9) & 0x7f);
    int b = (int)((c >> 1) & 0x7f);
    // The green LED is much stronger than the red one. Equal red and green
    // lights as lime, so yellow keeps full red and only a little green.
    if (c == 0xffff00) g = 36;
    ui_led(u, index, r, g, b);
}

// Pads 1 through 6 take their color while Shift shows the combos. The rest of
// those six go dark, and all six go dark when Shift is up.
static void light_help_pads(struct ui *u, const struct doc *d, int on) {
    struct help_hint hints[240];
    int used[HELP_PAD_N];
    int n, i;
    for (i = 0; i < HELP_PAD_N; i++) used[i] = 0;
    if (on && d) {
        n = assign_hints(d, hints, 240);
        for (i = 0; i < n; i++) {
            if (hints[i].a >= 1 && hints[i].a <= HELP_PAD_N) used[hints[i].a - 1] = 1;
            if (hints[i].n == 2 && hints[i].b >= 1 && hints[i].b <= HELP_PAD_N) used[hints[i].b - 1] = 1;
        }
    }
    for (i = 0; i < HELP_PAD_N; i++) {
        if (used[i]) led_pad(u, i, PAD_COLOR[i]);
        else ui_led(u, i, 0, 0, 0);
    }
}

int ui_help(struct ui *u, int x, int y, int w, int h, struct ui_help *hp, struct ui_help_style st, const struct input *in) {
    struct doc *d;
    int soft_h = 24;
    int body_w, body_h;
    int key = -1;
    const char *labels[3];
    struct ui_soft_style soft;
    if (!u || !hp) return UI_HELP_CLOSED;
    if (hp->closed) return UI_HELP_CLOSED;
    body_w = w - 8;
    body_h = h - soft_h - 8;
    d = hp->doc;
    if (d && hp->ready) ensure_layout(d, body_w, body_h);
    clamp_view(hp, body_h);
    if (in) {
        labels[0] = label_or_empty(st.back);
        labels[1] = label_or_empty(st.home);
        labels[2] = label_or_empty(st.close);
        memset(&soft, 0, sizeof soft);
        soft.fill = st.border ? st.border : (uint32_t)UI_BOX;
        soft.text = st.text;
        soft.active_text = st.fill;
        soft.highlight = st.highlight;
        soft.gap = 4;
        soft.feedback = UI_SOFT_FLASH;
        soft.flash_ms = 120;
        key = ui_soft_keys(u, x, y, w, soft_h, labels, 0, soft, in, &hp->keys);
        if (key == 2 && st.close) {
            hp->closed = 1;
            light_help_pads(u, 0, 0);
            return UI_HELP_CLOSED;
        }
        if (hp->ready && key == 0 && st.back) go_back(hp);
        else if (hp->ready && key == 1 && st.home) go_home(hp);
        else if (hp->ready && key < 0) {
            d = hp->doc;
            if (d) ensure_layout(d, body_w, body_h);
            clamp_view(hp, body_h);
            if (in->encoder && d) {
                int step = FONT_S_H;
                if (in->shift) {
                    step = body_h / 2;
                    if (step < FONT_S_H) step = FONT_S_H;
                }
                scroll_page(hp, d, body_h, in->encoder * step);
            }
            if (d) take_pads(hp, d, in);
        }
        d = hp->doc;
        if (d && hp->ready) ensure_layout(d, body_w, body_h);
        clamp_view(hp, body_h);
    }
    draw_page(u, x, y, w, h, hp, st, in && in->shift);
    if (in) light_help_pads(u, hp->ready ? hp->doc : 0, in->shift && hp->ready);
    return hp->closed ? UI_HELP_CLOSED : UI_HELP_ACTIVE;
}
