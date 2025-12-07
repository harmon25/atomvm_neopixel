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

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "ws2812";

#define STRIP_CHECK(a, str, goto_tag, ret_value, ...)                             \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

#define WS2812_T0H_NS (350)
#define WS2812_T0L_NS (1000)
#define WS2812_T1H_NS (1000)
#define WS2812_T1L_NS (350)
#define WS2812_RESET_US (280)

static uint32_t ws2812_t0h_ticks = 0;
static uint32_t ws2812_t1h_ticks = 0;
static uint32_t ws2812_t0l_ticks = 0;
static uint32_t ws2812_t1l_ticks = 0;

typedef struct {
    led_strip_t parent;
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t rmt_encoder;
    uint32_t strip_len;
    uint8_t brightness;
    uint8_t bytes_per_pixel;  // 3 for RGB, 4 for RGBW
    led_strip_type_t led_type;
    uint8_t *out_buf;      // Brightness-scaled output buffer
    uint8_t buffer[0];     // Raw RGB/RGBW values (flexible array member)
} ws2812_t;

// LED strip encoder
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                    const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    
    switch (led_encoder->state) {
    case 0: // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t *led_encoder = NULL;
    
    led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));
    STRIP_CHECK(led_encoder, "allocate memory for led strip encoder failed", err, ESP_ERR_NO_MEM);
    
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = ws2812_t0h_ticks,
            .level1 = 0,
            .duration1 = ws2812_t0l_ticks,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = ws2812_t1h_ticks,
            .level1 = 0,
            .duration1 = ws2812_t1l_ticks,
        },
        .flags.msb_first = 1
    };
    
    STRIP_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder) == ESP_OK,
                "create bytes encoder failed", err, ESP_FAIL);
    
    rmt_copy_encoder_config_t copy_encoder_config = {};
    STRIP_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder) == ESP_OK,
                "create copy encoder failed", err, ESP_FAIL);
    
    uint32_t reset_ticks = WS2812_RESET_US * 40; // 40MHz resolution
    led_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
    
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}

static esp_err_t ws2812_set_pixel(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    STRIP_CHECK(index < ws2812->strip_len, "index out of the maximum number of leds", err, ESP_ERR_INVALID_ARG);
    
    // Store raw RGB values - brightness applied at refresh time
    uint32_t start = index * ws2812->bytes_per_pixel;
    // In the order of GRB(W)
    ws2812->buffer[start + 0] = green & 0xFF;
    ws2812->buffer[start + 1] = red & 0xFF;
    ws2812->buffer[start + 2] = blue & 0xFF;
    if (ws2812->bytes_per_pixel == 4) {
        ws2812->buffer[start + 3] = 0; // White channel defaults to 0 for RGB calls
    }
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_set_pixel_rgbw(led_strip_t *strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    STRIP_CHECK(ws2812->led_type == LED_STRIP_RGBW, "set_pixel_rgbw called on non-RGBW strip", err, ESP_ERR_NOT_SUPPORTED);
    STRIP_CHECK(index < ws2812->strip_len, "index out of the maximum number of leds", err, ESP_ERR_INVALID_ARG);
    
    // Store raw RGBW values - brightness applied at refresh time
    uint32_t start = index * 4;
    // In the order of GRBW
    ws2812->buffer[start + 0] = green & 0xFF;
    ws2812->buffer[start + 1] = red & 0xFF;
    ws2812->buffer[start + 2] = blue & 0xFF;
    ws2812->buffer[start + 3] = white & 0xFF;
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_refresh(led_strip_t *strip, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    uint32_t buf_size = ws2812->strip_len * ws2812->bytes_per_pixel;
    uint8_t *tx_buf;
    
    // Apply brightness scaling to output buffer
    uint8_t br = ws2812->brightness;
    if (br < 255) {
        uint8_t *src = ws2812->buffer;
        uint8_t *dst = ws2812->out_buf;
        uint16_t scale = br + 1;
        
        // Process 4 bytes at a time when possible (common case for RGBW, 
        // and RGB strips with pixel count divisible by 4/3)
        uint32_t i = 0;
        uint32_t fast_end = buf_size & ~3U;  // Round down to multiple of 4
        for (; i < fast_end; i += 4) {
            dst[i + 0] = (src[i + 0] * scale) >> 8;
            dst[i + 1] = (src[i + 1] * scale) >> 8;
            dst[i + 2] = (src[i + 2] * scale) >> 8;
            dst[i + 3] = (src[i + 3] * scale) >> 8;
        }
        // Handle remaining bytes
        for (; i < buf_size; i++) {
            dst[i] = (src[i] * scale) >> 8;
        }
        tx_buf = ws2812->out_buf;
    } else {
        // Full brightness - transmit raw buffer directly (no copy needed)
        tx_buf = ws2812->buffer;
    }
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,  // End of transmission level low (reset state)
        },
    };
    
    // Temporarily boost task priority during transmission to reduce WiFi interference
    UBaseType_t orig_priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
    
    esp_err_t tx_err = rmt_transmit(ws2812->rmt_chan, ws2812->rmt_encoder, tx_buf, buf_size, &tx_config);
    if (tx_err == ESP_OK) {
        tx_err = rmt_tx_wait_all_done(ws2812->rmt_chan, timeout_ms);
    }
    
    // Restore original priority
    vTaskPrioritySet(NULL, orig_priority);
    
    STRIP_CHECK(tx_err == ESP_OK, "RMT transmission failed", err, ESP_FAIL);
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_clear(led_strip_t *strip, uint32_t timeout_ms)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    memset(ws2812->buffer, 0, ws2812->strip_len * ws2812->bytes_per_pixel);
    return ws2812_refresh(strip, timeout_ms);
}

