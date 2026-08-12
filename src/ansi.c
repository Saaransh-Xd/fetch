#include "ansi.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Print one palette block and restore the terminal's previous attributes. */
static void print_color_block(const char *background)
{
    printf("%s   %s", background, ANSI_RESET);
}

int colors_enabled(void)
{
    if (getenv("NO_COLOR")) return 0;
    return isatty(STDOUT_FILENO);
}

void printColorPalette(void)
{
    static const char *const standard_backgrounds[] = {
        BG_BLACK, BG_RED, BG_GREEN, BG_YELLOW,
        BG_BLUE, BG_MAGENTA, BG_CYAN, BG_WHITE
    };
    static const char *const bright_backgrounds[] = {
        BG_BRIGHT_BLACK, BG_BRIGHT_RED, BG_BRIGHT_GREEN, BG_BRIGHT_YELLOW,
        BG_BRIGHT_BLUE, BG_BRIGHT_MAGENTA, BG_BRIGHT_CYAN, BG_BRIGHT_WHITE
    };

    for (size_t i = 0; i < 8; ++i)
        print_color_block(standard_backgrounds[i]);
    printf("%s\n", ANSI_RESET);

    for (size_t i = 0; i < 8; ++i)
        print_color_block(bright_backgrounds[i]);
    printf("%s\n", ANSI_RESET);
}
