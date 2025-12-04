# Neopixel

This AtomVM Erlang library and Nif can be used to control WS2812 and SK6812 LED strips using the ESP32 SoC for any Erlang/Elixir programs targeted for AtomVM on the ESP32 platform.

The AtomVM NeoPixel library is only supported on the ESP32 platform.

## Features

- **RGB LED support** (WS2812, WS2812B) - 24-bit color (3 bytes per pixel)
- **RGBW LED support** (SK6812) - 32-bit color with dedicated white channel (4 bytes per pixel)
- **Global brightness control** - Efficient brightness scaling applied at refresh time
- **Multiple color spaces** - RGB, RGBW, and HSV

## Build Instructions

The AtomVM NeoPixel library is implemented as an AtomVM component, which includes some native C code that must be linked into the ESP32 AtomVM image.  In order to build and deploy this client code, you must build an AtomVM binary image with this component compiled and linked into the image.

For general instructions about how to build AtomVM and include third-party components into an AtomVM image, see the [AtomVM Build Instructions](https://doc.atomvm.net/build-instructions.html).

Once the AtomVM image including this component has been built, you can flash the image to your ESP32 device.  For instructions about how to flash AtomVM images to your ESP32 device, see the AtomVM [Getting Started Guide](https://doc.atomvm.net/getting-started-guide.html).

Once the AtomVM image including this component has been flashed to your ESP32 device, you can then include this project into your [`rebar3`](https://www.rebar3.org) project using the [`atomvm_rebar3_plugin`](https://github.com/atomvm/atomvm_rebar3_plugin), which provides targets for building AtomVM packbeam files and flashing them to your device.

## Programmer's Guide

The `atomvm_neopixel` library can be used to drive a strip of [WS2812](https://cdn-shop.adafruit.com/datasheets/WS2812.pdf) "Neopixel" LEDs or [SK6812](https://cdn-shop.adafruit.com/product-files/2757/p2757_SK6812RGBW_REV01.pdf) RGBW LEDs.

Neopixel LED strips are individually addressable sets of "pixels". RGB strips (WS2812) contain 3 LEDs per pixel (red, green, blue), while RGBW strips (SK6812) contain 4 LEDs per pixel (red, green, blue, white). Each color channel can be configured using an 8-bit value (`0..255`).

Colors can be specified using:
- **RGB** - Red, Green, Blue values (`0..255` each)
- **RGBW** - Red, Green, Blue, White values (`0..255` each) - for SK6812 strips only
- **HSV** - Hue (`0..359`), Saturation (`0..100`), Value (`0..100`)

AtomVM programmers interface with the `atomvm_neopixel` API via the `neopixel` module, which provides operations for starting and stopping an Erlang process associated with a specified LED strip, and for setting values on each pixel in the strip.

This guide provides an overview of the interfaces provided by this module.

### Lifecyle

Use the `neopixel:start/2` function to initialize a neopixel instance.  Specify the ESP32 pin to which the LED strip is connected, as well as the number of LEDs in the strip.  This function will return a reference to a neopixel process, which is used in subsequent operations.

    %% erlang
    Pin = 18,
    NumPixels = 4,
    {ok NeoPixel} = neopixel:start(Pin, NumPixels),
    ...

Use the `neopixel:start/3` function to initialize a neopixel instance with non-default options.  Options can be specified as an Erlang map or a proplist (keyword list in Elixir).  The permissible entries are encapsulated in the following table:

| Key | Type | Default | Description |
| ----- | ----- | ------| ----|
| `led_type` | `rgb \| rgbw` | `rgb` | LED strip type. Use `rgbw` for SK6812 RGBW strips. |
| `timeout`   | `non_neg_integer()` | 100 | Timeout (in milliseconds) used internally when communicating with the LED strip |
| `channel`   | `channel_0 \| channel_1 \| channel_2 \| channel_3` | `channel_0` | Legacy option, ignored in ESP-IDF 5.x |

For example, to use an RGBW strip:

    %% erlang
    Pin = 18,
    NumPixels = 4,
    {ok, NeoPixel} = neopixel:start(Pin, NumPixels, #{led_type => rgbw}),
    ...

Or in Elixir with a keyword list:

    # elixir
    {:ok, neo_pixel} = :neopixel.start(18, 4, led_type: :rgbw)

The returned `NeoPixel` instance should be used for subsequent operations.

Use the `neopixel:stop/1` function to stop a neopixel instance and free any resources in use by it.

    %% erlang
    neopixel:stop(NeoPixel).

### Clearing pixels

Use the `neopixel:clear/1` function to clear all the pixels in the LED strip to an "off" value.

    %% erlang
    ok = neopixel:clear(NeoPixel).

Clearing the LED strip will set all RGB values to 0.

### Setting Pixel values

Pixel colors can be set using and RGB or HSV color space.

Note that setting values will not change the state of the LED strip until the NeoPixel instance is refreshed (see below).

#### RGB Color Space

Pixel colors can be set using the RGB color space via the `neopixel:set_pixel_rgb/5` function.  Pixel indices are in the range `[0..NumPixels-1]`.

For example, to set the second pixel in the strip (index 1) to all red, use:

    %% erlang
    ok = neopixel:set_pixel_rgb(NeoPixel, 1, 255, 0, 0).

RGB values and their ranges are summarized in the following table:

| Parameter | Range | Description |
| ----- | ----- | ------|
| red   | `0..255` | Value of red LED |
| green | `0..255` | Value of green LED |
| blue | `0..255` | Value of blue LED |

#### RGBW Color Space (SK6812 only)

For RGBW strips (SK6812), pixel colors can be set using the `neopixel:set_pixel_rgbw/6` function. This function is only available when the strip was initialized with `led_type => rgbw`.

For example, to set the second pixel to red with 50% white:

    %% erlang
    ok = neopixel:set_pixel_rgbw(NeoPixel, 1, 255, 0, 0, 128).

Or in Elixir:

    # elixir
    :ok = :neopixel.set_pixel_rgbw(neo_pixel, 1, 255, 0, 0, 128)

RGBW values and their ranges are summarized in the following table:

| Parameter | Range | Description |
| ----- | ----- | ------|
| red   | `0..255` | Value of red LED |
| green | `0..255` | Value of green LED |
| blue  | `0..255` | Value of blue LED |
| white | `0..255` | Value of white LED |

> **Note:** Calling `set_pixel_rgbw/6` on an RGB strip will return `{error, not_supported}`.

> **Note:** You can use `set_pixel_rgb/5` on RGBW strips - the white channel will be set to 0.

#### HSV color space

Pixel colors can be set using the HSV color space via the `neopixel:set_pixel_hsv/5` function.  Pixel indices are in the range `[0..NumPixels-1]`.

Information about the HSV color space is available via [this article](https://en.wikipedia.org/wiki/HSL_and_HSV).

For example, to set the second pixel in the strip (index 1) to all red, use:

    %% erlang
    ok = neopixel:set_pixel_hsv(NeoPixel, 1, 0, 100, 100).

HSV values and their ranges are summarized in the following table:

| Parameter | Range | Description |
| ----- | ----- | ------|
| hue   | `0..259` | Pixel hue, expressed as degrees of rotation around a circle, starting with red at 0 degrees, green at 120 degrees, and blue at 240 degrees. |
| saturation | `0..100` | Color saturation, as a percentage, with 0 being all white, and 100 maximum color saturation. |
| value | `0..100` | Value, as a percentage, with 0 being all dark, and 100 maximum brightness. |

#### HSVW color space (SK6812 only)

For RGBW strips, use `neopixel:set_pixel_hsvw/6` to set a pixel using HSV values plus an independent white channel. The HSV values control the RGB LEDs while the white parameter controls the dedicated white LED.

    %% erlang
    %% Set pixel 0 to orange (H=30) at full saturation and value, with 50% white
    ok = neopixel:set_pixel_hsvw(NeoPixel, 0, 30, 100, 100, 128).

Or in Elixir:

    # elixir
    :ok = :neopixel.set_pixel_hsvw(neo_pixel, 0, 30, 100, 100, 128)

| Parameter | Range | Description |
| ----- | ----- | ------|
| hue   | `0..259` | Pixel hue (same as HSV) |
| saturation | `0..100` | Color saturation (same as HSV) |
| value | `0..100` | Value/brightness of RGB LEDs (same as HSV) |
| white | `0..255` | Value of the dedicated white LED |

> **Note:** Calling `set_pixel_hsvw/6` on an RGB strip will return `{error, not_supported}`.

### Refreshing pixels

Use the `neopixel:refresh/1` function to ref refresh all the pixels in the LED strip.

    %% erlang
    ok = neopixel:refresh(NeoPixel).

Refreshing the LED strip will manifest any changes made via any previous `set_pixel_*` operations (see above).

### Brightness Control

Use the `neopixel:set_brightness/2` function to set the global brightness for the strip. Brightness is a value from 0 (off) to 255 (full brightness).

    %% erlang
    ok = neopixel:set_brightness(NeoPixel, 128).  %% 50% brightness

Or in Elixir:

    # elixir
    :ok = :neopixel.set_brightness(neo_pixel, 128)

Brightness scaling is applied efficiently at refresh time, so:
- Original color values are preserved in memory
- Changing brightness and calling `refresh/1` immediately shows the effect
- No precision loss from repeated brightness changes

Use the `neopixel:get_brightness/1` function to get the current brightness:

    %% erlang
    Brightness = neopixel:get_brightness(NeoPixel).

### API Reference

To generate Reference API documentation in HTML, issue the rebar3 target

    shell$ rebar3 edoc

from the top level of the `atomvm_neopixel` source tree.  Output is written to the `doc` directory.
