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
%%-----------------------------------------------------------------------------
%% @doc WS2812/SK6812 LED Strip support.
%%
%% Use this module to drive a strip of WS2812 or SK6812 LED strips.
%%
%% Each LED in a strip is individually addressable and can be configured in
%% 24-bit color (RGB) or 32-bit color (RGBW for SK6812), using either a 
%% Red-Green-Blue (RGB/RGBW) or Hue-Saturation-Value (HSV) color space.
%%
%% Global brightness control is supported (0-255).
%%
%% Options:
%% <ul>
%%   <li>`led_type' - `rgb' (default) or `rgbw' for SK6812 RGBW strips</li>
%%   <li>`brightness' - Global brightness 0-255 (default 255)</li>
%%   <li>`timeout' - Refresh timeout in ms (default 100)</li>
%%   <li>`channel' - RMT channel (legacy, ignored in ESP-IDF 5.x)</li>
%% </ul>
%% @end
%%-----------------------------------------------------------------------------
-module(led_strip).

-export([
    start/2, start/3, stop/1, clear/1, set_pixel_rgb/5, set_pixel_rgbw/6, set_pixel_hsv/5, 
    set_pixel_hsvw/6, refresh/1, set_brightness/2, get_brightness/1,
    fill_rgb/4, fill_rgbw/5, fill_hsv/4, fill_hsvw/5, 
    set_pixels_rgb/2, set_pixels_rgb/3, set_pixels_rgbw/2, set_pixels_rgbw/3
]).
-export([nif_init/4, nif_clear/2, nif_refresh/2, nif_set_pixel_hsv/5, nif_set_pixel_hsvw/6,
         nif_set_pixel_rgb/5, nif_set_pixel_rgbw/6, nif_tini/2, nif_set_brightness/2, 
         nif_get_brightness/1, nif_fill_rgb/5, nif_fill_rgbw/6, nif_fill_hsv/5, nif_fill_hsvw/6,
         nif_set_pixels_rgb/3, nif_set_pixels_rgbw/3]). %% internal nif APIs
-export([init/1, handle_call/3, handle_cast/2, handle_info/2, terminate/2, code_change/3]).

-behaviour(gen_server).

-type led_strip() :: term().
-type pin() :: non_neg_integer().
-type options() :: map() | proplists:proplist().
-type channel() :: channel_0 | channel_1 | channel_2 | channel_3.
-type led_type() :: rgb | rgbw.

-type color() :: 0..255.
-type brightness() :: 0..255.
-type hue() :: 0..359.
-type saturation() :: 0..100.
-type value() :: 0..100.
-type rgb_color() :: {color(), color(), color()}.
-type rgbw_color() :: {color(), color(), color(), color()}.

-define(DEFAULT_OPTIONS, #{timeout => 100, channel => channel_0, led_type => rgb}).

-record(state, {
    pin :: pin(),
    num_pixels :: non_neg_integer(),
    nif_handle :: binary(),
    options :: options()
}).


%%-----------------------------------------------------------------------------
%% @param   Pin     pin connected to LED strip.
%% @returns ok | {error, Reason}
%% @doc     Start a LED strip driver.
%% @end
%%-----------------------------------------------------------------------------
-spec start(Pin::pin(), NumPixels::non_neg_integer()) -> {ok, led_strip()} | {error, Reason::term()}.
start(Pin, NumPixels) ->
    start(Pin, NumPixels, maps:new()).

%%-----------------------------------------------------------------------------
%% @param   Pin         pin connected to LED strip
%% @param   Options     extra options
%% @returns ok | {error, Reason}
%% @doc     Start a LED strip driver.
%%
%% Use the returned reference in subsequent LED strip operations.
%% @end
%%-----------------------------------------------------------------------------
-spec start(Pin::pin(), NumPixels::non_neg_integer(), Options::options()) -> {ok, led_strip()} | {error, Reason::term()}.
start(Pin, NumPixels, Options) ->
    NormalizedOpts = normalize_options(Options),
    MergedOpts = maps:merge(?DEFAULT_OPTIONS, NormalizedOpts),
    gen_server:start(?MODULE, [Pin, NumPixels, validate_options(MergedOpts)], []).

%%-----------------------------------------------------------------------------
%% @returns ok
%% @doc     Stop the specified LED strip driver.
%% @end
%%-----------------------------------------------------------------------------
-spec stop(LedStrip::led_strip()) -> ok.
stop(LedStrip) ->
    gen_server:call(LedStrip, stop).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @returns ok | {error, Reason}
%% @doc     Clear the LED strip.
%%
%% @end
%%-----------------------------------------------------------------------------
-spec clear(LedStrip::led_strip()) -> ok | {error, Reason::term()}.
clear(LedStrip) ->
    gen_server:call(LedStrip, clear).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @returns ok | {error, Reason}
%% @doc     Refresh the LED strip.
%%
%% @end
%%-----------------------------------------------------------------------------
-spec refresh(LedStrip::led_strip()) -> ok | {error, Reason::term()}.
refresh(LedStrip) ->
    gen_server:call(LedStrip, refresh).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   I               pixel index (`0..NumPixels - 1')
