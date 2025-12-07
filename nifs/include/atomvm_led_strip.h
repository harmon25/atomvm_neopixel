// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
// Copyright 2024 dushin.net (modifications for AtomVM)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal AtomVM LED strip interface
// This wraps the ESP-IDF led_strip component with our interface

#ifndef ATOMVM_LED_STRIP_H
#define ATOMVM_LED_STRIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief AtomVM LED Strip Type (forward declaration)
 */
typedef struct avm_led_strip_s avm_led_strip_t;

/**
 * @brief AtomVM LED Strip interface structure
 */
struct avm_led_strip_s {
    esp_err_t (*set_pixel)(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
    esp_err_t (*set_pixel_rgbw)(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);
    esp_err_t (*refresh)(avm_led_strip_t *strip, uint32_t timeout_ms);
    esp_err_t (*clear)(avm_led_strip_t *strip, uint32_t timeout_ms);
    esp_err_t (*del)(avm_led_strip_t *strip);
    esp_err_t (*set_brightness)(avm_led_strip_t *strip, uint8_t brightness);
    uint8_t (*get_brightness)(avm_led_strip_t *strip);
    esp_err_t (*fill)(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue);
    esp_err_t (*fill_rgbw)(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);
};

/**
 * @brief LED Strip Type (RGB vs RGBW)
 */
typedef enum {
    AVM_LED_STRIP_RGB = 0,   /*!< RGB LEDs (WS2812, WS2812B) - 3 bytes per pixel */
    AVM_LED_STRIP_RGBW = 1,  /*!< RGBW LEDs (SK6812) - 4 bytes per pixel */
} avm_led_strip_type_t;

/**
 * @brief AtomVM LED Strip Configuration
 */
typedef struct {
    uint32_t max_leds;           /*!< Maximum LEDs in a single strip */
    int gpio_num;                /*!< GPIO number */
    uint8_t brightness;          /*!< Global brightness (0-255), default 255 */
    avm_led_strip_type_t led_type; /*!< LED type (RGB or RGBW), default RGB */
} avm_led_strip_config_t;

/**
 * @brief Create a new LED strip driver
 *
 * Uses ESP-IDF led_strip component internally with automatic backend selection:
 * - ESP32-S3/C6: RMT with DMA (best performance)
 * - ESP32/C3: RMT without DMA, falls back to SPI if needed
 *
 * @param config LED strip configuration
 * @return LED strip instance or NULL on failure
 */
avm_led_strip_t *avm_led_strip_new(const avm_led_strip_config_t *config);

/**
 * @brief Convert HSV to RGB color space
 */
void avm_led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b);

#ifdef __cplusplus
}
#endif

#endif // ATOMVM_LED_STRIP_H
