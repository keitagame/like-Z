/*
 * ZED Plugin Example: word-counter
 *
 * Compile and link with zed, or use as shared library:
 *   gcc -shared -fPIC -o wordcount.so wordcount_plugin.c
 *
 * Then load from ~/.zedrc with:
 *   plugin wordcount.so   (future feature)
 */

#include "editor.h"
#include <string.h>
#include <stdio.h>

/* ── Word Counter Plugin ──────────────────────────── */

static int word_count = 0;

static void count_words(void) {
    EditorBuffer *b = buf_current();
    word_count = 0;
    for (int r = 0; r < b->numrows; r++) {
        char *chars = b->rows[r].chars;
        int sz = b->rows[r].size;
        bool in_word = false;
        for (int i = 0; i < sz; i++) {
            if (chars[i] != ' ' && chars[i] != '\t' && chars[i] != '\n') {
                if (!in_word) { word_count++; in_word = true; }
            } else {
                in_word = false;
            }
        }
    }
}

static int wc_init(void) {
    /* Register a command */
    /* cmd_register("wc", count_words_cmd); */
    return 0;
}

static const char *wc_statusline(void) {
    count_words();
    static char buf[32];
    snprintf(buf, sizeof(buf), " W:%d", word_count);
    return buf;
}

static void wc_on_buf_write(const char *fn) {
    (void)fn;
    count_words();
    set_status("Saved. Words: %d", word_count);
}

/* Export this struct from your plugin */
PluginDef wordcount_plugin = {
    .name          = "word-counter",
    .version       = "1.0",
    .description   = "Count words in buffer",
    .init          = wc_init,
    .on_buf_write  = wc_on_buf_write,
    .on_statusline = wc_statusline,
};

/*
 * ── How to write a ZED plugin ────────────────────────
 *
 * 1. Include "editor.h" and implement the PluginDef fields you need.
 *
 * 2. Available hook points:
 *    - init()              Called at startup
 *    - shutdown()          Called at exit
 *    - on_key(key)         Every keypress in any mode
 *    - on_mode_change()    Mode transitions
 *    - on_buf_read(fn)     After opening a file
 *    - on_buf_write(fn)    After saving a file
 *    - on_insert_char(c)   After inserting a character
 *    - on_complete(word)   Return completion candidates
 *    - on_render_row()     Custom row rendering
 *    - on_statusline()     Add text to status bar
 *
 * 3. Use the public editor API:
 *    - buf_current()           Get current buffer
 *    - win_current()           Get current window
 *    - set_status(fmt, ...)    Show message
 *    - keymap_set_fn()         Register keybindings
 *    - autocmd_add()           Register autocmds
 *    - cmd_execute("...")      Run a command
 *    - search_find()           Trigger search
 *
 * 4. Register with:
 *    plugin_register(&my_plugin);
 */
