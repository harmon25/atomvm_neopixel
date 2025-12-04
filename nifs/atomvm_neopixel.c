//
// Copyright (c) 2021 dushin.net
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

#include <stdlib.h>

#include <atomvm_neopixel.h>
#include <context.h>
#include <defaultatoms.h>
#include <esp_log.h>
#include <esp32_sys.h>
#include <nifs.h>
#include <term.h>
#include "led_strip.h"

// #define ENABLE_TRACE
#include "trace.h"

#define TAG "atomvm_neopixel"

static const char *const led_strip_atom = "\x9" "led_strip";
static const char *const rgbw_atom = "\x4" "rgbw";
static const char *const not_supported_atom = "\xD" "not_supported";


static inline term ptr_to_binary(void *ptr, Context* ctx)
{
    return term_from_literal_binary(&ptr, sizeof(int), &ctx->heap, ctx->global);
}


static inline void *binary_to_ptr(term binary)
{
    if (term_binary_size(binary) != sizeof(int)) {
        return NULL;
    }
    const char *ptr = term_binary_data(binary);
    return *((void **) ptr);
}


static term nif_init(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term pin = argv[0];
    VALIDATE_VALUE(pin, term_is_integer);
    term num_pixels = argv[1];
    VALIDATE_VALUE(num_pixels, term_is_integer);
    term channel = argv[2];
    VALIDATE_VALUE(channel, term_is_atom);
    term led_type_term = argv[3];
    VALIDATE_VALUE(led_type_term, term_is_atom);
    // Note: channel argument is kept for API compatibility but ignored in ESP-IDF 5.x

    if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }

    // Determine LED type from atom
    led_strip_type_t led_type = LED_STRIP_RGB;
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, led_type_term, rgbw_atom)) {
        led_type = LED_STRIP_RGBW;
        ESP_LOGI(TAG, "LED type set to RGBW (4 bytes per pixel)");
    } else {
        ESP_LOGI(TAG, "LED type set to RGB (3 bytes per pixel)");
    }

    led_strip_config_t strip_config = {
        .max_leds = term_to_int(num_pixels),
        .gpio_num = term_to_int(pin),
        .led_type = led_type
    };
    
    ESP_LOGI(TAG, "Creating strip: leds=%d, gpio=%d, led_type=%d", (int)strip_config.max_leds, strip_config.gpio_num, (int)strip_config.led_type);
    
    led_strip_t *strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip) {
        TRACE("Failed to install WS2812 driver.\n");
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, globalcontext_make_atom(ctx->global, led_strip_atom));
        return error_tuple;
    }
    
    ESP_LOGI(TAG, "Installed WS2812 driver.");
    return ptr_to_binary(strip, ctx);
}


