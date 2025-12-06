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

-export([start/0, demo_fill/0, demo_pattern/0]).

-define(NEOPIXEL_PIN, 18).
-define(NUM_PIXELS, 4).

-define(SATURATION, 100).
-define(VALUE, 15).

%% @doc Main example - rainbow cycle on each pixel
start() ->
    {ok, NeoPixel} = neopixel:start(?NEOPIXEL_PIN, ?NUM_PIXELS),
    ok = neopixel:clear(NeoPixel),
    lists:foreach(
        fun(I) ->
            spawn(fun() -> loop(NeoPixel, I, 0, 100) end)
        end,
        lists:seq(0, ?NUM_PIXELS - 1)
    ),
    timer:sleep(infinity).

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

loop(NeoPixel, I, Hue, SleepMs) ->
    ok = neopixel:set_pixel_hsv(NeoPixel, I, Hue, ?SATURATION, ?VALUE),
    ok = neopixel:refresh(NeoPixel),
    timer:sleep(SleepMs),
    loop(NeoPixel, I, (Hue + 1) rem 360, SleepMs).
