#ifndef _FIRMWARE_H_
#define _FIRMWARE_H_

#include <stdint.h>
#include "keyboard.h"

#define TX_POWER             0
#define KEY_INDEX_NUM        20
#define SCAN_DELAY           25 /* Less than 25ms make keyboard become errornous because Bluetooth cannot catch up with scanning speed. */
#define KEY_PRESS_DEBOUNCE   5
#define KEY_RELEASE_DEBOUNCE 3

void setup_matrix(void);
bool scan_matrix(void);
void update_key_index(int8_t index, uint8_t source);

#ifdef MASTER
void translate_key_index(void);
#endif

#endif
