#include "ble_firmware.h"

BLEDis ble_dis;

#ifdef MASTER
BLEHidAdafruit ble_hid;

#ifdef HAS_SLAVE
/* Slave link */
BLEClientService svc_slave_link = BLEClientService(UUID_SVC_SLAVE_LINK);
BLEClientCharacteristic chr_slave_index = BLEClientCharacteristic(UUID_CHR_SLAVE_INDEX);

void slave_notify_callback(BLEClientCharacteristic *chr, uint8_t *data, uint16_t len);
void slave_scan_callback(ble_gap_evt_adv_report_t *report);
void slave_connect_callback(uint16_t conn_hdl);
void slave_disconnect_callback(uint16_t conn_hdl, uint8_t reason);
#endif

bool sent_key_release = true;
#endif

#ifdef SLAVE
BLEService svc_link = BLEService(UUID_SVC_SLAVE_LINK);
BLECharacteristic chr_index = BLECharacteristic(UUID_CHR_SLAVE_INDEX);
#endif

void setup_ble() {
    Bluefruit.configUuid128Count(UUID_NUM);
    Bluefruit.configServiceChanged(true);
    Bluefruit.begin(PERIPHERAL_NUM, CENTRAL_NUM);
    /* Adafruit: Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4 */
    Bluefruit.setTxPower(TX_POWER);
    Bluefruit.setName(DEVICE_NAME);
    Bluefruit.setApperance(BLE_APPEARANCE_HID_KEYBOARD);

    ble_dis.setManufacturer(MANUFACTURER_NAME);
    ble_dis.setModel(DEVICE_MODEL);
    ble_dis.begin();

#ifdef MASTER
#ifdef HAS_SLAVE
    /* Setup slave link */
    svc_slave_link.begin();

    chr_slave_index.setNotifyCallback(slave_notify_callback);
    chr_slave_index.begin();

    /* Scan for slave setting */
    Bluefruit.Scanner.setRxCallback(slave_scan_callback);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.filterRssi(-80);                                       // Scan only near device
    Bluefruit.Scanner.filterUuid(BLEUART_UUID_SERVICE, UUID_SVC_SLAVE_LINK); // Look for only this 2 UUIDs
    Bluefruit.Scanner.setInterval(160, 80);                                  // In unit of 0.625 ms; interval = 100 ms, window = 50 ms
    Bluefruit.Scanner.useActiveScan(false);                                  // If true, will fetch scan response data
    Bluefruit.Scanner.start(120);                                            // Stop scanning after 120 s (or 2 minutes)

    /* On connect to slave */
    Bluefruit.Central.setConnectCallback(slave_connect_callback);
    Bluefruit.Central.setDisconnectCallback(slave_disconnect_callback);
#endif

    ble_hid.begin();
#endif

#ifdef SLAVE
    svc_link.begin();

    chr_index.setProperties(CHR_PROPS_NOTIFY + CHR_PROPS_READ);
    chr_index.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    chr_index.setFixedLen(1);
    chr_index.setUserDescriptor("Key index");
    chr_index.begin();
    chr_index.write8(0); // Init with key index 0 (no key)
#endif

    start_adv();
}

void start_adv() {
    LOG_LV1("BMK", "Setup adv");

    /* Adafruit: Advertising packet */
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);

    /* Add device name in scan response packet */
    Bluefruit.ScanResponse.addName();

#ifdef MASTER
    /* Adafruit: Include BLE HID service */
    Bluefruit.Advertising.addService(ble_hid);
#endif

#ifdef SLAVE
    Bluefruit.Advertising.addUuid(UUID_SVC_SLAVE_LINK);
    Bluefruit.Advertising.addUuid(UUID_CHR_SLAVE_INDEX);
    Bluefruit.Advertising.addService(svc_link);
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

#ifdef HAS_SLAVE
void slave_notify_callback(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len > 0) {
        if (chr->uuid == chr_slave_index.uuid) {
            /* Got key index from slave side, update it to key_index */
            /* TODO: Should get source from notification too? */
            update_key_index((int8_t) data[0], SOURCE_SLAVE);
        }
    }
}

void slave_scan_callback(ble_gap_evt_adv_report_t *adv_report) {
    if (Bluefruit.Scanner.checkReportForService(adv_report, svc_slave_link)) {
        LOG_LV2("BLE", "Found slave");

        Bluefruit.Central.connect(adv_report);
    }
}

void slave_connect_callback(uint16_t conn_hdl) {
    LOG_LV2("BLE", "Connecting to slave");

    if (svc_slave_link.discover(conn_hdl)) {
        if (chr_slave_index.discover()) {
            chr_slave_index.enableNotify();
        }
    } else {
        LOG_LV2("BLE", "Slave connection, service not found. Disconnecting");

        Bluefruit.Central.disconnect(conn_hdl);
    }
}

void slave_disconnect_callback(uint16_t conn_hdl, uint8_t reason) {
    /* Clear all registered key index from slave */
    /* TODO: Check for slave source then clear the correct one */
    clear_key_index_from_source(SOURCE_SLAVE);

    (void) conn_hdl;
    (void) reason;

    LOG_LV2("BLE", "Slave disconnected");
}
#endif
#endif

#ifdef SLAVE
void notify_key_index(int8_t *index) {
    LOG_LV2("BLE", "Notify key index to master");

    chr_index.notify(index, 1);
}
#endif
