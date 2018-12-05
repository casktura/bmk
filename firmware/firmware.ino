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
} key_index_t;

key_index_t key_index[KEY_INDEX_NUM]{0};

bool operator!=(const key_index_t &lhs, const key_index_t &rhs) {
    return lhs.index != rhs.index || lhs.source != rhs.source;
}

int next_index = 0;
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

            if (!key_pressed[i][j] && pressed) {
                /* On key press */
                if (timestamp[i][j] == 0) {
                    timestamp[i][j] = current_millis;
                } else if (current_millis - timestamp[i][j] > KEY_PRESS_DEBOUNCE) {
                    key_pressed[i][j] = true;
                    timestamp[i][j] = 0;
                    update_key_index(MATRIX[i][j], SOURCE);
                }
            } else if (key_pressed[i][j] && !pressed) {
                /* On key release */
                if (timestamp[i][j] == 0) {
                    timestamp[i][j] = current_millis;
                } else if (current_millis - timestamp[i][j] > KEY_RELEASE_DEBOUNCE) {
                    key_pressed[i][j] = false;
                    timestamp[i][j] = 0;
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
#ifdef MASTER
    key_index_t key{
        .index = index,
        .source = source
    };

    LOG_LV2("KINDEX", "I:%i S:%u", key.index, key.source);

    if (next_index < KEY_INDEX_NUM && key.index > 0) {
        key_index[next_index++] = key;
    } else if (next_index > 0 && key.index < 0) {
        int i = 0;
        key.index = -key.index;

        while (i < next_index && key_index[i] != key) {
            i++;
        }

        if (i < next_index) {
            for (i; i < next_index - 1; i++) {
                key_index[i] = key_index[i + 1];
            }

            key_index[--next_index] = {0};
        }
    }

    LOG_LV2("KINDEX", "KI:%i", next_index);

    translate_key_index();
#endif
}

#ifdef MASTER
void translate_key_index() {
    int report_index = 0;
    hid_keyboard_report_t report{0};
    uint8_t layer = _BASE_LAYER;

    for (int i = 0; i < next_index; i++) {
        uint8_t index = key_index[i].index - 1;
        uint32_t code = KEYMAP[layer][index];

        if (IS_LAYER(code)) {
            LOG_LV2("TKI", "Layer key");

            layer = LAYER(code);
            continue;
        }

        if (code == KC_TRANSPARENT) {
            LOG_LV2("TKI", "Transparent key");

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

            report.modifier |= MOD_BIT(code);
            code = MOD_CODE(code);
        }

        if (IS_KEY(code)) {
            LOG_LV2("TKI", "Normal key");

            if (report_index < 6) {
                report.keycode[report_index++] = code;
            }

            continue;
        }
    }

    send_key_report(&report);
}
#endif
