/*
  tetris - Tetris on the apollo-f429 (STM32F429IGT6 @ 180 MHz, stage-2 SDRAM),
  2.8" ILI9341 240x320 on the 16-bit FSMC parallel bus.

  Ported from D:/c5_prj/nucleo-c542/board_t1/tetris (ST7789 240x240). The
  panel here is 240x320, so the 10x20 playfield (12 px cells) is shifted down
  by YOFF=40 to sit vertically centered, with the right-side panel (NEXT
  preview + SCORE/LINES/LEVEL) aligned to it.

  Input (apollo-f429):
    TPAD touch button (TIM2-CH1 / PA5)   = start / pause / resume
    KEY_UP (PA0, pull-down, active high) = rotate
    KEY_2  (PC13, pull-up,  active low)  = left
    KEY_1  (PH2,  pull-up,  active low)  = drop (hard drop)
    KEY_0  (PH3,  pull-up,  active low)  = right

  Scoring: 100/300/500/800 x level for 1/2/3/4 lines, +2 per hard-dropped
  row; level (and speed) up every 10 lines.

  Top-5 high scores live in the on-board AT24C02 EEPROM (I2C2, PH4/PH5)
  behind board/ee_flash.c, stored as {magic, version, scores[5], crc} with a
  validity check on load (bad/virgin/mismatched -> fresh table).
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "ee_flash.h"
#include "lcd.h"
#include "lcd_fonts.h"
#include "lcd_font_1608.h"
#include "key.h"
#include "tpad.h"
#include "sys_compat.h"
#include "stm32f4xx_hal.h"

/* ---- board / panel geometry ----------------------------------------- */
#define YOFF       40          /* vertical centering offset (320 vs 240)  */
#define CELL       12
#define TCOLS      10
#define TROWS      20
#define FX         10          /* playfield origin (px)                  */
#define FY         YOFF
#define SEP_X      134         /* 2 px separator field | panel           */
#define PREV_X     166         /* NEXT preview box (4x4 cells)           */
#define PREV_Y     24 + YOFF
#define TOP_N      5           /* high-score table size                  */
#define OVER_TIMEOUT 60000     /* game over -> title after 60 s idle     */

/* ---- colors ---------------------------------------------------------- */
#define LCD_ORANGE  0xFFA500UL
#define LCD_GRAY    0x808080UL
#define LOCKED_COLOR 0xA0A0A0UL   /* unified color for landed blocks     */
#define BG          LCD_BLACK
#define FG          LCD_WHITE

/* ---- buttons (abstracted: touch button + 4 mechanical keys) ----------- */
enum { B_START, B_LEFT, B_RIGHT, B_ROT, B_DROP, B_COUNT };

typedef struct
{
    uint8_t active_high;      /* pressed level                            */
    uint8_t filter;           /* debounced state                          */
    uint8_t cnt;              /* consecutive deviating samples            */
    uint8_t state;            /* debounced state snapshot                 */
    uint8_t prev;             /* previous debounced state                 */
} btn_t;

static btn_t btn[B_COUNT];

/* ---- pieces ---------------------------------------------------------- */
/* [piece][rotation][4 cells][x,y] within the piece bounding box. */
static const int8_t shapes[7][4][4][2] =
{
    /* I */
    {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}},
     {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
    /* O */
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}},
     {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    /* T */
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}},
     {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    /* S */
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}},
     {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
    /* Z */
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}},
     {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
    /* J */
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}},
     {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    /* L */
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}},
     {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}},
};

static const uint32_t piece_color[7] =
{
    LCD_CYAN,    /* I */
    LCD_YELLOW,  /* O */
    LCD_MAGENTA, /* T */
    LCD_GREEN,   /* S */
    LCD_RED,     /* Z */
    LCD_BLUE,    /* J */
    LCD_ORANGE,  /* L */
};

/* ---- game state ------------------------------------------------------- */
enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };

/* ---- NV high scores in the AT24C02 EEPROM ---------------------------- */
/* A single record is kept at EEPROM offset 0 (256-byte part, sub-addressed
 * by offset): {magic, version, scores[5], crc}. The CRC covers the first 7
 * words; on load a mismatch / virgin EEPROM yields a fresh table. */
