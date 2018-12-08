#include <bluefruit.h>
#include <stdint.h>
#include "firmware.h"
#include "ble_firmware.h"
#include "keyboard.h"

#ifdef MASTER
#include "keymap.h"
#endif

uint8_t ROWS[] MATRIX_ROW_PINS;
uint8_t COLS[] MATRIX_COL_PINS;

/* Init matrix */
bool key_pressed[MATRIX_ROW_NUM][MATRIX_COL_NUM]{false};
uint32_t timestamp[MATRIX_ROW_NUM][MATRIX_COL_NUM]{0};

#ifdef MASTER
typedef struct {
    int8_t index;
    uint8_t source;
    bool translated;
    bool has_modifiers;
    bool is_key;
    uint8_t modifiers;
    uint8_t key;
} key_index_t;

key_index_t keys[KEY_NUM]{0};

int next_key = 0;
bool has_key_pending = false;
#endif

void setup() {
    Serial.begin(115200);

    LOG_LV1("BMK", "Starting");

    /* Adafruit: For nrf52840 with native usb. */
    while (!Serial) {
        delay(10);
    }

    setup_ble();
    setup_matrix();
}

void loop() {
    scan_matrix();

#ifdef MASTER
    if (has_key_pending) {
        generate_send_key_report();
    }
#endif

    delay(SCAN_DELAY);
}

void setup_matrix() {
    LOG_LV1("BMK", "Setup matrix");

    for (const uint8_t &row : ROWS) {
        pinMode(row, INPUT);
    }

    for (const uint8_t &col : COLS) {
        pinMode(col, INPUT);
    }
}

bool scan_matrix() {
    /*
     * TODO: For future optimization, put keyboard into sleep mode after no action for a period of time, or extend SCAN_DELAY.
     */
    bool has_key = false;
    uint32_t pin_data = 0;

    /* Set columns to INPUT_PULLUP */
    for (const uint8_t &col : COLS) {
        pinMode(col, INPUT_PULLUP);
    }

    for (int i = 0; i < MATRIX_ROW_NUM; i++) {
        pinMode(ROWS[i], OUTPUT);
        digitalWrite(ROWS[i], LOW);

        /* Delay a bit for pins to settle down before read pins input. */
        delay(1);

        pin_data = NRF_GPIO->IN;

        for (int j = 0; j < MATRIX_COL_NUM; j++) {
            bool pressed = ((pin_data >> COLS[j]) & 1) == 0;
            uint32_t current_millis = millis();

            if  (key_pressed[i][j] == pressed) {
                timestamp[i][j] = 0;
            } else {
                if (timestamp[i][j] == 0) {
                    timestamp[i][j] = current_millis;
                } else if (pressed && (current_millis - timestamp[i][j] > KEY_PRESS_DEBOUNCE)) {
                    /* On key press */
                    key_pressed[i][j] = true;
                    update_key_index(MATRIX[i][j], SOURCE);
                } else if (!pressed && current_millis - timestamp[i][j] > KEY_RELEASE_DEBOUNCE) {
                    /* On key release */
                    key_pressed[i][j] = false;
                    update_key_index(-MATRIX[i][j], SOURCE);
                }
            }

            if (pressed) {
                /* User interact with key press, activate has_key flag. */
                has_key = true;
            }
        }

        pinMode(ROWS[i], INPUT);
    }

    /* After scan, set columns back to INPUT to save power */
    for (const uint8_t &col : COLS) {
        pinMode(col, INPUT);
    }

    return has_key;
}

void update_key_index(int8_t index, uint8_t source) {
    LOG_LV2("KINDEX", "I:%i S:%u", index, source);

#ifdef MASTER
    key_index_t key{0};

    key.index = index;
    key.source = source;

    if (next_key < KEY_NUM && key.index > 0) {
        keys[next_key++] = key;
    } else if (next_key > 0 && key.index < 0) {
        int i = 0;
        key.index = -key.index;

        while (i < next_key && (keys[i].index != key.index || keys[i].source != key.source)) {
            i++;
        }

        if (i < next_key) {
            has_key_pending = true; // Has key release to send to computer

            for (i; i < next_key - 1; i++) {
                keys[i] = keys[i + 1];
            }

            keys[--next_key] = {0};
        }
    }

    LOG_LV2("KINDEX", "NI:%i", next_key);

    translate_key_index();
#endif

#ifdef SLAVE
    notify_key_index(&index);
#endif
}

#ifdef MASTER
void translate_key_index() {
    uint8_t layer = _BASE_LAYER;

    for (int i = 0; i < next_key; i++) {
        if (keys[i].translated) {
            continue;
        }

        int8_t index = keys[i].index - 1;
        uint32_t code = KEYMAP[layer][index];

        if (IS_LAYER(code)) {
            LOG_LV2("TKI", "Layer key");

            layer = LAYER(code);
            continue;
        }

        if (code == KC_TRANSPARENT) {
            LOG_LV2("TKI", "Transparent key");

            keys[i].translated = true;
            uint8_t temp_layer = layer;

            while (temp_layer >= 0 && KEYMAP[temp_layer][index] == KC_TRANSPARENT) {
                temp_layer--;
            }

            if (temp_layer < 0) {
                continue;
            } else {
                code = KEYMAP[temp_layer][index];
            }
        }

        if (IS_MOD(code)) {
            LOG_LV2("TKI", "Modifier key");

            has_key_pending = true;
            keys[i].translated = true;
            keys[i].has_modifiers = true;
            keys[i].modifiers = MOD_BIT(code);

            code = MOD_CODE(code);
        }

        if (IS_KEY(code)) {
            LOG_LV2("TKI", "Normal key");

            has_key_pending = true;
            keys[i].translated = true;
            keys[i].is_key = true;
            keys[i].key = code;

            continue;
        }
    }
}

void generate_send_key_report() {
    LOG_LV2("KEY", "Generating keyboard report");
    LOG_LV2("KEY", "NI:%i", next_key);

    has_key_pending = false;
    int report_index = 0;
    hid_keyboard_report_t report{0};

    for (int i = 0; i < next_key; i++) {
        if (keys[i].has_modifiers) {
            report.modifier |= keys[i].modifiers;
        }

        if (keys[i].is_key && report_index < 6) {
            report.keycode[report_index++] = keys[i].key;
        }
    }

    send_key_report(&report);
}

#ifdef HAS_SLAVE
void clear_key_index_from_source(uint8_t source) {
    LOG_LV2("KINDEX", "Clear all key index with S:%u, NI:%i", source, next_key);

    has_key_pending = true;

    for (int i = 0; i < next_key; i++) {
        if (keys[i].source == source) {
            keys[i] = {0};
        }
    }

    for (int i = 0; i < next_key; i++) {
        if (keys[i].source == 0) {
            int j = i + 1;

            while (j < next_key && keys[j].source == 0) {
                j++;
            }

            if (j < next_key) {
                keys[i] = keys[j];
            }
        }
    }

    while (next_key > 0 && keys[next_key - 1].source == 0) {
        next_key--;
    }

    LOG_LV2("KINDEX", "After clear KI:%i", next_key);
}
#endif
#endif
