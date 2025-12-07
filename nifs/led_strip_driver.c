//
// Copyright (c) 2021-2024 dushin.net
// All rights reserved.
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
//
// This implementation uses the ESP-IDF led_strip component for better
// WiFi coexistence and simpler maintenance.
//

#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"

// ESP-IDF led_strip component
#include "led_strip.h"

// Our internal interface
#include "atomvm_led_strip.h"

static const char *TAG = "avm_led_strip";

// Internal wrapper structure
typedef struct {
    avm_led_strip_t parent;              // Our interface (function pointers)
    led_strip_handle_t idf_strip;        // ESP-IDF led_strip handle
    uint32_t strip_len;
    uint8_t brightness;
    uint8_t bytes_per_pixel;
    avm_led_strip_type_t led_type;
    uint8_t *pixel_buf;                  // Raw pixel values (before brightness)
} strip_wrapper_t;

// Forward declarations
static esp_err_t wrapper_set_pixel(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
static esp_err_t wrapper_set_pixel_rgbw(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);
static esp_err_t wrapper_refresh(avm_led_strip_t *strip, uint32_t timeout_ms);
static esp_err_t wrapper_clear(avm_led_strip_t *strip, uint32_t timeout_ms);
static esp_err_t wrapper_del(avm_led_strip_t *strip);
static esp_err_t wrapper_set_brightness(avm_led_strip_t *strip, uint8_t brightness);
static uint8_t wrapper_get_brightness(avm_led_strip_t *strip);
static esp_err_t wrapper_fill(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue);
static esp_err_t wrapper_fill_rgbw(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);

// Apply brightness to a color value
static inline uint8_t apply_brightness(uint8_t value, uint8_t brightness)
{
    if (brightness == 255) return value;
    return (uint8_t)(((uint16_t)value * (brightness + 1)) >> 8);
}

static esp_err_t wrapper_set_pixel(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    
    if (index >= wrapper->strip_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store raw values in our buffer
    uint32_t offset = index * wrapper->bytes_per_pixel;
    wrapper->pixel_buf[offset + 0] = red & 0xFF;
    wrapper->pixel_buf[offset + 1] = green & 0xFF;
    wrapper->pixel_buf[offset + 2] = blue & 0xFF;
    if (wrapper->bytes_per_pixel == 4) {
        wrapper->pixel_buf[offset + 3] = 0;
    }
    
    // Apply brightness and send to ESP-IDF driver
    uint8_t br = wrapper->brightness;
    return led_strip_set_pixel(wrapper->idf_strip, index, 
                               apply_brightness(red, br),
                               apply_brightness(green, br),
                               apply_brightness(blue, br));
}

static esp_err_t wrapper_set_pixel_rgbw(avm_led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    
    if (wrapper->led_type != AVM_LED_STRIP_RGBW) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (index >= wrapper->strip_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store raw values
    uint32_t offset = index * 4;
    wrapper->pixel_buf[offset + 0] = red & 0xFF;
    wrapper->pixel_buf[offset + 1] = green & 0xFF;
    wrapper->pixel_buf[offset + 2] = blue & 0xFF;
    wrapper->pixel_buf[offset + 3] = white & 0xFF;
    
    // Apply brightness and send to ESP-IDF driver
    uint8_t br = wrapper->brightness;
    return led_strip_set_pixel_rgbw(wrapper->idf_strip, index,
                                    apply_brightness(red, br),
                                    apply_brightness(green, br),
                                    apply_brightness(blue, br),
                                    apply_brightness(white, br));
}

static esp_err_t wrapper_refresh(avm_led_strip_t *strip, uint32_t timeout_ms)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    (void)timeout_ms;  // ESP-IDF driver doesn't use timeout
    
    return led_strip_refresh(wrapper->idf_strip);
}

static esp_err_t wrapper_clear(avm_led_strip_t *strip, uint32_t timeout_ms)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    (void)timeout_ms;
    
    // Clear our buffer
    memset(wrapper->pixel_buf, 0, wrapper->strip_len * wrapper->bytes_per_pixel);
    
    return led_strip_clear(wrapper->idf_strip);
}

static esp_err_t wrapper_del(avm_led_strip_t *strip)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    
    esp_err_t ret = led_strip_del(wrapper->idf_strip);
    
    if (wrapper->pixel_buf) {
        free(wrapper->pixel_buf);
    }
    free(wrapper);
    
    return ret;
}

static esp_err_t wrapper_set_brightness(avm_led_strip_t *strip, uint8_t brightness)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    wrapper->brightness = brightness;
    
    // Re-apply brightness to all pixels
    uint8_t br = brightness;
    for (uint32_t i = 0; i < wrapper->strip_len; i++) {
        uint32_t offset = i * wrapper->bytes_per_pixel;
        uint8_t r = wrapper->pixel_buf[offset + 0];
        uint8_t g = wrapper->pixel_buf[offset + 1];
        uint8_t b = wrapper->pixel_buf[offset + 2];
        
        if (wrapper->bytes_per_pixel == 4) {
            uint8_t w = wrapper->pixel_buf[offset + 3];
            led_strip_set_pixel_rgbw(wrapper->idf_strip, i,
                                     apply_brightness(r, br),
                                     apply_brightness(g, br),
                                     apply_brightness(b, br),
                                     apply_brightness(w, br));
        } else {
            led_strip_set_pixel(wrapper->idf_strip, i,
                               apply_brightness(r, br),
                               apply_brightness(g, br),
                               apply_brightness(b, br));
        }
    }
    
    return ESP_OK;
}

static uint8_t wrapper_get_brightness(avm_led_strip_t *strip)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    return wrapper->brightness;
}

