%%
%% Copyright (c) 2021 dushin.net
%% All rights reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
-module(neopixel_example).

-export([start/0, demo_fill/0, demo_pattern/0, demo_rainbow/0]).

-define(NEOPIXEL_PIN, 18).
-define(NUM_PIXELS, 4).

-define(SATURATION, 100).
-define(VALUE, 15).

%% @doc Main example - rainbow cycle on each pixel (optimized single-process version)
%% This version updates all pixels in a single process, then calls refresh once.
%% This is the recommended pattern to avoid flickering.
start() ->
    demo_rainbow().

%% @doc Optimized rainbow demo - single process updates all pixels before refresh
demo_rainbow() ->
    {ok, NeoPixel} = neopixel:start(?NEOPIXEL_PIN, ?NUM_PIXELS),
    ok = neopixel:clear(NeoPixel),
    rainbow_loop(NeoPixel, 0, 100).

rainbow_loop(NeoPixel, BaseHue, SleepMs) ->
    %% Update all pixels first (no refresh yet)
    lists:foreach(
        fun(I) ->
            Hue = (BaseHue + (I * 360 div ?NUM_PIXELS)) rem 360,
            ok = neopixel:set_pixel_hsv(NeoPixel, I, Hue, ?SATURATION, ?VALUE)
        end,
        lists:seq(0, ?NUM_PIXELS - 1)
    ),
    %% Single refresh after all pixels are set - prevents flickering
    ok = neopixel:refresh(NeoPixel),
    timer:sleep(SleepMs),
    rainbow_loop(NeoPixel, (BaseHue + 1) rem 360, SleepMs).

%% @doc Demo: Fill entire strip with solid colors
demo_fill() ->
    {ok, NeoPixel} = neopixel:start(?NEOPIXEL_PIN, ?NUM_PIXELS),
    ok = neopixel:clear(NeoPixel),
    %% Cycle through red, green, blue
    fill_loop(NeoPixel, [{255, 0, 0}, {0, 255, 0}, {0, 0, 255}]).

fill_loop(NeoPixel, []) ->
    fill_loop(NeoPixel, [{255, 0, 0}, {0, 255, 0}, {0, 0, 255}]);
fill_loop(NeoPixel, [{R, G, B} | Rest]) ->
    ok = neopixel:fill_rgb(NeoPixel, R, G, B),
    ok = neopixel:refresh(NeoPixel),
    timer:sleep(1000),
    fill_loop(NeoPixel, Rest).

%% @doc Demo: Set multiple pixels with a pattern using set_pixels_rgb
demo_pattern() ->
    {ok, NeoPixel} = neopixel:start(?NEOPIXEL_PIN, ?NUM_PIXELS),
    ok = neopixel:clear(NeoPixel),
    %% Create a rainbow pattern
    Pattern = [{255, 0, 0}, {255, 127, 0}, {0, 255, 0}, {0, 0, 255}],
    pattern_loop(NeoPixel, Pattern, 0).

pattern_loop(NeoPixel, Pattern, Offset) ->
    %% Rotate the pattern by Offset positions
    RotatedPattern = rotate_list(Pattern, Offset),
    ok = neopixel:set_pixels_rgb(NeoPixel, RotatedPattern),
    ok = neopixel:refresh(NeoPixel),
    timer:sleep(200),
    pattern_loop(NeoPixel, Pattern, (Offset + 1) rem length(Pattern)).

rotate_list(List, 0) -> List;
rotate_list([H | T], N) -> rotate_list(T ++ [H], N - 1).
