#ifndef _FIRMWARE_H_
#define _FIRMWARE_H_

#include <stdint.h>
#include "keyboard.h"

#define TX_POWER             0
#define KEY_NUM              20
#define SCAN_DELAY           8
#define KEY_PRESS_DEBOUNCE   10
#define KEY_RELEASE_DEBOUNCE 15
#define SLAVE_BUFFER_NUM     10

void setup_matrix(void);
bool scan_matrix(void);
void update_key_index(int8_t index, uint8_t source);

#ifdef MASTER
void translate_key_index(void);
void generate_send_key_report(void);

#ifdef HAS_SLAVE
extern bool clear_slave;

void clear_slave_index_and_buffer(void);
void setup_slave_buffer(void);
void add_slave_key_index_to_buffer(int8_t index);
void process_slave_buffer(void);
#endif
#endif

#endif
