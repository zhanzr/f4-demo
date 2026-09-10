#ifndef __FONTS_H
#define __FONTS_H

#include <stdint.h>

/* ASCII 6x12 font structure (ported from the h723-mini st7789 example). */
typedef struct _pFont
{
    const uint8_t *pTable;  /* font data array address      */
    uint16_t       Width;   /* width of a single character  */
    uint16_t       Height;  /* height of a single character */
    uint16_t       Sizes;   /* font data bytes per character */
    uint16_t       Table_Rows; /* reserved (Chinese fonts)  */
} pFONT;

extern const uint8_t ASCII_1206_Table[];
extern pFONT ASCII_Font12;   /* 12x06 ASCII font */
extern const uint8_t asc2_1608[];
extern pFONT ASCII_Font16;   /* 16x08 ASCII font (asc2_1608), see lcd_font_1608.c */

#endif /* __FONTS_H */