#define NV_ADDR       0                            /* EEPROM byte offset  */
#define NV_MAGIC      0x54455452UL                 /* 'TETR'              */
#define NV_VERSION    1UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t scores[TOP_N];       /* descending, 0 = empty slot           */
    uint32_t crc;                 /* over magic + version + scores        */
} nv_scores_t;

static uint8_t  field[TROWS][TCOLS];
static uint8_t  cur_t, cur_r, next_t;
static int8_t   cur_x, cur_y;
static uint32_t score, lines, level, speed;
static uint32_t next_drop;        /* tick for the next gravity step       */
static uint32_t remain_ms;        /* gravity time left when pausing       */
static uint8_t  state = ST_TITLE;
static uint32_t rng;
static uint32_t over_since;       /* tick when game over was shown        */

/* session high-score table (mirrored into NV EEPROM), descending */
static uint32_t top_score[TOP_N];

/* blink/animation timers (title + game-over cards)                     */
static uint8_t  blink_phase, start_visible, title_phase, title_tag;
static uint32_t blink_next, title_anim_next, title_tag_next;

/* left/right auto-repeat (DAS/ARR) - initial delay + repeat interval */
#define DAS_MS   130
#define ARR_MS   40
static uint32_t rep_next[2];
static uint8_t  rep_on[2];

/* ---- PRNG (xorshift32), seeded from the MCU UID + boot tick ---------- */
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* uniform draw: reject the 4-value tail that would bias %7 */
static uint32_t rand_word(void)
{
    uint32_t v;
    do
    {
        v = rnd();
    } while (v >= 4294967292UL);              /* 2^32 - (2^32 % 7)       */
    return v;
}

/* 7-bag randomizer (Tetris guideline). */
static uint8_t bag[7];
static uint8_t bag_idx;

static void bag_refill(void)
{
    uint8_t i, j, t;
    for (i = 0; i < 7; i++)
    {
        bag[i] = i;
    }
    for (i = 6; i > 0; i--)                   /* Fisher-Yates shuffle     */
    {
        j = (uint8_t)(rand_word() % (uint32_t)(i + 1));
        t = bag[i];
        bag[i] = bag[j];
        bag[j] = t;
    }
    bag_idx = 0;
}

static uint8_t rnd7(void)
{
    if (bag_idx >= 7)
    {
        bag_refill();
    }
    return bag[bag_idx++];
}

/* insert a finished-game score into the top table (descending) */
static void nv_store(void);

static void top_add(uint32_t s)
{
    int i, j;
    if (!s)
    {
        return;
    }
    for (i = 0; i < TOP_N; i++)
    {
        if (s > top_score[i])
        {
            break;
        }
    }
    if (i >= TOP_N)
    {
        return;
    }
    for (j = TOP_N - 1; j > i; j--)
    {
        top_score[j] = top_score[j - 1];
    }
    top_score[i] = s;
    printf("[TETRIS] top score update: rank %d (%lu)\r\n", i + 1,
           (unsigned long)s);
    nv_store();
}

/* ---- EEPROM score table ---------------------------------------------- */
static uint32_t nv_crc(const nv_scores_t *nv)
{
    const uint32_t *w = (const uint32_t *)nv;   /* w7 = crc, skipped      */
    uint32_t c = 0xA5C33A5AU;
    uint32_t i;
    for (i = 0; i < 7U; i++)
    {
        c ^= w[i];                              /* magic,ver,scores[5]    */
        c = (c << 3U) | (c >> 29U);
    }
    return c;
}

/* load the top table from EEPROM; instantiate a fresh one if invalid.
 * The whole 256-byte part is read once into the caller's buffer.          */
