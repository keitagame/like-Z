#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

/* ============================================================
 *  ZED - Extensible Terminal Editor
 *  A Neovim-inspired editor with Lua plugin support
 * ============================================================ */

#define ZED_VERSION "0.1.0"
#define ZED_NAME    "zed"

/* ── Configuration ─────────────────────────────────────────── */
#define TAB_STOP          4
#define QUIT_TIMES        2
#define MAX_FILENAME      256
#define MAX_STATUS_MSG    256
#define MAX_UNDO_HISTORY  500
#define MAX_REGISTERS     36   /* a-z + 0-9 */
#define MAX_MARKS         52   /* a-z + A-Z */
#define MAX_MACRO_LEN     4096
#define MAX_SEARCH_LEN    256
#define MAX_CMD_LEN       512
#define MAX_PLUGINS       64
#define MAX_KEYMAPS       512
#define MAX_AUTOCMDS      256
#define MAX_BUFFERS       64
#define MAX_WINDOWS       16
#define HIGHLIGHT_MAX     2048

/* ── Key codes ─────────────────────────────────────────────── */
#define CTRL_KEY(k)    ((k) & 0x1f)
#define ESC            27
#define BACKSPACE      127

typedef enum {
    KEY_NULL = 0,
    KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN,
    KEY_DEL, KEY_INSERT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_F11, KEY_F12,
} EditorKey;

/* ── Editor modes ──────────────────────────────────────────── */
typedef enum {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_VISUAL,
    MODE_VISUAL_LINE,
    MODE_VISUAL_BLOCK,
    MODE_COMMAND,
    MODE_SEARCH,
    MODE_REPLACE,
    MODE_OPERATOR_PENDING,
} EditorMode;

/* ── Operators ─────────────────────────────────────────────── */
typedef enum {
    OP_NONE = 0,
    OP_DELETE,
    OP_YANK,
    OP_CHANGE,
    OP_INDENT,
    OP_UNINDENT,
    OP_UPPERCASE,
    OP_LOWERCASE,
    OP_FORMAT,
    OP_COMMENT,
} Operator;

/* ── Text motions ──────────────────────────────────────────── */
typedef enum {
    MOT_CHAR = 0,
    MOT_LINE,
    MOT_BLOCK,
} MotionType;

/* ── Highlight types ───────────────────────────────────────── */
typedef enum {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_KEYWORD,
    HL_TYPE,
    HL_STRING,
    HL_STRING2,
    HL_COMMENT,
    HL_ML_COMMENT,
    HL_OPERATOR,
    HL_PREPROCESSOR,
    HL_IDENTIFIER,
    HL_FUNCTION,
    HL_MATCH,
    HL_SELECTION,
    HL_CURSORLINE,
    HL_LINENR,
    HL_LINENR_CUR,
    HL_STATUSLINE,
    HL_STATUSLINE_NC,
    HL_TABLINE,
    HL_TABLINE_SEL,
    HL_TITLE,
    HL_DIAGNOSTIC_ERROR,
    HL_DIAGNOSTIC_WARN,
    HL_DIAGNOSTIC_INFO,
} HlType;

/* ── ANSI color helper ─────────────────────────────────────── */
typedef struct {
    int fg;     /* -1 = default */
    int fg_r, fg_g, fg_b;   /* true-color */
    int bg;
    int bg_r, bg_g, bg_b;
    bool bold, italic, underline, reverse;
    bool true_color;
} HlColor;

/* ── Row (line of text) ────────────────────────────────────── */
typedef struct {
    int      idx;           /* line index in file */
    char    *chars;         /* raw characters */
    int      size;
    char    *render;        /* rendered (tabs expanded) */
    int      rsize;
    uint8_t *hl;            /* highlight types per render char */
    bool     hl_open_comment;
    int      dirty;
} EditorRow;

/* ── Undo/Redo ─────────────────────────────────────────────── */
typedef enum {
    UNDO_INSERT_CHAR,
    UNDO_DELETE_CHAR,
    UNDO_INSERT_ROW,
    UNDO_DELETE_ROW,
    UNDO_CHANGE_ROW,
    UNDO_COMPOSITE,
} UndoType;

typedef struct UndoEntry {
    UndoType type;
    int cx, cy;             /* cursor before operation */
    int cx2, cy2;           /* cursor after operation */
    char *data;             /* saved text */
    int  data_len;
    struct UndoEntry *next; /* for composite */
} UndoEntry;

/* ── Register ──────────────────────────────────────────────── */
typedef struct {
    char  *text;
    int    len;
    bool   is_line;   /* linewise yank */
} Register;

/* ── Mark ──────────────────────────────────────────────────── */
typedef struct {
    int cx, cy;
    bool set;
} Mark;

/* ── Keymap entry ──────────────────────────────────────────── */
typedef struct {
    EditorMode mode;
    char       lhs[32];
    char       rhs[MAX_CMD_LEN];
    bool       noremap;
    bool       silent;
    void      (*fn)(void);  /* C function binding */
} Keymap;

