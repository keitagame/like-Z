#include "editor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

void die(const char *msg) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(msg);
    exit(1);
}

void term_disable_raw(void) {
    if (E.raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
        E.raw_mode = false;
    }
}

void term_enable_raw(void) {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(term_disable_raw);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |=  (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    E.raw_mode = true;
}

void term_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* fallback: move cursor to bottom-right and query position */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B\x1b[6n", 16) != 16)
            die("term_get_size");
        char buf[32]; int i = 0;
        while (i < (int)sizeof(buf)-1) {
            if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
            if (buf[i++] == 'R') break;
        }
        buf[i] = '\0';
        if (buf[0] != '\x1b' || buf[1] != '[') die("term_get_size");
        if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) die("term_get_size");
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

int term_read_key(void) {
    int nread; char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
        return KEY_NULL;
    }

    if (c == '\x1b') {
        char seq[8]; int i = 0;
        /* read up to 6 more bytes with timeout */
        struct termios raw = E.orig_termios;
        struct termios t = raw;
        t.c_cc[VTIME] = 0; t.c_cc[VMIN] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        while (i < 6 && read(STDIN_FILENO, &seq[i], 1) == 1) i++;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);   /* won't restore perfectly but ok */

        if (i == 0) return ESC;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (i >= 3 && seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '2': return KEY_INSERT;
                        case '3': return KEY_DEL;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
                /* F keys */
                if (i >= 3 && seq[2] == '~') {
                    if (seq[1]=='1' && i>=4) {
                        switch(seq[3]) {
                            case '1': return KEY_F1;
                            case '2': return KEY_F2;
                            case '3': return KEY_F3;
                            case '4': return KEY_F4;
                        }
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                    case 'P': return KEY_F1;
                    case 'Q': return KEY_F2;
                    case 'R': return KEY_F3;
                    case 'S': return KEY_F4;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case 'P': return KEY_F1;
                case 'Q': return KEY_F2;
                case 'R': return KEY_F3;
                case 'S': return KEY_F4;
            }
        }
        return ESC;
    }
    return (unsigned char)c;
}

void term_clear(void) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void term_goto(int row, int col) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row+1, col+1);
    write(STDOUT_FILENO, buf, len);
}
