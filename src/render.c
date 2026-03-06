#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Render buffer ────────────────────────────────────────── */

void render_append(const char *s, int len) {
    if (len <= 0) return;
    if (E.render_buf_len + len + 1 > E.render_buf_size) {
        E.render_buf_size = E.render_buf_len + len + 1024;
        E.render_buf = realloc(E.render_buf, E.render_buf_size);
        if (!E.render_buf) die("realloc render");
    }
    memcpy(E.render_buf + E.render_buf_len, s, len);
    E.render_buf_len += len;
}

#define RAPPEND(s)  render_append((s), (int)strlen(s))

void render_flush(void) {
    write(STDOUT_FILENO, E.render_buf, E.render_buf_len);
    E.render_buf_len = 0;
}

/* ── Window layout helpers ────────────────────────────────── */

int win_row_to_render(EditorBuffer *b, int row, int cx) {
    if (row < 0 || row >= b->numrows) return 0;
    EditorRow *r = &b->rows[row];
    int rx = 0;
    for (int i = 0; i < cx && i < r->size; i++) {
        if (r->chars[i] == '\t')
            rx += TAB_STOP - (rx % TAB_STOP);
        else
            rx++;
    }
    return rx;
}

EditorWindow *win_current(void) {
    return &E.windows[E.cur_win];
}

void win_set_cursor(int cx, int cy) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();

    if (cy < 0) cy = 0;
    if (cy >= b->numrows) cy = b->numrows - 1;
    if (cy < 0) cy = 0;

    int rowlen = (cy < b->numrows) ? b->rows[cy].size : 0;
    if (cx < 0) cx = 0;
    if (cx > rowlen) cx = rowlen;

    w->cx = cx; w->cy = cy;
    w->rx = win_row_to_render(b, cy, cx);
}

void win_scroll(void) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int text_rows = w->h - 2; /* minus status + cmd */
    int text_cols = w->w;
    if (E.show_line_numbers || E.show_relative_numbers)
        text_cols -= 5;

    w->rx = win_row_to_render(b, w->cy, w->cx);

    if (w->cy < w->rowoff + E.scroll_off) w->rowoff = w->cy - E.scroll_off;
    if (w->cy >= w->rowoff + text_rows - E.scroll_off)
        w->rowoff = w->cy - text_rows + 1 + E.scroll_off;
    if (w->rowoff < 0) w->rowoff = 0;

    if (w->rx < w->coloff) w->coloff = w->rx;
    if (w->rx >= w->coloff + text_cols) w->coloff = w->rx - text_cols + 1;
}

/* ── Row rendering ────────────────────────────────────────── */

static void render_one_row(int file_row) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int text_cols = w->w;
    int lnum_width = 0;

    /* Line numbers */
    if (E.show_line_numbers || E.show_relative_numbers) {
        lnum_width = 5;
        char lnum[16];
        int display_n;
        if (E.show_relative_numbers && file_row != w->cy)
            display_n = abs(file_row - w->cy);
        else
            display_n = file_row + 1;

        HlColor lc = hl_get_color(file_row == w->cy ? HL_LINENR_CUR : HL_LINENR);
        RAPPEND(hl_color_to_ansi(&lc, true));
        RAPPEND(hl_color_to_ansi(&lc, false));
        snprintf(lnum, sizeof(lnum), "%4d ", display_n);
        RAPPEND(lnum);
        RAPPEND("\x1b[0m");
        text_cols -= lnum_width;
    }

    if (file_row >= b->numrows) {
        /* Empty line */
        RAPPEND("\x1b[34m~\x1b[0m");
        return;
    }

    EditorRow *row = &b->rows[file_row];

    /* Cursor line background */
    bool is_cursor_line = (file_row == w->cy);

    int col = w->coloff;
    int printed = 0;
    int cur_hl = HL_NORMAL;

    /* Visual selection range */
    bool in_visual = (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE || E.mode == MODE_VISUAL_BLOCK);
    int vsel_start_row = w->cy < E.vy ? w->cy : E.vy;
    int vsel_end_row   = w->cy > E.vy ? w->cy : E.vy;
    int vsel_start_col = w->cx < E.vx ? w->cx : E.vx;
    int vsel_end_col   = w->cx > E.vx ? w->cx : E.vx;

    /* Set cursor line bg */
    if (is_cursor_line) {
        HlColor cl = hl_get_color(HL_CURSORLINE);
        RAPPEND(hl_color_to_ansi(&cl, false));
    }

    while (col < row->rsize && printed < text_cols) {
        char c = row->render[col];
        uint8_t hl = (row->hl && col < row->rsize) ? row->hl[col] : HL_NORMAL;

        /* Check visual highlight */
        bool sel = false;
        if (in_visual) {
            if (E.mode == MODE_VISUAL_LINE)
                sel = (file_row >= vsel_start_row && file_row <= vsel_end_row);
            else if (E.mode == MODE_VISUAL)
                sel = (file_row > vsel_start_row && file_row < vsel_end_row) ||
                      (file_row == vsel_start_row && col >= vsel_start_col &&
                       (file_row < vsel_end_row || col <= vsel_end_col)) ||
                      (file_row == vsel_end_row && col <= vsel_end_col &&
                       (file_row > vsel_start_row || col >= vsel_start_col));
        }

        /* Search highlight */
        bool srch_hl = false;
        if (E.hlsearch && E.search_query[0]) {
            int qlen = (int)strlen(E.search_query);
            if (col + qlen <= row->rsize &&
                strncmp(&row->render[col], E.search_query, qlen) == 0)
                srch_hl = true;
        }

        int new_hl = srch_hl ? HL_MATCH : (sel ? HL_SELECTION : hl);

        if (new_hl != cur_hl) {
            /* Reset */
            RAPPEND("\x1b[0m");
            if (is_cursor_line && !srch_hl && !sel) {
                HlColor cl = hl_get_color(HL_CURSORLINE);
                RAPPEND(hl_color_to_ansi(&cl, false));
            }
            cur_hl = new_hl;
            HlColor hc = hl_get_color((HlType)new_hl);
            if (hc.bold)      RAPPEND("\x1b[1m");
            if (hc.italic)    RAPPEND("\x1b[3m");
            if (hc.underline) RAPPEND("\x1b[4m");
            RAPPEND(hl_color_to_ansi(&hc, true));
            RAPPEND(hl_color_to_ansi(&hc, false));
        }

        if (c < 32) {
            /* Control char */
            char sym[4]; sym[0]='^'; sym[1]='@'+c; sym[2]='\0';
            render_append(sym, 2);
        } else {
            render_append(&c, 1);
        }
        col++; printed++;
    }

    RAPPEND("\x1b[0m");
    /* Clear rest of line */
    if (is_cursor_line) {
        HlColor cl = hl_get_color(HL_CURSORLINE);
        RAPPEND(hl_color_to_ansi(&cl, false));
        /* pad to end of window */
        for (int i = printed; i < text_cols; i++) RAPPEND(" ");
        RAPPEND("\x1b[0m");
    }
}

