#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ── Helpers ─────────────────────────────────────────────── */

static void enter_mode(EditorMode m) {
    EditorMode old = E.mode;
    E.prev_mode = old;
    E.mode = m;
    plugin_fire_mode_change(old, m);
    if (m == MODE_INSERT)  autocmd_fire(EVENT_INSERTENTER, "");
    if (old == MODE_INSERT) autocmd_fire(EVENT_INSERTLEAVE, "");
}

static void enter_normal(void) {
    /* Adjust cursor if past EOL */
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    if (w->cy < b->numrows) {
        int maxcol = b->rows[w->cy].size - 1;
        if (maxcol < 0) maxcol = 0;
        if (w->cx > maxcol) w->cx = maxcol;
    }
    enter_mode(MODE_NORMAL);
    comp_cancel();
}

static void insert_newline(void) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();

    if (E.auto_indent && w->cy < b->numrows) {
        /* Copy leading whitespace */
        EditorRow *row = &b->rows[w->cy];
        int indent = 0;
        while (indent < row->size && (row->chars[indent]==' '||row->chars[indent]=='\t'))
            indent++;

        /* Split current line */
        char *tail = str_ndup(&row->chars[w->cx], row->size - w->cx);
        int tail_len = row->size - w->cx;
        row->size = w->cx;
        row->chars[w->cx] = '\0';

        char *new_row = malloc(indent + tail_len + 1);
        if (!new_row) die("malloc");
        memcpy(new_row, row->chars, indent); /* copy indent */
        memcpy(new_row + indent, tail, tail_len);
        new_row[indent + tail_len] = '\0';

        buf_insert_row(b->id, w->cy + 1, new_row, indent + tail_len);
        free(new_row); free(tail);
        /* re-render original row */
        extern void row_update_render_pub(EditorBuffer*, EditorRow*);
        hl_update_row(b, w->cy);
        w->cy++; w->cx = indent;
    } else {
        char *tail = str_ndup(&b->rows[w->cy].chars[w->cx],
                              b->rows[w->cy].size - w->cx);
        int tail_len = b->rows[w->cy].size - w->cx;
        b->rows[w->cy].size = w->cx;
        b->rows[w->cy].chars[w->cx] = '\0';
        buf_insert_row(b->id, w->cy+1, tail, tail_len);
        free(tail);
        hl_update_row(b, w->cy);
        w->cy++; w->cx = 0;
    }
}

/* ── Count prefix accumulation ───────────────────────────── */

static int  s_count = 0;
static char s_reg   = '"';

static int flush_count(void) {
    int c = s_count > 0 ? s_count : 1;
    s_count = 0;
    return c;
}

/* ── Normal mode ─────────────────────────────────────────── */

static char last_f_char = 0;
static bool last_f_forward = true;
static bool last_f_till = false;

