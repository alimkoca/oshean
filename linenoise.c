/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 * http://github.com/hp-peti/linenoise
 *   (forked from http://github.com/msteveb/linenoise)
 *   (forked from http://github.com/antirez/linenoise)
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2011, Steve Bennett <steveb at workware dot net dot au>
 * Copyright (c) 2016, Peter Hanos-Puskai <hp dot peti at gmail dot com>
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
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Bloat:
 * - Completion?
 * - Color printing?
 *
 * Unix/termios
 * ------------
 * List of escape sequences used by this program, we do everything just
 * a few sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward of n chars
 *
 * CR (Carriage Return)
 *    Sequence: \r
 *    Effect: moves cursor to column 1
 *
 * The following are used to clear the screen: ESC [ H ESC [ 2 J
 * This is actually composed of two sequences:
 *
 * cursorhome
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED2 (Clear entire screen)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 * == For highlighting control characters, we also use the following two ==
 * SO (enter StandOut)
 *    Sequence: ESC [ 7 m
 *    Effect: Uses some standout mode such as reverse video
 *
 * SE (Standout End)
 *    Sequence: ESC [ 0 m
 *    Effect: Exit standout mode
 *
 * == Only used if TIOCGWINSZ fails ==
 * DSR/CPR (Report cursor position)
 *    Sequence: ESC [ 6 n
 *    Effect: reports current cursor position as ESC [ NNN ; MMM R
 *
 * win32/console
 * -------------
 * If __MINGW32__ is defined, the win32 console API is used.
 * This could probably be made to work for the msvc compiler too.
 * This support based in part on work by Jon Griffiths.

 * Added thread-synchronised coloured output support
 * Works with MSVC 2008, 2012
 */

#ifdef _WIN32 /* Windows platform, either MinGW or Visual Studio (MSVC) */
#include <windows.h>
#include <fcntl.h>
#define USE_WINCONSOLE
#ifdef __MINGW32__
#define HAVE_UNISTD_H
#else
/* Microsoft headers don't like old POSIX names */
#if defined(_MSC_VER) && _MSC_VER < 1900
#define strdup _strdup
#define snprintf _snprintf
#endif
#endif
#else
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#define USE_TERMIOS
#define USE_PTHREADS
#define USE_OWN_STRDUP
#define HAVE_UNISTD_H
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#include "include/linenoise.h"
#include "include/utf8.h"

#ifdef USE_PTHREADS
#include <pthread.h>
#endif

#include <ctype.h>
#include <assert.h>

#ifdef USE_OWN_STRDUP
// multi-threaded strdup is broken in eglibc-2.19 x64
#undef strdup
#define strdup _strdup
static char *_strdup(const char * str);
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

#define ctrl(C) ((C) - '@')
#define is_bright(x) ((unsigned)((x).has_fg) > 1)

/* Use -ve numbers here to co-exist with normal unicode chars */
enum {
    SPECIAL_NONE,
    SPECIAL_UP = -20,
    SPECIAL_DOWN = -21,
    SPECIAL_LEFT = -22,
    SPECIAL_RIGHT = -23,
    SPECIAL_DELETE = -24,
    SPECIAL_HOME = -25,
    SPECIAL_END = -26,
    SPECIAL_INSERT = -27,
    SPECIAL_PAGE_UP = -28,
    SPECIAL_PAGE_DOWN = -29,
    SPECIAL_SHIFT_TAB = -30
};

static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

/* Structure to contain the status of the current (being edited) line */
struct current {
    char *buf;  /* Current buffer. Always null terminated */
    int bufmax; /* Size of the buffer, including space for the null termination */
    int len;    /* Number of bytes in 'buf' */
    int chars;  /* Number of chars in 'buf' (utf-8 chars) */
    int pos;    /* Cursor position, measured in chars */
    int cols;   /* Size of the window, in chars */
    int rows;   /* Screen rows */
    const char *prompt;
    char *capture; /* Allocated capture buffer, or NULL for none. Always null terminated */
#if defined(USE_TERMIOS)
    int fd;     /* Terminal fd */
#elif defined(USE_WINCONSOLE)
    HANDLE outh; /* Console output handle */
    HANDLE inh; /* Console input handle */
    int x;      /* Current column during output */
    int y;      /* Current row */
#endif
};

static struct linenoiseTextAttr const *promptAttr = 0;

#ifndef _WIN32
static int interrupt_pipe[2] = { -1, -1 };
#else
static HANDLE interruptEvent = 0;
#endif

static void lineEditModeCritical_Enter();
static void lineEditModeCritical_Leave();
static void historyCritical_Enter();
static void historyCritical_Leave();

static void updateLineEditingMode(const char *prompt, struct current *current);

static int fd_read(struct current *current);
static int getWindowSize(struct current *current);

static int outputCharsAttr(struct current *current, const char *buf, int len, struct linenoiseTextAttr const *attr);

void linenoiseHistoryFree(void) {
    historyCritical_Enter();
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
        history = NULL;
        history_len = 0;
    }
    historyCritical_Leave();
}

#if defined(USE_TERMIOS)
static void linenoiseAtExit(void);
static struct termios orig_termios; /* in order to restore at exit */
static int rawmode = 0; /* for atexit() function to check if restore is needed*/
static int atexit_registered = 0; /* register atexit just 1 time */

static const char *unsupported_term[] = {"dumb","cons25",NULL};

static int is256ColorTerm() {
    char *term = getenv("TERM");
    if (term) {
        if (strstr(term,"256color"))
            return 1;
    }
    return 0;
}

static int is256ColorTerm_cached_value() {
    static int saved = 0;
    static int value = 0;
    if (saved == 0) {
        value = is256ColorTerm();
        saved = 1;
    }
    return value;
}

static int isUnsupportedTerm(void) {
    char *term = getenv("TERM");

    if (term) {
        int j;
        for (j = 0; unsupported_term[j]; j++) {
            if (strcmp(term, unsupported_term[j]) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int enableRawMode(struct current *current) {
    struct termios raw;

    current->fd = STDIN_FILENO;
    current->cols = 0;

    if (!isatty(current->fd) || isUnsupportedTerm() ||
        tcgetattr(current->fd, &orig_termios) == -1) {
fatal:
        errno = ENOTTY;
        return -1;
    }

    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(current->fd,TCSADRAIN,&raw) < 0) {
        goto fatal;
    }

    updateLineEditingMode(0, current);

    rawmode = 1;
    return 0;
}

static void disableRawMode(struct current *current) {
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(current->fd,TCSADRAIN,&orig_termios) != -1)
        rawmode = 0;
    updateLineEditingMode(0,0);
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
    if (rawmode) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &orig_termios);
    }
    linenoiseHistoryFree();
}

/* gcc/glibc insists that we care about the return code of write!
 * Clarification: This means that a void-cast like "(void) (EXPR)"
 * does not work.
 */
#define IGNORE_RC(EXPR) if (EXPR) {}

/* This is fdprintf() on some systems, but use a different
 * name to avoid conflicts
 */
static void fd_printf(int fd, const char *format, ...)
{
    va_list args;
    char buf[64];
    int n;

    va_start(args, format);
    n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    IGNORE_RC(write(fd, buf, n));
}

static void clearScreen(struct current *current)
{
    fd_printf(current->fd, "\x1b[H\x1b[2J");
}

static void cursorToLeft(struct current *current)
{
    fd_printf(current->fd, "\r");
}

static int outputChars(struct current *current, const char *buf, int len)
{
    return write(current->fd, buf, len);
}

static void outputControlChar(struct current *current, char ch)
{
    fd_printf(current->fd, "\x1b[7m^%c\x1b[0m", ch);
}

static void eraseEol(struct current *current)
{
    fd_printf(current->fd, "\x1b[0K");
}

static void setCursorPos(struct current *current, int x)
{
    if (x > 0)
        fd_printf(current->fd, "\r\x1b[%dC", x);
    else
        write(current->fd, "\r", 1);
}

/**
 * Reads a char from 'fd', waiting at most 'timeout' milliseconds.
 *
 * A timeout of -1 means to wait forever.
 *
 * Returns -1 if no char is received within the time or an error occurs.
 */

static int fd_read_char(int fd, int timeout)
{
    struct pollfd p[2];
    unsigned char c;

    p[0].fd = fd;
    p[0].events = POLLIN;
    p[0].revents = 0;
    p[1].fd = interrupt_pipe[0];
    p[1].events = POLLIN;
    p[1].revents = 0;

    if (poll(p, 2, timeout) == 0) {
        /* timeout */
        return -1;
    }
    if (p[1].revents & POLLIN)
    {
        char tmp;
        read(p[1].fd, &tmp, 1);
        return -1;
    }

    if (read(fd, &c, 1) != 1) {
        return -1;
    }
    return c;
}

static void ensure_interrupt_pipe()
{
    if (interrupt_pipe[1] == -1)
        pipe(interrupt_pipe);
}

/**
 * Reads a complete utf-8 character
 * and returns the unicode value, or -1 on error.
 */
static int fd_read(struct current *current)
{
#ifdef USE_UTF8
    unsigned char buf[4];
    int n;
    int i;
#endif
    int c;
    ensure_interrupt_pipe();
    lineEditModeCritical_Leave();
    c = fd_read_char(current->fd, -1);
    lineEditModeCritical_Enter();

#ifdef USE_UTF8
    if (c < 0 || c > 0xFF) {

        return c;
    }
    buf[0] = c;
    n = utf8_charlen(buf[0]);
    if (n < 1 || n > 3) {
        return -1;
    }
    for (i = 1; i < n; i++) {
        lineEditModeCritical_Leave();
        c = fd_read_char(current->fd, -1);
        lineEditModeCritical_Enter();
        if (c < 0 || c > 0xFF) {
            return -1;
        }
        buf[i] = c;
    }
    buf[n] = 0;
    /* decode and return the character */
    utf8_tounicode(buf, &c);
#endif
    return c;
}

static int countColorControlChars(const char* prompt)
{
    /* ANSI color control sequences have the form:
     * "\x1b" "[" [0-9;]* "m"
     * We parse them with a simple state machine.
     */

    enum {
        search_esc,
        expect_bracket,
        expect_trail
    } state = search_esc;
    int len = 0, found = 0;
    char ch;

    /* XXX: Strictly we should be checking utf8 chars rather than
     *      bytes in case of the extremely unlikely scenario where
     *      an ANSI sequence is part of a utf8 sequence.
     */
    while ((ch = *prompt++) != 0) {
        switch (state) {
        case search_esc:
            if (ch == '\x1b') {
                state = expect_bracket;
            }
            break;
        case expect_bracket:
            if (ch == '[') {
                state = expect_trail;
                /* 3 for "\e[ ... m" */
                len = 3;
                break;
            }
            state = search_esc;
            break;
        case expect_trail:
            if ((ch == ';') || ((ch >= '0' && ch <= '9'))) {
                /* 0-9, or semicolon */
                len++;
                break;
            }
            if (ch == 'm') {
                found += len;
            }
            state = search_esc;
            break;
        }
    }

    return found;
}

/**
 * Stores the current cursor column in '*cols'.
 * Returns 1 if OK, or 0 if failed to determine cursor pos.
 */
static int queryCursor(int fd, int* cols, int *rows)
{
    /* control sequence - report cursor location */
    fd_printf(fd, "\x1b[6n");

    /* Parse the response: ESC [ rows ; cols R */
    if (fd_read_char(fd, 100) == 0x1b &&
        fd_read_char(fd, 100) == '[') {

        int n = 0;
        while (1) {
            int ch = fd_read_char(fd, 100);
            if (ch == ';') {
                /* Got rows */
                if (rows)
                    *rows = n;
                n = 0;
            }
            else if (ch == 'R') {
                /* Got cols */
                if (n != 0 && n < 1000) {
                    *cols = n;
                }
                break;
            }
            else if (ch >= 0 && ch <= '9') {
                n = n * 10 + ch - '0';
            }
            else {
                break;
            }
        }
        return 1;
    }

    return 0;
}

/**
 * Updates current->cols with the current window size (width)
 */
static int getWindowSize(struct current *current)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col != 0) {
        current->cols = ws.ws_col;
        current->rows = ws.ws_row;
        return 0;
    }

    /* Failed to query the window size. Perhaps we are on a serial terminal.
     * Try to query the width by sending the cursor as far to the right
     * and reading back the cursor position.
     * Note that this is only done once per call to linenoise rather than
     * every time the line is refreshed for efficiency reasons.
     *
     * In more detail, we:
     * (a) request current cursor position,
     * (b) move cursor far right,
     * (c) request cursor position again,
     * (d) at last move back to the old position.
     * This gives us the width without messing with the externally
     * visible cursor position.
     */

    if (current->cols == 0) {
        int here;

        current->cols = 80;
        current->rows = 0;

        /* (a) */
        if (queryCursor (current->fd, &here, 0)) {
            /* (b) */
            fd_printf(current->fd, "\x1b[999C");

            /* (c). Note: If (a) succeeded, then (c) should as well.
             * For paranoia we still check and have a fallback action
             * for (d) in case of failure..
             */
            if (!queryCursor (current->fd, &current->cols, 0)) {
                /* (d') Unable to get accurate position data, reset
                 * the cursor to the far left. While this may not
                 * restore the exact original position it should not
                 * be too bad.
                 */
                fd_printf(current->fd, "\r");
            } else {
                /* (d) Reset the cursor back to the original location. */
                if (current->cols > here) {
                    fd_printf(current->fd, "\x1b[%dD", current->cols - here);
                }
            }
        } /* 1st query failed, doing nothing => default 80 */
    }

    return 0;
}

/**
 * If escape (27) was received, reads subsequent
 * chars to determine if this is a known special key.
 *
 * Returns SPECIAL_NONE if unrecognised, or -1 if EOF.
 *
 * If no additional char is received within a short time,
 * 27 is returned.
 */
static int check_special(int fd)
{
    int c = fd_read_char(fd, 50);
    int c2;

    if (c < 0) {
        return 27;
    }

    c2 = fd_read_char(fd, 50);
    if (c2 < 0) {
        return c2;
    }
    if (c == '[' || c == 'O') {
        /* Potential arrow key */
        switch (c2) {
            case 'A':
                return SPECIAL_UP;
            case 'B':
                return SPECIAL_DOWN;
            case 'C':
                return SPECIAL_RIGHT;
            case 'D':
                return SPECIAL_LEFT;
            case 'F':
                return SPECIAL_END;
            case 'H':
                return SPECIAL_HOME;
            case 'Z':
                return SPECIAL_SHIFT_TAB;
        }
    }
    if (c == '[' && c2 >= '1' && c2 <= '8') {
        /* extended escape */
        c = fd_read_char(fd, 50);
        if (c == '~') {
            switch (c2) {
                case '2':
                    return SPECIAL_INSERT;
                case '3':
                    return SPECIAL_DELETE;
                case '5':
                    return SPECIAL_PAGE_UP;
                case '6':
                    return SPECIAL_PAGE_DOWN;
                case '7':
                case '1': /* This version is used by screen/tmux */
                    return SPECIAL_HOME;
                case '8':
                case '4': /* This version is used by screen/tmux */
                    return SPECIAL_END;
            }
        } else if (c == ';') {
            switch (c2) {
                case '1':
                    c = fd_read_char(fd, 50);
                    if (c == '5') {
                        c2 = fd_read_char(fd, 50);
                        switch (c2) {
                        case 'C':
                            return ctrl(SPECIAL_RIGHT);
                        case 'D':
                            return ctrl(SPECIAL_LEFT);
                    }
                }
            }
        }
        while (c != -1 && c != '~') {
            /* .e.g \e[12~ or '\e[11;2~   discard the complete sequence */
            c = fd_read_char(fd, 50);
        }
    }

    return SPECIAL_NONE;
}
#elif defined(USE_WINCONSOLE)

static DWORD orig_consolemode = 0;

static int enableRawMode(struct current *current) {
    DWORD n;
    INPUT_RECORD irec;

    current->outh = GetStdHandle(STD_OUTPUT_HANDLE);
    current->inh = GetStdHandle(STD_INPUT_HANDLE);

    if (!PeekConsoleInput(current->inh, &irec, 1, &n)) {
        return -1;
    }
    if (getWindowSize(current) != 0) {
        return -1;
    }
    if (GetConsoleMode(current->inh, &orig_consolemode)) {
        SetConsoleMode(current->inh, 0 /*  ENABLE_PROCESSED_INPUT */);
    }
    updateLineEditingMode(0, current);
    return 0;
}

static void disableRawMode(struct current *current)
{
    SetConsoleMode(current->inh, orig_consolemode);
    updateLineEditingMode(0, 0);
}

static void clearScreen(struct current *current)
{
    COORD topleft = { 0, 0 };
    DWORD n;

    FillConsoleOutputCharacter(current->outh, ' ',
        current->cols * current->rows, topleft, &n);
    FillConsoleOutputAttribute(current->outh,
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN,
        current->cols * current->rows, topleft, &n);
    SetConsoleCursorPosition(current->outh, topleft);
}

static void cursorToLeft(struct current *current)
{
    COORD pos = { 0, (SHORT)current->y };
    DWORD n;

    FillConsoleOutputAttribute(current->outh,
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN, current->cols, pos, &n);
    current->x = 0;
}

#if USE_UTF8
static
BOOL WriteConsoleUTF8(HANDLE hConsoleOutput, const char* buf, int len, DWORD *n, LPVOID reserved)
{
    wchar_t *wbuf;
    wchar_t tmp[256];
    int pchars;
    int i;

    if (len <= 0) {
        *n = 0;
        return TRUE;
    }

    pchars = utf8_to_wchar(buf, len, tmp, sizeof(tmp)/sizeof(tmp[0]));

    if (pchars == len)
        return WriteConsoleA(hConsoleOutput, buf, len, n, reserved);

    if (pchars < sizeof(tmp) / sizeof(tmp[0])) {
        wbuf = tmp;
    } else {
        wbuf = (wchar_t *)malloc(sizeof(wchar_t) * (pchars + 1));
        utf8_to_wchar(buf, len, wbuf, pchars + 1);
    }

    BOOL result = WriteConsoleW(hConsoleOutput, wbuf, pchars, n, reserved);

    if (wbuf != tmp)
        free(wbuf);

    return result;
}

#endif

static int outputChars(struct current *current, const char *buf, int len)
{
    COORD pos = { (SHORT)current->x, (SHORT)current->y };
    DWORD n;

    SetConsoleCursorPosition(current->outh, pos);
#if USE_UTF8
    WriteConsoleUTF8(current->outh, buf, len, &n, 0);
#else
    WriteConsoleA(current->outh, buf, len, &n, 0);
#endif
    current->x += n;
    return 0;
}

static void outputControlChar(struct current *current, char ch)
{
    COORD pos = { (SHORT)current->x, (SHORT)current->y };
    DWORD n;

    FillConsoleOutputAttribute(current->outh, BACKGROUND_INTENSITY, 2, pos, &n);
    outputChars(current, "^", 1);
    outputChars(current, &ch, 1);
}

static void eraseEol(struct current *current)
{
    COORD pos = { (SHORT)current->x, (SHORT)current->y };
    DWORD n;

    FillConsoleOutputCharacter(current->outh, ' ', current->cols - current->x, pos, &n);
}

static void setCursorPos(struct current *current, int x)
{
    COORD pos = { (SHORT)x, (SHORT)current->y };

    SetConsoleCursorPosition(current->outh, pos);
    current->x = x;
}

static void ensureInterruptEvent()
{
    if (interruptEvent == 0)
        interruptEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
}

static int getControlState(KEY_EVENT_RECORD *k)
{
    return k->dwControlKeyState & (
        LEFT_ALT_PRESSED | LEFT_CTRL_PRESSED |
        RIGHT_ALT_PRESSED | RIGHT_CTRL_PRESSED |
        SHIFT_PRESSED);
}

static int controlStateIsOnlyAnyOf(KEY_EVENT_RECORD *k, int anyOf)
{
    int state = getControlState(k);
    return (state & anyOf) && !(state & ~anyOf);
}

static int fd_read(struct current *current)
{
    while (1) {
        INPUT_RECORD irec;
        DWORD n;
        HANDLE waitHandles[2];

        ensureInterruptEvent();
        waitHandles[0] = current->inh;
        waitHandles[1] = interruptEvent;
        lineEditModeCritical_Leave();
        if (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE) != WAIT_OBJECT_0) {
            lineEditModeCritical_Enter();
            break;
        }
        if (!ReadConsoleInput (current->inh, &irec, 1, &n)) {
            lineEditModeCritical_Enter();
            break;
        }
        lineEditModeCritical_Enter();
        if (irec.EventType == KEY_EVENT && irec.Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD *k = &irec.Event.KeyEvent;
            if (getControlState(k) == 0) {
                switch (k->wVirtualKeyCode) {
                case VK_LEFT:
                    return SPECIAL_LEFT;
                case VK_RIGHT:
                    return SPECIAL_RIGHT;
                case VK_UP:
                    return SPECIAL_UP;
                case VK_DOWN:
                    return SPECIAL_DOWN;
                case VK_INSERT:
                    return SPECIAL_INSERT;
                case VK_DELETE:
                    return SPECIAL_DELETE;
                case VK_HOME:
                    return SPECIAL_HOME;
                case VK_END:
                    return SPECIAL_END;
                case VK_PRIOR:
                    return SPECIAL_PAGE_UP;
                case VK_NEXT:
                    return SPECIAL_PAGE_DOWN;
                /* apparently TightVNC has some issues sending these */
                case VK_TAB:
                case VK_BACK:
                case VK_RETURN:
                    return k->uChar.AsciiChar;
                }
            } else if (controlStateIsOnlyAnyOf(k, LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                switch (k->wVirtualKeyCode) {
                case VK_LEFT:
                    return ctrl(SPECIAL_LEFT);
                case VK_RIGHT:
                    return ctrl(SPECIAL_RIGHT);
                }
            } else if (controlStateIsOnlyAnyOf(k, SHIFT_PRESSED)) {
                switch (k->wVirtualKeyCode) {
                case VK_TAB:
                    return SPECIAL_SHIFT_TAB;
                }
            }
            if (k->dwControlKeyState & ENHANCED_KEY) {
                /* Note that control characters are already translated in AsciiChar */
            } else if (k->wVirtualKeyCode == VK_CONTROL) {
                continue;
            } else if (k->wVirtualKeyCode == VK_SHIFT) {
                continue;
            } else {
#ifdef USE_UTF8
                return k->uChar.UnicodeChar;
#else
                return k->uChar.AsciiChar;
#endif
            }
        }
    }
    return -1;
}

static int countColorControlChars(const char* prompt)
{
    /* For windows we assume that there are no embedded ansi color
     * control sequences.
     */
    return 0;
}

static int getWindowSize(struct current *current)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(current->outh, &info)) {
        return -1;
    }
    current->cols = info.dwSize.X;
    current->rows = (info.srWindow.Bottom - info.srWindow.Top) + 1;
    if (current->cols <= 0 || current->rows <= 0) {
        current->cols = 80;
        return -1;
    }
    current->y = info.dwCursorPosition.Y;
    current->x = info.dwCursorPosition.X;
    return 0;
}

#endif

static void goLeftToStartOfWord(struct current *current);
static void goRightToEndOfWord(struct current *current);

static int utf8_getchars(char *buf, int c)
{
#ifdef USE_UTF8
    return utf8_fromunicode(buf, c);
#else
    *buf = c;
    return 1;
#endif
}

/**
 * Returns the unicode character at the given offset,
 * or -1 if none.
 */
static int get_char(struct current *current, int pos)
{
    if (pos >= 0 && pos < current->chars) {
        int c;
        int i = utf8_index(current->buf, pos);
        (void)utf8_tounicode(current->buf + i, &c);
        return c;
    }
    return -1;
}


struct line_editing_mode
{
    int rawmode;
    struct current *current;
    const char * prompt;
#ifdef USE_TERMIOS
    int out;
    int err;
#endif
};

static struct line_editing_mode line_editing_mode =
{
    0, 0
};

#if !defined USE_PTHREADS && defined(USE_WINCONSOLE)

typedef struct {
    volatile long init;
    CRITICAL_SECTION realMutex;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {0}

static void pthread_mutex_lock(pthread_mutex_t *mutex)
{
    switch (InterlockedCompareExchange(&mutex->init,
        /*exhange: */ 1, /*comparand*/ 0
        ) /* --> previous value */ )
    {
    case 0: // we need to initialise it
        InitializeCriticalSection(&mutex->realMutex);
        InterlockedIncrement(&mutex->init); // signal that we're done
        break;
    case 1: // someone is initialising it
        while (InterlockedCompareExchange(&mutex->init, 2, 2) < 2)
        {
            Sleep(0);
        }
        break;
    }
    EnterCriticalSection(&mutex->realMutex);
}

static void pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    LeaveCriticalSection(&mutex->realMutex);
}

#endif

static pthread_mutex_t line_edit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;
static int line_edit_mutex_taken = 0;
static int history_mutex_taken = 0;

static void lineEditModeCritical_Enter()
{
    pthread_mutex_lock(&line_edit_mutex);
    assert(++line_edit_mutex_taken == 1);
}
static void lineEditModeCritical_Leave()
{
    assert(--line_edit_mutex_taken == 0);
    pthread_mutex_unlock(&line_edit_mutex);
}
static void historyCritical_Enter()
{
    pthread_mutex_lock(&history_mutex);
    assert(++history_mutex_taken == 1);
}
static void historyCritical_Leave()
{
    assert(--history_mutex_taken == 0);
    pthread_mutex_unlock(&history_mutex);
}


static void updateLineEditingMode(const char* prompt, struct current* current)
{
    line_editing_mode.rawmode = (current != 0);
    line_editing_mode.prompt = prompt;
    line_editing_mode.current = current;
}

static void refreshLine(const char *prompt, struct current *current)
{
    int plen;
    int pchars;
    int backup = 0;
    int i;
    const char *buf = current->buf;
    int chars = current->chars;
    int pos = current->pos;
    int b;
    int ch;
    int n;

    updateLineEditingMode(prompt, current);

    /* Should intercept SIGWINCH. For now, just get the size every time */
    getWindowSize(current);

    plen = strlen(prompt);
    pchars = utf8_strlen(prompt, plen);

    /* Scan the prompt for embedded ansi color control sequences and
     * discount them as characters/columns.
     */
    pchars -= countColorControlChars(prompt);

    /* Account for a line which is too long to fit in the window.
     * Note that control chars require an extra column
     */

    /* How many cols are required to the left of 'pos'?
     * The prompt, plus one extra for each control char
     */
    n = pchars + utf8_strlen(buf, current->len);
    b = 0;
    for (i = 0; i < pos; i++) {
        b += utf8_tounicode(buf + b, &ch);
        if (ch < ' ') {
            n++;
        }
    }

    /* If too many are needed, strip chars off the front of 'buf'
     * until it fits. Note that if the current char is a control character,
     * we need one extra col.
     */
    if (current->pos < current->chars && get_char(current, current->pos) < ' ') {
        n++;
    }

    while (n >= current->cols && pos > 0) {
        b = utf8_tounicode(buf, &ch);
        if (ch < ' ') {
            n--;
        }
        n--;
        buf += b;
        pos--;
        chars--;
    }

    /* Cursor to left edge, then the prompt */
    cursorToLeft(current);
    outputCharsAttr(current, prompt, plen, promptAttr);

    /* Now the current buffer content */

    /* Need special handling for control characters.
     * If we hit 'cols', stop.
     */
    b = 0; /* unwritted bytes */
    n = 0; /* How many control chars were written */
    for (i = 0; i < chars; i++) {
        int ch;
        int w = utf8_tounicode(buf + b, &ch);
        if (ch < ' ') {
            n++;
        }
        if (pchars + i + n >= current->cols) {
            break;
        }
        if (ch < ' ') {
            /* A control character, so write the buffer so far */
            outputChars(current, buf, b);
            buf += b + w;
            b = 0;
            outputControlChar(current, ch + '@');
            if (i < pos) {
                backup++;
            }
        }
        else {
            b += w;
        }
    }
    outputChars(current, buf, b);

    /* Erase to right, move cursor to original position */
    eraseEol(current);
    setCursorPos(current, pos + pchars + backup);
}

static void set_current_space_tail(struct current *current, const char *str, char space, const char *tail)
{
    int len = strlen(str);
    if (current->bufmax-1 < len) {
        len = current->bufmax-1;
    }
    memcpy(current->buf, str, len);
    current->buf[len] = 0;
    current->pos = current->chars = utf8_strlen(current->buf, len);

    if (tail) {
        int tlen;
        if (space && current->bufmax > len+1) {
            current->buf[len++] = space;
        }
        tlen = strlen(tail);
        if (current->bufmax-1 < len + tlen) {
            tlen = current->bufmax-1 - (len + tlen);
        }
        memcpy(current->buf + len, tail, tlen);
        len += tlen;
        current->chars = utf8_strlen(current->buf, len);

    }
    current->len = len;
    memset(current->buf + len, 0, current->bufmax - len);
}

static void set_current(struct current *current, const char *str)
{
    set_current_space_tail(current, str, 0, 0);
}

static int has_room(struct current *current, int bytes)
{
    return current->len + bytes < current->bufmax - 1;
}

/**
 * Removes the char at 'pos'.
 *
 * Returns 1 if the line needs to be refreshed, 2 if not
 * and 0 if nothing was removed
 */
static int remove_char(struct current *current, int pos)
{
    if (pos >= 0 && pos < current->chars) {
        int p1, p2;
        int ret = 1;
        p1 = utf8_index(current->buf, pos);
        p2 = p1 + utf8_index(current->buf + p1, 1);

#ifdef USE_TERMIOS
        /* optimise remove char in the case of removing the last char */
        if (current->pos == pos + 1 && current->pos == current->chars) {
            if (current->buf[pos] >= ' ' && utf8_strlen(current->prompt, -1) + utf8_strlen(current->buf, current->len) < current->cols - 1) {
                ret = 2;
                fd_printf(current->fd, "\b \b");
            }
        }
#endif

        /* Move the null char too */
        memmove(current->buf + p1, current->buf + p2, current->len - p2 + 1);
        current->len -= (p2 - p1);
        current->chars--;

        if (current->pos > pos) {
            current->pos--;
        }
        return ret;
    }
    return 0;
}

/**
 * Insert 'ch' at position 'pos'
 *
 * Returns 1 if the line needs to be refreshed, 2 if not
 * and 0 if nothing was inserted (no room)
 */
static int insert_char(struct current *current, int pos, int ch)
{
    char buf[3];
    int n = utf8_getchars(buf, ch);

    if (has_room(current, n) && pos >= 0 && pos <= current->chars) {
        int p1, p2;
        int ret = 1;
        p1 = utf8_index(current->buf, pos);
        p2 = p1 + n;

#ifdef USE_TERMIOS
        /* optimise the case where adding a single char to the end and no scrolling is needed */
        if (current->pos == pos && current->chars == pos) {
            if (ch >= ' ' && utf8_strlen(current->prompt, -1) + utf8_strlen(current->buf, current->len) < current->cols - 1) {
                IGNORE_RC(write(current->fd, buf, n));
                ret = 2;
            }
        }
#endif

        memmove(current->buf + p2, current->buf + p1, current->len - p1);
        memcpy(current->buf + p1, buf, n);
        current->len += n;

        current->chars++;
        if (current->pos >= pos) {
            current->pos++;
        }
        return ret;
    }
    return 0;
}

/**
 * Captures up to 'n' characters starting at 'pos' for the cut buffer.
 *
 * This replaces any existing characters in the cut buffer.
 */
static void capture_chars(struct current *current, int pos, int n)
{
    if (pos >= 0 && (pos + n - 1) < current->chars) {
        int p1 = utf8_index(current->buf, pos);
        int nbytes = utf8_index(current->buf + p1, n);

        if (nbytes) {
            free(current->capture);
            /* Include space for the null terminator */
            current->capture = (char *)malloc(nbytes + 1);
            if (current->capture) {
                memcpy(current->capture, current->buf + p1, nbytes);
                current->capture[nbytes] = '\0';
            }
        }
    }
}

/**
 * Removes up to 'n' characters at cursor position 'pos'.
 *
 * Returns 0 if no chars were removed or non-zero otherwise.
 */
static int remove_chars(struct current *current, int pos, int n)
{
    int removed = 0;

    /* First save any chars which will be removed */
    capture_chars(current, pos, n);

    while (n-- && remove_char(current, pos)) {
        removed++;
    }
    return removed;
}
/**
 * Inserts the characters (string) 'chars' at the cursor position 'pos'.
 *
 * Returns 0 if no chars were inserted or non-zero otherwise.
 */
static int insert_chars(struct current *current, int pos, const char *chars)
{
    int inserted = 0;

    while (*chars) {
        int ch;
        int n = utf8_tounicode(chars, &ch);
        if (insert_char(current, pos, ch) == 0) {
            break;
        }
        inserted++;
        pos++;
        chars += n;
    }
    return inserted;
}

#ifndef NO_COMPLETION
static linenoiseCompletionCallback *completionCallback = NULL;

static void beep() {
#ifdef USE_TERMIOS
    fprintf(stderr, "\x7");
    fflush(stderr);
#endif
}

static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    free(lc->cvec);
}

#define COMPLETE_SPACE_OPT 0

static int completeLine(struct current *current) {
    linenoiseCompletions lc = { 0, NULL };
    int c = 0;
    char *tail = 0;
    char *tmpBuf = 0;
    int tailIndex = 0;
    int tailLen = 0;

    if (current->pos != current->chars) {
        tailIndex = utf8_index(current->buf, current->pos);
        tail = strdup(current->buf + tailIndex);
        /* cut off tail for completion */
        current->buf[tailIndex] = 0;
    }

    disableRawMode(current);
    lineEditModeCritical_Leave();
    completionCallback(current->buf, &lc);
    lineEditModeCritical_Enter();
    enableRawMode(current);

    if (tail) {
        /* put back cut-off tail */
        current->buf[tailIndex] = *tail;
    }
    if (lc.len == 0) {
        beep();
    } else {
        size_t stop = 0, i = 0;

        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct current tmp = *current;
                tmp.buf = lc.cvec[i];
                tmp.bufmax = strlen(tmp.buf) + 1;
                if (tail)
                {
                    if (!tailLen)
                        tailLen = strlen(tail);

                    tmp.bufmax += tailLen + 1;
                    tmp.buf = (char*) malloc(tmp.bufmax);

                    set_current_space_tail(&tmp, lc.cvec[i], COMPLETE_SPACE_OPT, tail);

                    if (tmpBuf)
                        free(tmpBuf);
                    tmpBuf = tmp.buf;
                }
                else
                {
                    tmp.len = strlen(tmp.buf);
                    tmp.pos = tmp.chars = utf8_strlen(tmp.buf, tmp.len);
                }

                refreshLine(current->prompt, &tmp);

            } else {
                refreshLine(current->prompt, current);
            }

            c = fd_read(current);
            if (c == -1) {
                break;
            }
#ifdef USE_TERMIOS
            if (c == 27) {
                c = check_special(current->fd);
            }
#endif
            switch(c) {
                case '\t': /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) beep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) {
                        refreshLine(current->prompt, current);
                    }
                    stop = 1;
                    c = 0;
                    break;
                case SPECIAL_SHIFT_TAB:
                    if (i == 0) {
                        refreshLine(current->prompt, current);
                        stop = 1;
                        c = 0;
                    } else {
                        --i;
                    }
                    break;
                default:
                    /* Update buffer and return */
                    if (i < lc.len) {
                        set_current_space_tail(current,lc.cvec[i], COMPLETE_SPACE_OPT, tail);
                        updateLineEditingMode(current->prompt, current);
                    }
                    stop = 1;
                    break;
            }
        }
    }
    if (tail)
        free(tail);
    if (tmpBuf)
        free(tmpBuf);

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion.
   Returns the prior callback so that the caller may (if needed)
   restore it when done. */
