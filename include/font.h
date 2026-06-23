#ifndef RACING_FONT_H
#define RACING_FONT_H
#include <stdint.h>

#define FONT_CELL_W 12
#define FONT_CELL_H 18
#define FONT_FIRST  32
#define FONT_LAST   126

/* 返回该字符 CELL_W*CELL_H 的 alpha 字节(行优先);不可见字符返回 NULL。 */
const uint8_t *font_glyph(int c);

#endif
