#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Built-in demo plugins ───────────────────────────────── */

/* File explorer plugin */
static int fe_init(void) { return 0; }
static const char *fe_statusline(void) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[38;2;97;175;239m[%s]\x1b[0m\x1b[48;2;40;44;52m\x1b[38;2;171;178;191m",
             buf_current()->filetype);
    return buf;
}

static PluginDef fe_plugin = {
    .name        = "filetype-indicator",
    .version     = "1.0",
    .description = "Shows filetype in status line",
    .init        = fe_init,
    .on_statusline = fe_statusline,
};

/* Auto-pairs plugin */
static void ap_on_key(int key) { (void)key; }

static PluginDef ap_plugin = {
    .name        = "auto-pairs",
    .version     = "1.0",
    .description = "Auto-close brackets",
    .on_key      = ap_on_key,
};

/* ── Main ─────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS] [file...]\n"
        "\n"
        "  A Neovim-inspired terminal editor\n"
        "\n"
        "Options:\n"
        "  -v, --version   Print version and exit\n"
        "  -h, --help      Print this help\n"
        "  +N              Open file at line N\n"
        "\n"
        "Keybindings (Normal mode):\n"
        "  h/j/k/l         Move cursor\n"
        "  w/b/e/W/B/E     Word motions\n"
        "  0/^/$           Line start/first-nonblank/end\n"
        "  gg/G/<n>G       File start/end/line N\n"
        "  i/a/o/O/I/A     Enter insert mode\n"
        "  v/V/<C-v>       Visual / Visual-line / Visual-block\n"
        "  d/y/c + motion  Delete/Yank/Change\n"
        "  dd/yy/cc        Delete/Yank/Change line\n"
        "  p/P             Paste after/before\n"
        "  u/<C-r>         Undo/Redo\n"
        "  //?             Search forward/backward\n"
        "  n/N             Next/prev search match\n"
        "  */#             Search word under cursor\n"
        "  f/F/t/T<char>   Find char on line\n"
        "  %%              Jump to matching bracket\n"
        "  >>/<<           Indent/Unindent line\n"
        "  :               Command mode\n"
        "  :w/:q/:wq       Write/Quit/Write+Quit\n"
        "  :e <file>       Open file\n"
        "  :set nu/nonu    Toggle line numbers\n"
        "  :s/pat/rep/g    Substitute\n"
        "  m<a-z>          Set mark\n"
        "  '<a-z>          Jump to mark\n"
        "  q<reg>          Record macro\n"
        "  @<reg>          Replay macro\n"
        "  <C-n>/<C-p>     Next/prev completion\n"
        "  <C-s>           Save file\n"
        "\n"
        "Config:  ~/.zedrc  (same syntax as commands)\n"
        "Example: set nu\n"
        "         set rnu\n"
        "         nmap <leader>w :w\n",
        prog);
}

int main(int argc, char *argv[]) {
    editor_init();

    /* Register built-in plugins */
    plugin_register(&fe_plugin);
    plugin_register(&ap_plugin);

    /* Parse arguments */
    int start_line = 0;
    bool opened_file = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            term_disable_raw();
            printf("zed %s\n", ZED_VERSION);
            exit(0);
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            term_disable_raw();
            print_usage(argv[0]);
            exit(0);
        }
        if (argv[i][0] == '+') {
            start_line = atoi(argv[i]+1);
            continue;
        }
        /* Open file */
        int id = buf_new();
        buf_open(id, argv[i]);
        if (!opened_file) {
            E.cur_buf = id;
            win_current()->buf_id = id;
            opened_file = true;
        }
    }

    if (!opened_file) {
        int id = buf_new();
        E.cur_buf = id;
        win_current()->buf_id = id;
        buf_insert_row(id, 0, "", 0);
    }

    if (start_line > 0) motion_goto_line(start_line);

    /* Default keymaps */
    keymap_set(MODE_NORMAL, "ZZ", "wq", true, false);
    keymap_set(MODE_NORMAL, "ZQ", "q!", true, false);

    set_status("ZED %s | :help for commands | Press : to enter command mode", ZED_VERSION);

    editor_run();
    editor_cleanup();
    return 0;
}
