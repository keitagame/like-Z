#include "editor.h"
#include <string.h>
#include <stdlib.h>

/* Simple substring search with wrap-around */

void search_find(const char *query, bool forward) {
    if (!query || !query[0]) return;
    strncpy(E.search_query, query, MAX_SEARCH_LEN-1);
    E.search_forward = forward;
    E.hlsearch = true;

    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    int start_row = w->cy;
    int start_col = forward ? w->cx + 1 : w->cx - 1;

    int qlen = (int)strlen(query);

    for (int pass = 0; pass < 2; pass++) {
        if (forward) {
            for (int r = (pass == 0 ? start_row : 0); r < b->numrows; r++) {
                EditorRow *row = &b->rows[r];
                int sc = (r == start_row && pass == 0) ? start_col : 0;
                if (sc < 0) sc = 0;
                for (int c = sc; c + qlen <= row->rsize; c++) {
                    if (strncmp(&row->render[c], query, qlen) == 0) {
                        /* find matching raw col */
                        w->cy = r;
                        /* convert render col to raw col */
                        int rx = 0, raw = 0;
                        while (raw < row->size && rx < c) {
                            if (row->chars[raw] == '\t')
                                rx += TAB_STOP - (rx % TAB_STOP);
                            else rx++;
                            raw++;
                        }
                        w->cx = raw;
                        E.search_last_row = r;
                        E.search_last_col = c;
                        return;
                    }
                }
                if (pass == 0 && r == b->numrows - 1) break;
            }
        } else {
            for (int r = (pass == 0 ? start_row : b->numrows-1); r >= 0; r--) {
                EditorRow *row = &b->rows[r];
                int sc = (r == start_row && pass == 0) ? start_col : row->rsize - qlen;
                if (sc < 0) sc = 0;
                if (sc + qlen > row->rsize) sc = row->rsize - qlen;
                for (int c = sc; c >= 0; c--) {
                    if (c + qlen <= row->rsize &&
                        strncmp(&row->render[c], query, qlen) == 0) {
                        w->cy = r;
                        int rx = 0, raw = 0;
                        while (raw < row->size && rx < c) {
                            if (row->chars[raw] == '\t')
                                rx += TAB_STOP - (rx % TAB_STOP);
                            else rx++;
                            raw++;
                        }
                        w->cx = raw;
                        E.search_last_row = r;
                        E.search_last_col = c;
                        return;
                    }
                }
                if (pass == 0 && r == 0) break;
            }
        }
    }
    set_status("Pattern not found: %s", query);
}

void search_next(bool same_dir) {
    bool forward = same_dir ? E.search_forward : !E.search_forward;
    if (!E.search_query[0]) { set_status("No previous search"); return; }
    search_find(E.search_query, forward);
}