static esp_err_t ws2812_set_brightness(led_strip_t *strip, uint8_t brightness)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    ws2812->brightness = brightness;
    return ESP_OK;
}

static uint8_t ws2812_get_brightness(led_strip_t *strip)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    return ws2812->brightness;
}

static esp_err_t ws2812_fill(led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    uint8_t bytes_per_pixel = ws2812->bytes_per_pixel;
    uint32_t strip_len = ws2812->strip_len;
    uint8_t *buf = ws2812->buffer;
    
    // Pre-compute the GRB(W) values once
    uint8_t g = green & 0xFF;
    uint8_t r = red & 0xFF;
    uint8_t b = blue & 0xFF;
    
    if (bytes_per_pixel == 3) {
        // RGB: write 3 bytes per pixel
        for (uint32_t i = 0; i < strip_len; i++) {
            uint32_t idx = i * 3;
            buf[idx + 0] = g;
            buf[idx + 1] = r;
            buf[idx + 2] = b;
        }
    } else {
        // RGBW: write 4 bytes per pixel, W=0
        for (uint32_t i = 0; i < strip_len; i++) {
            uint32_t idx = i * 4;
            buf[idx + 0] = g;
            buf[idx + 1] = r;
            buf[idx + 2] = b;
            buf[idx + 3] = 0;
        }
    }
    return ESP_OK;
}

static esp_err_t ws2812_fill_rgbw(led_strip_t *strip, uint32_t red, uint32_t green, uint32_t blue, uint32_t white)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    
    if (ws2812->led_type != LED_STRIP_RGBW) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    uint32_t strip_len = ws2812->strip_len;
    uint8_t *buf = ws2812->buffer;
    
    // Pre-compute the GRBW values once
    uint8_t g = green & 0xFF;
    uint8_t r = red & 0xFF;
    uint8_t b = blue & 0xFF;
    uint8_t w = white & 0xFF;
    
    for (uint32_t i = 0; i < strip_len; i++) {
        uint32_t idx = i * 4;
        buf[idx + 0] = g;
        buf[idx + 1] = r;
        buf[idx + 2] = b;
        buf[idx + 3] = w;
    }
    return ESP_OK;
}

static esp_err_t ws2812_del(led_strip_t *strip)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    
    if (ws2812->rmt_encoder) {
        rmt_del_encoder(ws2812->rmt_encoder);
    }
    if (ws2812->rmt_chan) {
        rmt_disable(ws2812->rmt_chan);
        rmt_del_channel(ws2812->rmt_chan);
    }
    if (ws2812->out_buf) {
        free(ws2812->out_buf);
    }
    free(ws2812);
    return ESP_OK;
}