/* ── Status line ──────────────────────────────────────────── */

static const char *mode_name(void) {
    switch (E.mode) {
        case MODE_NORMAL:          return "NORMAL";
        case MODE_INSERT:          return "INSERT";
        case MODE_VISUAL:          return "VISUAL";
        case MODE_VISUAL_LINE:     return "V-LINE";
        case MODE_VISUAL_BLOCK:    return "V-BLOCK";
        case MODE_COMMAND:         return "COMMAND";
        case MODE_SEARCH:          return "SEARCH";
        case MODE_REPLACE:         return "REPLACE";
        case MODE_OPERATOR_PENDING:return "PENDING";
    }
    return "NORMAL";
}

static const char *mode_color(void) {
    switch (E.mode) {
        case MODE_INSERT:      return "\x1b[48;2;97;175;239m\x1b[38;2;30;31;36m";
        case MODE_VISUAL:
        case MODE_VISUAL_LINE:
        case MODE_VISUAL_BLOCK:return "\x1b[48;2;198;120;221m\x1b[38;2;30;31;36m";
        case MODE_COMMAND:
        case MODE_SEARCH:      return "\x1b[48;2;229;192;123m\x1b[38;2;30;31;36m";
        default:               return "\x1b[48;2;152;195;121m\x1b[38;2;30;31;36m";
    }
}

void render_statusline(void) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    int y = w->y + w->h - 2;
    char buf[512];
    int col = 0;

    term_goto(y, 0);
    RAPPEND("\x1b[K"); /* clear line */

    /* Mode badge */
    snprintf(buf, sizeof(buf), " %s ", mode_name());
    RAPPEND(mode_color());
    RAPPEND(buf);
    col += (int)strlen(buf);

    /* Separator */
    RAPPEND("\x1b[48;2;40;44;52m\x1b[38;2;171;178;191m");
    RAPPEND("\xe2\x96\x93"); /* ░ */
    col++;

    /* Filename + dirty flag */
    snprintf(buf, sizeof(buf), " %s%s ",
             b->filename,
             b->dirty ? " [+]" : "");
    RAPPEND(buf);
    col += (int)strlen(buf);

    /* Filetype */
    snprintf(buf, sizeof(buf), "[%s] ", b->filetype);
    RAPPEND(buf);
    col += (int)strlen(buf);

    /* Macro recording indicator */
    if (E.recording_macro) {
        snprintf(buf, sizeof(buf), "\x1b[38;2;224;108;117m[REC @%c] \x1b[38;2;171;178;191m", E.macro_reg);
        RAPPEND(buf);
    }

    /* Check plugin statusline */
    for (int i = 0; i < E.num_plugins; i++) {
        if (E.plugins[i] && E.plugins[i]->on_statusline) {
            const char *ps = E.plugins[i]->on_statusline();
            if (ps) { RAPPEND(ps); RAPPEND(" "); }
        }
    }

    /* Right side: position */
    char right[64];
    snprintf(right, sizeof(right), " %d:%d  %d%% ",
             w->cy + 1, w->cx + 1,
             b->numrows ? (w->cy + 1) * 100 / b->numrows : 0);
    int rlen = (int)strlen(right);
    int pad = w->w - col - rlen;
    for (int i = 0; i < pad; i++) RAPPEND(" ");
    RAPPEND(right);
    RAPPEND("\x1b[0m");
}

