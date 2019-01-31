#ifndef _BLE_FIRMWARE_H_
#define _BLE_FIRMWARE_H_

#include <bluefruit.h>
#include "firmware.h"
#include "keyboard.h"

#define UUID_SVC_SLAVE_LINK  0xD2E52F8B564D4F11A4DE411FF1B6EAC6
#define UUID_CHR_SLAVE_INDEX 0xDAABAD40D802499782C1B518BD9D9FD3
#define UUID_NUM             2

void setup_ble(void);
void start_adv(void);

#ifdef MASTER
void send_key_report(hid_keyboard_report_t *report);
#endif

#ifdef SLAVE
void notify_key_index(int8_t *index);
#endif

#endif
