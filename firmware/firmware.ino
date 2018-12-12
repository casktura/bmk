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
bool has_key_translate_pending = false;
bool has_key_send_pending = false;

#ifdef HAS_SLAVE
typedef struct slave_buffer_s {
    struct slave_buffer_s *next;
    int8_t *buffer;
    int num;
} slave_buffer_t;

slave_buffer_t *slave_buffer;

bool clear_slave = false;
bool has_slave_buffer = false;
#endif
#endif

void setup() {
    Serial.begin(115200);

    LOG_LV1("BMK", "Starting");

    /* Adafruit: For nrf52840 with native usb. */
    while (!Serial) {
        delay(10);
    }

#ifdef HAS_SLAVE
    setup_slave_buffer();
#endif

    setup_matrix();
    setup_ble();
}

void loop() {
    scan_matrix();

#ifdef MASTER
#ifdef HAS_SLAVE
    if (clear_slave) {
        clear_slave_index_and_buffer();
    }

    if (has_slave_buffer) {
        process_slave_buffer();
    }
#endif

    if (has_key_translate_pending) {
        translate_key_index();
    }

    if (has_key_send_pending) {
        generate_send_key_report();
    }
#endif

    delay(SCAN_DELAY);
}

void setup_matrix() {
    LOG_LV1("BMK", "Setup matrix");

    for (const uint8_t &row : ROWS) {
        pinMode(row, INPUT_PULLUP);
    }

    for (const uint8_t &col : COLS) {
        pinMode(col, INPUT_PULLUP);
    }
}

bool scan_matrix() {
    /*
     * TODO: For future optimization, put keyboard into sleep mode after no action for a period of time, or extend SCAN_DELAY.
     */
    bool has_key = false;
    uint32_t pin_data = 0;

    for (int i = 0; i < MATRIX_ROW_NUM; i++) {
        pinMode(ROWS[i], OUTPUT);
        digitalWrite(ROWS[i], LOW);

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

        pinMode(ROWS[i], INPUT_PULLUP);
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
        /* Has new key press, needs translation */
        has_key_translate_pending = true;

        keys[next_key++] = key;
    } else if (next_key > 0 && key.index < 0) {
        /* Has key release, can send it to computer directly. Don't have to translate again */
        has_key_send_pending = true;

        int i = 0;
        key.index = -key.index;

        while (i < next_key) {
            while (keys[i].index != key.index || keys[i].source != key.source) {
                i++;
            }

            if (i < next_key) {
                for (i; i < next_key - 1; i++) {
                    keys[i] = keys[i + 1];
                }

                keys[--next_key] = {0};
            }
        }
    }

    LOG_LV2("KINDEX", "NI:%i", next_key);
#endif

#ifdef SLAVE
    notify_key_index(&index);
#endif
}

#ifdef MASTER
void translate_key_index() {
    has_key_translate_pending = false;
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

            has_key_send_pending = true;
            keys[i].translated = true;
            keys[i].has_modifiers = true;
            keys[i].modifiers = MOD_BIT(code);

            code = MOD_CODE(code);
        }

        if (IS_KEY(code)) {
            LOG_LV2("TKI", "Normal key");

            has_key_send_pending = true;
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

    has_key_send_pending = false;
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
void clear_slave_index_and_buffer() {
    LOG_LV2("KINDEX", "Clear all key index from slave");

    /* Reset clear slave flag */
    clear_slave = false;

    /* Set send key pending flag to release all key press from slave */
    has_key_send_pending = true;

    /* Reset slave buffer */
    has_slave_buffer = false;
    slave_buffer->num = 0;
    slave_buffer->next->num = 0;

    for (int i = 0; i < next_key; i++) {
        if (keys[i].source == SOURCE_SLAVE) {
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
}

void setup_slave_buffer() {
    LOG_LV2("BMK", "Setup slave buffer");

    slave_buffer_t *buffer_a = new slave_buffer_t{0};
    buffer_a->buffer = new int8_t[SLAVE_BUFFER_NUM];

    slave_buffer_t *buffer_b = new slave_buffer_t{0};
    buffer_b->buffer = new int8_t[SLAVE_BUFFER_NUM];

    buffer_a->next = buffer_b;
    buffer_b->next = buffer_a;

    slave_buffer = buffer_a;
}

void add_slave_key_index_to_buffer(int8_t index) {
    LOG_LV2("ADDSKI", "I:%i", index);

    has_slave_buffer = true;
    if (slave_buffer->num < SLAVE_BUFFER_NUM) {
        slave_buffer->buffer[slave_buffer->num++] = index;
    }
}

void process_slave_buffer() {
    LOG_LV2("BMK", "Process slave buffer");

    has_slave_buffer = false;
    slave_buffer = slave_buffer->next;

    slave_buffer_t *buffer = slave_buffer->next;
    for (int i = 0; i < buffer->num; i++) {
        update_key_index(buffer->buffer[i], SOURCE_SLAVE);
    }

    buffer->num = 0;
}
#endif
#endif