led_strip_t *led_strip_new_rmt_ws2812(const led_strip_config_t *config)
{
    led_strip_t *ret = NULL;
    ws2812_t *ws2812 = NULL;
    uint8_t *out_buf = NULL;
    
    STRIP_CHECK(config, "configuration can't be null", err, NULL);

    // Determine bytes per pixel based on LED type
    uint8_t bytes_per_pixel = (config->led_type == LED_STRIP_RGBW) ? 4 : 3;
    
    // Allocate buffer based on LED type (3 bytes for RGB, 4 bytes for RGBW)
    uint32_t buf_size = config->max_leds * bytes_per_pixel;
    uint32_t ws2812_size = sizeof(ws2812_t) + buf_size;
    ws2812 = calloc(1, ws2812_size);
    STRIP_CHECK(ws2812, "request memory for ws2812 failed", err, NULL);
    
    // Allocate output buffer for brightness-scaled data
    out_buf = malloc(buf_size);
    STRIP_CHECK(out_buf, "request memory for output buffer failed", err, NULL);
    ws2812->out_buf = out_buf;

    // Configurable memory block size - larger values improve WiFi coexistence
#ifdef CONFIG_AVM_NEOPIXEL_RMT_MEM_BLOCK_SYMBOLS
    uint32_t mem_block_symbols = CONFIG_AVM_NEOPIXEL_RMT_MEM_BLOCK_SYMBOLS;
#else
    uint32_t mem_block_symbols = 192;
#endif

    // DMA mode enables hardware-driven transfers that are interrupt-resistant
#ifdef CONFIG_AVM_NEOPIXEL_USE_DMA
    bool use_dma = CONFIG_AVM_NEOPIXEL_USE_DMA;
#else
    bool use_dma = true;  // Default to DMA enabled
#endif

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = (gpio_num_t)config->gpio_num,
        .mem_block_symbols = mem_block_symbols,
        .resolution_hz = 40000000, // 40MHz
        .trans_queue_depth = 8,    // Increased from 4 for better buffering
        .flags = {
            .with_dma = use_dma ? 1 : 0,
        },
    };
    
    esp_err_t chan_err = rmt_new_tx_channel(&tx_chan_config, &ws2812->rmt_chan);
    if (chan_err != ESP_OK && use_dma) {
        // Fallback: some ESP32 variants don't support DMA, try without it
        ESP_LOGW(TAG, "DMA mode failed, falling back to non-DMA mode");
        tx_chan_config.flags.with_dma = 0;
        tx_chan_config.mem_block_symbols = mem_block_symbols > 128 ? 128 : mem_block_symbols;
        chan_err = rmt_new_tx_channel(&tx_chan_config, &ws2812->rmt_chan);
    }
    STRIP_CHECK(chan_err == ESP_OK, "create RMT TX channel failed", err, NULL);

    // Calculate timing ticks (40MHz resolution)
    uint32_t counter_clk_hz = 40000000;
    float ratio = (float)counter_clk_hz / 1e9;
    ws2812_t0h_ticks = (uint32_t)(ratio * WS2812_T0H_NS);
    ws2812_t0l_ticks = (uint32_t)(ratio * WS2812_T0L_NS);
    ws2812_t1h_ticks = (uint32_t)(ratio * WS2812_T1H_NS);
    ws2812_t1l_ticks = (uint32_t)(ratio * WS2812_T1L_NS);

    STRIP_CHECK(rmt_new_led_strip_encoder(&ws2812->rmt_encoder) == ESP_OK,
                "create led strip encoder failed", err, NULL);

    STRIP_CHECK(rmt_enable(ws2812->rmt_chan) == ESP_OK,
                "enable RMT TX channel failed", err, NULL);

    ws2812->strip_len = config->max_leds;
    ws2812->brightness = config->brightness ? config->brightness : 255;
    ws2812->bytes_per_pixel = bytes_per_pixel;
    ws2812->led_type = config->led_type;
    ws2812->parent.set_pixel = ws2812_set_pixel;
    ws2812->parent.set_pixel_rgbw = ws2812_set_pixel_rgbw;
    ws2812->parent.refresh = ws2812_refresh;
    ws2812->parent.clear = ws2812_clear;
    ws2812->parent.del = ws2812_del;
    ws2812->parent.set_brightness = ws2812_set_brightness;
    ws2812->parent.get_brightness = ws2812_get_brightness;
    ws2812->parent.fill = ws2812_fill;
    ws2812->parent.fill_rgbw = ws2812_fill_rgbw;

    return &ws2812->parent;
err:
    if (out_buf) {
        free(out_buf);
    }
    if (ws2812) {
        free(ws2812);
    }
    return ret;
}

void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360;
    // Use integer math: rgb_max = v * 255 / 100 (avoiding float 2.55f)
    uint32_t rgb_max = (v * 255 + 50) / 100;  // +50 for rounding
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
