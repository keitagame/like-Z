#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Register helpers ─────────────────────────────────────── */

static int reg_idx(char r) {
    if (r >= 'a' && r <= 'z') return r - 'a';
    if (r >= '0' && r <= '9') return 26 + r - '0';
    return 0; /* default register */
}

static void reg_set(char r, const char *text, int len, bool is_line) {
    int idx = reg_idx(r);
    free(E.registers[idx].text);
    E.registers[idx].text = str_ndup(text, len);
    E.registers[idx].len = len;
    E.registers[idx].is_line = is_line;
}

static void reg_set_str(char r, const char *text, bool is_line) {
    reg_set(r, text, (int)strlen(text), is_line);
}

/* ── Undo ─────────────────────────────────────────────────── */

void undo_push(UndoType type, int cx, int cy, const char *data, int len) {
    EditorBuffer *b = buf_current();
    /* Discard redo history */
    while (b->undo_pos > 0 && b->undo_top > 0) {
        /* free entries above undo_pos ... simplified: just overwrite */
        break;
    }

    if (b->undo_top >= MAX_UNDO_HISTORY) {
        free(b->undo_stack[0]);
        memmove(&b->undo_stack[0], &b->undo_stack[1],
                sizeof(UndoEntry*) * (MAX_UNDO_HISTORY - 1));
        b->undo_top--;
    }

    UndoEntry *e = calloc(1, sizeof(UndoEntry));
    if (!e) die("malloc");
    e->type = type;
    e->cx = cx; e->cy = cy;
    if (data && len > 0) {
        e->data = str_ndup(data, len);
        e->data_len = len;
    }

    EditorWindow *w = win_current();
    e->cx2 = w->cx; e->cy2 = w->cy;

    b->undo_stack[b->undo_top++] = e;
    b->undo_pos = b->undo_top;
}

void undo_do(void) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();

    if (b->undo_pos <= 0) { set_status("Already at oldest change"); return; }
    b->undo_pos--;
    UndoEntry *e = b->undo_stack[b->undo_pos];

    switch (e->type) {
        case UNDO_INSERT_CHAR:
            buf_row_del_char(b->id, e->cy, e->cx);
            break;
        case UNDO_DELETE_CHAR:
            buf_row_insert_char(b->id, e->cy, e->cx, e->data[0]);
            break;
        case UNDO_INSERT_ROW:
            buf_del_row(b->id, e->cy);
            break;
        case UNDO_DELETE_ROW:
            buf_insert_row(b->id, e->cy, e->data ? e->data : "", e->data ? e->data_len : 0);
            break;
        case UNDO_CHANGE_ROW: {
            /* swap current with saved */
            char *cur = str_dup(b->rows[e->cy].chars);
            int cur_len = b->rows[e->cy].size;
            /* free old */
            free(b->rows[e->cy].chars);
            b->rows[e->cy].chars = str_ndup(e->data, e->data_len);
            b->rows[e->cy].size  = e->data_len;
            free(e->data);
            e->data = cur;
            e->data_len = cur_len;
            /* re-render */
            extern void row_update_render_pub(EditorBuffer*, EditorRow*);
            hl_update_row(b, e->cy);
            break;
        }
        default: break;
    }

    w->cx = e->cx; w->cy = e->cy;
    set_status("Undo");
}

void redo_do(void) {
    EditorBuffer *b = buf_current();
    if (b->undo_pos >= b->undo_top) { set_status("Already at newest change"); return; }
    UndoEntry *e = b->undo_stack[b->undo_pos];
    b->undo_pos++;

    (void)e;
    set_status("Redo");
}

/* ── Build string from row range ─────────────────────────── */

