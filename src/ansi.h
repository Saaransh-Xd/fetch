#ifndef FETCH_ANSI_H
#define FETCH_ANSI_H

/* ANSI Select Graphic Rendition (SGR) escape sequences. */
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_DIM "\033[2m"

/* Standard foreground colors. */
#define FG_BLACK "\033[30m"
#define FG_RED "\033[31m"
#define FG_GREEN "\033[32m"
#define FG_YELLOW "\033[33m"
#define FG_BLUE "\033[34m"
#define FG_MAGENTA "\033[35m"
#define FG_CYAN "\033[36m"
#define FG_WHITE "\033[37m"

/* Bright foreground colors. */
#define FG_BRIGHT_BLACK "\033[90m"
#define FG_BRIGHT_RED "\033[91m"
#define FG_BRIGHT_GREEN "\033[92m"
#define FG_BRIGHT_YELLOW "\033[93m"
#define FG_BRIGHT_BLUE "\033[94m"
#define FG_BRIGHT_MAGENTA "\033[95m"
#define FG_BRIGHT_CYAN "\033[96m"
#define FG_BRIGHT_WHITE "\033[97m"

/* Compatibility aliases used by the existing fetch output code. */
#define ANSI_BLACK FG_BLACK
#define ANSI_RED FG_RED
#define ANSI_GREEN FG_GREEN
#define ANSI_YELLOW FG_YELLOW
#define ANSI_BLUE FG_BLUE
#define ANSI_MAGENTA FG_MAGENTA
#define ANSI_CYAN FG_CYAN
#define ANSI_WHITE FG_WHITE

/* Standard background colors. */
#define BG_BLACK "\033[40m"
#define BG_RED "\033[41m"
#define BG_GREEN "\033[42m"
#define BG_YELLOW "\033[43m"
#define BG_BLUE "\033[44m"
#define BG_MAGENTA "\033[45m"
#define BG_CYAN "\033[46m"
#define BG_WHITE "\033[47m"

/* Bright background colors. */
#define BG_BRIGHT_BLACK "\033[100m"
#define BG_BRIGHT_RED "\033[101m"
#define BG_BRIGHT_GREEN "\033[102m"
#define BG_BRIGHT_YELLOW "\033[103m"
#define BG_BRIGHT_BLUE "\033[104m"
#define BG_BRIGHT_MAGENTA "\033[105m"
#define BG_BRIGHT_CYAN "\033[106m"
#define BG_BRIGHT_WHITE "\033[107m"

/* Print the standard and bright ANSI background color palettes. */
void printColorPalette(void);

/* Return non-zero when ANSI color output should be enabled. */
int colors_enabled(void);

#endif /* FETCH_ANSI_H */
