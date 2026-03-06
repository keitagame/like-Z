#include "editor.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Keyword tables ───────────────────────────────────────── */

static const char *c_keywords[] = {
    "auto","break","case","continue","default","do","else","enum",
    "extern","for","goto","if","inline","register","restrict","return",
    "sizeof","static","struct","switch","typedef","union","volatile","while",
    "_Alignas","_Alignof","_Atomic","_Bool","_Complex","_Generic",
    "_Imaginary","_Noreturn","_Static_assert","_Thread_local",NULL
};
static const char *c_types[] = {
    "char","const","double","float","int","long","short","signed",
    "unsigned","void","bool","int8_t","int16_t","int32_t","int64_t",
    "uint8_t","uint16_t","uint32_t","uint64_t","size_t","ssize_t",
    "ptrdiff_t","uintptr_t","intptr_t","NULL","true","false",NULL
};
static const char *py_keywords[] = {
    "and","as","assert","async","await","break","class","continue",
    "def","del","elif","else","except","finally","for","from","global",
    "if","import","in","is","lambda","nonlocal","not","or","pass",
    "raise","return","try","while","with","yield",NULL
};
static const char *py_types[] = {
    "int","float","str","bool","list","dict","tuple","set","None",
    "True","False","bytes","bytearray","complex","type","object",NULL
};
static const char *rs_keywords[] = {
    "as","async","await","break","const","continue","crate","dyn","else",
    "enum","extern","fn","for","if","impl","in","let","loop","match",
    "mod","move","mut","pub","ref","return","self","Self","static","struct",
    "super","trait","type","unsafe","use","where","while",NULL
};
static const char *rs_types[] = {
    "i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128",
    "usize","f32","f64","bool","char","str","String","Vec","Option",
    "Result","Box","Rc","Arc","None","Some","Ok","Err","true","false",NULL
};
static const char *go_keywords[] = {
    "break","case","chan","const","continue","default","defer","else",
    "fallthrough","for","func","go","goto","if","import","interface",
    "map","package","range","return","select","struct","switch","type","var",NULL
};
static const char *go_types[] = {
    "bool","byte","complex64","complex128","error","float32","float64",
    "int","int8","int16","int32","int64","rune","string","uint","uint8",
    "uint16","uint32","uint64","uintptr","nil","true","false","iota",NULL
};
static const char *js_keywords[] = {
    "break","case","catch","class","const","continue","debugger","default",
    "delete","do","else","export","extends","finally","for","function","if",
    "import","in","instanceof","let","new","of","return","static","super",
    "switch","this","throw","try","typeof","var","void","while","with",
    "yield","async","await",NULL
};
static const char *js_types[] = {
    "Array","Boolean","Date","Error","Function","Map","Math","Number",
    "Object","Promise","RegExp","Set","String","Symbol","WeakMap","WeakSet",
    "null","undefined","true","false","NaN","Infinity",NULL
};
static const char *lua_keywords[] = {
    "and","break","do","else","elseif","end","false","for","function",
    "goto","if","in","local","nil","not","or","repeat","return","then",
    "true","until","while",NULL
};
static const char *lua_types[] = {
    "string","number","boolean","table","function","userdata","thread","nil",
    "print","pairs","ipairs","next","pcall","xpcall","error","assert",
    "type","tostring","tonumber","require","dofile","load","loadfile",
    "rawget","rawset","rawequal","rawlen","select","setmetatable",
    "getmetatable","coroutine","io","os","math","string","table",NULL
};
static const char *sh_keywords[] = {
    "if","then","else","elif","fi","for","in","do","done","while","until",
    "case","esac","function","return","exit","break","continue","local",
    "export","readonly","declare","typeset","set","unset","shift",NULL
};
static const char *sh_types[] = {
    "true","false","echo","printf","read","source","alias","cd","ls","pwd",
    "mkdir","rm","cp","mv","cat","grep","sed","awk","find","xargs",NULL
};

/* ── Syntax table ─────────────────────────────────────────── */