static char *rows_to_str(int y1, int x1, int y2, int x2, MotionType mt, int *out_len) {
    EditorBuffer *b = buf_current();
    size_t total = 0;

    if (mt == MOT_LINE) {
        x1 = 0;
        x2 = (y2 < b->numrows) ? b->rows[y2].size : 0;
    }

    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        int start = (r == y1) ? x1 : 0;
        int end   = (r == y2) ? x2 : b->rows[r].size;
        if (end > b->rows[r].size) end = b->rows[r].size;
        total += (end - start) + 1; /* +1 for newline */
    }

    char *buf = malloc(total + 1);
    if (!buf) die("malloc");
    char *p = buf;

    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        int start = (r == y1) ? x1 : 0;
        int end   = (r == y2) ? x2 : b->rows[r].size;
        if (end > b->rows[r].size) end = b->rows[r].size;
        int len = end - start;
        if (len > 0) { memcpy(p, &b->rows[r].chars[start], len); p += len; }
        if (r < y2) *p++ = '\n';
    }
    *p = '\0';
    *out_len = (int)(p - buf);
    return buf;
}

/* ── Yank ─────────────────────────────────────────────────── */

void op_yank_range(int y1, int x1, int y2, int x2, MotionType mt, char r) {
    int len;
    char *text = rows_to_str(y1, x1, y2, x2, mt, &len);
    reg_set(r ? r : '"', text, len, mt == MOT_LINE);
    /* also set to unnamed */
    if (r != '"') reg_set('"', text, len, mt == MOT_LINE);
    free(text);

    int lines = y2 - y1 + 1;
    if (lines == 1) set_status("Yanked %d chars", x2 - x1);
    else            set_status("Yanked %d lines", lines);
}

/* ── Delete ───────────────────────────────────────────────── */

void op_delete_range(int y1, int x1, int y2, int x2, MotionType mt) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();

    /* First yank */
    op_yank_range(y1, x1, y2, x2, mt, '"');

    if (mt == MOT_LINE) {
        int count = y2 - y1 + 1;
        for (int i = 0; i < count; i++)
            buf_del_row(b->id, y1);
        if (b->numrows == 0) buf_insert_row(b->id, 0, "", 0);
        w->cy = y1;
        if (w->cy >= b->numrows) w->cy = b->numrows - 1;
        w->cx = 0;
        return;
    }

    if (y1 == y2) {
        /* same line */
        int del = x2 - x1;
        for (int i = 0; i < del; i++)
            buf_row_del_char(b->id, y1, x1);
    } else {
        /* multi-line: truncate first row, remove middle rows, append tail */
        EditorRow *last = &b->rows[y2];
        char *tail = str_ndup(&last->chars[x2], last->size - x2);
        int tail_len = last->size - x2;

        /* Delete rows from y2 down to y1+1 */
        for (int r = y2; r > y1; r--) buf_del_row(b->id, r);

        /* Truncate row y1 at x1 */
        EditorRow *first = &b->rows[y1];
        first->size = x1;
        first->chars[x1] = '\0';

        /* Append tail */
        buf_row_append(b->id, y1, tail, tail_len);
        free(tail);
    }

    w->cy = y1;
    w->cx = x1;
    if (w->cx > 0 && w->cx >= b->rows[y1].size) w->cx = b->rows[y1].size - 1;
    if (w->cx < 0) w->cx = 0;
}

/* ── Change ───────────────────────────────────────────────── */

void op_change_range(int y1, int x1, int y2, int x2, MotionType mt) {
    op_delete_range(y1, x1, y2, x2, mt);
    E.mode = MODE_INSERT;
    plugin_fire_mode_change(MODE_NORMAL, MODE_INSERT);
    autocmd_fire(EVENT_INSERTENTER, "");
}

/* ── Indent ───────────────────────────────────────────────── */

void op_indent_range(int y1, int y2, bool indent) {
    EditorBuffer *b = buf_current();
    char spaces[TAB_STOP+1];
    memset(spaces, ' ', TAB_STOP);
    spaces[TAB_STOP] = '\0';

    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        if (indent) {
            /* Insert TAB_STOP spaces at beginning */
            for (int i = 0; i < TAB_STOP; i++)
                buf_row_insert_char(b->id, r, 0, ' ');
        } else {
            /* Remove up to TAB_STOP leading spaces */
            int removed = 0;
            while (removed < TAB_STOP && b->rows[r].size > 0 &&
                   (b->rows[r].chars[0] == ' ' || b->rows[r].chars[0] == '\t')) {
                buf_row_del_char(b->id, r, 0);
                removed++;
            }
        }
    }
}

