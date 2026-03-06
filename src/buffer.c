#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ── Utilities ────────────────────────────────────────────── */

char *str_dup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (!p) die("malloc");
    memcpy(p, s, len);
    return p;
}

char *str_ndup(const char *s, int n) {
    char *p = malloc(n + 1);
    if (!p) die("malloc");
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, MAX_STATUS_MSG, fmt, ap);
    va_end(ap);
    E.status_time = time(NULL);
}

bool is_separator(char c) {
    return c == '\0' || strchr(" \t\n\r,.(){}[]<>\"';:+-*/=~|&!@#$%^?\\`", c) != NULL;
}
bool is_word_char(char c) { return !is_separator(c); }

/* ── Row operations ───────────────────────────────────────── */

static void row_free(EditorRow *row) {
    free(row->chars);
    free(row->render);
    free(row->hl);
    row->chars = NULL;
    row->render = NULL;
    row->hl = NULL;
}

static void row_update_render(EditorBuffer *b, EditorRow *row) {
    (void)b;
    int tabs = 0;
    for (int i = 0; i < row->size; i++)
        if (row->chars[i] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);
    if (!row->render) die("malloc");

    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[i];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

/* ── Buffer new / lookup ──────────────────────────────────── */

int buf_new(void) {
    if (E.num_buffers >= MAX_BUFFERS) return -1;
    int id = E.num_buffers++;
    EditorBuffer *b = &E.buffers[id];
    memset(b, 0, sizeof(*b));
    b->id = id;
    b->rows = NULL;
    b->numrows = 0;
    b->dirty = 0;
    b->undo_top = 0;
    b->undo_pos = 0;
    strcpy(b->filename, "[No Name]");
    strcpy(b->filetype, "text");
    return id;
}

EditorBuffer *buf_current(void) {
    return &E.buffers[E.cur_buf];
}

/* ── Filetype detection ───────────────────────────────────── */

static void detect_filetype(EditorBuffer *b) {
    const char *fn = b->filename;
    const char *ext = strrchr(fn, '.');
    if (!ext) { strcpy(b->filetype, "text"); return; }
    ext++;
    if (!strcmp(ext,"c")||!strcmp(ext,"h"))   { strcpy(b->filetype,"c"); return; }
    if (!strcmp(ext,"cpp")||!strcmp(ext,"cc")||!strcmp(ext,"cxx")||!strcmp(ext,"hpp"))
                                               { strcpy(b->filetype,"cpp"); return; }
    if (!strcmp(ext,"py"))                     { strcpy(b->filetype,"python"); return; }
    if (!strcmp(ext,"rs"))                     { strcpy(b->filetype,"rust"); return; }
    if (!strcmp(ext,"js")||!strcmp(ext,"mjs")) { strcpy(b->filetype,"javascript"); return; }
    if (!strcmp(ext,"ts"))                     { strcpy(b->filetype,"typescript"); return; }
    if (!strcmp(ext,"go"))                     { strcpy(b->filetype,"go"); return; }
    if (!strcmp(ext,"lua"))                    { strcpy(b->filetype,"lua"); return; }
    if (!strcmp(ext,"sh")||!strcmp(ext,"bash")){ strcpy(b->filetype,"shell"); return; }
    if (!strcmp(ext,"md")||!strcmp(ext,"markdown")){ strcpy(b->filetype,"markdown"); return; }
    if (!strcmp(ext,"json"))                   { strcpy(b->filetype,"json"); return; }
    if (!strcmp(ext,"html")||!strcmp(ext,"htm")){ strcpy(b->filetype,"html"); return; }
    if (!strcmp(ext,"css"))                    { strcpy(b->filetype,"css"); return; }
    if (!strcmp(ext,"yaml")||!strcmp(ext,"yml")){ strcpy(b->filetype,"yaml"); return; }
    strcpy(b->filetype, "text");
}

/* ── Row insert/delete ────────────────────────────────────── */

void buf_insert_row(int id, int at, const char *s, int len) {
    EditorBuffer *b = &E.buffers[id];
    if (at < 0 || at > b->numrows) return;

    b->rows = realloc(b->rows, sizeof(EditorRow) * (b->numrows + 1));
    if (!b->rows) die("realloc");
    memmove(&b->rows[at+1], &b->rows[at], sizeof(EditorRow) * (b->numrows - at));

    EditorRow *row = &b->rows[at];
    memset(row, 0, sizeof(*row));
    row->idx   = at;
    row->size  = len;
    row->chars = str_ndup(s, len);
    row->render = NULL;
    row->hl     = NULL;
    row->hl_open_comment = false;
    row_update_render(b, row);

    for (int i = at+1; i <= b->numrows; i++) b->rows[i].idx = i;
    b->numrows++;
    b->dirty++;
}

void buf_del_row(int id, int at) {
    EditorBuffer *b = &E.buffers[id];
    if (at < 0 || at >= b->numrows) return;
    row_free(&b->rows[at]);
    memmove(&b->rows[at], &b->rows[at+1], sizeof(EditorRow)*(b->numrows - at - 1));
    b->numrows--;
    for (int i = at; i < b->numrows; i++) b->rows[i].idx = i;
    b->dirty++;
}

void buf_row_insert_char(int id, int row, int col, char c) {
    EditorBuffer *b = &E.buffers[id];
    EditorRow *r = &b->rows[row];
    if (col < 0 || col > r->size) col = r->size;
    r->chars = realloc(r->chars, r->size + 2);
    if (!r->chars) die("realloc");
    memmove(&r->chars[col+1], &r->chars[col], r->size - col + 1);
    r->chars[col] = c;
    r->size++;
    row_update_render(b, r);
    hl_update_row(b, row);
    b->dirty++;
}

void buf_row_del_char(int id, int row, int col) {
    EditorBuffer *b = &E.buffers[id];
    EditorRow *r = &b->rows[row];
    if (col < 0 || col >= r->size) return;
    memmove(&r->chars[col], &r->chars[col+1], r->size - col);
    r->size--;
    row_update_render(b, r);
    hl_update_row(b, row);
    b->dirty++;
}

void buf_row_append(int id, int row, const char *s, int len) {
    EditorBuffer *b = &E.buffers[id];
    EditorRow *r = &b->rows[row];
    r->chars = realloc(r->chars, r->size + len + 1);
    if (!r->chars) die("realloc");
    memcpy(&r->chars[r->size], s, len);
    r->size += len;
    r->chars[r->size] = '\0';
    row_update_render(b, r);
    hl_update_row(b, row);
    b->dirty++;
}

/* ── File open / save ────────────────────────────────────── */

void buf_open(int id, const char *filename) {
    EditorBuffer *b = &E.buffers[id];
    FILE *fp = fopen(filename, "r");

    strncpy(b->filename, filename, MAX_FILENAME-1);
    detect_filetype(b);

    if (!fp) {
        /* new file */
        buf_insert_row(id, 0, "", 0);
        b->dirty = 0;
        return;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            linelen--;
        buf_insert_row(id, b->numrows, line, (int)linelen);
    }
    free(line);
    fclose(fp);
    b->dirty = 0;

    if (b->numrows == 0) buf_insert_row(id, 0, "", 0);

    hl_update_all(b);
    plugin_fire_buf_read(filename);
    autocmd_fire(EVENT_BUFREAD, filename);
}

void buf_save(int id) {
    EditorBuffer *b = &E.buffers[id];

    if (strcmp(b->filename, "[No Name]") == 0) {
        set_status("No filename. Use :w <filename>");
        return;
    }

    /* Build output */
    size_t total = 0;
    for (int i = 0; i < b->numrows; i++)
        total += b->rows[i].size + 1;

    char *buf = malloc(total);
    if (!buf) { set_status("Out of memory!"); return; }
    char *p = buf;
    for (int i = 0; i < b->numrows; i++) {
        memcpy(p, b->rows[i].chars, b->rows[i].size);
        p += b->rows[i].size;
        *p++ = '\n';
    }

    int fd = open(b->filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { free(buf); set_status("Can't save: %s", strerror(errno)); return; }

    ssize_t written = write(fd, buf, total);
    close(fd);
    free(buf);

    if (written != (ssize_t)total) {
        set_status("Write error: %s", strerror(errno));
        return;
    }

    b->dirty = 0;
    plugin_fire_buf_write(b->filename);
    autocmd_fire(EVENT_BUFWRITE, b->filename);
    set_status("%d lines, %zu bytes written", b->numrows, total);
}

void buf_close(int id) {
    EditorBuffer *b = &E.buffers[id];
    for (int i = 0; i < b->numrows; i++) row_free(&b->rows[i]);
    free(b->rows);
    memset(b, 0, sizeof(*b));
}
