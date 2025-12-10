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
-module(led_strip_example).

-export([start/0, demo_fill/0, demo_pattern/0]).

-define(LED_STRIP_PIN, 18).
-define(NUM_PIXELS, 4).

-define(SATURATION, 100).
-define(VALUE, 15).

%% @doc Main example - rainbow cycle on each pixel
start() ->
    {ok, LedStrip} = led_strip:start(?LED_STRIP_PIN, ?NUM_PIXELS),
    ok = led_strip:clear(LedStrip),
    lists:foreach(
        fun(I) ->
            spawn(fun() -> loop(LedStrip, I, 0, 100) end)
        end,
        lists:seq(0, ?NUM_PIXELS - 1)
    ),
    timer:sleep(infinity).

%% @doc Demo: Fill entire strip with solid colors
demo_fill() ->
    {ok, LedStrip} = led_strip:start(?LED_STRIP_PIN, ?NUM_PIXELS),
    ok = led_strip:clear(LedStrip),
    %% Cycle through red, green, blue
    fill_loop(LedStrip, [{255, 0, 0}, {0, 255, 0}, {0, 0, 255}]).

fill_loop(LedStrip, []) ->
    fill_loop(LedStrip, [{255, 0, 0}, {0, 255, 0}, {0, 0, 255}]);
fill_loop(LedStrip, [{R, G, B} | Rest]) ->
    ok = led_strip:fill_rgb(LedStrip, R, G, B),
    ok = led_strip:refresh(LedStrip),
    timer:sleep(1000),
    fill_loop(LedStrip, Rest).

%% @doc Demo: Set multiple pixels with a pattern using set_pixels_rgb
demo_pattern() ->
    {ok, LedStrip} = led_strip:start(?LED_STRIP_PIN, ?NUM_PIXELS),
    ok = led_strip:clear(LedStrip),
    %% Create a rainbow pattern
    Pattern = [{255, 0, 0}, {255, 127, 0}, {0, 255, 0}, {0, 0, 255}],
    pattern_loop(LedStrip, Pattern, 0).

pattern_loop(LedStrip, Pattern, Offset) ->
    %% Rotate the pattern by Offset positions
    RotatedPattern = rotate_list(Pattern, Offset),
    ok = led_strip:set_pixels_rgb(LedStrip, RotatedPattern),
    ok = led_strip:refresh(LedStrip),
    timer:sleep(200),
    pattern_loop(LedStrip, Pattern, (Offset + 1) rem length(Pattern)).

rotate_list(List, 0) -> List;
rotate_list([H | T], N) -> rotate_list(T ++ [H], N - 1).

loop(LedStrip, I, Hue, SleepMs) ->
    ok = led_strip:set_pixel_hsv(LedStrip, I, Hue, ?SATURATION, ?VALUE),
    ok = led_strip:refresh(LedStrip),
    timer:sleep(SleepMs),
    loop(LedStrip, I, (Hue + 1) rem 360, SleepMs).
