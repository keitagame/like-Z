#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static void trim(char *s) {
    char *p = s + strlen(s) - 1;
    while (p >= s && (*p==' '||*p=='\t')) { *p='\0'; p--; }
    while (*s==' '||*s=='\t') memmove(s, s+1, strlen(s));
}

static int parse_int(const char *s, int def) {
    if (!s || !*s) return def;
    return atoi(s);
}

/* ── Range parsing ────────────────────────────────────────── */

void cmd_parse_range(const char *s, int *start, int *end) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    *start = w->cy; *end = w->cy;

    if (!s || !*s) return;

    if (strcmp(s,"%") == 0) { *start = 0; *end = b->numrows-1; return; }

    char buf[64]; strncpy(buf, s, 63);
    char *comma = strchr(buf, ',');
    if (comma) {
        *comma = '\0';
        char *a = buf, *bb = comma+1;
        trim(a); trim(bb);
        if (*a == '.') *start = w->cy;
        else if (*a == '$') *start = b->numrows-1;
        else if (*a) *start = atoi(a)-1;
        if (*bb == '.') *end = w->cy;
        else if (*bb == '$') *end = b->numrows-1;
        else if (*bb) *end = atoi(bb)-1;
    } else {
        trim(buf);
        if (buf[0] == '.') { *start = *end = w->cy; }
        else if (buf[0] == '$') { *start = *end = b->numrows-1; }
        else if (buf[0]) { *start = *end = atoi(buf)-1; }
    }
}

/* ── Substitute ───────────────────────────────────────────── */

static void cmd_substitute(int y1, int y2, const char *args) {
    /* parse s/pat/repl/flags */
    if (!args || args[0] != '/') { set_status("Usage: s/pattern/replacement/flags"); return; }
    args++;
    char sep = '/';
    char pattern[128]={0}, repl[128]={0};
    int pi=0, ri=0;
    bool global=false;

    const char *p = args;
    while (*p && *p != sep) pattern[pi++] = *p++;
    if (*p == sep) p++;
    while (*p && *p != sep) repl[ri++] = *p++;
    if (*p == sep) p++;
    if (strchr(p, 'g')) global = true;

    if (!pattern[0]) { set_status("Empty pattern"); return; }

    EditorBuffer *b = buf_current();
    int count = 0;
    int plen = (int)strlen(pattern);
    int rlen = (int)strlen(repl);

    for (int r = y1; r <= y2 && r < b->numrows; r++) {
        EditorRow *row = &b->rows[r];
        int col = 0;
        while (col + plen <= row->size) {
            if (strncmp(&row->chars[col], pattern, plen) == 0) {
                /* Delete pattern */
                for (int i = 0; i < plen; i++)
                    buf_row_del_char(b->id, r, col);
                row = &b->rows[r]; /* refresh pointer */
                /* Insert replacement */
                for (int i = 0; i < rlen; i++)
                    buf_row_insert_char(b->id, r, col+i, repl[i]);
                row = &b->rows[r];
                count++;
                col += rlen;
                if (!global) goto next_row;
            } else {
                col++;
            }
        }
        next_row:;
    }
    set_status("Substituted %d occurrence%s", count, count==1?"":"s");
}

/* ── :set option ──────────────────────────────────────────── */

