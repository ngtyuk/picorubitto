#ifndef PICO_RUBY_GAMING_GAME_CLOCK_H
#define PICO_RUBY_GAMING_GAME_CLOCK_H

#include <stdbool.h>

void game_clock_init(void);
void game_clock_report(void);
bool game_clock_check_psram(void);

#endif
