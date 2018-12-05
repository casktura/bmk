#ifndef _BLE_FIRMWARE_H_
#define _BLE_FIRMWARE_H_

#include <bluefruit.h>
#include "firmware.h"
#include "keyboard.h"

void setup_ble(void);
void start_adv(void);

#ifdef MASTER
void send_key_report(hid_keyboard_report_t *report);
#endif

#endif