static void nv_load(uint8_t *buf)
{
    nv_scores_t *nv = (nv_scores_t *)buf;

    EE_Flash_Init();
    EE_Flash_Read(buf);

    if ((nv->magic == NV_MAGIC) && (nv->version == NV_VERSION) &&
        (nv->crc == nv_crc(nv)))
    {
        memcpy(top_score, nv->scores, sizeof top_score);
        printf("[TETRIS] NV scores loaded: %lu %lu %lu %lu %lu\r\n",
               (unsigned long)top_score[0], (unsigned long)top_score[1],
               (unsigned long)top_score[2], (unsigned long)top_score[3],
               (unsigned long)top_score[4]);
    }
    else
    {
        memset(top_score, 0, sizeof top_score);
        printf("[TETRIS] NV: no valid record, fresh table\r\n");
    }
}

/* persist the top table: build a valid record at offset 0 of the part,
 * keep the rest of the 256-byte buffer as-is.                         */
static void nv_store(void)
{
    uint8_t buf[EE_FLASH_SIZE];
    nv_scores_t *nv = (nv_scores_t *)buf;

    EE_Flash_Read(buf);                          /* preserve other data   */
    nv->magic = NV_MAGIC;
    nv->version = NV_VERSION;
    memcpy(nv->scores, top_score, sizeof nv->scores);
    nv->crc = nv_crc(nv);

    EE_Flash_Erase(0xFFU);
    EE_Flash_Program(buf);
    printf("[TETRIS] NV store ok\r\n");
}

/* ---- drawing helpers -------------------------------------------------- */
static void draw_field(void)
{
    int x, y;
    for (y = 0; y < TROWS; y++)
    {
        for (x = 0; x < TCOLS; x++)
        {
            LCD_SetColor(field[y][x] ? LOCKED_COLOR : BG);
            LCD_FillRect((uint16_t)(FX + x * CELL), (uint16_t)(FY + y * CELL),
                         CELL, CELL);
        }
    }
}

static void draw_piece(void)
{
    int i;
    LCD_SetColor(piece_color[cur_t]);
    for (i = 0; i < 4; i++)
    {
        int cx = cur_x + shapes[cur_t][cur_r][i][0];
        int cy = cur_y + shapes[cur_t][cur_r][i][1];
        if (cy >= 0)
        {
            LCD_FillRect((uint16_t)(FX + cx * CELL), (uint16_t)(FY + cy * CELL),
                         CELL, CELL);
        }
    }
}

static void erase_piece(void)
{
    int i;
    LCD_SetColor(BG);
    for (i = 0; i < 4; i++)
    {
        int cx = cur_x + shapes[cur_t][cur_r][i][0];
        int cy = cur_y + shapes[cur_t][cur_r][i][1];
        if (cy >= 0)
        {
            LCD_FillRect((uint16_t)(FX + cx * CELL), (uint16_t)(FY + cy * CELL),
                         CELL, CELL);
        }
    }
}

static void draw_preview(void)
{
    int i, minx = 3, maxx = 0, miny = 3, maxy = 0;
    LCD_SetColor(BG);
    LCD_FillRect(PREV_X, PREV_Y, 4 * CELL, 4 * CELL);
    LCD_SetColor(piece_color[next_t]);
    for (i = 0; i < 4; i++)
    {
        int cx = shapes[next_t][0][i][0];
        int cy = shapes[next_t][0][i][1];
        if (cx < minx) minx = cx;
        if (cx > maxx) maxx = cx;
        if (cy < miny) miny = cy;
        if (cy > maxy) maxy = cy;
    }
    for (i = 0; i < 4; i++)
    {
        int cx = shapes[next_t][0][i][0] - minx + (4 - (maxx - minx + 1)) / 2;
        int cy = shapes[next_t][0][i][1] - miny + (4 - (maxy - miny + 1)) / 2;
        LCD_FillRect((uint16_t)(PREV_X + cx * CELL), (uint16_t)(PREV_Y + cy * CELL),
                     CELL, CELL);
    }
}

/* fixed-width value so redraws overwrite the previous digits cleanly */
static void panel_text(uint16_t x, uint16_t y, const char *s)
{
    char buf[12];
    snprintf(buf, sizeof buf, "%-6s", s);
    LCD_DisplayString(x, y, buf);
}