%% @param   R               Red value (`0..255')
%% @param   G               Green value (`0..255')
%% @param   B               Blue value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Set a pixel value in the RGB color space.
%%
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixel_rgb(LedStrip::led_strip(), I::non_neg_integer(), R::color(), G::color(), B::color()) -> ok | {error, Reason::term()}.
set_pixel_rgb(LedStrip, I, R, G, B) when is_pid(LedStrip), 0 =< R, R =< 255, 0 =< G, G =< 255, 0 =< B, B =< 255 ->
    gen_server:call(LedStrip, {set_pixel_rgb, I, R, G, B});
set_pixel_rgb(_LedStrip, _I, _R, _G, _B) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   I               pixel index (`0..NumPixels - 1')
%% @param   R               Red value (`0..255')
%% @param   G               Green value (`0..255')
%% @param   B               Blue value (`0..255')
%% @param   W               White value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Set a pixel value in the RGBW color space (for SK6812 RGBW strips).
%%
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixel_rgbw(LedStrip::led_strip(), I::non_neg_integer(), R::color(), G::color(), B::color(), W::color()) -> ok | {error, Reason::term()}.
set_pixel_rgbw(LedStrip, I, R, G, B, W) when is_pid(LedStrip), 0 =< R, R =< 255, 0 =< G, G =< 255, 0 =< B, B =< 255, 0 =< W, W =< 255 ->
    gen_server:call(LedStrip, {set_pixel_rgbw, I, R, G, B, W});
set_pixel_rgbw(_LedStrip, _I, _R, _G, _B, _W) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   I               pixel index (`0..NumPixels - 1')
%% @param   H               Hue value (`0..359')
%% @param   S               Saturation value (`0..100')
%% @param   V               Value (`0..100')
%% @returns ok | {error, Reason}
%% @doc     Set a pixel value in the HSV color space.
%%
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixel_hsv(LedStrip::led_strip(), I::non_neg_integer(), H::hue(), S::saturation(), V::value()) -> ok | {error, Reason::term()}.
set_pixel_hsv(LedStrip, I, H, S, V) when is_pid(LedStrip), 0 =< H, H < 360, 0 =< S, S =< 100, 0 =< V, V =< 100 ->
    gen_server:call(LedStrip, {set_pixel_hsv, I, H, S, V});
set_pixel_hsv(_LedStrip, _I, _R, _G, _B) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   I               pixel index (`0..NumPixels - 1')
%% @param   H               Hue value (`0..359')
%% @param   S               Saturation value (`0..100')
%% @param   V               Value (`0..100')
%% @param   W               White value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Set a pixel value in the HSV color space with white channel (for SK6812 RGBW strips).
%%
%% The H, S, V values are converted to RGB, then combined with the white channel.
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixel_hsvw(LedStrip::led_strip(), I::non_neg_integer(), H::hue(), S::saturation(), V::value(), W::color()) -> ok | {error, Reason::term()}.
set_pixel_hsvw(LedStrip, I, H, S, V, W) when is_pid(LedStrip), 0 =< H, H < 360, 0 =< S, S =< 100, 0 =< V, V =< 100, 0 =< W, W =< 255 ->
    gen_server:call(LedStrip, {set_pixel_hsvw, I, H, S, V, W});
set_pixel_hsvw(_LedStrip, _I, _H, _S, _V, _W) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   Brightness      Brightness value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Set global brightness for the strip.
%%
%% Brightness is applied when pixels are set. A value of 255 means full
%% brightness (no scaling), 128 means 50% brightness, 0 means off.
%% Note: You need to call refresh/1 and re-set pixels to see the effect.
%% @end
%%-----------------------------------------------------------------------------
-spec set_brightness(LedStrip::led_strip(), Brightness::brightness()) -> ok | {error, Reason::term()}.
set_brightness(LedStrip, Brightness) when is_pid(LedStrip), 0 =< Brightness, Brightness =< 255 ->
    gen_server:call(LedStrip, {set_brightness, Brightness});
set_brightness(_LedStrip, _Brightness) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @returns Brightness value (`0..255')
%% @doc     Get current global brightness for the strip.
%% @end
%%-----------------------------------------------------------------------------
-spec get_brightness(LedStrip::led_strip()) -> brightness().
get_brightness(LedStrip) when is_pid(LedStrip) ->
    gen_server:call(LedStrip, get_brightness);
get_brightness(_LedStrip) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   R               Red value (`0..255')
%% @param   G               Green value (`0..255')
%% @param   B               Blue value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Fill entire strip with a single RGB color.
%%
%% Sets all pixels to the same color. Call refresh/1 to display.
%% @end
%%-----------------------------------------------------------------------------
-spec fill_rgb(LedStrip::led_strip(), R::color(), G::color(), B::color()) -> ok | {error, Reason::term()}.
fill_rgb(LedStrip, R, G, B) when is_pid(LedStrip), 0 =< R, R =< 255, 0 =< G, G =< 255, 0 =< B, B =< 255 ->
    gen_server:call(LedStrip, {fill_rgb, R, G, B});
fill_rgb(_LedStrip, _R, _G, _B) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   R               Red value (`0..255')
%% @param   G               Green value (`0..255')
%% @param   B               Blue value (`0..255')
%% @param   W               White value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Fill entire strip with a single RGBW color (for SK6812 RGBW strips).
%%
%% Sets all pixels to the same color. Call refresh/1 to display.
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec fill_rgbw(LedStrip::led_strip(), R::color(), G::color(), B::color(), W::color()) -> ok | {error, Reason::term()}.
fill_rgbw(LedStrip, R, G, B, W) when is_pid(LedStrip), 0 =< R, R =< 255, 0 =< G, G =< 255, 0 =< B, B =< 255, 0 =< W, W =< 255 ->
    gen_server:call(LedStrip, {fill_rgbw, R, G, B, W});
fill_rgbw(_LedStrip, _R, _G, _B, _W) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   H               Hue value (`0..359')
%% @param   S               Saturation value (`0..100')
%% @param   V               Value (`0..100')
%% @returns ok | {error, Reason}
%% @doc     Fill entire strip with a single HSV color.
%%
%% HSV is converted to RGB in the NIF. Sets all pixels to the same color.
%% Call refresh/1 to display.
%% @end
%%-----------------------------------------------------------------------------
-spec fill_hsv(LedStrip::led_strip(), H::hue(), S::saturation(), V::value()) -> ok | {error, Reason::term()}.
fill_hsv(LedStrip, H, S, V) when is_pid(LedStrip), 0 =< H, H < 360, 0 =< S, S =< 100, 0 =< V, V =< 100 ->
    gen_server:call(LedStrip, {fill_hsv, H, S, V});
fill_hsv(_LedStrip, _H, _S, _V) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   H               Hue value (`0..359')
%% @param   S               Saturation value (`0..100')
%% @param   V               Value (`0..100')
%% @param   W               White value (`0..255')
%% @returns ok | {error, Reason}
%% @doc     Fill entire strip with a single HSVW color (for SK6812 RGBW strips).
%%
%% HSV is converted to RGB in the NIF, combined with the white channel.
%% Sets all pixels to the same color. Call refresh/1 to display.
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec fill_hsvw(LedStrip::led_strip(), H::hue(), S::saturation(), V::value(), W::color()) -> ok | {error, Reason::term()}.
fill_hsvw(LedStrip, H, S, V, W) when is_pid(LedStrip), 0 =< H, H < 360, 0 =< S, S =< 100, 0 =< V, V =< 100, 0 =< W, W =< 255 ->
    gen_server:call(LedStrip, {fill_hsvw, H, S, V, W});
fill_hsvw(_LedStrip, _H, _S, _V, _W) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   Colors          List of `{R, G, B}' tuples
%% @returns ok | {error, Reason}
%% @doc     Set multiple pixels from a list of RGB colors.
%%
%% Each element in the list sets the corresponding pixel starting at index 0.
%% The list can be shorter than the strip length.
%% Call refresh/1 to display.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixels_rgb(LedStrip::led_strip(), Colors::[rgb_color()]) -> ok | {error, Reason::term()}.
set_pixels_rgb(LedStrip, Colors) ->
    set_pixels_rgb(LedStrip, 0, Colors).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   Offset          Starting pixel index (`0..NumPixels - 1')
%% @param   Colors          List of `{R, G, B}' tuples
%% @returns ok | {error, Reason}
%% @doc     Set multiple pixels from a list of RGB colors starting at offset.
%%
%% Each element in the list sets the corresponding pixel starting at the given offset.
%% The list can be shorter than the strip length.
%% Call refresh/1 to display.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixels_rgb(LedStrip::led_strip(), Offset::non_neg_integer(), Colors::[rgb_color()]) -> ok | {error, Reason::term()}.
set_pixels_rgb(LedStrip, Offset, Colors) when is_pid(LedStrip), is_integer(Offset), Offset >= 0, is_list(Colors) ->
    gen_server:call(LedStrip, {set_pixels_rgb, Offset, Colors});
set_pixels_rgb(_LedStrip, _Offset, _Colors) ->
    throw(badarg).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   Colors          List of `{R, G, B, W}' tuples
%% @returns ok | {error, Reason}
%% @doc     Set multiple pixels from a list of RGBW colors (for SK6812 RGBW strips).
%%
%% Each element in the list sets the corresponding pixel starting at index 0.
%% The list can be shorter than the strip length.
%% Call refresh/1 to display.
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixels_rgbw(LedStrip::led_strip(), Colors::[rgbw_color()]) -> ok | {error, Reason::term()}.
set_pixels_rgbw(LedStrip, Colors) ->
    set_pixels_rgbw(LedStrip, 0, Colors).

%%-----------------------------------------------------------------------------
%% @param   LedStrip        LedStrip instance
%% @param   Offset          Starting pixel index (`0..NumPixels - 1')
%% @param   Colors          List of `{R, G, B, W}' tuples
%% @returns ok | {error, Reason}
%% @doc     Set multiple pixels from a list of RGBW colors starting at offset.
%%
%% Each element in the list sets the corresponding pixel starting at the given offset.
%% The list can be shorter than the strip length.
%% Call refresh/1 to display.
%% Returns `{error, not_supported}' if called on an RGB strip.
%% @end
%%-----------------------------------------------------------------------------
-spec set_pixels_rgbw(LedStrip::led_strip(), Offset::non_neg_integer(), Colors::[rgbw_color()]) -> ok | {error, Reason::term()}.
set_pixels_rgbw(LedStrip, Offset, Colors) when is_pid(LedStrip), is_integer(Offset), Offset >= 0, is_list(Colors) ->
    gen_server:call(LedStrip, {set_pixels_rgbw, Offset, Colors});
set_pixels_rgbw(_LedStrip, _Offset, _Colors) ->
    throw(badarg).

%%
%% gen_server API
%%

%% @hidden
init([Pin, NumPixels, Options]) ->
    Handle = ?MODULE:nif_init(Pin, NumPixels, maps:get(channel, Options), maps:get(led_type, Options)),
    {ok, #state{
        pin=Pin,
        num_pixels=NumPixels,
        options=Options,
        nif_handle=Handle
    }}.

%% @hidden
handle_call(stop, _From, State) ->
    {stop, normal, ?MODULE:nif_tini(State#state.nif_handle, maps:get(channel, State#state.options)), State};
handle_call(clear, _From, State) ->
    {reply, ?MODULE:nif_clear(State#state.nif_handle, maps:get(timeout, State#state.options)), State};
handle_call(refresh, _From, State) ->
    {reply, ?MODULE:nif_refresh(State#state.nif_handle, maps:get(timeout, State#state.options)), State};
handle_call({set_pixel_rgb, I, R, G, B}, _From, State) ->
    {reply, ?MODULE:nif_set_pixel_rgb(State#state.nif_handle, I, R, G, B), State};
handle_call({set_pixel_rgbw, I, R, G, B, W}, _From, State) ->
    {reply, ?MODULE:nif_set_pixel_rgbw(State#state.nif_handle, I, R, G, B, W), State};
handle_call({set_pixel_hsv, I, H, S, V}, _From, State) ->
    {reply, ?MODULE:nif_set_pixel_hsv(State#state.nif_handle, I, H, S, V), State};
handle_call({set_pixel_hsvw, I, H, S, V, W}, _From, State) ->
    {reply, ?MODULE:nif_set_pixel_hsvw(State#state.nif_handle, I, H, S, V, W), State};
handle_call({set_brightness, Brightness}, _From, State) ->
    {reply, ?MODULE:nif_set_brightness(State#state.nif_handle, Brightness), State};
handle_call(get_brightness, _From, State) ->
    {reply, ?MODULE:nif_get_brightness(State#state.nif_handle), State};
handle_call({fill_rgb, R, G, B}, _From, State) ->
    {reply, ?MODULE:nif_fill_rgb(State#state.nif_handle, State#state.num_pixels, R, G, B), State};
handle_call({fill_rgbw, R, G, B, W}, _From, State) ->
    {reply, ?MODULE:nif_fill_rgbw(State#state.nif_handle, State#state.num_pixels, R, G, B, W), State};
handle_call({fill_hsv, H, S, V}, _From, State) ->
    {reply, ?MODULE:nif_fill_hsv(State#state.nif_handle, State#state.num_pixels, H, S, V), State};
handle_call({fill_hsvw, H, S, V, W}, _From, State) ->
    {reply, ?MODULE:nif_fill_hsvw(State#state.nif_handle, State#state.num_pixels, H, S, V, W), State};
handle_call({set_pixels_rgb, Offset, Colors}, _From, State) ->
    {reply, ?MODULE:nif_set_pixels_rgb(State#state.nif_handle, Offset, Colors), State};
handle_call({set_pixels_rgbw, Offset, Colors}, _From, State) ->
    {reply, ?MODULE:nif_set_pixels_rgbw(State#state.nif_handle, Offset, Colors), State};
handle_call(Request, _From, State) ->
    {reply, {error, {unknown_request, Request}}, State}.

%% @hidden
handle_cast(_Msg, State) ->
    {noreply, State}.

%% @hidden
handle_info(_Info, State) ->
    {noreply, State}.

%% @hidden
terminate(_Reason, _State) ->
    ok.

%% @hidden
code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

%%
%% internal operations
%%

%% @private
%% @doc Convert options to map format, supporting both maps and proplists
normalize_options(Options) when is_map(Options) ->
    Options;
normalize_options(Options) when is_list(Options) ->
    maps:from_list(Options);
normalize_options(_) ->
    throw(badarg).

%% @private
validate_options(Options) ->
    validate_timeout_option(maps:get(timeout, Options, undefined)),
    validate_channel_option(maps:get(channel, Options, undefined)),
    validate_led_type_option(maps:get(led_type, Options, undefined)),
    Options.

%% @private
validate_timeout_option(Timeout) when is_integer(Timeout), 0 =< Timeout ->
    ok;
validate_timeout_option(_Timeout) ->
    throw(badarg).

%% @private
validate_channel_option(channel_0) ->
    ok;
validate_channel_option(channel_1) ->
    ok;
validate_channel_option(channel_2) ->
    ok;
validate_channel_option(channel_3) ->
    ok;
validate_channel_option(_Timeout) ->
    throw(badarg).

%% @private
validate_led_type_option(rgb) ->
    ok;
validate_led_type_option(rgbw) ->
    ok;
validate_led_type_option(_LedType) ->
    throw(badarg).


%%
%% Nifs
%%

%% @hidden
nif_init(_Pin, _NumPixels, _Channel, _LedType) ->
    throw(nif_error).

%% @hidden
nif_clear(_NifHandle, _Timeout) ->
    throw(nif_error).

%% @hidden
nif_refresh(_NifHandle, _Timeout) ->
    throw(nif_error).

%% @hidden
nif_set_pixel_rgb(_NifHandle, _Index, _Red, _Green, _Blue) ->
    throw(nif_error).

%% @hidden
nif_set_pixel_rgbw(_NifHandle, _Index, _Red, _Green, _Blue, _White) ->
    throw(nif_error).

%% @hidden
nif_set_pixel_hsv(_NifHandle, _Index, _Hue, _Saturation, _Value) ->
    throw(nif_error).

%% @hidden
nif_set_pixel_hsvw(_NifHandle, _Index, _Hue, _Saturation, _Value, _White) ->
    throw(nif_error).

%% @hidden
nif_set_brightness(_NifHandle, _Brightness) ->
    throw(nif_error).

%% @hidden
nif_get_brightness(_NifHandle) ->
    throw(nif_error).

%% @hidden
nif_fill_rgb(_NifHandle, _NumPixels, _Red, _Green, _Blue) ->
    throw(nif_error).

%% @hidden
nif_fill_rgbw(_NifHandle, _NumPixels, _Red, _Green, _Blue, _White) ->
    throw(nif_error).

%% @hidden
nif_fill_hsv(_NifHandle, _NumPixels, _Hue, _Saturation, _Value) ->
    throw(nif_error).

%% @hidden
nif_fill_hsvw(_NifHandle, _NumPixels, _Hue, _Saturation, _Value, _White) ->
    throw(nif_error).

%% @hidden
nif_set_pixels_rgb(_NifHandle, _Offset, _Colors) ->
    throw(nif_error).

%% @hidden
nif_set_pixels_rgbw(_NifHandle, _Offset, _Colors) ->
    throw(nif_error).

%% @hidden
nif_tini(_NifHandle, _Channel) ->
    throw(nif_error).
