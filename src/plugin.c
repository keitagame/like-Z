#include "editor.h"
#include <string.h>
#include <stdlib.h>

/* ── Keymap ───────────────────────────────────────────────── */

void keymap_set(EditorMode mode, const char *lhs, const char *rhs, bool noremap, bool silent) {
    if (E.num_keymaps >= MAX_KEYMAPS) return;
    /* Check for existing */
    for (int i = 0; i < E.num_keymaps; i++) {
        if (E.keymaps[i].mode == mode && strcmp(E.keymaps[i].lhs, lhs) == 0) {
            strncpy(E.keymaps[i].rhs, rhs, MAX_CMD_LEN-1);
            E.keymaps[i].noremap = noremap;
            E.keymaps[i].silent  = silent;
            E.keymaps[i].fn      = NULL;
            return;
        }
    }
    Keymap *km = &E.keymaps[E.num_keymaps++];
    km->mode    = mode;
    strncpy(km->lhs, lhs, 31);
    strncpy(km->rhs, rhs, MAX_CMD_LEN-1);
    km->noremap = noremap;
    km->silent  = silent;
    km->fn      = NULL;
}

void keymap_set_fn(EditorMode mode, const char *lhs, void(*fn)(void), bool silent) {
    if (E.num_keymaps >= MAX_KEYMAPS) return;
    for (int i = 0; i < E.num_keymaps; i++) {
        if (E.keymaps[i].mode == mode && strcmp(E.keymaps[i].lhs, lhs) == 0) {
            E.keymaps[i].fn = fn;
            E.keymaps[i].silent = silent;
            return;
        }
    }
    Keymap *km = &E.keymaps[E.num_keymaps++];
    km->mode   = mode;
    strncpy(km->lhs, lhs, 31);
    km->rhs[0] = '\0';
    km->fn     = fn;
    km->silent = silent;
}

/* Returns 0 = not found, 1 = found rhs, 2 = found fn */
int keymap_lookup(EditorMode mode, const char *seq, char *rhs_out, void(**fn_out)(void)) {
    for (int i = 0; i < E.num_keymaps; i++) {
        if (E.keymaps[i].mode == mode && strcmp(E.keymaps[i].lhs, seq) == 0) {
            if (E.keymaps[i].fn) { *fn_out = E.keymaps[i].fn; return 2; }
            if (rhs_out) strncpy(rhs_out, E.keymaps[i].rhs, MAX_CMD_LEN-1);
            return 1;
        }
    }
    return 0;
}

/* ── Autocmd ──────────────────────────────────────────────── */

void autocmd_add(AutocmdEvent ev, const char *pat, const char *cmd) {
    if (E.num_autocmds >= MAX_AUTOCMDS) return;
    Autocmd *ac = &E.autocmds[E.num_autocmds++];
    ac->event = ev;
    strncpy(ac->pattern, pat, 127);
    strncpy(ac->cmd, cmd, MAX_CMD_LEN-1);
    ac->fn = NULL;
}

static bool pattern_match(const char *pat, const char *ctx) {
    if (strcmp(pat, "*") == 0) return true;
    /* Simple glob: *.c etc */
    if (pat[0] == '*' && pat[1] == '.') {
        const char *ext = strrchr(ctx, '.');
        if (ext && strcmp(ext+1, pat+2) == 0) return true;
    }
    return strstr(ctx, pat) != NULL;
}

void autocmd_fire(AutocmdEvent ev, const char *ctx) {
    for (int i = 0; i < E.num_autocmds; i++) {
        if (E.autocmds[i].event == ev && pattern_match(E.autocmds[i].pattern, ctx ? ctx : "")) {
            if (E.autocmds[i].fn) E.autocmds[i].fn();
            else if (E.autocmds[i].cmd[0]) cmd_execute(E.autocmds[i].cmd);
        }
    }
}

/* ── Plugin system ────────────────────────────────────────── */

int plugin_register(PluginDef *def) {
    if (E.num_plugins >= MAX_PLUGINS) return -1;
    E.plugins[E.num_plugins++] = def;
    if (def->init) return def->init();
    return 0;
}

void plugin_fire_key(int key) {
    for (int i = 0; i < E.num_plugins; i++)
        if (E.plugins[i] && E.plugins[i]->on_key)
            E.plugins[i]->on_key(key);
}