static void panel_labels(void)
{
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    panel_text(146, 8 + YOFF,   "NEXT");
    panel_text(146, 88 + YOFF,  "SCORE");
    panel_text(146, 122 + YOFF, "LINES");
    panel_text(146, 156 + YOFF, "LEVEL");
}

static void panel_values(void)
{
    char buf[12];
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    snprintf(buf, sizeof buf, "%-6lu", (unsigned long)score);
    LCD_DisplayString(146, 102 + YOFF, buf);
    snprintf(buf, sizeof buf, "%-6lu", (unsigned long)lines);
    LCD_DisplayString(146, 136 + YOFF, buf);
    snprintf(buf, sizeof buf, "%-6lu", (unsigned long)level);
    LCD_DisplayString(146, 170 + YOFF, buf);
}

static void draw_static_chrome(void)
{
    LCD_SetColor(LCD_GRAY);
    LCD_FillRect(SEP_X, FY, 2, TROWS * CELL);    /* field | panel sep     */
    LCD_FillRect(FX - 2U, FY, 2, TROWS * CELL);  /* playfield left border */
    LCD_FillRect(FX + TCOLS * CELL, FY, 2, TROWS * CELL); /* right border */
    panel_labels();
}

/* Blinking game-over card; `show` toggles the text (box stays). */
static void gameover_overlay(int show)
{
    LCD_SetColor(LCD_BLACK);
    LCD_SetBackColor(LCD_WHITE);
    LCD_FillRect(35, 88 + YOFF, 170, 56);
    if (show)
    {
        LCD_SetAsciiFont(&ASCII_Font16);
        LCD_DisplayString(84, 94 + YOFF, "GAME OVER");
        LCD_SetAsciiFont(&ASCII_Font12);
        LCD_DisplayString(87, 120 + YOFF, "press START");
    }
}

/* Colorful tetromino-color wave under the title. */
static void title_squares(uint8_t phase)
{
    int i;
    for (i = 0; i < 7; i++)
    {
        LCD_SetColor(piece_color[(uint8_t)((i + phase) % 7)]);
        LCD_FillRect((uint16_t)(90 + i * 16), 70 + YOFF, 14, 14);
    }
}

/* Blinking "press START" prompt on the title screen. */
static void title_press(int show)
{
    LCD_SetColor(BG);
    LCD_SetBackColor(BG);
    LCD_FillRect(84, 168 + YOFF, 52, 16);
    if (show)
    {
        LCD_SetColor(FG);
        LCD_DisplayString(87, 170 + YOFF, "press START");
    }
}

/* high-score ranks (gold, silver, bronze, cyan, green), 0s hidden */
static void title_scores(void)
{
    static const uint32_t rank_color[TOP_N] =
    {
        LCD_YELLOW, LCD_WHITE, LCD_ORANGE, LCD_CYAN, LCD_GREEN
    };
    char buf[16];
    int i;
    LCD_SetAsciiFont(&ASCII_Font12);
    for (i = 0; i < TOP_N; i++)
    {
        if (!top_score[i])
        {
            break;
        }
        LCD_SetColor(rank_color[i]);
        LCD_SetBackColor(BG);
        snprintf(buf, sizeof buf, "%d. %-7lu", i + 1,
                 (unsigned long)top_score[i]);
        LCD_DisplayString(96, (uint16_t)(92 + YOFF + i * 14), buf);
    }
}

/* rotating tagline under the game title */
static const char *const taglines[] =
{
    "stack till you drop!",
    "bricks are falling...",
    "rotate or regret",
    "no pressure. just blocks.",
    "180 MHz of pure gravity",
    "the blocks are coming",
};

static void title_tagline(uint8_t idx)
{
    const char *s = taglines[idx % (uint8_t)(sizeof taglines / sizeof taglines[0])];
    LCD_SetColor(BG);
    LCD_SetBackColor(BG);
    LCD_FillRect(0, 46 + YOFF, 240, 16);
    LCD_SetColor(LCD_CYAN);
    LCD_DisplayString((uint16_t)((240 - (int)strlen(s) * 6) / 2), 48 + YOFF, (char *)s);
}