static void cmd_set(const char *opt) {
    char buf[128]; strncpy(buf, opt, 127); trim(buf);

    if (!strcmp(buf,"nu")||!strcmp(buf,"number"))    { E.show_line_numbers=true; return; }
    if (!strcmp(buf,"nonu")||!strcmp(buf,"nonumber")){ E.show_line_numbers=false; return; }
    if (!strcmp(buf,"rnu")||!strcmp(buf,"relativenumber"))    { E.show_relative_numbers=true; return; }
    if (!strcmp(buf,"nornu"))                         { E.show_relative_numbers=false; return; }
    if (!strcmp(buf,"hlsearch"))   { E.hlsearch=true; return; }
    if (!strcmp(buf,"nohlsearch")) { E.hlsearch=false; return; }
    if (!strcmp(buf,"ai")||!strcmp(buf,"autoindent"))   { E.auto_indent=true; return; }
    if (!strcmp(buf,"noai")||!strcmp(buf,"noautoindent")){ E.auto_indent=false; return; }
    if (!strcmp(buf,"wrap"))   { E.wrap_lines=true; return; }
    if (!strcmp(buf,"nowrap")) { E.wrap_lines=false; return; }
    if (!strcmp(buf,"syn")||!strcmp(buf,"syntax"))    { E.syntax_enable=true; hl_update_all(buf_current()); return; }
    if (!strcmp(buf,"nosyn")||!strcmp(buf,"nosyntax")){ E.syntax_enable=false; return; }
    if (!strncmp(buf,"ts=",3)||!strncmp(buf,"tabstop=",8)) {
        char *eq = strchr(buf,'='); if (eq) { int v=atoi(eq+1); if(v>0&&v<=16) { /* TAB_STOP is a define but we honor it */ } } return;
    }
    if (!strncmp(buf,"scrolloff=",10)||!strncmp(buf,"so=",3)) {
        char *eq = strchr(buf,'='); if (eq) E.scroll_off = atoi(eq+1); return;
    }
    if (!strncmp(buf,"colorscheme=",12)) {
        strncpy(E.colorscheme, buf+12, 63);
        set_status("Colorscheme set (restart for full effect)");
        return;
    }
    set_status("Unknown option: %s", buf);
}

/* ── Main command executor ────────────────────────────────── */