void input_process_normal(int key) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();

    /* Record macro */
    if (E.recording_macro && !E.playing_macro) {
        if (key != 'q') {
            if (E.macro_len < MAX_MACRO_LEN-1)
                E.macro_buf[E.macro_len++] = (char)key;
        }
    }

    /* Count digits */
    if (key >= '1' && key <= '9' && s_count == 0) { s_count = key - '0'; return; }
    if (key >= '0' && key <= '9' && s_count > 0)  { s_count = s_count * 10 + key - '0'; return; }

    /* Register prefix */
    if (key == '"') {
        /* read next key for register */
        int r2 = term_read_key();
        while (r2 == KEY_NULL) r2 = term_read_key();
        s_reg = (char)r2;
        return;
    }

    int count = flush_count();
    char reg = s_reg; s_reg = '"';

    /* Check user keymaps first */
    char km_rhs[MAX_CMD_LEN]; void(*km_fn)(void) = NULL;
    char seq[4] = {(char)key, 0};
    int km = keymap_lookup(MODE_NORMAL, seq, km_rhs, &km_fn);
    if (km == 2) { km_fn(); return; }
    if (km == 1) { cmd_execute(km_rhs); return; }

    plugin_fire_key(key);

    switch (key) {
        /* ── Movement ─── */
        case 'h': case 'l': case 'j': case 'k':
        case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
        case '0': case '$': case '^':
        case KEY_HOME: case KEY_END:
            for (int i = 0; i < count; i++) motion_move(key);
            break;

        case 'g': {
            int k2 = term_read_key();
            while (k2 == KEY_NULL) k2 = term_read_key();
            if (k2 == 'g') motion_goto_line(count > 1 ? count : 1);
            else if (k2 == 'e') { for(int i=0;i<count;i++) motion_word_end_exec(false); }
            else if (k2 == 'E') { for(int i=0;i<count;i++) motion_word_end_exec(true); }
            break;
        }
        case 'G':
            if (s_count == 0) motion_goto_line(b->numrows);
            else motion_goto_line(count);
            break;
        case KEY_PAGE_UP: case KEY_PAGE_DOWN:
            motion_move(key);
            break;
        case 'w': for (int i=0;i<count;i++) motion_word_forward_exec(false); break;
        case 'W': for (int i=0;i<count;i++) motion_word_forward_exec(true);  break;
        case 'b': for (int i=0;i<count;i++) motion_word_backward_exec(false); break;
        case 'B': for (int i=0;i<count;i++) motion_word_backward_exec(true);  break;
        case 'e': for (int i=0;i<count;i++) motion_word_end_exec(false); break;
        case 'E': for (int i=0;i<count;i++) motion_word_end_exec(true);  break;
        case '%': motion_matching_bracket(); break;
        case 'H': w->cy = w->rowoff; break;
        case 'M': w->cy = w->rowoff + (w->h-4)/2; if(w->cy>=b->numrows) w->cy=b->numrows-1; break;
        case 'L': w->cy = w->rowoff + w->h - 5; if(w->cy>=b->numrows) w->cy=b->numrows-1; break;

        case 'f': case 'F': case 't': case 'T': {
            int fc = term_read_key();
            while (fc == KEY_NULL) fc = term_read_key();
            bool fwd  = (key == 'f' || key == 't');
            bool till = (key == 't' || key == 'T');
            motion_find_char((char)fc, fwd, till);
            last_f_char = (char)fc;
            last_f_forward = fwd; last_f_till = till;
            break;
        }
        case ';': motion_find_char(last_f_char, last_f_forward, last_f_till); break;
        case ',': motion_find_char(last_f_char, !last_f_forward, last_f_till); break;

        /* ── Scrolling ─── */
        case CTRL_KEY('d'): w->cy += (w->h-4)/2; if(w->cy>=b->numrows) w->cy=b->numrows-1; break;
        case CTRL_KEY('u'): w->cy -= (w->h-4)/2; if(w->cy<0) w->cy=0; break;
        case CTRL_KEY('e'): w->rowoff++; break;
        case CTRL_KEY('y'): if(w->rowoff>0) w->rowoff--; break;
        case 'z': {
            int k2 = term_read_key();
            while (k2 == KEY_NULL) k2 = term_read_key();
            if (k2 == 'z') w->rowoff = w->cy - (w->h-4)/2;
            else if (k2 == 't') w->rowoff = w->cy;
            else if (k2 == 'b') w->rowoff = w->cy - (w->h-5);
            if (w->rowoff < 0) w->rowoff = 0;
            break;
        }

        /* ── Insert modes ─── */
        case 'i': enter_mode(MODE_INSERT); break;
        case 'I':
            w->cx = 0;
            while (w->cx < b->rows[w->cy].size &&
                   (b->rows[w->cy].chars[w->cx]==' '||b->rows[w->cy].chars[w->cx]=='\t'))
                w->cx++;
            enter_mode(MODE_INSERT);
            break;
        case 'a':
            if (w->cx < b->rows[w->cy].size) w->cx++;
            enter_mode(MODE_INSERT);
            break;
        case 'A':
            w->cx = b->rows[w->cy].size;
            enter_mode(MODE_INSERT);
            break;
        case 'o':
            buf_insert_row(b->id, w->cy+1, "", 0);
            w->cy++; w->cx=0;
            /* Auto-indent */
            if (E.auto_indent && w->cy > 0) {
                EditorRow *pr = &b->rows[w->cy-1];
                int ind=0; while(ind<pr->size&&(pr->chars[ind]==' '||pr->chars[ind]=='\t')) ind++;
                for(int i=0;i<ind;i++) buf_row_insert_char(b->id,w->cy,i,' ');
                w->cx=ind;
            }
            enter_mode(MODE_INSERT);
            break;
        case 'O':
            buf_insert_row(b->id, w->cy, "", 0);
            w->cx=0;
            if (E.auto_indent && w->cy+1 < b->numrows) {
                EditorRow *nr = &b->rows[w->cy+1];
                int ind=0; while(ind<nr->size&&(nr->chars[ind]==' '||nr->chars[ind]=='\t')) ind++;
                for(int i=0;i<ind;i++) buf_row_insert_char(b->id,w->cy,i,' ');
                w->cx=ind;
            }
            enter_mode(MODE_INSERT);
            break;
        case 's':
            if (b->numrows && w->cy < b->numrows && w->cx < b->rows[w->cy].size)
                buf_row_del_char(b->id, w->cy, w->cx);
            enter_mode(MODE_INSERT);
            break;
        case 'S': {
            /* Clear line and enter insert */
            EditorRow *row = &b->rows[w->cy];
            /* save indent */
            int ind=0; while(ind<row->size&&(row->chars[ind]==' '||row->chars[ind]=='\t')) ind++;
            for (int i = row->size-1; i >= ind; i--) buf_row_del_char(b->id, w->cy, i);
            w->cx=ind;
            enter_mode(MODE_INSERT);
            break;
        }
        case 'R': enter_mode(MODE_REPLACE); break;

        /* ── Delete operators ─── */
        case 'x':
            for (int i=0;i<count;i++)
                if (w->cx < b->rows[w->cy].size)
                    buf_row_del_char(b->id, w->cy, w->cx);
            break;
        case 'X':
            for (int i=0;i<count;i++)
                if (w->cx > 0) { w->cx--; buf_row_del_char(b->id, w->cy, w->cx); }
            break;
        case 'D':
            op_delete_range(w->cy, w->cx, w->cy, b->rows[w->cy].size, MOT_CHAR);
            break;
        case 'C':
            op_change_range(w->cy, w->cx, w->cy, b->rows[w->cy].size, MOT_CHAR);
            break;
        case 'Y':
            op_yank_range(w->cy, 0, w->cy, b->rows[w->cy].size, MOT_LINE, reg);
            break;
        case 'J': {
            /* Join lines */
            if (w->cy + 1 < b->numrows) {
                EditorRow *next = &b->rows[w->cy+1];
                buf_row_append(b->id, w->cy, " ", 1);
                buf_row_append(b->id, w->cy, next->chars, next->size);
                int cx_save = b->rows[w->cy].size - next->size - 1;
                buf_del_row(b->id, w->cy+1);
                w->cx = cx_save > 0 ? cx_save : 0;
            }
            break;
        }

        /* ── d/y/c operator ─── */
        case 'd': case 'y': case 'c': case '>': case '<': {
            int k2 = term_read_key();
            while (k2 == KEY_NULL) k2 = term_read_key();
            int y1=w->cy, x1=w->cx, y2=w->cy, x2=w->cx;
            MotionType mt = MOT_CHAR;

            if (k2 == key) { /* dd, yy, cc */
                y1 = w->cy; y2 = w->cy + count - 1;
                if (y2 >= b->numrows) y2 = b->numrows-1;
                x1=0; x2=b->rows[y2].size;
                mt = MOT_LINE;
            } else if (k2=='j') { y2=w->cy+count; if(y2>=b->numrows) y2=b->numrows-1; x1=0; x2=b->rows[y2].size; mt=MOT_LINE; }
            else if (k2=='k') { y1=w->cy-count; if(y1<0)y1=0; x1=0; x2=b->rows[y2].size; mt=MOT_LINE; }
            else if (k2=='h') { x1=w->cx-count; if(x1<0)x1=0; }
            else if (k2=='l') { x2=w->cx+count; if(x2>b->rows[y1].size)x2=b->rows[y1].size; }
            else if (k2=='w') {
                int ox=w->cx; for(int i=0;i<count;i++) motion_word_forward_exec(false);
                x2=w->cx; w->cx=ox;
            }
            else if (k2=='W') {
                int ox=w->cx; for(int i=0;i<count;i++) motion_word_forward_exec(true);
                x2=w->cx; w->cx=ox;
            }
            else if (k2=='e') {
                int ox=w->cx; for(int i=0;i<count;i++) motion_word_end_exec(false);
                x2=w->cx+1; w->cx=ox;
            }
            else if (k2=='b') {
                int ox=w->cx; for(int i=0;i<count;i++) motion_word_backward_exec(false);
                x1=w->cx; w->cx=ox;
            }
            else if (k2=='$') { x2=b->rows[y1].size; }
            else if (k2=='0') { x1=0; }
            else if (k2=='G') { y2=b->numrows-1; x1=0; x2=b->rows[y2].size; mt=MOT_LINE; }
            else if (k2=='i'||k2=='a') {
                /* text objects */
                int k3 = term_read_key();
                while (k3==KEY_NULL) k3=term_read_key();
                if (k3=='w'||k3=='W') {
                    int tx1, tx2;
                    motion_inner_word(&tx1, &tx2);
                    x1=tx1; x2=tx2+1;
                } else if (k3=='('||k3==')'||k3=='b') {
                    motion_inner_pair('(',')', &y1,&x1,&y2,&x2);
                    x2++;
                } else if (k3=='['||k3==']') {
                    motion_inner_pair('[',']', &y1,&x1,&y2,&x2); x2++;
                } else if (k3=='{'||k3=='}'||k3=='B') {
                    motion_inner_pair('{','}', &y1,&x1,&y2,&x2); x2++;
                } else if (k3=='"'||k3=='\'') {
                    /* Simple: find enclosing quotes on same line */
                    EditorRow *row = &b->rows[w->cy];
                    int lq=-1, rq=-1;
                    for (int ci=w->cx-1;ci>=0;ci--) if(row->chars[ci]==k3){lq=ci;break;}
                    for (int ci=w->cx+1;ci<row->size;ci++) if(row->chars[ci]==k3){rq=ci;break;}
                    if (lq>=0&&rq>=0) { x1=lq+(k2=='i'?1:0); x2=rq+(k2=='i'?0:1); }
                }
            }

            if      (key=='d') op_delete_range(y1,x1,y2,x2,mt);
            else if (key=='y') op_yank_range(y1,x1,y2,x2,mt,reg);
            else if (key=='c') op_change_range(y1,x1,y2,x2,mt);
            else if (key=='>') op_indent_range(y1,y2,true);
            else if (key=='<') op_indent_range(y1,y2,false);
            break;
        }

        /* ── Paste ─── */
        case 'p': op_paste(reg, true);  break;
        case 'P': op_paste(reg, false); break;

        /* ── Undo/Redo ─── */
        case 'u':           undo_do(); break;
        case CTRL_KEY('r'): redo_do(); break;

        /* ── Search ─── */
        case '/':
            enter_mode(MODE_SEARCH);
            E.search_forward = true;
            E.search_query[0] = '\0';
            break;
        case '?':
            enter_mode(MODE_SEARCH);
            E.search_forward = false;
            E.search_query[0] = '\0';
            break;
        case 'n': search_next(true);  break;
        case 'N': search_next(false); break;
        case '*': {
            /* Search current word */
            EditorRow *row = &b->rows[w->cy];
            int ws=w->cx, we=w->cx;
            while (ws>0 && is_word_char(row->chars[ws-1])) ws--;
            while (we<row->size && is_word_char(row->chars[we])) we++;
            char word[128]={0}; strncpy(word, &row->chars[ws], we-ws < 127 ? we-ws : 127);
            search_find(word, true);
            break;
        }
        case '#': {
            EditorRow *row = &b->rows[w->cy];
            int ws=w->cx, we=w->cx;
            while (ws>0 && is_word_char(row->chars[ws-1])) ws--;
            while (we<row->size && is_word_char(row->chars[we])) we++;
            char word[128]={0}; strncpy(word, &row->chars[ws], we-ws < 127 ? we-ws : 127);
            search_find(word, false);
            break;
        }

        /* ── Visual mode ─── */
        case 'v':
            enter_mode(MODE_VISUAL);
            E.vx = w->cx; E.vy = w->cy;
            break;
        case 'V':
            enter_mode(MODE_VISUAL_LINE);
            E.vx = w->cx; E.vy = w->cy;
            break;
        case CTRL_KEY('v'):
            enter_mode(MODE_VISUAL_BLOCK);
            E.vx = w->cx; E.vy = w->cy;
            break;

        /* ── Marks ─── */
        case 'm': {
            int mk = term_read_key();
            while (mk==KEY_NULL) mk=term_read_key();
            if ((mk>='a'&&mk<='z')||(mk>='A'&&mk<='Z')) {
                int mi = mk>='a' ? mk-'a' : mk-'A'+26;
                E.marks[mi].cx=w->cx; E.marks[mi].cy=w->cy; E.marks[mi].set=true;
            }
            break;
        }
        case '\'': case '`': {
            int mk = term_read_key();
            while (mk==KEY_NULL) mk=term_read_key();
            if ((mk>='a'&&mk<='z')||(mk>='A'&&mk<='Z')) {
                int mi = mk>='a' ? mk-'a' : mk-'A'+26;
                if (E.marks[mi].set) { w->cy=E.marks[mi].cy; w->cx=E.marks[mi].cx; }
            }
            break;
        }

        /* ── Replace char ─── */
        case 'r': {
            int rc = term_read_key();
            while (rc==KEY_NULL) rc=term_read_key();
            if (w->cy < b->numrows && w->cx < b->rows[w->cy].size) {
                b->rows[w->cy].chars[w->cx] = (char)rc;
                b->dirty++;
                hl_update_row(b, w->cy);
            }
            break;
        }
        case '~': {
            if (w->cy < b->numrows && w->cx < b->rows[w->cy].size) {
                char c = b->rows[w->cy].chars[w->cx];
                b->rows[w->cy].chars[w->cx] = isupper(c) ? tolower(c) : toupper(c);
                b->dirty++; hl_update_row(b,w->cy); w->cx++;
            }
            break;
        }

        /* ── Macro recording ─── */
        case 'q': {
            if (E.recording_macro) {
                E.recording_macro = false;
                /* Remove the 'q' we may have recorded */
                set_status("Recorded macro @%c (%d keys)", E.macro_reg, E.macro_len);
            } else {
                int mr = term_read_key();
                while (mr==KEY_NULL) mr=term_read_key();
                E.macro_reg = (char)mr;
                E.macro_len = 0;
                E.recording_macro = true;
                set_status("Recording macro @%c", E.macro_reg);
            }
            break;
        }
        case '@': {
            int mr = term_read_key();
            while (mr==KEY_NULL) mr=term_read_key();
            /* Replay macro_buf */
            if (!E.playing_macro) {
                E.playing_macro = true;
                for (int i=0;i<E.macro_len;i++) input_process_normal((unsigned char)E.macro_buf[i]);
                E.playing_macro = false;
            }
            break;
        }
        case CTRL_KEY('@'):
            /* @@ repeat last macro */
            if (!E.playing_macro && E.macro_len > 0) {
                E.playing_macro = true;
                for (int i=0;i<E.macro_len;i++) input_process_normal((unsigned char)E.macro_buf[i]);
                E.playing_macro = false;
            }
            break;

        /* ── Dot repeat ─── */
        case '.':
            /* TODO: repeat last change */
            set_status("Dot repeat (TODO)");
            break;

        /* ── Case convert in visual ─── */
        case 'U':
            if (w->cy < b->numrows && w->cx < b->rows[w->cy].size)
                op_case_range(w->cy, w->cx, w->cy, w->cx+count, true);
            break;

        /* ── Indent ─── */
        case CTRL_KEY(']'):
            /* ctags jump (stub) */
            set_status("ctags not available");
            break;

        /* ── Buffer navigation ─── */
        case CTRL_KEY('n'):
            E.cur_buf = (E.cur_buf+1) % E.num_buffers;
            win_current()->buf_id = E.cur_buf;
            break;
        case CTRL_KEY('p'):
            E.cur_buf = (E.cur_buf+E.num_buffers-1) % E.num_buffers;
            win_current()->buf_id = E.cur_buf;
            break;

        /* ── Command mode ─── */
        case ':':
            enter_mode(MODE_COMMAND);
            E.cmd_len = 0;
            E.cmd_buf[0] = '\0';
            break;

        /* ── Save / Quit shortcuts ─── */
        case CTRL_KEY('s'): buf_save(E.cur_buf); break;
        case CTRL_KEY('q'): cmd_execute("q"); break;

        /* ── Clear search highlight ─── */
        case CTRL_KEY('l'):
            E.hlsearch = false;
            term_clear();
            break;

        default: break;
    }
}