/* ---- game mechanics ----------------------------------------------------- */
static int fits(int t, int r, int x, int y)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        int cx = x + shapes[t][r][i][0];
        int cy = y + shapes[t][r][i][1];
        if (cx < 0 || cx >= TCOLS || cy >= TROWS)
        {
            return 0;
        }
        if (cy >= 0 && field[cy][cx])
        {
            return 0;
        }
    }
    return 1;
}

static void spawn(void)
{
    cur_t = next_t;
    cur_r = 0;
    cur_x = 3;
    cur_y = 0;
    next_t = rnd7();
    draw_preview();
    if (!fits(cur_t, cur_r, cur_x, cur_y))
    {
        printf("[TETRIS] game over: score=%lu lines=%lu\r\n",
               (unsigned long)score, (unsigned long)lines);
        state = ST_OVER;
        top_add(score);
        over_since = HAL_GetTick();
        blink_phase = 1;
        blink_next = over_since + 450;
        gameover_overlay(1);
    }
    else
    {
        draw_piece();
    }
}

static int clear_full_lines(void)
{
    int full[TROWS];
    int n = 0, r, w;

    for (r = 0; r < TROWS; r++)
    {
        int c, is_full = 1;
        for (c = 0; c < TCOLS; c++)
        {
            if (!field[r][c]) { is_full = 0; break; }
        }
        full[r] = is_full;
        n += is_full;
    }
    if (!n)
    {
        return 0;
    }

    LCD_SetColor(LCD_WHITE);                 /* flash the cleared rows    */
    for (r = 0; r < TROWS; r++)
    {
        if (full[r])
        {
            LCD_FillRect(FX, (uint16_t)(FY + r * CELL), TCOLS * CELL, CELL);
        }
    }
    HAL_Delay(120);

    w = TROWS - 1;                           /* collapse downwards        */
    for (r = TROWS - 1; r >= 0; r--)
    {
        if (!full[r])
        {
            if (w != r)
            {
                memcpy(field[w], field[r], TCOLS);
            }
            w--;
        }
    }
    for (; w >= 0; w--)
    {
        memset(field[w], 0, TCOLS);
    }
    draw_field();
    return n;
}

static void lock_piece(void)
{
    static const uint16_t line_pts[5] = { 0, 100, 300, 500, 800 };
    uint32_t old_level = level;
    int n, i;

    for (i = 0; i < 4; i++)
    {
        int cx = cur_x + shapes[cur_t][cur_r][i][0];
        int cy = cur_y + shapes[cur_t][cur_r][i][1];
        field[cy][cx] = (uint8_t)(cur_t + 1);
    }

    n = clear_full_lines();
    if (n)
    {
        score += (uint32_t)line_pts[n] * level;
        lines += (uint32_t)n;
        level = lines / 10 + 1;
        if (level != old_level)
        {
            speed = 800 - (level - 1) * 70;
            if (speed < 120) { speed = 120; }
            printf("[TETRIS] level up: %lu (speed %lu ms)\r\n",
                   (unsigned long)level, (unsigned long)speed);
        }
        printf("[TETRIS] +%d lines, score=%lu lines=%lu\r\n",
               n, (unsigned long)score, (unsigned long)lines);
        panel_values();
    }
    else
    {
        LCD_SetColor(LOCKED_COLOR);
        for (i = 0; i < 4; i++)
        {
            int cx = cur_x + shapes[cur_t][cur_r][i][0];
            int cy = cur_y + shapes[cur_t][cur_r][i][1];
            LCD_FillRect((uint16_t)(FX + cx * CELL), (uint16_t)(FY + cy * CELL),
                         CELL, CELL);
        }
    }

    spawn();
}

static void new_game(void)
{
    memset(field, 0, sizeof field);
    score = 0;
    lines = 0;
    level = 1;
    speed = 800;
    bag_refill();                        /* fresh shuffled 7-bag per game */
    next_drop = HAL_GetTick() + speed;
    next_t = rnd7();
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    LCD_Clear(BG);                           /* wipe title/over cards     */
    draw_field();
    draw_static_chrome();
    panel_values();
    spawn();
    state = ST_PLAY;
    printf("[TETRIS] new game\r\n");
}

