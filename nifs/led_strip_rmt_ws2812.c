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
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
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
    uint8_t buffer[0];
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
    
    // Apply brightness scaling
    uint8_t br = ws2812->brightness;
    if (br < 255) {
        red = (red * br) >> 8;
        green = (green * br) >> 8;
        blue = (blue * br) >> 8;
    }
    
    uint32_t start = index * 3;
    // In the order of GRB
    ws2812->buffer[start + 0] = green & 0xFF;
    ws2812->buffer[start + 1] = red & 0xFF;
    ws2812->buffer[start + 2] = blue & 0xFF;
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_refresh(led_strip_t *strip, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    STRIP_CHECK(rmt_transmit(ws2812->rmt_chan, ws2812->rmt_encoder, ws2812->buffer, ws2812->strip_len * 3, &tx_config) == ESP_OK,
                "transmit RMT samples failed", err, ESP_FAIL);
    STRIP_CHECK(rmt_tx_wait_all_done(ws2812->rmt_chan, timeout_ms) == ESP_OK,
                "wait tx done failed", err, ESP_FAIL);
    return ESP_OK;
err:
    return ret;
}

static esp_err_t ws2812_clear(led_strip_t *strip, uint32_t timeout_ms)
{
    ws2812_t *ws2812 = __containerof(strip, ws2812_t, parent);
    memset(ws2812->buffer, 0, ws2812->strip_len * 3);
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
    free(ws2812);
    return ESP_OK;
}

led_strip_t *led_strip_new_rmt_ws2812(const led_strip_config_t *config)
{
    led_strip_t *ret = NULL;
    ws2812_t *ws2812 = NULL;
    
    STRIP_CHECK(config, "configuration can't be null", err, NULL);

    // 24 bits per LED (3 bytes: G, R, B)
    uint32_t ws2812_size = sizeof(ws2812_t) + config->max_leds * 3;
    ws2812 = calloc(1, ws2812_size);
    STRIP_CHECK(ws2812, "request memory for ws2812 failed", err, NULL);

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = (gpio_num_t)config->gpio_num,
        .mem_block_symbols = 64,
        .resolution_hz = 40000000, // 40MHz
        .trans_queue_depth = 4,
    };
    STRIP_CHECK(rmt_new_tx_channel(&tx_chan_config, &ws2812->rmt_chan) == ESP_OK,
                "create RMT TX channel failed", err, NULL);

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
    ws2812->parent.set_pixel = ws2812_set_pixel;
    ws2812->parent.refresh = ws2812_refresh;
    ws2812->parent.clear = ws2812_clear;
    ws2812->parent.del = ws2812_del;
    ws2812->parent.set_brightness = ws2812_set_brightness;
    ws2812->parent.get_brightness = ws2812_get_brightness;

    return &ws2812->parent;
err:
    if (ws2812) {
        free(ws2812);
    }
    return ret;
}

void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360;
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

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