void plugin_fire_mode_change(EditorMode old, EditorMode nm) {
    for (int i = 0; i < E.num_plugins; i++)
        if (E.plugins[i] && E.plugins[i]->on_mode_change)
            E.plugins[i]->on_mode_change(old, nm);
}

void plugin_fire_buf_read(const char *fn) {
    for (int i = 0; i < E.num_plugins; i++)
        if (E.plugins[i] && E.plugins[i]->on_buf_read)
            E.plugins[i]->on_buf_read(fn);
}

void plugin_fire_buf_write(const char *fn) {
    for (int i = 0; i < E.num_plugins; i++)
        if (E.plugins[i] && E.plugins[i]->on_buf_write)
            E.plugins[i]->on_buf_write(fn);
}

/* ── Completion ───────────────────────────────────────────── */

static void comp_collect_buffer_words(void) {
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    EditorRow *cur_row = &b->rows[w->cy];

    /* Find current word prefix */
    int end = w->cx;
    int start = end;
    while (start > 0 && is_word_char(cur_row->chars[start-1])) start--;
    if (start == end) { E.comp_count = 0; return; }

    char prefix[128] = {0};
    int plen = end - start;
    if (plen >= 127) plen = 127;
    strncpy(prefix, &cur_row->chars[start], plen);

    E.comp_count = 0;

    /* Plugin completions first */
    for (int i = 0; i < E.num_plugins; i++) {
        if (E.plugins[i] && E.plugins[i]->on_complete && E.comp_count < 128) {
            int got = E.plugins[i]->on_complete(prefix,
                &E.comp_items[E.comp_count], 128 - E.comp_count);
            E.comp_count += got;
        }
    }

    /* Collect unique words from all buffers */
    for (int bi = 0; bi < E.num_buffers && E.comp_count < 128; bi++) {
        EditorBuffer *bb = &E.buffers[bi];
        for (int r = 0; r < bb->numrows && E.comp_count < 128; r++) {
            char *chars = bb->rows[r].chars;
            int sz = bb->rows[r].size;
            int i = 0;
            while (i < sz) {
                if (is_word_char(chars[i])) {
                    int ws = i;
                    while (i < sz && is_word_char(chars[i])) i++;
                    int wlen = i - ws;
                    if (wlen > plen && strncmp(&chars[ws], prefix, plen) == 0) {
                        /* Check not duplicate */
                        char word[128] = {0};
                        strncpy(word, &chars[ws], wlen < 127 ? wlen : 127);
                        bool dup = false;
                        for (int ci = 0; ci < E.comp_count; ci++)
                            if (strcmp(E.comp_items[ci].word, word) == 0) { dup = true; break; }
                        if (!dup) {
                            strncpy(E.comp_items[E.comp_count].word, word, 127);
                            strcpy(E.comp_items[E.comp_count].kind, "word");
                            E.comp_count++;
                        }
                    }
                } else i++;
            }
        }
    }
}

void comp_trigger(void) {
    comp_collect_buffer_words();
    if (E.comp_count > 0) {
        E.comp_active = true;
        E.comp_idx = -1;
    }
}

void comp_next(void) {
    if (!E.comp_active) { comp_trigger(); return; }
    E.comp_idx = (E.comp_idx + 1) % E.comp_count;
}

void comp_prev(void) {
    if (!E.comp_active) return;
    E.comp_idx = (E.comp_idx - 1 + E.comp_count) % E.comp_count;
}

void comp_accept(void) {
    if (!E.comp_active || E.comp_idx < 0) { E.comp_active=false; return; }
    EditorBuffer *b = buf_current();
    EditorWindow *w = win_current();
    EditorRow *row = &b->rows[w->cy];

    /* Find prefix start */
    int end = w->cx;
    int start = end;
    while (start > 0 && is_word_char(row->chars[start-1])) start--;

    /* Delete prefix */
    for (int i = start; i < end; i++) buf_row_del_char(b->id, w->cy, start);

    /* Insert completion */
    const char *word = E.comp_items[E.comp_idx].word;
    int wlen = (int)strlen(word);
    for (int i = 0; i < wlen; i++) buf_row_insert_char(b->id, w->cy, start+i, word[i]);
    w->cx = start + wlen;
    E.comp_active = false;
}

void comp_cancel(void) {
    E.comp_active = false;
    E.comp_idx    = -1;
}
