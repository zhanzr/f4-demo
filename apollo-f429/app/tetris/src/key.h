#ifndef __KEY_H
#define __KEY_H

#include "sys_compat.h"

/* Key ids (matching the game's button slots). */
#define KEY_ID_LEFT   0   /* KEY_2  PC13 active low  */
#define KEY_ID_RIGHT  1   /* KEY_0  PH3  active low  */
#define KEY_ID_ROT    2   /* KEY_UP PA0  active high */
#define KEY_ID_DROP   3   /* KEY_1  PH2  active low  */

void KEY_Init(void);       /* IO config with internal pulls (no ext resistors) */
int  KEY_Read(int id);     /* raw pressed level (1 = pressed) for a key id     */

#endif /* __KEY_H */