/* ── Insert mode ─────────────────────────────────────────── */

void input_process_insert(int key) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();

    /* Check user keymaps */
    char km_rhs[MAX_CMD_LEN]; void(*km_fn)(void)=NULL;
    char seq[4]={(char)key,0};
    int km = keymap_lookup(MODE_INSERT, seq, km_rhs, &km_fn);
    if (km==2) { km_fn(); return; }
    if (km==1) { /* execute rhs as keys? */ }

    switch (key) {
        case ESC: /* also CTRL+[ */
            enter_normal();
            break;
        case '\r':
            undo_push(UNDO_INSERT_ROW, w->cx, w->cy, NULL, 0);
            insert_newline();
            break;
        case BACKSPACE: case CTRL_KEY('h'):
            if (w->cx > 0) {
                undo_push(UNDO_DELETE_CHAR, w->cx-1, w->cy,
                          &b->rows[w->cy].chars[w->cx-1], 1);
                w->cx--;
                buf_row_del_char(b->id, w->cy, w->cx);
            } else if (w->cy > 0) {
                /* Merge with previous line */
                int prev_size = b->rows[w->cy-1].size;
                buf_row_append(b->id, w->cy-1, b->rows[w->cy].chars, b->rows[w->cy].size);
                buf_del_row(b->id, w->cy);
                w->cy--; w->cx = prev_size;
            }
            break;
        case KEY_DEL:
            if (w->cx < b->rows[w->cy].size) {
                buf_row_del_char(b->id, w->cy, w->cx);
            }
            break;
        case KEY_UP:   if(w->cy>0) w->cy--; break;
        case KEY_DOWN: if(w->cy<b->numrows-1) w->cy++; break;
        case KEY_LEFT: if(w->cx>0) w->cx--; break;
        case KEY_RIGHT:if(w->cx<b->rows[w->cy].size) w->cx++; break;
        case KEY_HOME: w->cx=0; break;
        case KEY_END:  w->cx=b->rows[w->cy].size; break;
        case CTRL_KEY('n'): comp_next(); break;
        case CTRL_KEY('p'): comp_prev(); break;
        case '\t':
            if (E.comp_active) { comp_accept(); break; }
            /* Insert spaces */
            for (int i=0; i<TAB_STOP-(w->cx%TAB_STOP); i++)
                buf_row_insert_char(b->id, w->cy, w->cx++, ' ');
            break;
        case CTRL_KEY('w'):
            /* Delete word back */
            while (w->cx > 0 && (b->rows[w->cy].chars[w->cx-1]==' '||b->rows[w->cy].chars[w->cx-1]=='\t'))
            { w->cx--; buf_row_del_char(b->id, w->cy, w->cx); }
            while (w->cx > 0 && is_word_char(b->rows[w->cy].chars[w->cx-1]))
            { w->cx--; buf_row_del_char(b->id, w->cy, w->cx); }
            break;
        case CTRL_KEY('u'):
            /* Delete to start of line */
            while (w->cx > 0) { w->cx--; buf_row_del_char(b->id, w->cy, w->cx); }
            break;
        default:
            if (key >= 32 && key < 127) {
                undo_push(UNDO_INSERT_CHAR, w->cx, w->cy, NULL, 0);
                buf_row_insert_char(b->id, w->cy, w->cx, (char)key);
                w->cx++;
                /* Auto-trigger completion on word chars */
                if (is_word_char((char)key) && E.comp_active) comp_trigger();
            }
            break;
    }
}