/* ---- title / pause screens --------------------------------------------- */
static void title_screen(void)
{
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    LCD_Clear(BG);
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_DisplayString(96, 20 + YOFF, "TETRIS");
    LCD_SetAsciiFont(&ASCII_Font12);
    title_tagline(title_tag);
    title_squares(title_phase);
    title_scores();
    start_visible = 1;
    title_press(1);
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    LCD_DisplayString(66, 236 + YOFF, "L/R move  ROT turn");
    LCD_DisplayString(78, 250 + YOFF, "DROP = hard drop");
}

static void pause_overlay(int show)
{
    LCD_SetColor(FG);
    LCD_SetBackColor(BG);
    LCD_FillRect(40, 100 + YOFF, 60, 24);
    if (show)
    {
        LCD_SetAsciiFont(&ASCII_Font12);
        LCD_DisplayString(55, 106 + YOFF, "PAUSE");
    }
}

/* ---- button sampling -------------------------------------------------- */
static void btns_sample(void)
{
    int i;

    /* touch button (B_START) + four mechanical keys: 2 stable samples @ 5 ms
     * debounce. The touch level and each key level run through the same
     * filter, so a sustained press yields exactly one edge. */
    for (i = B_START; i <= B_DROP; i++)
    {
        btn_t *b = &btn[i];
        int cur = (i == B_START) ? TPAD_Touched()
                                 : ((i == B_ROT)  ? KEY_Read(KEY_ID_ROT)
                                   : ((i == B_LEFT)  ? KEY_Read(KEY_ID_LEFT)
                                     : ((i == B_RIGHT) ? KEY_Read(KEY_ID_RIGHT)
                                                       : KEY_Read(KEY_ID_DROP))));
        if (cur == b->filter)
        {
            b->cnt = 0;
        }
        else if (++b->cnt >= 2)
        {
            b->filter = (uint8_t)cur;
            b->cnt = 0;
            b->prev = b->state;
            b->state = (uint8_t)cur;
        }
    }
}

static int held(int i)        { return btn[i].state; }

/* edge-triggered read; consumes the edge so it fires exactly once */
static int pressed(int i)
{
    btn_t *b = &btn[i];
    int p = b->state && !b->prev;
    b->prev = b->state;
    return p;
}

