#include "ble_firmware.h"

BLEDis ble_dis;

#ifdef MASTER
BLEHidAdafruit ble_hid;

bool sent_key_release = true;
#endif

void setup_ble() {
    Bluefruit.begin();
    /* Adafruit: Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4 */
    Bluefruit.setTxPower(TX_POWER);
    Bluefruit.setName(DEVICE_NAME);
    Bluefruit.setApperance(BLE_APPEARANCE_HID_KEYBOARD);

    ble_dis.setManufacturer(MANUFACTURER_NAME);
    ble_dis.setModel(DEVICE_MODEL);
    ble_dis.begin();

#ifdef MASTER
    ble_hid.begin();
#endif

    start_adv();
}

void start_adv() {
    LOG_LV1("BMK", "Setup adv");

    /* Adafruit: Advertising packet */
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);

    /* Adafruit: There is enough room for the dev name in the advertising packet */
    Bluefruit.Advertising.addName();

#ifdef MASTER
    /* Adafruit: Include BLE HID service */
    Bluefruit.Advertising.addService(ble_hid);
#endif

    /*
     * Adafruit:
     * Start Advertising
     * - Enable auto advertising if disconnected
     * - Interval: fast mode = 20 ms, slow mode = 152.5 ms
     * - Timeout for fast mode is 30 seconds
     * - Start(timeout) with timeout = 0 will advertise forever (until connected)
     *
     * For recommended advertising interval
     * https://developer.apple.com/library/content/qa/qa1931/_index.html
     */
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244); // Adafruit: in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);   // Adafruit: number of seconds in fast mode
    Bluefruit.Advertising.start(0);             // Adafruit: 0 = Don't stop advertising after n seconds
}

#ifdef MASTER
void send_key_report(hid_keyboard_report_t *report) {
    if (report->modifier == 0 && report->keycode[0] == 0) {
        if (!sent_key_release) {
            LOG_LV2("HIDRL", "Send key release");

            sent_key_release = true;
            ble_hid.keyRelease();
        }
    } else {
        LOG_LV2("HIDRP", "M:%u K:%u %u %u %u %u %u", report->modifier, report->keycode[0], report->keycode[1], report->keycode[2], report->keycode[3], report->keycode[4], report->keycode[5]);

        sent_key_release = false;
        ble_hid.keyboardReport(report);
    }
}
#endif