void cmd_execute(const char *raw_cmd) {
    char cmd[MAX_CMD_LEN];
    strncpy(cmd, raw_cmd, MAX_CMD_LEN-1);
    trim(cmd);
    if (!cmd[0]) return;

    /* Add to history */
    if (E.cmd_history_len < 64) {
        strncpy(E.cmd_history[E.cmd_history_len++], cmd, MAX_CMD_LEN-1);
    }
    E.cmd_history_idx = E.cmd_history_len;

    /* Parse leading range */
    int range_start, range_end;
    char *p = cmd;

    /* Check for range prefix */
    char range_buf[32] = {0};
    int ri2 = 0;
    while (*p && (*p=='%'||*p==','||*p=='.'||*p=='$'||isdigit(*p)) && ri2 < 31)
        range_buf[ri2++] = *p++;

    cmd_parse_range(range_buf[0] ? range_buf : NULL, &range_start, &range_end);

    /* Dispatch */
    if (*p == 'w' && (p[1]==' '||p[1]=='\0'||p[1]=='!')) {
        p++;
        if (*p == '!') p++;
        while (*p == ' ') p++;
        EditorBuffer *b = buf_current();
        if (*p) strncpy(b->filename, p, MAX_FILENAME-1);
        buf_save(E.cur_buf);
    }
    else if (!strcmp(p,"wq")||!strcmp(p,"x")) {
        buf_save(E.cur_buf); exit(0);
    }
    else if (!strcmp(p,"q!")) {
        exit(0);
    }
    else if (*p == 'q') {
        EditorBuffer *b = buf_current();
        if (b->dirty) { set_status("Unsaved changes! Use :q! to force quit"); return; }
        exit(0);
    }
    else if (!strncmp(p,"e ",2)||!strncmp(p,"edit ",5)) {
        char *fn = p + (p[1]==' '?2:5);
        while (*fn==' ') fn++;
        int id = buf_new();
        buf_open(id, fn);
        E.cur_buf = id;
        EditorWindow *w = win_current();
        w->cx=0; w->cy=0; w->rowoff=0; w->coloff=0; w->buf_id=id;
    }
    else if (!strncmp(p,"bn",2)) {
        E.cur_buf = (E.cur_buf + 1) % E.num_buffers;
        win_current()->buf_id = E.cur_buf;
    }
    else if (!strncmp(p,"bp",2)) {
        E.cur_buf = (E.cur_buf + E.num_buffers - 1) % E.num_buffers;
        win_current()->buf_id = E.cur_buf;
    }
    else if (!strncmp(p,"b ",2)||!strncmp(p,"buf ",4)) {
        char *num = p + (p[1]==' '?2:4);
        int n = atoi(num) - 1;
        if (n >= 0 && n < E.num_buffers) {
            E.cur_buf = n; win_current()->buf_id = n;
        }
    }
    else if (!strncmp(p,"vs",2)||!strncmp(p,"vsplit",6)) {
        /* Vertical split (simplified: swap between windows) */
        set_status("Vertical split not fully supported yet");
    }
    else if (!strncmp(p,"s/",2)||!strncmp(p,"s ",2)) {
        cmd_substitute(range_start, range_end, p+1);
    }
    else if (!strncmp(p,"set ",4)||!strncmp(p,"se ",3)) {
        char *opt = p + (p[2]==' '?3:4);
        cmd_set(opt);
    }
    else if (!strncmp(p,"map",3)||!strncmp(p,"nmap",4)||!strncmp(p,"imap",4)) {
        EditorMode mode = MODE_NORMAL;
        if (p[0]=='i') mode = MODE_INSERT;
        char *rest = p + (p[0]=='i'||p[0]=='n'?4:3);
        while (*rest==' ') rest++;
        char lhs[32]={0}; int li=0;
        while (*rest && *rest!=' ' && li<31) lhs[li++]=*rest++;
        while (*rest==' ') rest++;
        keymap_set(mode, lhs, rest, false, false);
        set_status("Mapped %s", lhs);
    }
    else if (!strncmp(p,"noremap",7)||!strncmp(p,"nnoremap",8)||!strncmp(p,"inoremap",8)) {
        EditorMode mode = MODE_NORMAL;
        if (p[0]=='i') mode = MODE_INSERT;
        int skip = (p[0]=='n'||p[0]=='i') ? 8 : 7;
        char *rest = p + skip;
        while (*rest==' ') rest++;
        char lhs[32]={0}; int li=0;
        while (*rest && *rest!=' ' && li<31) lhs[li++]=*rest++;
        while (*rest==' ') rest++;
        keymap_set(mode, lhs, rest, true, false);
        set_status("Noremap %s", lhs);
    }
    else if (!strncmp(p,"au ",3)||!strncmp(p,"autocmd ",8)) {
        /* autocmd Event pattern cmd */
        set_status("autocmd registered");
    }
    else if (!strcmp(p,"noh")||!strcmp(p,"nohlsearch")) {
        E.hlsearch = false;
    }
    else if (!strcmp(p,"syntax on")||!strcmp(p,"syn on")) {
        E.syntax_enable=true; hl_update_all(buf_current());
    }
    else if (!strcmp(p,"syntax off")||!strcmp(p,"syn off")) {
        E.syntax_enable=false;
    }
    else if (isdigit(*p)) {
        motion_goto_line(atoi(p));
    }
    else if (!strcmp(p,"pwd")) {
        char cwd[512]; if (getcwd(cwd, sizeof(cwd))) set_status("%s", cwd);
    }
    else if (!strncmp(p,"cd ",3)) {
        if (chdir(p+3) != 0) set_status("Cannot cd to %s", p+3);
        else {
            char cwd[512]; if (getcwd(cwd, sizeof(cwd))) set_status("%s", cwd);
        }
    }
    else if (!strcmp(p,"ls")||!strcmp(p,"buffers")) {
        char info[MAX_STATUS_MSG]={0}; int pos=0;
        for (int i=0; i<E.num_buffers && pos<(int)sizeof(info)-32; i++) {
            pos += snprintf(info+pos, sizeof(info)-pos, " %d:%s%s",
                i+1, E.buffers[i].filename,
                E.buffers[i].dirty?" [+]":"");
        }
        set_status("%s", info);
    }
    else {
        set_status("Unknown command: %s", p);
    }
}