/* ── Tab line ─────────────────────────────────────────────── */

void render_tabline(void) {
    if (E.num_buffers <= 1) return;
    term_goto(0, 0);
    RAPPEND("\x1b[K");
    for (int i = 0; i < E.num_buffers; i++) {
        bool sel = (i == E.cur_buf);
        if (sel) RAPPEND("\x1b[48;2;40;44;52m\x1b[38;2;171;178;191m\x1b[1m");
        else     RAPPEND("\x1b[48;2;33;37;43m\x1b[38;2;92;99;112m");
        char tab[64];
        const char *fn = E.buffers[i].filename;
        const char *base = strrchr(fn, '/');
        base = base ? base+1 : fn;
        snprintf(tab, sizeof(tab), " %d:%s%s ", i+1, base,
                 E.buffers[i].dirty ? " ●" : "");
        RAPPEND(tab);
        RAPPEND("\x1b[0m");
    }
    RAPPEND("\x1b[0m");
}

/* ── Command/search line ─────────────────────────────────── */

void render_cmdline(void) {
    EditorWindow *w = win_current();
    int y = w->y + w->h - 1;
    term_goto(y, 0);
    RAPPEND("\x1b[K\x1b[0m");

    time_t now = time(NULL);
    if (E.mode == MODE_COMMAND) {
        render_append(":", 1);
        render_append(E.cmd_buf, E.cmd_len);
    } else if (E.mode == MODE_SEARCH) {
        render_append(E.search_forward ? "/" : "?", 1);
        render_append(E.search_query, (int)strlen(E.search_query));
    } else if (E.status_msg[0] && now - E.status_time < 4) {
        render_append(E.status_msg, (int)strlen(E.status_msg));
    }

    /* Completion menu */
    if (E.comp_active && E.comp_count > 0) {
        int menu_y = w->cy - w->rowoff + (E.num_buffers > 1 ? 1 : 0);
        int menu_x = (E.show_line_numbers ? 5 : 0) + w->rx - w->coloff;
        int show = E.comp_count < 8 ? E.comp_count : 8;
        for (int i = 0; i < show; i++) {
            term_goto(menu_y + 1 + i, menu_x);
            bool sel = (i == E.comp_idx);
            if (sel) RAPPEND("\x1b[48;2;62;68;81m\x1b[1m");
            else     RAPPEND("\x1b[48;2;40;44;52m");
            char item[64];
            snprintf(item, sizeof(item), " %-20s %-10s",
                     E.comp_items[i].word, E.comp_items[i].kind);
            RAPPEND(item);
            RAPPEND("\x1b[0m");
        }
    }
}

/* ── Full screen render ───────────────────────────────────── */

void render_screen(void) {
    EditorWindow *w = win_current();
    win_scroll();

    E.render_buf_len = 0;
    RAPPEND("\x1b[?25l"); /* hide cursor */
    RAPPEND("\x1b[H");    /* home */

    int tab_offset = (E.num_buffers > 1) ? 1 : 0;
    if (tab_offset) render_tabline();

    int text_rows = w->h - 2; /* status + cmdline */

    for (int y = 0; y < text_rows; y++) {
        int file_row = y + w->rowoff;
        term_goto(y + tab_offset, 0);
        RAPPEND("\x1b[K\x1b[0m");
        render_one_row(file_row);
    }

    render_statusline();
    render_cmdline();

    /* Position cursor */
    int cur_y = w->cy - w->rowoff + tab_offset;
    int cur_x = w->rx - w->coloff + (E.show_line_numbers || E.show_relative_numbers ? 5 : 0);
    if (E.mode == MODE_COMMAND || E.mode == MODE_SEARCH) {
        cur_y = w->y + w->h - 1;
        int prefix = 1;
        cur_x = prefix + (E.mode == MODE_COMMAND ? E.cmd_len : (int)strlen(E.search_query));
    }
    char pos[32];
    snprintf(pos, sizeof(pos), "\x1b[%d;%dH", cur_y+1, cur_x+1);
    RAPPEND(pos);

    /* Cursor shape by mode */
    switch (E.mode) {
        case MODE_INSERT:  RAPPEND("\x1b[6 q"); break; /* beam */
        case MODE_REPLACE: RAPPEND("\x1b[4 q"); break; /* underline */
        default:           RAPPEND("\x1b[2 q"); break; /* block */
    }
    RAPPEND("\x1b[?25h"); /* show cursor */
    render_flush();
}