linenoiseCompletionCallback * linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    linenoiseCompletionCallback * old = completionCallback;
    completionCallback = fn;
    return old;
}

void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    char **nvec = (char **)realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (nvec == NULL)
        return;
    lc->cvec = nvec;
    lc->cvec[lc->len++] = strdup(str);
}

#endif

static int linenoiseEdit(struct current *current) {
    int history_index = 0;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

    set_current(current, "");
    refreshLine(current->prompt, current);

    while(1) {
        int dir = -1;
        int c = fd_read(current);

#ifndef NO_COMPLETION
        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (c == '\t' && /* current->pos == current->chars  && */ completionCallback != NULL) {
            c = completeLine(current);
            /* Return on errors */
            if (c == -1) {
                updateLineEditingMode(0, 0);
                return current->len;
            }
            /* Read next character when 0 */
            if (c == 0) continue;
        }
#endif

process_char:
        if (c == -1) return current->len;
#ifdef USE_TERMIOS
        if (c == 27) {   /* escape sequence */
            c = check_special(current->fd);
        }
#endif
        switch(c) {
        case '\n':    /* enter received while the console is busy */
        case '\r':    /* enter received in raw mode while the console is ready */
            historyCritical_Enter();
            history_len--;
            free(history[history_len]);
            historyCritical_Leave();
            return current->len;
        case ctrl('C'):     /* ctrl-c */
            errno = EAGAIN;
            return -1;
        case 127:   /* backspace */
        case ctrl('H'):
            if (remove_char(current, current->pos - 1) == 1) {
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl('D'):     /* ctrl-d */
            if (current->len == 0) {
                /* Empty line, so EOF */
                historyCritical_Enter();
                history_len--;
                free(history[history_len]);
                historyCritical_Leave();
                return -1;
            }
            goto SpecialDelete;
            /* Otherwise fall through to delete char to right of cursor */
        case SPECIAL_DELETE: SpecialDelete:
            if (remove_char(current, current->pos) == 1) {
                refreshLine(current->prompt, current);
            }
            break;
        case SPECIAL_INSERT:
            /* Ignore. Expansion Hook.
             * Future possibility: Toggle Insert/Overwrite Modes
             */
            break;
        case ctrl('W'):    /* ctrl-w, delete word at left. save deleted chars */
            /* eat any spaces on the left */
            {
                int pos = current->pos;
                while (pos > 0 && get_char(current, pos - 1) == ' ') {
                    pos--;
                }

                /* now eat any non-spaces on the left */
                while (pos > 0 && get_char(current, pos - 1) != ' ') {
                    pos--;
                }

                if (remove_chars(current, pos, current->pos - pos)) {
                    refreshLine(current->prompt, current);
                }
            }
            break;
        case ctrl('R'):    /* ctrl-r */
            {
                /* Display the reverse-i-search prompt and process chars */
                char rbuf[50];
                char rprompt[80];
                int rchars = 0;
                int rlen = 0;
                int searchpos = history_len - 1;

                historyCritical_Enter();

                rbuf[0] = 0;
                while (1) {
                    int n = 0;
                    const char *p = NULL;
                    int skipsame = 0;
                    int searchdir = -1;

                    snprintf(rprompt, sizeof(rprompt), "(reverse-i-search)'%s': ", rbuf);
                    refreshLine(rprompt, current);
                    historyCritical_Leave();
                    c = fd_read(current);
                    historyCritical_Enter();
                    if (c == ctrl('H') || c == 127) {
                        if (rchars) {
                            int p = utf8_index(rbuf, --rchars);
                            rbuf[p] = 0;
                            rlen = strlen(rbuf);
                        }
                        continue;
                    }
#ifdef USE_TERMIOS
                    if (c == 27) {
                        c = check_special(current->fd);
                    }
#endif
                    if (c == ctrl('P') || c == SPECIAL_UP) {
                        /* Search for the previous (earlier) match */
                        if (searchpos > 0) {
                            searchpos--;
                        }
                        skipsame = 1;
                    }
                    else if (c == ctrl('N') || c == SPECIAL_DOWN) {
                        /* Search for the next (later) match */
                        if (searchpos < history_len) {
                            searchpos++;
                        }
                        searchdir = 1;
                        skipsame = 1;
                    }
                    else if (c >= ' ') {
                        if (rlen >= (int)sizeof(rbuf) + 3) {
                            continue;
                        }

                        n = utf8_getchars(rbuf + rlen, c);
                        rlen += n;
                        rchars++;
                        rbuf[rlen] = 0;

                        /* Adding a new char resets the search location */
                        searchpos = history_len - 1;
                    }
                    else {
                        /* Exit from incremental search mode */
                        break;
                    }

                    /* Now search through the history for a match */
                    for (; searchpos >= 0 && searchpos < history_len; searchpos += searchdir) {
                        p = strstr(history[searchpos], rbuf);
                        if (p) {
                            /* Found a match */
                            if (skipsame && strcmp(history[searchpos], current->buf) == 0) {
                                /* But it is identical, so skip it */
                                continue;
                            }
                            /* Copy the matching line and set the cursor position */
                            set_current(current,history[searchpos]);
                            current->pos = utf8_strlen(history[searchpos], p - history[searchpos]);
                            break;
                        }
                    }
                    if (!p && n) {
                        /* No match, so don't add it */
                        rchars--;
                        rlen -= n;
                        rbuf[rlen] = 0;
                    }
                }
                if (c == ctrl('G') || c == ctrl('C')) {
                    /* ctrl-g terminates the search with no effect */
                    set_current(current, "");
                    c = 0;
                }
                else if (c == ctrl('J')) {
                    /* ctrl-j terminates the search leaving the buffer in place */
                    c = 0;
                }

                historyCritical_Leave();

                /* Go process the char normally */
                refreshLine(current->prompt, current);
                goto process_char;
            }
            break;
        case ctrl('T'):    /* ctrl-t */
            if (current->pos > 0 && current->pos <= current->chars) {
                /* If cursor is at end, transpose the previous two chars */
                int fixer = (current->pos == current->chars);
                c = get_char(current, current->pos - fixer);
                remove_char(current, current->pos - fixer);
                insert_char(current, current->pos - 1, c);
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl('V'):    /* ctrl-v */
            if (has_room(current, 3)) {
                /* Insert the ^V first */
                if (insert_char(current, current->pos, c)) {
                    refreshLine(current->prompt, current);
                    /* Now wait for the next char. Can insert anything except \0 */
                    c = fd_read(current);

                    /* Remove the ^V first */
                    remove_char(current, current->pos - 1);
                    if (c != -1) {
                        /* Insert the actual char */
                        insert_char(current, current->pos, c);
                    }
                    refreshLine(current->prompt, current);
                }
            }
            break;
        case ctrl('B'):
        case SPECIAL_LEFT:
            if (current->pos > 0) {
                current->pos--;
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl(SPECIAL_LEFT):
            goLeftToStartOfWord(current);
            refreshLine(current->prompt, current);
            break;
        case ctrl('F'):
        case SPECIAL_RIGHT:
            if (current->pos < current->chars) {
                current->pos++;
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl(SPECIAL_RIGHT):
            goRightToEndOfWord(current);
            refreshLine(current->prompt, current);
            break;
        case SPECIAL_PAGE_UP:
          historyCritical_Enter();
          dir = history_len - history_index - 1; /* move to start of history */
          goto history_navigation;
        case SPECIAL_PAGE_DOWN:
          historyCritical_Enter();
          dir = -history_index; /* move to 0 == end of history, i.e. current */
          goto history_navigation;
        case ctrl('P'):
        case SPECIAL_UP:
          historyCritical_Enter();
          dir = 1;
          goto history_navigation;
        case ctrl('N'):
        case SPECIAL_DOWN:
            historyCritical_Enter();
history_navigation:
            if (history_len > 1) {
                /* Update the current history entry before to
                 * overwrite it with tne next one. */
                free(history[history_len - 1 - history_index]);
                history[history_len - 1 - history_index] = strdup(current->buf);
                /* Show the new entry */
                history_index += dir;
                if (history_index < 0) {
                    history_index = 0;
                    historyCritical_Leave();
                    break;
                } else if (history_index >= history_len) {
                    history_index = history_len - 1;
                    historyCritical_Leave();
                    break;
                }
                set_current(current, history[history_len - 1 - history_index]);
                refreshLine(current->prompt, current);
            }
            historyCritical_Leave();
            break;
        case ctrl('A'): /* Ctrl+a, go to the start of the line */
        case SPECIAL_HOME:
            current->pos = 0;
            refreshLine(current->prompt, current);
            break;
        case ctrl('E'): /* ctrl+e, go to the end of the line */
        case SPECIAL_END:
            current->pos = current->chars;
            refreshLine(current->prompt, current);
            break;
        case ctrl('U'): /* Ctrl+u, delete to beginning of line, save deleted chars. */
            if (remove_chars(current, 0, current->pos)) {
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl('K'): /* Ctrl+k, delete from current to end of line, save deleted chars. */
            if (remove_chars(current, current->pos, current->chars - current->pos)) {
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl('Y'): /* Ctrl+y, insert saved chars at current position */
            if (current->capture && insert_chars(current, current->pos, current->capture)) {
                refreshLine(current->prompt, current);
            }
            break;
        case ctrl('L'): /* Ctrl+L, clear screen */
            clearScreen(current);
            /* Force recalc of window size for serial terminals */
            current->cols = 0;
            refreshLine(current->prompt, current);
            break;
        default:
            /* Only tab is allowed without ^V */
            if (c == '\t' || c >= ' ') {
                if (insert_char(current, current->pos, c) == 1) {
                    refreshLine(current->prompt, current);
                }
            }
            break;
        }
    }
    return current->len;
}

int linenoiseColumns(void)
{
    struct current current;
    enableRawMode (&current);
    getWindowSize (&current);
    disableRawMode (&current);
    return current.cols;
}

char *linenoise(const char *prompt)
{
    int count;
    struct current current;
    char buf[LINENOISE_MAX_LINE];

    if (enableRawMode(&current) == -1) {
        printf("%s", prompt);
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            return NULL;
        }
        count = strlen(buf);
        if (count && buf[count-1] == '\n') {
            count--;
            buf[count] = '\0';
        }
    }
    else
    {
        current.buf = buf;
        current.bufmax = sizeof(buf);
        current.len = 0;
        current.chars = 0;
        current.pos = 0;
        current.prompt = prompt;
        current.capture = NULL;

        lineEditModeCritical_Enter();
        errno = 0;
        count = linenoiseEdit(&current);
        lineEditModeCritical_Leave();

        disableRawMode(&current);
        printf("\n");

        free(current.capture);
        if (count == -1) {
            return NULL;
        }
    }
    return strdup(buf);
}

/* Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    historyCritical_Enter();

    if (history == NULL) {
        history = (char **)malloc(sizeof(char*)*history_max_len);
        if (history == NULL) {
            historyCritical_Leave();
            return 0;
        }
        memset(history,0,(sizeof(char*)*history_max_len));
    }

    /* do not insert duplicate lines into history */
    if (history_len > 0 && strcmp(line, history[history_len - 1]) == 0) {
        historyCritical_Leave();
        return 0;
    }

    linecopy = strdup(line);
    if (!linecopy) {
        historyCritical_Leave();
        return 0;
    }
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    historyCritical_Leave();
    return 1;
}

int linenoiseHistoryGetMaxLen(void) {
    int len;
    historyCritical_Enter();
    len = history_max_len;
    historyCritical_Leave();
    return len;
}

int linenoiseHistorySetMaxLen(int len) {
    char **newHistory;

    if (len < 1) return 0;
    historyCritical_Enter();
    if (history) {
        int tocopy = history_len;

        newHistory = (char **)malloc(sizeof(char*)*len);
        if (newHistory == NULL) {
            historyCritical_Leave();
            return 0;
        }

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(newHistory,0,sizeof(char*)*len);
        memcpy(newHistory,history+(history_len-tocopy), sizeof(char*)*tocopy);
        free(history);
        history = newHistory;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;

    historyCritical_Leave();
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename) {
    FILE *fp = fopen(filename,"w");
    int j;

    if (fp == NULL) return -1;

    historyCritical_Enter();

    for (j = 0; j < history_len; j++) {
        const char *str = history[j];
        /* Need to encode backslash, nl and cr */
        while (*str) {
            if (*str == '\\') {
                fputs("\\\\", fp);
            }
            else if (*str == '\n') {
                fputs("\\n", fp);
            }
            else if (*str == '\r') {
                fputs("\\r", fp);
            }
            else {
                fputc(*str, fp);
            }
            str++;
        }
        fputc('\n', fp);
    }

    fclose(fp);

    historyCritical_Leave();
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];

    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *src, *dest;

        /* Decode backslash escaped values */
        for (src = dest = buf; *src; src++) {
            char ch = *src;

            if (ch == '\\') {
                src++;
                if (*src == 'n') {
                    ch = '\n';
                }
                else if (*src == 'r') {
                    ch = '\r';
                } else {
                    ch = *src;
                }
            }
            *dest++ = ch;
        }
        /* Remove trailing newline */
        if (dest != buf && (dest[-1] == '\n' || dest[-1] == '\r')) {
            dest--;
        }
        *dest = 0;

        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}

/* Provide access to the history buffer.
 *
 * If 'len' is not NULL, the length is stored in *len.
 */
char **linenoiseHistory(int *len) {
    if (len) {
        *len = history_len;
    }
    return history;
}

/*  */
struct previous_mode
{
    int rawmode;
    const char *prompt;
    const char *buf;
#if defined(USE_TERMIOS)
    int fd;     /* Terminal fd */
    struct termios raw_termios;
#elif defined(USE_WINCONSOLE)
    HANDLE outh; /* Console output handle */
    HANDLE inh; /* Console input handle */
    DWORD raw_consolemode;
#endif
};

static int enableOriginalMode(struct previous_mode *mode) {
    mode->rawmode = 0;
    if (line_editing_mode.rawmode) {
#if defined(USE_TERMIOS)
        mode->fd = STDIN_FILENO;
        if (tcgetattr(mode->fd, &mode->raw_termios) == -1)
            return -1;
        if (tcsetattr(mode->fd, TCSADRAIN, &orig_termios) == -1)
            return -1;
#elif defined(USE_WINCONSOLE)
        mode->outh = GetStdHandle(STD_OUTPUT_HANDLE);
        mode->inh = GetStdHandle(STD_INPUT_HANDLE);

        if (!GetConsoleMode(mode->inh, &mode->raw_consolemode))
            return -1;

        SetConsoleMode(mode->inh, orig_consolemode);
#endif
        mode->rawmode = 1;

        if (line_editing_mode.current) {
            cursorToLeft(line_editing_mode.current);
            eraseEol(line_editing_mode.current);
        }
    }
    return 0;
}

static int disableOriginalMode(struct previous_mode *mode) {
    if (mode->rawmode) {
#if defined(USE_TERMIOS)
        if (tcsetattr(mode->fd, TCSADRAIN, &mode->raw_termios) == -1)
            return -1;
#elif defined(USE_WINCONSOLE)
        if (!SetConsoleMode(mode->inh, mode->raw_consolemode))
            return -1;
#endif
        if (line_editing_mode.prompt && line_editing_mode.current)
            refreshLine(line_editing_mode.prompt, line_editing_mode.current);
    }
    return 0;
}

static const char CRLF[2] = {'\r','\n'};

static struct linenoiseTextAttr promptAttrCopy;

void linenoiseSetPromptAttr(struct linenoiseTextAttr const *textAttr)
{
    if (!textAttr) {
        promptAttr = NULL;
        return;
    }
    promptAttrCopy = *textAttr;
    promptAttr = &promptAttrCopy;
}

#if !defined (USE_WINCONSOLE)

static int setTextAttr(int fd, struct linenoiseTextAttr *textAttr)
{
    char buf[32];
    int pos;
    if (!isatty(fd))
        return -1;
    pos = snprintf(buf, 32, "\x1b[0");
    if (textAttr != NULL) {
        int bold = textAttr->bold_fg;
        if (textAttr->has_fg) {
            if (textAttr->fg_color >= 0 && textAttr->fg_color <= 7) {
                int fg_base = 30;
                static const int bright_base = 90;

                if (is_bright(*textAttr)) {
                    if (is256ColorTerm_cached_value()) {
                        fg_base = bright_base;
                    } else {
                        bold = 1;
                    }
                } else if (textAttr->bold_fg) {
                    if (is256ColorTerm_cached_value()) {
                        fg_base = bright_base;
                    }
                }

                int fg = textAttr->fg_color + fg_base;
                pos += sprintf(buf + pos, ";%d", fg);
            }
        }
        if (bold) {
            pos += sprintf(buf + pos, ";%d", 1);
        }
        if (textAttr->underline){
            pos += snprintf(buf + pos, 32 - pos, ";%d", 4);
        }
        if (textAttr->has_bg) {
            if (textAttr->bg_color >= 0 && textAttr->bg_color <= 7) {
                int bg;
                bg = textAttr->bg_color + 40;
                pos += snprintf(buf + pos, 32 - pos, ";%d", bg);
            }
        }
        if (textAttr->invert_bg_fg) {
            pos += snprintf(buf + pos, 32 - pos, ";%d", 7);
        }
    }
    pos += snprintf(buf + pos, 32 - pos, "m");
    write(fd, buf, pos);
    return 0;
}

static int outputCharsAttr(struct current *current, const char *buf, int len, struct linenoiseTextAttr const *attr)
{
    int res;
    if (attr != NULL) {
        res = setTextAttr(current->fd, attr);
        if (res != 0)
            attr = NULL;
    }
    res = outputChars(current, buf, len);
    if (attr != NULL)
        setTextAttr(current->fd, 0);
    return res;
}

static void printLineFromStart(int fd, const struct linenoiseTextWithAttr *textWithAttr, size_t n) {
    struct previous_mode mode;
    int res;
    size_t i;
    struct linenoiseTextAttr const* textAttr;

    lineEditModeCritical_Enter();
    if (!line_editing_mode.current)
        write(fd, &CRLF, 1);

    res = enableOriginalMode(&mode);

    textAttr = NULL;
    for (int i = 0; i != n; ++i) {
        if (res == 0) {
            if (textWithAttr[i].attr != textAttr) {
                textAttr = textWithAttr[i].attr;
                setTextAttr(fd, textAttr);
            }
        }
        if (textWithAttr[i].text != NULL) {
            size_t len;
            len = strlen(textWithAttr[i].text);
            write(fd, textWithAttr[i].text, len);
        }
    }

    if (textAttr != NULL)
        setTextAttr(fd, NULL);

    write(fd, &CRLF, 2);
    fsync(fd);
    disableOriginalMode(&mode);
    lineEditModeCritical_Leave();
}

void linenoisePrintLine(const char *line, struct linenoiseTextAttr const * textAttr) {
    const struct linenoiseTextWithAttr textWithAttr = {
        line,
        textAttr
    };
    printLineFromStart(STDOUT_FILENO, &textWithAttr, 1);
}

void linenoiseErrorLine(const char *line, struct linenoiseTextAttr const * textAttr) {
    const struct linenoiseTextWithAttr textWithAttr = {
        line,
        textAttr
    };
    printLineFromStart(STDERR_FILENO, &textWithAttr, 1);
}

void linenoisePrintAttrLine(struct linenoiseTextWithAttr const * textWithAttr, size_t count) {
    printLineFromStart(STDOUT_FILENO, textWithAttr, count);
}

void linenoiseErrorAttrLine(struct linenoiseTextWithAttr const * textWithAttr, size_t count) {
    printLineFromStart(STDERR_FILENO, textWithAttr, count);
}

void linenoiseCancel()
{
    static const char tmp[] = {0};
    write(interrupt_pipe[1], tmp, 1);
}

int linenoiseWinSize(int *columns, int *rows)
{
    struct winsize ws;
    int result;

    lineEditModeCritical_Enter();

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (columns)
            *columns = ws.ws_col;
        if (rows)
            *rows = ws.ws_row;
        result = 0;
    } else {
        result = -1;
    }

    lineEditModeCritical_Leave();

    return result;
}

#else

#define FOREGROUND_DEFAULT FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED
#define BACKGROUND_DEFAULT 0

static int setTextAttr(HANDLE handle, struct linenoiseTextAttr const * textAttr)
{
    WORD attributes;

    if (textAttr == NULL) {
        if (SetConsoleTextAttribute(handle, FOREGROUND_DEFAULT | BACKGROUND_DEFAULT)) {
            return 0;
        }
        return -1;
    }
    attributes = 0;
    if (textAttr->has_fg && textAttr->fg_color >= 0 && textAttr->fg_color <= 7)
    {
        int fg;
        fg = textAttr->fg_color;
        if (fg & 1)
            attributes |= FOREGROUND_RED;
        if (fg & 2)
            attributes |= FOREGROUND_GREEN;
        if (fg & 4)
            attributes |= FOREGROUND_BLUE;
    }
    else
    {
        attributes |= FOREGROUND_DEFAULT;
    }
    if (textAttr->bold_fg || is_bright(*textAttr))
        attributes |= FOREGROUND_INTENSITY;

    if (textAttr->has_bg & textAttr->bg_color >= 0 && textAttr->bg_color <= 7)
    {
        int bg;
        bg = textAttr->bg_color;
        if (bg & 1)
            attributes |= BACKGROUND_RED;
        if (bg & 2)
            attributes |= BACKGROUND_GREEN;
        if (bg & 4)
            attributes |= BACKGROUND_BLUE;
    }
    else
    {
        attributes |= BACKGROUND_DEFAULT;
    }
    if (textAttr->invert_bg_fg)
    {
        attributes |= COMMON_LVB_REVERSE_VIDEO;
    }
    if (textAttr->underline)
    {
        attributes |= COMMON_LVB_UNDERSCORE;
    }

    if (SetConsoleTextAttribute(handle, attributes)) {
        return 0;
    }
    return -1;
}

static int outputCharsAttr(struct current *current, const char *buf, int len, struct linenoiseTextAttr const *attr)
{
    int res;
    if (attr != NULL) {
        res = setTextAttr(current->outh, attr);
        if (res != 0)
            attr = NULL;
    }
    res = outputChars(current, buf, len);
    
    if (attr != NULL)
        setTextAttr(current->outh, 0);

    return res;
}

static void printLineFromStart(HANDLE handle, struct linenoiseTextWithAttr const *textWithAttr, size_t n) {
    struct previous_mode mode;
    int res;
    size_t i;
    DWORD dummy;
    struct linenoiseTextAttr const * textAttr;

    lineEditModeCritical_Enter();
    WriteFile(handle, CRLF, 1, &dummy, 0);

    res = enableOriginalMode(&mode);

    textAttr = NULL;
    for (i = 0; i != n; ++i) {
        if (res == 0) {
            if (textWithAttr[i].attr != textAttr) {
                textAttr = textWithAttr[i].attr;
                setTextAttr(handle, textAttr);
            }
        }
        if (textWithAttr[i].text != NULL) {
            size_t len = strlen(textWithAttr[i].text);
#if USE_UTF8
            WriteConsoleUTF8(handle, textWithAttr[i].text, len, &dummy, NULL);
#else
            WriteFile(handle, textWithAttr[i].text, len, &dummy, NULL);
#endif
        }
    }

    if (textAttr != NULL)
        setTextAttr(handle, NULL);

    WriteFile(handle, CRLF, 2, &dummy, NULL);

    disableOriginalMode(&mode);
    lineEditModeCritical_Leave();
}

void linenoiseCancel()
{
    if (interruptEvent != 0)
        SetEvent(interruptEvent);
}

void linenoisePrintLine(const char *line, struct linenoiseTextAttr const * textAttr) {
    const struct linenoiseTextWithAttr textWithAttr = {
        line,
        textAttr
    };
    printLineFromStart(GetStdHandle(STD_OUTPUT_HANDLE), &textWithAttr, 1);
}

void linenoiseErrorLine(const char *line, struct linenoiseTextAttr const * textAttr) {
    const struct linenoiseTextWithAttr textWithAttr = {
        line,
        textAttr
    };
    printLineFromStart(GetStdHandle(STD_ERROR_HANDLE), &textWithAttr, 1);
}

void linenoisePrintAttrLine(struct linenoiseTextWithAttr const * textWithAttr, size_t n) {
    printLineFromStart(GetStdHandle(STD_OUTPUT_HANDLE), textWithAttr, n);
}

void linenoiseErrorAttrLine(struct linenoiseTextWithAttr const * textWithAttr, size_t n) {
    printLineFromStart(GetStdHandle(STD_ERROR_HANDLE), textWithAttr, n);
}

int linenoiseWinSize(int *columns, int *rows)
{
    struct current current;
    int result;
    current.outh = GetStdHandle(STD_OUTPUT_HANDLE);
    result = getWindowSize (&current);
    if (columns)
        *columns = current.cols;
    if (rows)
        *rows = current.rows;
    return result;
}

#endif

static int isWordChar(int ch)
{
    return isalnum(ch);
}

static void goLeftToStartOfWord(struct current *current)
{
    if (!current->buf)
        return;

    if (current->pos == 0)
        return;

    current->pos--;

    while (current->pos > 0 && !isWordChar(get_char(current, current->pos))) {
        current->pos--;
    }

    while (current->pos > 0 && isWordChar(get_char(current, current->pos-1))) {
        current->pos--;
    }
}
static void goRightToEndOfWord(struct current *current)
{
    if (!current->buf)
        return;

    while (current->pos < current->chars && !isWordChar(get_char(current, current->pos))) {
        current->pos++;
    }

    while (current->pos < current->chars && isWordChar(get_char(current, current->pos))) {
        current->pos++;
    }
}


#ifdef USE_OWN_STRDUP
// multi-threaded strdup is broken in glibc-2.19 x64
static char *_strdup(const char * str)
{
    size_t len;
    size_t malloclen;
    char * ptr;
    
    len = strlen(str);
    malloclen = len + 1;
    if (malloclen % 16)
        malloclen += 16 - (malloclen % 16);

    ptr = (char*) malloc(malloclen);

    if (!ptr)
        return ptr;

    memcpy(ptr, str, len + 1);

    return ptr;
}
#endif