static esp_err_t wrapper_fill(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    
    uint8_t br = wrapper->brightness;
    uint8_t r = apply_brightness(red, br);
    uint8_t g = apply_brightness(green, br);
    uint8_t b = apply_brightness(blue, br);
    
    // Store in our buffer and set in ESP-IDF driver
    for (uint32_t i = 0; i < wrapper->strip_len; i++) {
        uint32_t offset = i * wrapper->bytes_per_pixel;
        wrapper->pixel_buf[offset + 0] = red & 0xFF;
        wrapper->pixel_buf[offset + 1] = green & 0xFF;
        wrapper->pixel_buf[offset + 2] = blue & 0xFF;
        if (wrapper->bytes_per_pixel == 4) {
            wrapper->pixel_buf[offset + 3] = 0;
        }
        
        if (wrapper->bytes_per_pixel == 4) {
            led_strip_set_pixel_rgbw(wrapper->idf_strip, i, r, g, b, 0);
        } else {
            led_strip_set_pixel(wrapper->idf_strip, i, r, g, b);
        }
    }
    
    return ESP_OK;
}

static esp_err_t wrapper_fill_rgbw(avm_led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue, uint32_t white)
{
    strip_wrapper_t *wrapper = __containerof(strip, strip_wrapper_t, parent);
    
    if (wrapper->led_type != AVM_LED_STRIP_RGBW) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    uint8_t br = wrapper->brightness;
    uint8_t r = apply_brightness(red, br);
    uint8_t g = apply_brightness(green, br);
    uint8_t b = apply_brightness(blue, br);
    uint8_t w = apply_brightness(white, br);
    
    for (uint32_t i = 0; i < wrapper->strip_len; i++) {
        uint32_t offset = i * 4;
        wrapper->pixel_buf[offset + 0] = red & 0xFF;
        wrapper->pixel_buf[offset + 1] = green & 0xFF;
        wrapper->pixel_buf[offset + 2] = blue & 0xFF;
        wrapper->pixel_buf[offset + 3] = white & 0xFF;
        
        led_strip_set_pixel_rgbw(wrapper->idf_strip, i, r, g, b, w);
    }
    
    return ESP_OK;
}

avm_led_strip_t *avm_led_strip_new(const avm_led_strip_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration cannot be null");
        return NULL;
    }
    
    uint8_t bytes_per_pixel = (config->led_type == AVM_LED_STRIP_RGBW) ? 4 : 3;
    
    // Allocate wrapper structure
    strip_wrapper_t *wrapper = calloc(1, sizeof(strip_wrapper_t));
    if (!wrapper) {
        ESP_LOGE(TAG, "Failed to allocate wrapper");
        return NULL;
    }
    
    // Allocate pixel buffer
    wrapper->pixel_buf = calloc(config->max_leds, bytes_per_pixel);
    if (!wrapper->pixel_buf) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        free(wrapper);
        return NULL;
    }
    
    // Configure ESP-IDF led_strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = config->gpio_num,
        .max_leds = config->max_leds,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = (config->led_type == AVM_LED_STRIP_RGBW) ? LED_STRIP_COLOR_COMPONENT_FMT_GRBW : LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    
    // Try RMT backend first (with DMA on supported chips)
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };
    
    // Enable DMA on chips that support it
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C6
    rmt_config.flags.with_dma = true;
    ESP_LOGI(TAG, "Using RMT with DMA");
#elif CONFIG_IDF_TARGET_ESP32
    // Original ESP32: use larger memory block for better WiFi coexistence
    rmt_config.mem_block_symbols = 192;
    ESP_LOGI(TAG, "Using RMT without DMA (ESP32), mem_block_symbols=%d", (int)rmt_config.mem_block_symbols);
#else
    ESP_LOGI(TAG, "Using RMT without DMA");
#endif
    
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &wrapper->idf_strip);
    
    // If RMT fails, try SPI backend (better for WiFi coexistence on all chips)
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT backend failed (err=%d), trying SPI backend", ret);
        
        led_strip_spi_config_t spi_config = {
            .spi_bus = SPI2_HOST,
            .flags.with_dma = true,
        };
        
        ret = led_strip_new_spi_device(&strip_config, &spi_config, &wrapper->idf_strip);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Using SPI backend with DMA");
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %d", ret);
        free(wrapper->pixel_buf);
        free(wrapper);
        return NULL;
    }
    
    // Initialize wrapper
    wrapper->strip_len = config->max_leds;
    wrapper->brightness = config->brightness ? config->brightness : 255;
    wrapper->bytes_per_pixel = bytes_per_pixel;
    wrapper->led_type = config->led_type;
    
    // Set up function pointers
    wrapper->parent.set_pixel = wrapper_set_pixel;
    wrapper->parent.set_pixel_rgbw = wrapper_set_pixel_rgbw;
    wrapper->parent.refresh = wrapper_refresh;
    wrapper->parent.clear = wrapper_clear;
    wrapper->parent.del = wrapper_del;
    wrapper->parent.set_brightness = wrapper_set_brightness;
    wrapper->parent.get_brightness = wrapper_get_brightness;
    wrapper->parent.fill = wrapper_fill;
    wrapper->parent.fill_rgbw = wrapper_fill_rgbw;
    
    ESP_LOGI(TAG, "LED strip initialized: %lu LEDs, %s", 
             (unsigned long)config->max_leds,
             config->led_type == AVM_LED_STRIP_RGBW ? "RGBW" : "RGB");
    
    return &wrapper->parent;
}

void avm_led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360;
    uint32_t rgb_max = (v * 255 + 50) / 100;
    uint32_t rgb_min = rgb_max * (100 - s) / 100;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}
