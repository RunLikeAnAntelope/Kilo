/* Kilo minus -- A very simple editor based on the kilo test editor made by
 * Salvatore Sanfilippo. Based on instruction from
 * https://viewsourcecode.org/snaptoken/kilo/
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Information on the vt100 terminal can be found at
 * https://vt100.net/docs/vt100-ug/chapter3.html#ED */

/*+++ includes +++*/
#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*+++ defines +++*/
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

/*+++ data +++*/
struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*+++ terminal +++*/
void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);  // clear screen
    write(STDOUT_FILENO, "\x1b[H", 3);   // reposition cursor
    perror(s);
    exit(1);
}

// restore original mode when we exit
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    /***************
     * Enables "raw" mode of a terminal.
     ***************/

    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;  // save original

    /***************
     * input flags
     ***************/
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP);  // classic "raw" mode
    raw.c_iflag &= ~(IXON);                     // disable ctrl-s and ctrl-q
    raw.c_iflag &= ~(ICRNL);  // disable converting carriage returns to new
                              // lines. ctrl-m and the enter key now show 13

    /***************
     * output flags
     ***************/
    raw.c_oflag &=
        ~(OPOST);  // turn off conversion of \n to \r\n on the output side

    /***************
     * local flags
     ***************/
    raw.c_lflag &= ~(ECHO);    // Disable echo out of terminal
    raw.c_lflag &= ~(ICANON);  // Read input byte by byte
    raw.c_lflag &= ~(ISIG);    // Disable ctrl-c, ctrl-z, ctrl-y
    raw.c_lflag &= ~(IEXTEN);  // Disable ctrl-v and ctrol-o

    /***************
     * Add a timeout for read
     ***************/
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    return c;
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }
    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*+++ append buffer +++*/
struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT \
    { NULL, 0 }

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab) { free(ab->b); }
/*+++ input +++*/
void editorMoveCursor(char key) {
    switch (key) {
        case 'a':
            if (E.cx > 0) {
                E.cx--;
            }
            break;
        case 'd':
            if (E.cx >= E.screencols) {
                E.cx = E.screencols;
            } else {
                E.cx++;
            }
            break;
        case 'w':
            if (E.cy > 0) {
                E.cy--;
            }
            break;
        case 's':
            if (E.cy >= E.screenrows) {
                E.cy = E.screenrows;
            } else {
                E.cy++;
            }
            break;
    }
}
void editorProcessKeypress() {
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            exit(0);
            break;
        case 'w':
        case 's':
        case 'a':
        case 'd':
            editorMoveCursor(c);
            break;
    }
}

/*+++ output +++*/

/*
 * Here we are writing 4 bytes out to terminal "\x1b" is the escape character
 * (27 in decimal). Escape sequences, which always start with the escape
 * character followed by [, instruct the terminal to do various text formatting
 * tasks, such as coloring text. In this case, the J command clears the screen.
 * The arguments come first, in this case "2" is the argument, which clears the
 * entire screen.
 */

void editorDrawWelcome(struct abuf* ab) {
    char welcome[80];
    int welcomelen = snprintf(welcome, sizeof(welcome),
                              "Kilo editor -- version %s", KILO_VERSION);
    if (welcomelen > E.screencols) {
        welcomelen = E.screencols;
    }

    int padding = (E.screencols - welcomelen) / 2;
    if (padding) {
        abAppend(ab, "~", 1);
    }

    // center the welcome message
    while (padding--) {
        abAppend(ab, " ", 1);
    }

    abAppend(ab, welcome, welcomelen);
}
// write tildas for lines with no content
void editorDrawRows(struct abuf* ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            editorDrawWelcome(ab);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);  // erase current line
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);  // cusor at the top left corner
    editorDrawRows(&ab);         // add tildas

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*+++ init +++*/
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
}
int main() {
    enableRawMode();
    initEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