static Syntax SYNTAXES[] = {
    { "c",    c_keywords,  c_types,   "//", "/*", "*/", SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES|SYNTAX_HL_PREPROC },
    { "cpp",  c_keywords,  c_types,   "//", "/*", "*/", SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES|SYNTAX_HL_PREPROC },
    { "rust", rs_keywords, rs_types,  "//", "/*", "*/", SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "go",   go_keywords, go_types,  "//", "/*", "*/", SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "python",py_keywords,py_types,  "#",  NULL, NULL,  SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "javascript",js_keywords,js_types,"//","/*","*/",  SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "typescript",js_keywords,js_types,"//","/*","*/",  SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "lua",  lua_keywords,lua_types,  "--","--[[","]]",SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS|SYNTAX_HL_OPERATORS|SYNTAX_HL_TYPES },
    { "shell",sh_keywords, sh_types,  "#",  NULL, NULL,  SYNTAX_HL_NUMBERS|SYNTAX_HL_STRINGS|SYNTAX_HL_COMMENTS },
};

#define N_SYNTAXES (int)(sizeof(SYNTAXES)/sizeof(SYNTAXES[0]))

void buf_update_syntax(int id) {
    EditorBuffer *b = &E.buffers[id];
    b->syntax = NULL;
    for (int i = 0; i < N_SYNTAXES; i++) {
        if (strcmp(b->filetype, SYNTAXES[i].filetype) == 0) {
            b->syntax = &SYNTAXES[i];
            return;
        }
    }
}

/* ── Per-row highlight ────────────────────────────────────── */

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