/* ── Autocmd ───────────────────────────────────────────────── */
typedef enum {
    EVENT_BUFREAD = 0,
    EVENT_BUFWRITE,
    EVENT_BUFENTER,
    EVENT_BUFLEAVE,
    EVENT_INSERTENTER,
    EVENT_INSERTLEAVE,
    EVENT_CURSORHOLD,
    EVENT_VIMENTER,
    EVENT_VIMLEAVE,
} AutocmdEvent;

typedef struct {
    AutocmdEvent  event;
    char          pattern[128];
    char          cmd[MAX_CMD_LEN];
    void         (*fn)(void);
} Autocmd;

/* ── Syntax rule ───────────────────────────────────────────── */
typedef struct {
    const char  *filetype;
    const char **keywords;
    const char **types;
    const char  *singleline_comment;
    const char  *multiline_comment_start;
    const char  *multiline_comment_end;
    int          flags;
} Syntax;

#define SYNTAX_HL_NUMBERS   (1<<0)
#define SYNTAX_HL_STRINGS   (1<<1)
#define SYNTAX_HL_COMMENTS  (1<<2)
#define SYNTAX_HL_OPERATORS (1<<3)
#define SYNTAX_HL_TYPES     (1<<4)
#define SYNTAX_HL_PREPROC   (1<<5)

/* ── Completion item ───────────────────────────────────────── */
typedef struct {
    char word[128];
    char kind[32];     /* function, keyword, variable… */
    char menu[128];    /* extra info */
} CompletionItem;

/* ── Plugin API ────────────────────────────────────────────── */
typedef struct Plugin Plugin;

typedef struct {
    const char  *name;
    const char  *version;
    const char  *description;
    int        (*init)(void);
    void       (*shutdown)(void);
    /* Extension points */
    void       (*on_key)(int key);
    void       (*on_mode_change)(EditorMode old, EditorMode new_mode);
    void       (*on_buf_read)(const char *filename);
    void       (*on_buf_write)(const char *filename);
    void       (*on_insert_char)(char c, int cx, int cy);
    int        (*on_complete)(const char *word, CompletionItem *out, int max);
    void       (*on_render_row)(int row, char *buf, int *len);
    const char*(*on_statusline)(void);
} PluginDef;

/* ── Window (split) ────────────────────────────────────────── */
typedef struct EditorBuffer EditorBuffer;
typedef struct EditorWindow EditorWindow;

struct EditorWindow {
    int      x, y;          /* position in terminal */
    int      w, h;          /* size */
    int      cx, cy;        /* cursor col, row */
    int      rx;            /* render cursor col */
    int      rowoff, coloff;/* scroll offsets */
    int      buf_id;        /* which buffer */
};

/* ── Buffer ────────────────────────────────────────────────── */
struct EditorBuffer {
    int         id;
    char        filename[MAX_FILENAME];
    char        filetype[64];
    EditorRow  *rows;
    int         numrows;
    int         dirty;       /* unsaved changes count */
    bool        readonly;
    Syntax     *syntax;
    /* Per-buffer undo stack */
    UndoEntry  *undo_stack[MAX_UNDO_HISTORY];
    int         undo_top;
    int         undo_pos;
};

/* ── Global editor state ───────────────────────────────────── */
typedef struct {
    /* Terminal */
    struct termios  orig_termios;
    int             term_rows, term_cols;
    bool            raw_mode;

    /* Buffers & Windows */
    EditorBuffer    buffers[MAX_BUFFERS];
    int             num_buffers;
    int             cur_buf;
    EditorWindow    windows[MAX_WINDOWS];
    int             num_windows;
    int             cur_win;

    /* Editor mode */
    EditorMode      mode;
    EditorMode      prev_mode;

    /* Visual selection */
    int             vx, vy;   /* visual anchor */

    /* Operator pending */
    Operator        pending_op;
    int             op_count;
    char            op_register;

    /* Registers & marks */
    Register        registers[MAX_REGISTERS];
    Mark            marks[MAX_MARKS];

    /* Macros */
    char            macro_buf[MAX_MACRO_LEN];
    int             macro_len;
    bool            recording_macro;
    char            macro_reg;
    bool            playing_macro;

    /* Search */
    char            search_query[MAX_SEARCH_LEN];
    bool            search_forward;
    int             search_last_row, search_last_col;
    bool            hlsearch;

    /* Command line */
    char            cmd_buf[MAX_CMD_LEN];
    int             cmd_len;
    char            cmd_history[64][MAX_CMD_LEN];
    int             cmd_history_len;
    int             cmd_history_idx;

    /* Status */
    char            status_msg[MAX_STATUS_MSG];
    time_t          status_time;

    /* Keymaps */
    Keymap          keymaps[MAX_KEYMAPS];
    int             num_keymaps;
    char            key_seq[32];   /* pending key sequence */
    int             key_seq_len;

    /* Autocmds */
    Autocmd         autocmds[MAX_AUTOCMDS];
    int             num_autocmds;

    /* Plugins */
    PluginDef      *plugins[MAX_PLUGINS];
    int             num_plugins;

    /* Completion */
    CompletionItem  comp_items[128];
    int             comp_count;
    int             comp_idx;
    bool            comp_active;

    /* Config */
    bool            show_line_numbers;
    bool            show_relative_numbers;
    bool            auto_indent;
    bool            show_whitespace;
    bool            wrap_lines;
    bool            syntax_enable;
    int             scroll_off;
    char            colorscheme[64];

    /* Diagnostics (LSP-like) */
    struct {
        int  row, col;
        char msg[256];
        int  severity; /* 0=error,1=warn,2=info */
    } diagnostics[256];
    int num_diagnostics;

    /* Render buffer */
    char   *render_buf;
    size_t  render_buf_size;
    size_t  render_buf_len;
} EditorState;

