#include "editor.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ── Basic cursor movement ────────────────────────────────── */

void motion_move(int key) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    EditorRow *row = (w->cy < b->numrows) ? &b->rows[w->cy] : NULL;

    switch (key) {
        case 'h': case KEY_LEFT:
            if (w->cx > 0) w->cx--;
            else if (E.mode == MODE_INSERT && w->cy > 0) {
                w->cy--;
                w->cx = b->rows[w->cy].size;
            }
            break;
        case 'l': case KEY_RIGHT:
            if (row && w->cx < row->size - (E.mode == MODE_NORMAL ? 1 : 0))
                w->cx++;
            else if (E.mode == MODE_INSERT && w->cy < b->numrows - 1) {
                w->cy++; w->cx = 0;
            }
            break;
        case 'k': case KEY_UP:
            if (w->cy > 0) w->cy--;
            break;
        case 'j': case KEY_DOWN:
            if (w->cy < b->numrows - 1) w->cy++;
            break;
        case '0': case KEY_HOME:
            w->cx = 0;
            break;
        case '$': case KEY_END:
            if (row) w->cx = row->size - (E.mode == MODE_NORMAL && row->size > 0 ? 1 : 0);
            break;
        case '^': {
            /* first non-whitespace */
            if (row) {
                int i = 0;
                while (i < row->size && (row->chars[i] == ' ' || row->chars[i] == '\t')) i++;
                w->cx = i;
            }
            break;
        }
        case 'g': /* handled as gg at call site */
            break;
        case 'G':
            w->cy = b->numrows - 1;
            w->cx = 0;
            break;
        case KEY_PAGE_UP: {
            EditorWindow *ww = win_current();
            int rows = ww->h - 4;
            w->cy -= rows;
            if (w->cy < 0) w->cy = 0;
            break;
        }
        case KEY_PAGE_DOWN: {
            EditorWindow *ww = win_current();
            int rows = ww->h - 4;
            w->cy += rows;
            if (w->cy >= b->numrows) w->cy = b->numrows - 1;
            break;
        }
    }

    /* Clamp */
    if (w->cy < 0) w->cy = 0;
    if (w->cy >= b->numrows) w->cy = b->numrows - 1;
    row = &b->rows[w->cy];
    int maxcol = row->size;
    if (E.mode == MODE_NORMAL && maxcol > 0) maxcol--;
    if (w->cx > maxcol) w->cx = maxcol;
    if (w->cx < 0) w->cx = 0;
}

/* ── Word motions ─────────────────────────────────────────── */

/* Move forward to start of next word (w) or WORD (W) */
void motion_word_forward_exec(bool WORD) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int cx = w->cx, cy = w->cy;

    EditorRow *row = &b->rows[cy];
    bool big = WORD;

    if (cx < row->size) {
        char c = row->chars[cx];
        /* skip current "word" type */
        if (!big && is_separator(c) && c != ' ' && c != '\t') {
            while (cx < row->size && is_separator(row->chars[cx]) &&
                   row->chars[cx] != ' ' && row->chars[cx] != '\t')
                cx++;
        } else if (!big) {
            while (cx < row->size && is_word_char(row->chars[cx])) cx++;
        } else {
            while (cx < row->size && row->chars[cx] != ' ' && row->chars[cx] != '\t') cx++;
        }
        /* skip whitespace */
        while (cx < row->size && (row->chars[cx] == ' ' || row->chars[cx] == '\t')) cx++;
    }

    if (cx >= row->size && cy + 1 < b->numrows) {
        cy++; cx = 0;
        row = &b->rows[cy];
        while (cx < row->size && (row->chars[cx] == ' ' || row->chars[cx] == '\t')) cx++;
    }

    w->cx = cx; w->cy = cy;
}

/* b / B backward word */
void motion_word_backward_exec(bool WORD) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int cx = w->cx, cy = w->cy;

    if (cx == 0 && cy > 0) {
        cy--;
        cx = b->rows[cy].size;
    } else {
        cx--;
    }

    EditorRow *row = &b->rows[cy];

    /* skip whitespace */
    while (cx > 0 && (row->chars[cx] == ' ' || row->chars[cx] == '\t')) cx--;

    if (!WORD) {
        if (is_separator(row->chars[cx]) && row->chars[cx] != ' ')
            while (cx > 0 && is_separator(row->chars[cx-1]) && row->chars[cx-1] != ' ') cx--;
        else
            while (cx > 0 && is_word_char(row->chars[cx-1])) cx--;
    } else {
        while (cx > 0 && row->chars[cx-1] != ' ' && row->chars[cx-1] != '\t') cx--;
    }

    w->cx = cx; w->cy = cy;
}