static term nif_clear(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term timeout = argv[1];
    VALIDATE_VALUE(timeout, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    esp_err_t err = strip->clear(strip, term_to_int(timeout));
    if (err != ESP_OK) {
        TRACE("Failed to clear led strip.  err=%i\n", err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Cleared led strip.\n");
    return OK_ATOM;
}


static term nif_refresh(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term timeout = argv[1];
    VALIDATE_VALUE(timeout, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    esp_err_t err = strip->refresh(strip, term_to_int(timeout));
    if (err != ESP_OK) {
        TRACE("Failed to refresh led strip.  err=%i\n", err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Refreshed led strip.\n");
    return OK_ATOM;
}


static term nif_set_pixel_rgb(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term index = argv[1];
    VALIDATE_VALUE(index, term_is_integer);
    term red = argv[2];
    VALIDATE_VALUE(red, term_is_integer);
    term green = argv[3];
    VALIDATE_VALUE(green, term_is_integer);
    term blue = argv[4];
    VALIDATE_VALUE(blue, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    avm_int_t i = term_to_int(index);
    esp_err_t err = strip->set_pixel(strip, i, term_to_int(red), term_to_int(green), term_to_int(blue));
    if (err != ESP_OK) {
        TRACE("Failed to set pixel value on index %i (r=%i g=%i b=%i).  err=%i\n", i, red, green, blue, err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Set pixel %i to r=%i g=%i b=%i\n", i, term_to_int(red), term_to_int(green), term_to_int(blue));
    return OK_ATOM;
}


static term nif_set_pixel_hsv(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term index = argv[1];
    VALIDATE_VALUE(index, term_is_integer);
    term hue = argv[2];
    VALIDATE_VALUE(hue, term_is_integer);
    term saturation = argv[3];
    VALIDATE_VALUE(saturation, term_is_integer);
    term value = argv[4];
    VALIDATE_VALUE(value, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    uint32_t red = 0;
    uint32_t green = 0;
    uint32_t blue = 0;
    led_strip_hsv2rgb(term_to_int(hue), term_to_int(saturation), term_to_int(value), &red, &green, &blue);

    avm_int_t i = term_to_int(index);
    esp_err_t err = strip->set_pixel(strip, i, red, green, blue);
    if (err != ESP_OK) {
        TRACE("Failed to set pixel value on index %i (r=%i g=%i b=%i).  err=%i\n", i, red, green, blue, err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Set pixel %i to r=%i g=%i b=%i\n", i, term_to_int(red), term_to_int(green), term_to_int(blue));
    return OK_ATOM;
}


static term nif_set_pixel_rgbw(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term index = argv[1];
    VALIDATE_VALUE(index, term_is_integer);
    term red = argv[2];
    VALIDATE_VALUE(red, term_is_integer);
    term green = argv[3];
    VALIDATE_VALUE(green, term_is_integer);
    term blue = argv[4];
    VALIDATE_VALUE(blue, term_is_integer);
    term white = argv[5];
    VALIDATE_VALUE(white, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    avm_int_t i = term_to_int(index);
    esp_err_t err = strip->set_pixel_rgbw(strip, i, term_to_int(red), term_to_int(green), term_to_int(blue), term_to_int(white));
    if (err == ESP_ERR_NOT_SUPPORTED) {
        TRACE("set_pixel_rgbw called on non-RGBW strip\n");
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, globalcontext_make_atom(ctx->global, not_supported_atom));
        return error_tuple;
    }
    if (err != ESP_OK) {
        TRACE("Failed to set pixel value on index %i (r=%i g=%i b=%i w=%i).  err=%i\n", i, term_to_int(red), term_to_int(green), term_to_int(blue), term_to_int(white), err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Set pixel %i to r=%i g=%i b=%i w=%i\n", i, term_to_int(red), term_to_int(green), term_to_int(blue), term_to_int(white));
    return OK_ATOM;
}


static term nif_set_brightness(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term brightness = argv[1];
    VALIDATE_VALUE(brightness, term_is_integer);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    avm_int_t br = term_to_int(brightness);
    if (br < 0 || br > 255) {
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, BADARG_ATOM);
        return error_tuple;
    }

    esp_err_t err = strip->set_brightness(strip, (uint8_t)br);
    if (err != ESP_OK) {
        TRACE("Failed to set brightness to %i.  err=%i\n", br, err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }
    TRACE("Set brightness to %i\n", br);
    return OK_ATOM;
}


static term nif_get_brightness(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    uint8_t brightness = strip->get_brightness(strip);
    return term_from_int(brightness);
}


static term nif_tini(Context *ctx, int argc, term argv[])
{
    UNUSED(argc);

    term handle = argv[0];
    VALIDATE_VALUE(handle, term_is_binary);
    term channel = argv[1];
    VALIDATE_VALUE(channel, term_is_atom);
    // Note: channel argument is kept for API compatibility but ignored in ESP-IDF 5.x

    led_strip_t *strip = (led_strip_t *) binary_to_ptr(handle);

    esp_err_t err = strip->del(strip);
    if (err != ESP_OK) {
        TRACE("Failed to delete led strip.  err=%i\n", err);
        if (UNLIKELY(memory_ensure_free(ctx, 3) != MEMORY_GC_OK)) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term error_tuple = term_alloc_tuple(2, &ctx->heap);
        term_put_tuple_element(error_tuple, 0, ERROR_ATOM);
        term_put_tuple_element(error_tuple, 1, term_from_int(err));
        return error_tuple;
    }

    TRACE("LED strip tini'd\n");
    return OK_ATOM;
}


static const struct Nif init_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_init
};
static const struct Nif clear_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_clear
};
static const struct Nif refresh_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_refresh
};
static const struct Nif set_pixel_hsv_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_set_pixel_hsv
};
static const struct Nif set_pixel_rgb_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_set_pixel_rgb
};
static const struct Nif set_pixel_rgbw_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_set_pixel_rgbw
};
static const struct Nif set_brightness_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_set_brightness
};
static const struct Nif get_brightness_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_get_brightness
};
static const struct Nif tini_nif =
{
    .base.type = NIFFunctionType,
    .nif_ptr = nif_tini
};


//
// Component Nif Entrypoints
//

void atomvm_neopixel_init(GlobalContext *global)
{
    // no-op
}

const struct Nif *atomvm_neopixel_get_nif(const char *nifname)
{
    TRACE("Locating nif %s ...", nifname);
    if (strcmp("neopixel:nif_init/4", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &init_nif;
    }
    if (strcmp("neopixel:nif_clear/2", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &clear_nif;
    }
    if (strcmp("neopixel:nif_refresh/2", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &refresh_nif;
    }
    if (strcmp("neopixel:nif_set_pixel_rgb/5", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &set_pixel_rgb_nif;
    }
    if (strcmp("neopixel:nif_set_pixel_rgbw/6", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &set_pixel_rgbw_nif;
    }
    if (strcmp("neopixel:nif_set_pixel_hsv/5", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &set_pixel_hsv_nif;
    }
    if (strcmp("neopixel:nif_set_brightness/2", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &set_brightness_nif;
    }
    if (strcmp("neopixel:nif_get_brightness/1", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &get_brightness_nif;
    }
    if (strcmp("neopixel:nif_tini/2", nifname) == 0) {
        TRACE("Resolved platform nif %s ...\n", nifname);
        return &tini_nif;
    }
    return NULL;
}

#include <sdkconfig.h>
#ifdef CONFIG_AVM_NEOPIXEL_ENABLE
REGISTER_NIF_COLLECTION(atomvm_neopixel, atomvm_neopixel_init, NULL, atomvm_neopixel_get_nif)
#endif