void hl_update_row(EditorBuffer *b, int row_idx) {
    if (row_idx < 0 || row_idx >= b->numrows) return;
    EditorRow *row = &b->rows[row_idx];

    free(row->hl);
    row->hl = malloc(row->rsize + 1);
    if (!row->hl) return;
    memset(row->hl, HL_NORMAL, row->rsize + 1);

    if (!b->syntax || !E.syntax_enable) return;
    Syntax *s = b->syntax;

    /* Inherit open comment state from previous row */
    bool in_ml_comment = (row_idx > 0) && b->rows[row_idx-1].hl_open_comment;
    row->hl_open_comment = false;

    int i = 0;
    char prev_sep = 1; /* start of line counts as separator */
    bool in_string = false;
    char string_char = 0;

    while (i < row->rsize) {
        char c = row->render[i];
        uint8_t prev_hl = (i > 0) ? row->hl[i-1] : HL_NORMAL;
        (void)prev_hl;

        /* Multi-line comment continuation */
        if (in_ml_comment) {
            row->hl[i] = HL_ML_COMMENT;
            if (s->multiline_comment_end &&
                starts_with(&row->render[i], s->multiline_comment_end)) {
                int ml = (int)strlen(s->multiline_comment_end);
                for (int j = 0; j < ml; j++) row->hl[i+j] = HL_ML_COMMENT;
                i += ml;
                in_ml_comment = false;
                prev_sep = 1;
            } else {
                i++;
            }
            continue;
        }

        /* String */
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i+1 < row->rsize) {
                row->hl[i+1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == string_char) in_string = false;
            i++; prev_sep = 0;
            continue;
        }

        if ((s->flags & SYNTAX_HL_STRINGS) && (c == '"' || c == '\'')) {
            in_string = true; string_char = c;
            row->hl[i] = HL_STRING;
            i++; prev_sep = 0;
            continue;
        }

        /* Preprocessor */
        if ((s->flags & SYNTAX_HL_PREPROC) && c == '#' && prev_sep) {
            for (int j = i; j < row->rsize; j++) row->hl[j] = HL_PREPROCESSOR;
            break;
        }

        /* Single-line comment */
        if (s->singleline_comment && starts_with(&row->render[i], s->singleline_comment)) {
            for (int j = i; j < row->rsize; j++) row->hl[j] = HL_COMMENT;
            break;
        }

        /* Multi-line comment start */
        if (s->multiline_comment_start &&
            starts_with(&row->render[i], s->multiline_comment_start)) {
            int ml = (int)strlen(s->multiline_comment_start);
            for (int j = i; j < i+ml && j < row->rsize; j++) row->hl[j] = HL_ML_COMMENT;
            i += ml;
            in_ml_comment = true;
            continue;
        }

        /* Numbers */
        if ((s->flags & SYNTAX_HL_NUMBERS) && (isdigit(c) && prev_sep || (c == '.' && prev_sep))) {
            bool is_hex = (c == '0' && i+1 < row->rsize &&
                          (row->render[i+1] == 'x' || row->render[i+1] == 'X'));
            while (i < row->rsize) {
                char nc = row->render[i];
                if (isdigit(nc) || nc == '.' || nc == '_' ||
                    (is_hex && isxdigit(nc)) ||
                    (is_hex && (nc=='x'||nc=='X')))
                    row->hl[i++] = HL_NUMBER;
                else break;
            }
            prev_sep = 0;
            continue;
        }

        /* Operators */
        if ((s->flags & SYNTAX_HL_OPERATORS) &&
            strchr("+-*/%=<>!&|^~?:", c)) {
            row->hl[i] = HL_OPERATOR;
            i++; prev_sep = 1;
            continue;
        }

        /* Keywords and types */
        if (prev_sep) {
            bool found = false;
            /* Types first (longer match priority same, check types first) */
            if (s->types) {
                for (int t = 0; s->types[t]; t++) {
                    int klen = (int)strlen(s->types[t]);
                    if (i + klen <= row->rsize &&
                        strncmp(&row->render[i], s->types[t], klen) == 0 &&
                        (i+klen == row->rsize || is_separator(row->render[i+klen]))) {
                        for (int j = i; j < i+klen; j++) row->hl[j] = HL_TYPE;
                        i += klen; found = true; break;
                    }
                }
            }
            if (!found && s->keywords) {
                for (int t = 0; s->keywords[t]; t++) {
                    int klen = (int)strlen(s->keywords[t]);
                    if (i + klen <= row->rsize &&
                        strncmp(&row->render[i], s->keywords[t], klen) == 0 &&
                        (i+klen == row->rsize || is_separator(row->render[i+klen]))) {
                        for (int j = i; j < i+klen; j++) row->hl[j] = HL_KEYWORD;
                        i += klen; found = true; break;
                    }
                }
            }
            /* Identifiers followed by '(' → function */
            if (!found && (isalpha(c) || c == '_')) {
                int j = i;
                while (j < row->rsize && (isalnum(row->render[j]) || row->render[j]=='_')) j++;
                if (j < row->rsize && row->render[j] == '(') {
                    for (int k = i; k < j; k++) row->hl[k] = HL_FUNCTION;
                    i = j; found = true;
                }
            }
            if (found) { prev_sep = 0; continue; }
        }

        prev_sep = is_separator(c);
        i++;
    }

    row->hl_open_comment = in_ml_comment;
    /* Propagate if state changed */
    if (row_idx + 1 < b->numrows &&
        b->rows[row_idx+1].hl_open_comment != in_ml_comment)
        hl_update_row(b, row_idx + 1);
}

void hl_update_all(EditorBuffer *b) {
    buf_update_syntax(b->id);
    for (int i = 0; i < b->numrows; i++)
        hl_update_row(b, i);
}

/* ── Color scheme ─────────────────────────────────────────── */

