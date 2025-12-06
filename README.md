# AtomVM NeoPixel Library

This AtomVM Erlang library and Nif can be used to control WS2812 and SK6812 LED strips using the ESP32 SoC for any Erlang/Elixir programs targeted for AtomVM on the ESP32 platform.

## Features

- **RGB LED support** (WS2812, WS2812B) - 24-bit color
- **RGBW LED support** (SK6812) - 32-bit color with dedicated white channel
- **Global brightness control** - Hardware-efficient brightness scaling (0-255)
- **Multiple color spaces** - RGB, RGBW, HSV, and HSVW
- **Batch operations** - Fill entire strip or set multiple pixels in a single call
- **ESP-IDF 5.x compatible** - Uses the new RMT driver API

This Nif is included as an add-on to the AtomVM base image.  In order to use this Nif in your AtomVM program, you must be able to build the AtomVM virtual machine, which in turn requires installation of the Espressif IDF SDK and tool chain.

Documentation for this library can be found in the following sections:

* [Programmer's Guide](markdown/neopixel.md)
* [Neopixel Example Program](examples/neopixel_example/README.md)