/* ── Visual mode ─────────────────────────────────────────── */

void input_process_visual(int key) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();

    switch (key) {
        case ESC: /* also CTRL+[ */ case 'v': case 'V':
            enter_normal(); break;
        case 'h':case 'l':case 'j':case 'k':
        case KEY_UP:case KEY_DOWN:case KEY_LEFT:case KEY_RIGHT:
        case '0':case '$':case '^':case 'w':case 'b':case 'e':case 'G':
            if (key=='w') motion_word_forward_exec(false);
            else if (key=='b') motion_word_backward_exec(false);
            else if (key=='e') motion_word_end_exec(false);
            else motion_move(key);
            break;
        case 'd': case 'x': {
            int y1=w->cy<E.vy?w->cy:E.vy, x1=w->cx<E.vx?w->cx:E.vx;
            int y2=w->cy>E.vy?w->cy:E.vy, x2=w->cx>E.vx?w->cx:E.vx;
            MotionType mt = E.mode==MODE_VISUAL_LINE ? MOT_LINE : MOT_CHAR;
            op_delete_range(y1,x1,y2,x2+1,mt);
            enter_normal(); break;
        }
        case 'y': {
            int y1=w->cy<E.vy?w->cy:E.vy, x1=w->cx<E.vx?w->cx:E.vx;
            int y2=w->cy>E.vy?w->cy:E.vy, x2=w->cx>E.vx?w->cx:E.vx;
            MotionType mt = E.mode==MODE_VISUAL_LINE ? MOT_LINE : MOT_CHAR;
            op_yank_range(y1,x1,y2,x2+1,mt,'"');
            enter_normal(); break;
        }
        case '>': {
            int y1=w->cy<E.vy?w->cy:E.vy, y2=w->cy>E.vy?w->cy:E.vy;
            op_indent_range(y1,y2,true); enter_normal(); break;
        }
        case '<': {
            int y1=w->cy<E.vy?w->cy:E.vy, y2=w->cy>E.vy?w->cy:E.vy;
            op_indent_range(y1,y2,false); enter_normal(); break;
        }
        case ':':
            enter_mode(MODE_COMMAND);
            E.cmd_len=0; E.cmd_buf[0]='\0';
            /* Pre-fill range */
            int y1=w->cy<E.vy?w->cy:E.vy, y2=w->cy>E.vy?w->cy:E.vy;
            E.cmd_len = snprintf(E.cmd_buf, MAX_CMD_LEN, "'<,'>"); (void)y1; (void)y2;
            break;
        case 'c': case 'C': {
            int y1=w->cy<E.vy?w->cy:E.vy, x1=w->cx<E.vx?w->cx:E.vx;
            int y2=w->cy>E.vy?w->cy:E.vy, x2=w->cx>E.vx?w->cx:E.vx;
            op_change_range(y1,x1,y2,x2+1,E.mode==MODE_VISUAL_LINE?MOT_LINE:MOT_CHAR);
            break;
        }
        default: break;
    }
}

