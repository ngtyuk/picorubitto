#ifndef GAME_DVI_H
#define GAME_DVI_H

#include <stdbool.h>
#include <stdint.h>

bool game_dvi_init(void);
bool game_dvi_present(const uint8_t *rgb565_be);

#endif