/* ---- main ---------------------------------------------------------------- */
int main(void)
{
    const uint32_t *uid = (const uint32_t *)0x1FFF7A10UL;
    uint8_t nvbuf[EE_FLASH_SIZE];
    uint32_t now, last_sample = 0;

    HAL_Init();
    Board_Init();

    printf("\r\n==== apollo-f429 (STM32F429IGT6) tetris @ %lu MHz ====\r\n",
           (unsigned long)(SystemCoreClock / 1000000UL));
    printf("ILI9341 240x320 16-bit FSMC; SCORE/EEPROM AT24C02 I2C2\r\n");
    printf("TOUCH(start)=PA5  ROT=PA0  L=PC13  R=PH3  DROP=PH2\r\n");

    /* seed the xorshift PRNG from the MCU UID + boot tick */
    rng = uid[0] ^ uid[1] ^ uid[2] ^ HAL_GetTick();
    if (rng == 0U)
    {
        rng = 0x1234ABCD;
    }
    bag_refill();                    /* first 7-bag must be shuffled, not 0s */

    nv_load(nvbuf);

    if (TPAD_Init(2) != 0)
    {
        printf("[TETRIS] TPAD init FAIL (pad not connected?)\r\n");
    }
    KEY_Init();

    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    LCD_SetBackColor(BG);
    LCD_SetColor(FG);
    LCD_Clear(BG);

    title_screen();
    blink_next = title_anim_next = title_tag_next = HAL_GetTick() + 450;
    printf("[TETRIS] title screen\r\n");

    while (1)
    {
        now = HAL_GetTick();

        /* sample buttons every 5 ms (debounce + touch edge) */
        if (now - last_sample >= 5)
        {
            last_sample = now;
            btns_sample();
        }

        if (state == ST_TITLE)
        {
            if (now >= title_anim_next)
            {
                title_anim_next = now + 250;
                title_phase = (uint8_t)((title_phase + 1) % 7);
                title_squares(title_phase);
            }
            if (now >= title_tag_next)
            {
                title_tag_next = now + 1500;
                title_tag++;
                title_tagline(title_tag);
            }
            if (now >= blink_next)
            {
                blink_next = now + 450;
                start_visible ^= 1;
                title_press(start_visible);
            }
            if (pressed(B_START))
            {
                new_game();
            }
        }
        else if (state == ST_PLAY)
        {
            int dx, i;

            if (pressed(B_START))
            {
                remain_ms = (next_drop > now) ? (next_drop - now) : 0;
                pause_overlay(1);
                state = ST_PAUSE;
                printf("[TETRIS] pause\r\n");
            }

            /* left / right with auto-repeat (edge, then DAS/ARR) */
            for (i = 0; i < 2; i++)
            {
                int b = i ? B_RIGHT : B_LEFT;
                dx = i ? 1 : -1;
                if (pressed(b))
                {
                    if (fits(cur_t, cur_r, cur_x + dx, cur_y))
                    {
                        erase_piece();
                        cur_x = (int8_t)(cur_x + dx);
                        draw_piece();
                    }
                    rep_next[i] = now + DAS_MS;
                    rep_on[i] = 1;
                }
                else if (rep_on[i] && held(b) && now >= rep_next[i])
                {
                    if (fits(cur_t, cur_r, cur_x + dx, cur_y))
                    {
                        erase_piece();
                        cur_x = (int8_t)(cur_x + dx);
                        draw_piece();
                    }
                    rep_next[i] = now + ARR_MS;
                }
                else if (!held(b))
                {
                    rep_on[i] = 0;
                }
            }

            if (pressed(B_ROT))
            {
                static const int8_t kick[5] = { 0, -1, 1, -2, 2 };
                int nr = (cur_r + 1) & 3, k;
                for (k = 0; k < 5; k++)
                {
                    if (fits(cur_t, nr, cur_x + kick[k], cur_y))
                    {
                        erase_piece();
                        cur_r = (uint8_t)nr;
                        cur_x = (int8_t)(cur_x + kick[k]);
                        draw_piece();
                        break;
                    }
                }
            }

            if (pressed(B_DROP))
            {
                int n = 0;
                erase_piece();
                while (fits(cur_t, cur_r, cur_x, cur_y + 1))
                {
                    cur_y++;
                    n++;
                }
                draw_piece();
                score += (uint32_t)n * 2;
                if (n)
                {
                    panel_values();
                }
                lock_piece();
                if (state == ST_PLAY)
                {
                    next_drop = HAL_GetTick() + speed;
                }
            }

            if (state == ST_PLAY && now >= next_drop)
            {
                if (fits(cur_t, cur_r, cur_x, cur_y + 1))
                {
                    erase_piece();
                    cur_y++;
                    draw_piece();
                }
                else
                {
                    lock_piece();
                }
                next_drop = HAL_GetTick() + speed;
            }
        }
        else if (state == ST_PAUSE)
        {
            if (pressed(B_START))
            {
                pause_overlay(0);
                draw_field();
                panel_values();
                next_drop = HAL_GetTick() + remain_ms;
                state = ST_PLAY;
                printf("[TETRIS] resume\r\n");
            }
        }
        else /* ST_OVER */
        {
            if (now >= blink_next)
            {
                blink_next = now + 450;
                blink_phase ^= 1;
                gameover_overlay(blink_phase);
            }
            if (pressed(B_START))
            {
                new_game();
            }
            else if (now - over_since >= OVER_TIMEOUT)
            {
                state = ST_TITLE;
                title_screen();
                blink_next = title_anim_next = now + 450;
                printf("[TETRIS] game over idle -> title\r\n");
            }
        }

        HAL_Delay(2);
    }

    return 0;
}