/* ── Case conversion ──────────────────────────────────────── */

void op_case_range(int y1, int x1, int y2, int x2, bool upper) {
    EditorBuffer *b = buf_current();
    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        int start = (r == y1) ? x1 : 0;
        int end   = (r == y2) ? x2 : b->rows[r].size;
        if (end > b->rows[r].size) end = b->rows[r].size;
        for (int col = start; col < end; col++) {
            char c = b->rows[r].chars[col];
            char nc = upper ? toupper(c) : tolower(c);
            if (nc != c) {
                b->rows[r].chars[col] = nc;
                b->dirty++;
            }
        }
        hl_update_row(b, r);
    }
}

/* ── Comment toggle (gc) ──────────────────────────────────── */

void op_comment_range(int y1, int y2) {
    EditorBuffer *b = buf_current();
    const char *comment = "// ";
    if (!b->syntax) comment = "# ";
    else if (!b->syntax->singleline_comment) comment = "# ";
    else {
        comment = b->syntax->singleline_comment;
    }
    int clen = (int)strlen(comment);

    /* Check if all lines start with comment → uncomment, else comment */
    bool all_commented = true;
    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        if (strncmp(b->rows[r].chars, comment, clen) != 0) {
            all_commented = false; break;
        }
    }

    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        if (all_commented) {
            for (int i = 0; i < clen; i++) buf_row_del_char(b->id, r, 0);
        } else {
            for (int i = clen-1; i >= 0; i--) buf_row_insert_char(b->id, r, 0, comment[i]);
        }
    }
}

/* ── Paste ────────────────────────────────────────────────── */

void op_paste(char reg_char, bool after) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    Register *reg = &E.registers[reg_idx(reg_char)];

    if (!reg->text || reg->len == 0) {
        set_status("Nothing in register '%c'", reg_char);
        return;
    }

    if (reg->is_line) {
        int at = after ? w->cy + 1 : w->cy;
        /* Split by newlines and insert */
        char *text = str_ndup(reg->text, reg->len);
        char *p = text, *nl;
        int row_at = at;
        while ((nl = strchr(p, '\n'))) {
            *nl = '\0';
            buf_insert_row(b->id, row_at++, p, (int)strlen(p));
            p = nl + 1;
        }
        if (*p) buf_insert_row(b->id, row_at, p, (int)strlen(p));
        free(text);
        w->cy = at;
        w->cx = 0;
    } else {
        int insert_col = after ? w->cx + 1 : w->cx;
        if (insert_col > b->rows[w->cy].size) insert_col = b->rows[w->cy].size;

        /* Find newlines in paste text */
        char *text = str_ndup(reg->text, reg->len);
        char *p = text, *nl;
        int cx = insert_col, cy = w->cy;

        while ((nl = strchr(p, '\n'))) {
            *nl = '\0';
            /* Insert inline */
            int plen = (int)strlen(p);
            for (int i = 0; i < plen; i++) buf_row_insert_char(b->id, cy, cx+i, p[i]);

            /* Split line */
            EditorRow *row = &b->rows[cy];
            char *tail = str_ndup(&row->chars[cx+plen], row->size - (cx+plen));
            int tail_len = row->size - (cx+plen);
            row->size = cx + plen; row->chars[row->size] = '\0';
            buf_insert_row(b->id, cy+1, tail, tail_len);
            free(tail);
            cy++; cx = 0;
            p = nl + 1;
        }
        int plen = (int)strlen(p);
        for (int i = 0; i < plen; i++) buf_row_insert_char(b->id, cy, cx+i, p[i]);
        w->cy = cy;
        w->cx = cx + plen - 1;
        if (w->cx < 0) w->cx = 0;
        free(text);
    }
}