/* e / E end of word */
void motion_word_end_exec(bool WORD) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int cx = w->cx, cy = w->cy;
    EditorRow *row = &b->rows[cy];

    if (cx + 1 >= row->size && cy + 1 < b->numrows) {
        cy++; cx = 0;
        row = &b->rows[cy];
    } else {
        cx++;
    }
    /* skip whitespace */
    while (cx < row->size && (row->chars[cx] == ' ' || row->chars[cx] == '\t')) cx++;

    if (!WORD) {
        if (is_separator(row->chars[cx]) && row->chars[cx] != ' ')
            while (cx + 1 < row->size && is_separator(row->chars[cx+1]) && row->chars[cx+1] != ' ') cx++;
        else
            while (cx + 1 < row->size && is_word_char(row->chars[cx+1])) cx++;
    } else {
        while (cx + 1 < row->size && row->chars[cx+1] != ' ' && row->chars[cx+1] != '\t') cx++;
    }

    w->cx = cx; w->cy = cy;
}

/* ── Goto line ────────────────────────────────────────────── */

void motion_goto_line(int n) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    if (n <= 0) n = 1;
    if (n > b->numrows) n = b->numrows;
    w->cy = n - 1;
    w->cx = 0;
    /* first non-blank */
    EditorRow *row = &b->rows[w->cy];
    while (w->cx < row->size && (row->chars[w->cx]==' '||row->chars[w->cx]=='\t'))
        w->cx++;
}

/* ── Find char (f/F/t/T) ─────────────────────────────────── */

void motion_find_char(char c, bool forward, bool till) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    EditorRow *row = &b->rows[w->cy];

    if (forward) {
        for (int i = w->cx + 1; i < row->size; i++) {
            if (row->chars[i] == c) {
                w->cx = till ? i - 1 : i;
                return;
            }
        }
    } else {
        for (int i = w->cx - 1; i >= 0; i--) {
            if (row->chars[i] == c) {
                w->cx = till ? i + 1 : i;
                return;
            }
        }
    }
}

/* ── Matching bracket (%) ────────────────────────────────── */

void motion_matching_bracket(void) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    EditorRow *row = &b->rows[w->cy];
    char c = row->chars[w->cx];

    char open, close; bool go_forward;
    if      (c=='('||c==')') { open='('; close=')'; go_forward=(c=='('); }
    else if (c=='['||c==']') { open='['; close=']'; go_forward=(c=='['); }
    else if (c=='{'||c=='}') { open='{'; close='}'; go_forward=(c=='{'); }
    else if (c=='<'||c=='>') { open='<'; close='>'; go_forward=(c=='<'); }
    else return;

    int depth = 1;
    int cx = w->cx, cy = w->cy;

    if (go_forward) {
        cx++;
        while (cy < b->numrows) {
            row = &b->rows[cy];
            while (cx < row->size) {
                if (row->chars[cx] == open)  depth++;
                if (row->chars[cx] == close) depth--;
                if (depth == 0) { w->cx = cx; w->cy = cy; return; }
                cx++;
            }
            cy++; cx = 0;
        }
    } else {
        cx--;
        while (cy >= 0) {
            row = &b->rows[cy];
            if (cx < 0) cx = row->size - 1;
            while (cx >= 0) {
                if (row->chars[cx] == close) depth++;
                if (row->chars[cx] == open)  depth--;
                if (depth == 0) { w->cx = cx; w->cy = cy; return; }
                cx--;
            }
            cy--;
            if (cy >= 0) cx = b->rows[cy].size - 1;
        }
    }
}

/* ── Text object helpers ──────────────────────────────────── */

void motion_inner_word(int *x1, int *x2) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    EditorRow *row = &b->rows[w->cy];
    int cx = w->cx;
    *x1 = cx; *x2 = cx;
    if (cx >= row->size) return;

    if (is_word_char(row->chars[cx])) {
        while (*x1 > 0 && is_word_char(row->chars[*x1-1])) (*x1)--;
        while (*x2 < row->size-1 && is_word_char(row->chars[*x2+1])) (*x2)++;
    } else {
        while (*x1 > 0 && is_separator(row->chars[*x1-1]) && row->chars[*x1-1]!=' ') (*x1)--;
        while (*x2 < row->size-1 && is_separator(row->chars[*x2+1]) && row->chars[*x2+1]!=' ') (*x2)++;
    }
}

void motion_inner_pair(char open, char close, int *y1, int *x1, int *y2, int *x2) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    /* Search backward for open, forward for close */
    int cy = w->cy, cx = w->cx;
    *y1 = cy; *x1 = cx; *y2 = cy; *x2 = cx;

    int depth = 0;
    for (int row = cy; row >= 0; row--) {
        int start = (row == cy) ? cx : b->rows[row].size - 1;
        for (int col = start; col >= 0; col--) {
            char c = b->rows[row].chars[col];
            if (c == close) depth++;
            if (c == open) {
                if (depth == 0) { *y1 = row; *x1 = col + 1; goto find_close; }
                depth--;
            }
        }
    }
    return;

find_close:
    depth = 0;
    for (int row = cy; row < b->numrows; row++) {
        int start = (row == cy) ? cx : 0;
        for (int col = start; col < b->rows[row].size; col++) {
            char c = b->rows[row].chars[col];
            if (c == open) depth++;
            if (c == close) {
                if (depth == 0) { *y2 = row; *x2 = col - 1; return; }
                depth--;
            }
        }
    }
}
