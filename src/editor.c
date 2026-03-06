#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

EditorState E;

/* ── Signal handler for resize ───────────────────────────── */

static void handle_sigwinch(int sig) {
    (void)sig;
    term_get_size(&E.term_rows, &E.term_cols);
    /* Resize window */
    E.windows[0].w = E.term_cols;
    E.windows[0].h = E.term_rows;
    render_screen();
}

/* ── Default config ──────────────────────────────────────── */

static void set_defaults(void) {
    E.show_line_numbers      = true;
    E.show_relative_numbers  = false;
    E.auto_indent            = true;
    E.hlsearch               = true;
    E.syntax_enable          = true;
    E.scroll_off             = 5;
    E.wrap_lines             = false;
    strcpy(E.colorscheme, "onedark");
}

/* ── Load config file (~/.zedrc) ──────────────────────────── */

static void load_config(void) {
    char path[512];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.zedrc", home);

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[MAX_CMD_LEN];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]='\0';
        /* Skip comments */
        if (line[0] == '"' || line[0] == '#' || line[0] == '\0') continue;
        cmd_execute(line);
    }
    fclose(fp);
}

/* ── Init ────────────────────────────────────────────────── */

void editor_init(void) {
    memset(&E, 0, sizeof(E));
    set_defaults();

    term_get_size(&E.term_rows, &E.term_cols);
    term_enable_raw();

    signal(SIGWINCH, handle_sigwinch);

    /* Init render buffer */
    E.render_buf_size = 65536;
    E.render_buf = malloc(E.render_buf_size);
    if (!E.render_buf) die("malloc render_buf");

    /* Create default window */
    E.windows[0].x = 0; E.windows[0].y = 0;
    E.windows[0].w = E.term_cols;
    E.windows[0].h = E.term_rows;
    E.windows[0].cx = 0; E.windows[0].cy = 0;
    E.windows[0].rx = 0;
    E.windows[0].rowoff = 0; E.windows[0].coloff = 0;
    E.windows[0].buf_id = 0;
    E.num_windows = 1;
    E.cur_win = 0;

    E.mode = MODE_NORMAL;
    E.hlsearch = true;

    load_config();
    autocmd_fire(EVENT_VIMENTER, "");
}

/* ── Cleanup ─────────────────────────────────────────────── */

void editor_cleanup(void) {
    /* Shutdown plugins */
    for (int i = 0; i < E.num_plugins; i++)
        if (E.plugins[i] && E.plugins[i]->shutdown)
            E.plugins[i]->shutdown();

    /* Free buffers */
    for (int i = 0; i < E.num_buffers; i++)
        buf_close(i);

    free(E.render_buf);

    /* Free registers */
    for (int i = 0; i < MAX_REGISTERS; i++)
        free(E.registers[i].text);

    autocmd_fire(EVENT_VIMLEAVE, "");
    term_disable_raw();
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    write(STDOUT_FILENO, "\x1b[?25h", 6); /* show cursor */
}

/* ── Main loop ───────────────────────────────────────────── */

void editor_run(void) {
    render_screen();

    while (1) {
        int key = term_read_key();
        if (key != KEY_NULL) {
            input_process_key(key);
            win_set_cursor(win_current()->cx, win_current()->cy);
        }
        render_screen();
    }
}
