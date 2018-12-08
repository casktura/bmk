#ifndef _FIRMWARE_H_
#define _FIRMWARE_H_

#include <stdint.h>
#include "keyboard.h"

#define TX_POWER             0
#define KEY_NUM              20
#define SCAN_DELAY           8
#define KEY_PRESS_DEBOUNCE   10
#define KEY_RELEASE_DEBOUNCE 15

void setup_matrix(void);
bool scan_matrix(void);
void update_key_index(int8_t index, uint8_t source);

#ifdef MASTER
void translate_key_index(void);
void generate_send_key_report(void);

#ifdef HAS_SLAVE
void clear_key_index_from_source(uint8_t source);
#endif
#endif

#endif