extern EditorState E;

/* ── Public API ────────────────────────────────────────────── */

/* core */
void editor_init(void);
void editor_run(void);
void editor_cleanup(void);

/* terminal */
void term_enable_raw(void);
void term_disable_raw(void);
int  term_read_key(void);
void term_get_size(int *rows, int *cols);
void term_clear(void);
void term_goto(int row, int col);

/* buffer */
int           buf_new(void);
EditorBuffer *buf_current(void);
void          buf_open(int id, const char *filename);
void          buf_save(int id);
void          buf_close(int id);
void          buf_insert_row(int id, int at, const char *s, int len);
void          buf_del_row(int id, int at);
void          buf_row_insert_char(int id, int row, int col, char c);
void          buf_row_del_char(int id, int row, int col);
void          buf_row_append(int id, int row, const char *s, int len);
void          buf_update_syntax(int id);

/* window */
EditorWindow *win_current(void);
void          win_set_cursor(int cx, int cy);
void          win_scroll(void);
int           win_row_to_render(EditorBuffer *b, int row, int cx);

/* input */
int   input_read_key(void);
void  input_process_key(int key);
void  input_process_normal(int key);
void  input_process_insert(int key);
void  input_process_visual(int key);
void  input_process_command(int key);
void  input_process_search(int key);

/* motion */
void motion_move(int key);
void motion_word_forward_exec(bool WORD);
void motion_word_backward_exec(bool WORD);
void motion_word_end_exec(bool WORD);
void motion_goto_line(int n);
void motion_find_char(char c, bool forward, bool till);
void motion_matching_bracket(void);
void motion_inner_word(int *x1, int *x2);
void motion_inner_pair(char open, char close, int *y1, int *x1, int *y2, int *x2);

/* operator */
void op_execute(Operator op, int count, char reg);
void op_delete_range(int y1, int x1, int y2, int x2, MotionType mt);
void op_yank_range(int y1, int x1, int y2, int x2, MotionType mt, char reg);
void op_change_range(int y1, int x1, int y2, int x2, MotionType mt);
void op_indent_range(int y1, int y2, bool indent);
void op_case_range(int y1, int x1, int y2, int x2, bool upper);
void op_comment_range(int y1, int y2);
void op_paste(char reg_char, bool after);

/* undo/redo */
void undo_push(UndoType type, int cx, int cy, const char *data, int len);
void undo_do(void);
void redo_do(void);

/* search */
void search_find(const char *query, bool forward);
void search_next(bool same_dir);
void search_highlight_update(void);

/* command */
void  cmd_execute(const char *cmd);
void  cmd_parse_range(const char *s, int *start, int *end);

/* highlight */
void   hl_update_row(EditorBuffer *b, int row);
void   hl_update_all(EditorBuffer *b);
HlColor hl_get_color(HlType hl);
const char *hl_color_to_ansi(HlColor *c, bool is_fg);

/* render */
void render_screen(void);
void render_rows(void);
void render_statusline(void);
void render_tabline(void);
void render_cmdline(void);
void render_flush(void);
void render_append(const char *s, int len);

/* completion */
void  comp_trigger(void);
void  comp_next(void);
void  comp_prev(void);
void  comp_accept(void);
void  comp_cancel(void);

/* plugin */
int  plugin_register(PluginDef *def);
void plugin_fire_key(int key);
void plugin_fire_mode_change(EditorMode old, EditorMode nm);
void plugin_fire_buf_read(const char *fn);
void plugin_fire_buf_write(const char *fn);

/* keymap */
void keymap_set(EditorMode mode, const char *lhs, const char *rhs, bool noremap, bool silent);
void keymap_set_fn(EditorMode mode, const char *lhs, void(*fn)(void), bool silent);
int  keymap_lookup(EditorMode mode, const char *seq, char *rhs_out, void(**fn_out)(void));

/* autocmd */
void autocmd_add(AutocmdEvent ev, const char *pat, const char *cmd);
void autocmd_fire(AutocmdEvent ev, const char *ctx);

/* utilities */
void  set_status(const char *fmt, ...);
char *str_dup(const char *s);
char *str_ndup(const char *s, int n);
void  die(const char *msg);
bool  is_separator(char c);
bool  is_word_char(char c);

#endif /* EDITOR_H */