/* ── Command mode ────────────────────────────────────────── */

void input_process_command(int key) {
    switch (key) {
        case ESC: /* also CTRL+[ */
            enter_normal();
            break;
        case '\r': {
            char cmd[MAX_CMD_LEN];
            strncpy(cmd, E.cmd_buf, MAX_CMD_LEN-1);
            enter_normal();
            cmd_execute(cmd);
            break;
        }
        case BACKSPACE: case CTRL_KEY('h'):
            if (E.cmd_len > 0) E.cmd_buf[--E.cmd_len] = '\0';
            else enter_normal();
            break;
        case KEY_UP:
            if (E.cmd_history_idx > 0) {
                E.cmd_history_idx--;
                strncpy(E.cmd_buf, E.cmd_history[E.cmd_history_idx], MAX_CMD_LEN-1);
                E.cmd_len = (int)strlen(E.cmd_buf);
            }
            break;
        case KEY_DOWN:
            if (E.cmd_history_idx < E.cmd_history_len-1) {
                E.cmd_history_idx++;
                strncpy(E.cmd_buf, E.cmd_history[E.cmd_history_idx], MAX_CMD_LEN-1);
                E.cmd_len = (int)strlen(E.cmd_buf);
            } else {
                E.cmd_history_idx = E.cmd_history_len;
                E.cmd_buf[0]='\0'; E.cmd_len=0;
            }
            break;
        default:
            if (key >= 32 && key < 127 && E.cmd_len < MAX_CMD_LEN-1) {
                E.cmd_buf[E.cmd_len++] = (char)key;
                E.cmd_buf[E.cmd_len] = '\0';
            }
            break;
    }
}