HlColor hl_get_color(HlType hl) {
    HlColor c = {-1,-1,-1,-1,-1,-1,-1,-1,false,false,false,false,false};
#define RGB(r,g,b) .true_color=true, .fg_r=r, .fg_g=g, .fg_b=b
    switch (hl) {
        case HL_NORMAL:        c.fg = -1; break;
        case HL_NUMBER:        c.true_color=true; c.fg_r=215; c.fg_g=135; c.fg_b=95;  break; /* orange */
        case HL_KEYWORD:       c.true_color=true; c.fg_r=197; c.fg_g=119; c.fg_b=221; break; /* purple */
        case HL_TYPE:          c.true_color=true; c.fg_r= 86; c.fg_g=182; c.fg_b=194; break; /* cyan */
        case HL_STRING:        c.true_color=true; c.fg_r=152; c.fg_g=195; c.fg_b=121; break; /* green */
        case HL_STRING2:       c.true_color=true; c.fg_r=224; c.fg_g=108; c.fg_b=117; break; /* red */
        case HL_COMMENT:
        case HL_ML_COMMENT:    c.true_color=true; c.fg_r= 92; c.fg_g= 99; c.fg_b=112; c.italic=true; break;
        case HL_OPERATOR:      c.true_color=true; c.fg_r=209; c.fg_g=154; c.fg_b=102; break; /* amber */
        case HL_PREPROCESSOR:  c.true_color=true; c.fg_r=224; c.fg_g=108; c.fg_b=117; break; /* red */
        case HL_FUNCTION:      c.true_color=true; c.fg_r= 97; c.fg_g=175; c.fg_b=239; break; /* blue */
        case HL_IDENTIFIER:    c.fg = -1; break;
        case HL_MATCH:         c.true_color=true; c.bg_r= 61; c.bg_g= 72; c.bg_b= 50; c.fg_r=255; c.fg_g=220; c.fg_b=100; c.fg=-1; c.bg=-1; c.bold=true; break;
        case HL_SELECTION:     c.true_color=true; c.bg_r= 62; c.bg_g= 68; c.bg_b= 81; break;
        case HL_CURSORLINE:    c.true_color=true; c.bg_r= 40; c.bg_g= 44; c.bg_b= 52; break;
        case HL_LINENR:        c.true_color=true; c.fg_r= 92; c.fg_g= 99; c.fg_b=112; break;
        case HL_LINENR_CUR:    c.true_color=true; c.fg_r=171; c.fg_g=178; c.fg_b=191; c.bold=true; break;
        case HL_STATUSLINE:    c.true_color=true; c.bg_r= 40; c.bg_g= 44; c.bg_b= 52; c.fg_r=171; c.fg_g=178; c.fg_b=191; break;
        case HL_STATUSLINE_NC: c.true_color=true; c.bg_r= 33; c.bg_g= 37; c.bg_b= 43; c.fg_r= 92; c.fg_g= 99; c.fg_b=112; break;
        case HL_TABLINE:       c.true_color=true; c.bg_r= 33; c.bg_g= 37; c.bg_b= 43; c.fg_r= 92; c.fg_g= 99; c.fg_b=112; break;
        case HL_TABLINE_SEL:   c.true_color=true; c.bg_r= 40; c.bg_g= 44; c.bg_b= 52; c.fg_r=171; c.fg_g=178; c.fg_b=191; c.bold=true; break;
        case HL_TITLE:         c.true_color=true; c.fg_r= 97; c.fg_g=175; c.fg_b=239; c.bold=true; break;
        case HL_DIAGNOSTIC_ERROR: c.true_color=true; c.fg_r=224; c.fg_g=108; c.fg_b=117; break;
        case HL_DIAGNOSTIC_WARN:  c.true_color=true; c.fg_r=229; c.fg_g=192; c.fg_b=123; break;
        case HL_DIAGNOSTIC_INFO:  c.true_color=true; c.fg_r= 97; c.fg_g=175; c.fg_b=239; break;
        default: break;
    }
    return c;
}

const char *hl_color_to_ansi(HlColor *c, bool is_fg) {
    static char buf[64];
    if (c->true_color) {
        if (is_fg && (c->fg_r||c->fg_g||c->fg_b))
            snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", c->fg_r, c->fg_g, c->fg_b);
        else if (!is_fg && (c->bg_r||c->bg_g||c->bg_b))
            snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", c->bg_r, c->bg_g, c->bg_b);
        else return "";
    } else {
        int code = is_fg ? (c->fg + 30) : (c->bg + 40);
        if ((is_fg && c->fg < 0) || (!is_fg && c->bg < 0)) return "";
        snprintf(buf, sizeof(buf), "\x1b[%dm", code);
    }
    return buf;
}
