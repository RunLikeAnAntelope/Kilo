/*+++ includes +++*/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*+++ data +++*/
struct termios orig_termios;

/*+++ terminal +++*/
void die(const char *s) {
    perror(s);
    exit(1);
}

// restore original mode when we exit
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    /***************
     * Enables "raw" mode of a terminal.
     ***************/

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        die("tcsetattr");
    }
    atexit(disableRawMode);

    struct termios raw = orig_termios;  // save original

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

/*+++ init +++*/
int main() {
    enableRawMode();
    char c;
    while (1) {
        // EAGAIN check is for compatability with cygwin
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
            die("read");
        }
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') {
            break;
        }
    }
    return 0;
}