/* ── Search mode ─────────────────────────────────────────── */

void input_process_search(int key) {
    int qlen = (int)strlen(E.search_query);
    switch (key) {
        case ESC: /* also CTRL+[ */
            enter_normal();
            break;
        case '\r':
            search_find(E.search_query, E.search_forward);
            enter_normal();
            break;
        case BACKSPACE: case CTRL_KEY('h'):
            if (qlen > 0) E.search_query[qlen-1] = '\0';
            else enter_normal();
            break;
        default:
            if (key >= 32 && key < 127 && qlen < MAX_SEARCH_LEN-1) {
                E.search_query[qlen] = (char)key;
                E.search_query[qlen+1] = '\0';
                /* Incremental search */
                search_find(E.search_query, E.search_forward);
            }
            break;
    }
}

/* ── Replace mode ────────────────────────────────────────── */

static void input_process_replace(int key) {
    EditorWindow *w = win_current();
    EditorBuffer *b = buf_current();
    switch (key) {
        case ESC: enter_normal(); break;
        default:
            if (key >= 32 && key < 127) {
                if (w->cx < b->rows[w->cy].size)
                    b->rows[w->cy].chars[w->cx] = (char)key;
                else
                    buf_row_insert_char(b->id, w->cy, w->cx, (char)key);
                b->dirty++;
                hl_update_row(b, w->cy);
                w->cx++;
            }
            break;
    }
}

/* ── Main input dispatcher ───────────────────────────────── */

void input_process_key(int key) {
    if (key == KEY_NULL) return;

    switch (E.mode) {
        case MODE_NORMAL:
        case MODE_OPERATOR_PENDING: input_process_normal(key); break;
        case MODE_INSERT:           input_process_insert(key); break;
        case MODE_VISUAL:
        case MODE_VISUAL_LINE:
        case MODE_VISUAL_BLOCK:     input_process_visual(key); break;
        case MODE_COMMAND:          input_process_command(key); break;
        case MODE_SEARCH:           input_process_search(key); break;
        case MODE_REPLACE:          input_process_replace(key); break;
    }
}
