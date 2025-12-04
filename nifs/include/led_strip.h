// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
* @brief LED Strip Type
*
*/
typedef struct led_strip_s led_strip_t;

/**
* @brief Declare of LED Strip Type
*
*/
struct led_strip_s {
    /**
    * @brief Set RGB for a specific pixel
    *
    * @param strip: LED strip
    * @param index: index of pixel to set
    * @param red: red part of color
    * @param green: green part of color
    * @param blue: blue part of color
    *
    * @return
    *      - ESP_OK: Set RGB for a specific pixel successfully
    *      - ESP_ERR_INVALID_ARG: Set RGB for a specific pixel failed because of invalid parameters
    *      - ESP_FAIL: Set RGB for a specific pixel failed because other error occurred
    */
    esp_err_t (*set_pixel)(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);

    /**
    * @brief Set RGBW for a specific pixel (for RGBW strips like SK6812)
    *
    * @param strip: LED strip
    * @param index: index of pixel to set
    * @param red: red part of color
    * @param green: green part of color
    * @param blue: blue part of color
    * @param white: white part of color
    *
    * @return
    *      - ESP_OK: Set RGBW for a specific pixel successfully
    *      - ESP_ERR_INVALID_ARG: Set RGBW failed because of invalid parameters
    *      - ESP_ERR_NOT_SUPPORTED: Strip is not RGBW type
    *      - ESP_FAIL: Set RGBW for a specific pixel failed because other error occurred
    */
    esp_err_t (*set_pixel_rgbw)(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);

    /**
    * @brief Refresh memory colors to LEDs
    *
    * @param strip: LED strip
    * @param timeout_ms: timeout value for refreshing task
    *
    * @return
    *      - ESP_OK: Refresh successfully
    *      - ESP_ERR_TIMEOUT: Refresh failed because of timeout
    *      - ESP_FAIL: Refresh failed because some other error occurred
    *
    * @note:
    *      After updating the LED colors in the memory, a following invocation of this API is needed to flush colors to strip.
    */
    esp_err_t (*refresh)(led_strip_t *strip, uint32_t timeout_ms);

    /**
    * @brief Clear LED strip (turn off all LEDs)
    *
    * @param strip: LED strip
    * @param timeout_ms: timeout value for clearing task
    *
    * @return
    *      - ESP_OK: Clear LEDs successfully
    *      - ESP_ERR_TIMEOUT: Clear LEDs failed because of timeout
    *      - ESP_FAIL: Clear LEDs failed because some other error occurred
    */
    esp_err_t (*clear)(led_strip_t *strip, uint32_t timeout_ms);

    /**
    * @brief Free LED strip resources
    *
    * @param strip: LED strip
    *
    * @return
    *      - ESP_OK: Free resources successfully
    *      - ESP_FAIL: Free resources failed because error occurred
    */
    esp_err_t (*del)(led_strip_t *strip);

    /**
    * @brief Set global brightness for the strip
    *
    * @param strip: LED strip
    * @param brightness: brightness value (0-255)
    *
    * @return
    *      - ESP_OK: Set brightness successfully
    */
    esp_err_t (*set_brightness)(led_strip_t *strip, uint8_t brightness);

    /**
    * @brief Get current global brightness
    *
    * @param strip: LED strip
    *
    * @return current brightness value (0-255)
    */
    uint8_t (*get_brightness)(led_strip_t *strip);
};

/**
* @brief LED Strip Type (RGB vs RGBW)
*/
typedef enum {
    LED_STRIP_RGB = 0,   /*!< RGB LEDs (WS2812, WS2812B) - 3 bytes per pixel */
    LED_STRIP_RGBW = 1,  /*!< RGBW LEDs (SK6812) - 4 bytes per pixel */
} led_strip_type_t;

/**
* @brief LED Strip Configuration Type
*
*/
typedef struct {
    uint32_t max_leds;        /*!< Maximum LEDs in a single strip */
    int gpio_num;             /*!< GPIO number */
    uint8_t brightness;       /*!< Global brightness (0-255), default 255 */
    led_strip_type_t led_type; /*!< LED type (RGB or RGBW), default RGB */
} led_strip_config_t;

/**
* @brief Install a new ws2812 driver (based on RMT peripheral)
*
* @param config: LED strip configuration
* @return
*      LED strip instance or NULL
*/
led_strip_t *led_strip_new_rmt_ws2812(const led_strip_config_t *config);

void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b);

#ifdef __cplusplus
}
#endif
