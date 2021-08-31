/*
 * PebbleGotchi - A Tamagotchi P1 emulator for the Pebble smartwatches
 *
 * Copyright (C) 2021 Jean-Christophe Rona <jc@rona.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <pebble.h>

#include "lib/tamalib.h"
#include "rom.h"

#define FRAMERATE 					30

#define PIXEL_SIZE					4
#define ICON_SIZE					16
#define ICON_STRIDE_X					32
#define ICON_STRIDE_Y					104
#define ICON_OFFSET_X					14
#define ICON_OFFSET_Y					22
#define LCD_OFFET_X					8
#define LCD_OFFET_Y					52

#define BG_WIDTH					144
#define BG_HEIGHT					144

#define STATE_STORAGE_VERSION				0

static Window *s_window;

static AppTimer *s_cpu_timer = NULL;
static AppTimer *s_render_timer = NULL;

static Layer *s_canvas_layer;

static BitmapLayer *s_bg_layer;
static GBitmap *s_bg_bitmap;

static BitmapLayer *s_icons_layer[ICON_NUM];
static GBitmap *s_icons_bitmap[ICON_NUM];

static uint32_t const notif_segments[] = { 5000 };
static VibePattern notif = {
  .durations = notif_segments,
  .num_segments = ARRAY_LENGTH(notif_segments),
};

static bool_t matrix_buffer[LCD_HEIGTH][LCD_WIDTH] = {{0}};
static bool_t icon_buffer[ICON_NUM] = {0};

static bool_t tamalib_is_late = 0;


/* No need to support breakpoints */
static void * hal_malloc(u32_t size)
{
	return NULL;
}

static void hal_free(void *ptr)
{
}

static void hal_halt(void)
{
}

/* No need to support logs */
static bool_t hal_is_log_enabled(log_level_t level)
{
	return 0;
}

static void hal_log(log_level_t level, char *buff, ...)
{
}

static timestamp_t hal_get_timestamp(void)
{
	time_t t;
	uint16_t ms;
	
	time_ms(&t, &ms);

	return (timestamp_t) (t * 1000000 + ms * 1000);
}

static void hal_sleep_until(timestamp_t ts)
{
	if ((int32_t) (ts - hal_get_timestamp()) > 0) {
		tamalib_is_late = 0;
	}
}

static void hal_update_screen(void)
{
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
	matrix_buffer[y][x] = val;
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
	icon_buffer[icon] = val;
}

static void hal_set_frequency(u32_t freq)
{
}

static void hal_play_frequency(bool_t en)
{
	if (en) {
		vibes_enqueue_custom_pattern(notif);
	} else {
		vibes_cancel();
	}
}

static int hal_handler(void)
{
	return 0;
}

static hal_t hal = {
	.malloc = &hal_malloc,
	.free = &hal_free,
	.halt = &hal_halt,
	.is_log_enabled = &hal_is_log_enabled,
	.log = &hal_log,
	.sleep_until = &hal_sleep_until,
	.get_timestamp = &hal_get_timestamp,
	.update_screen = &hal_update_screen,
	.set_lcd_matrix = &hal_set_lcd_matrix,
	.set_lcd_icon = &hal_set_lcd_icon,
	.set_frequency = &hal_set_frequency,
	.play_frequency = &hal_play_frequency,
	.handler = &hal_handler,
};

static void state_save(void)
{
	state_t *state;
	uint8_t buf[160];
	uint32_t key = 0;
	uint32_t i, j;

	state = tamalib_get_state();

	/* Start with the version number */
	persist_write_int(key++, STATE_STORAGE_VERSION);

	/* All fields are written following the struct order */
	persist_write_int(key++, *(state->pc) & 0x1FFF);
	persist_write_int(key++, *(state->x) & 0xFFF);
	persist_write_int(key++, *(state->y) & 0xFFF);
	persist_write_int(key++, *(state->a) & 0xF);
	persist_write_int(key++, *(state->b) & 0xF);
	persist_write_int(key++, *(state->np) & 0x1F);
	persist_write_int(key++, *(state->sp) & 0xFF);
	persist_write_int(key++, *(state->flags) & 0xF);
	persist_write_int(key++, *(state->tick_counter));
	persist_write_int(key++, *(state->clk_timer_timestamp));
	persist_write_int(key++, *(state->prog_timer_timestamp));
	persist_write_int(key++, *(state->prog_timer_enabled) & 0x1);
	persist_write_int(key++, *(state->prog_timer_data) & 0xFF);
	persist_write_int(key++, *(state->prog_timer_rld) & 0xFF);
	persist_write_int(key++, *(state->call_depth));

	for (i = 0; i < INT_SLOT_NUM; i++) {
		persist_write_int(key++, state->interrupts[i].factor_flag_reg & 0xF);
		persist_write_int(key++, state->interrupts[i].mask_reg & 0xF);
		persist_write_int(key++, state->interrupts[i].triggered & 0x1);
	}

	/* First 640 half bytes correspond to the RAM */
	for (j = 0; j < 4; j++) {
		for (i = 0; i < sizeof(buf); i++) {
			buf[i] = state->memory[i + j * sizeof(buf)];
		}
		persist_write_data(key++, &buf, sizeof(buf));
	}

	/* I/Os are from 0xF00 to 0xF7F */
	for (i = 0; i < sizeof(buf); i++) {
		buf[i] = state->memory[i + 0xF00];
	}
	persist_write_data(key++, &buf, sizeof(buf));
}

static void state_load(void)
{
	state_t *state;
	uint8_t buf[160];
	uint32_t version;
	uint32_t key = 0;
	uint32_t i, j;

	/* Start with the version number */
	if (!persist_exists(key)) {
		/* Assume no key exists */
		return;
	}

	version = persist_read_int(key++);
	if (version != STATE_STORAGE_VERSION) {
		/* Invalid version
		 * TODO: Handle migration at a point
		 */
		return;
	}

	state = tamalib_get_state();

	/* All fields are written following the struct order */
	*(state->pc) = persist_read_int(key++) & 0x1FFF;
	*(state->x) = persist_read_int(key++) & 0xFFF;
	*(state->y) = persist_read_int(key++) & 0xFFF;
	*(state->a) = persist_read_int(key++) & 0xF;
	*(state->b) = persist_read_int(key++) & 0xF;
	*(state->np) = persist_read_int(key++) & 0x1F;
	*(state->sp) = persist_read_int(key++) & 0xFF;
	*(state->flags) = persist_read_int(key++) & 0xF;
	*(state->tick_counter) = persist_read_int(key++);
	*(state->clk_timer_timestamp) = persist_read_int(key++);
	*(state->prog_timer_timestamp) = persist_read_int(key++);
	*(state->prog_timer_enabled) = persist_read_int(key++) & 0x1;
	*(state->prog_timer_data) = persist_read_int(key++) & 0xFF;
	*(state->prog_timer_rld) = persist_read_int(key++) & 0xFF;
	*(state->call_depth) = persist_read_int(key++);

	for (i = 0; i < INT_SLOT_NUM; i++) {
		state->interrupts[i].factor_flag_reg = persist_read_int(key++) & 0xF;
		state->interrupts[i].mask_reg = persist_read_int(key++) & 0xF;
		state->interrupts[i].triggered = persist_read_int(key++) & 0x1;
	}

	/* First 640 half bytes correspond to the RAM */
	for (j = 0; j < 4; j++) {
		persist_read_data(key++, &buf, sizeof(buf));
		for (i = 0; i < sizeof(buf); i++) {
			state->memory[i + j * sizeof(buf)] = buf[i];
		}
	}

	/* I/Os are from 0xF00 to 0xF7F */
	persist_read_data(key++, &buf, sizeof(buf));
	for (i = 0; i < sizeof(buf); i++) {
		state->memory[i + 0xF00] = buf[i];
	}
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
	u8_t i, j;

	GRect rect_bounds;

	graphics_context_set_fill_color(ctx, GColorOxfordBlue);

	/* Dot matrix */
	for (j = 0; j < LCD_HEIGTH; j++) {
		for (i = 0; i < LCD_WIDTH; i++) {
			if (matrix_buffer[j][i]) {
				rect_bounds = GRect(i * PIXEL_SIZE + LCD_OFFET_X, j * PIXEL_SIZE + LCD_OFFET_Y, PIXEL_SIZE, PIXEL_SIZE);
				graphics_fill_rect(ctx, rect_bounds, 0, GCornerNone);
			}
		}
	}

	/* Icons */
	for (i = 0; i < ICON_NUM; i++) {
		layer_set_hidden(bitmap_layer_get_layer(s_icons_layer[i]), !icon_buffer[i]);
	}
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_MIDDLE, BTN_STATE_PRESSED);
}

static void prv_select_click_handler_release(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_MIDDLE, BTN_STATE_RELEASED);
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_LEFT, BTN_STATE_PRESSED);
}

static void prv_up_click_handler_release(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_LEFT, BTN_STATE_RELEASED);
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_RIGHT, BTN_STATE_PRESSED);
}

static void prv_down_click_handler_release(ClickRecognizerRef recognizer, void *context) {
	tamalib_set_button(BTN_RIGHT, BTN_STATE_RELEASED);
}

static void prv_click_config_provider(void *context) {
	window_raw_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler, prv_select_click_handler_release, NULL);
	window_raw_click_subscribe(BUTTON_ID_UP, prv_up_click_handler, prv_up_click_handler_release, NULL);
	window_raw_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler, prv_down_click_handler_release, NULL);
}

static void prv_window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	uint8_t i;

	window_set_background_color(window, GColorBlack);

	s_bg_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND);
	s_bg_layer = bitmap_layer_create(GRect((bounds.size.w - BG_WIDTH)/2, (bounds.size.h - BG_HEIGHT)/2, BG_WIDTH, BG_HEIGHT));
	bitmap_layer_set_compositing_mode(s_bg_layer, GCompOpSet);
	bitmap_layer_set_background_color(s_bg_layer, GColorClear);
	bitmap_layer_set_bitmap(s_bg_layer, s_bg_bitmap);
	layer_add_child(window_layer, bitmap_layer_get_layer(s_bg_layer));

	for (i = 0; i < ICON_NUM; i++) {
		s_icons_bitmap[i] = gbitmap_create_with_resource(RESOURCE_ID_ICON1 + i);
		s_icons_layer[i] = bitmap_layer_create(GRect((i % 4) * ICON_STRIDE_X + ICON_OFFSET_X, (i / 4) * ICON_STRIDE_Y + ICON_OFFSET_Y, ICON_SIZE, ICON_SIZE));
		bitmap_layer_set_compositing_mode(s_icons_layer[i], GCompOpSet);
		bitmap_layer_set_background_color(s_icons_layer[i], GColorClear);
		bitmap_layer_set_bitmap(s_icons_layer[i], s_icons_bitmap[i]);
		layer_add_child(window_layer, bitmap_layer_get_layer(s_icons_layer[i]));
		layer_set_hidden(bitmap_layer_get_layer(s_icons_layer[i]), true);
	}

	s_canvas_layer = layer_create(bounds);
	layer_set_update_proc(s_canvas_layer, canvas_update_proc);
	layer_add_child(window_get_root_layer(window), s_canvas_layer);

	layer_mark_dirty(s_canvas_layer);
}

static void prv_window_unload(Window *window) {
}

static void cpu_handler(void *context) {
	s_cpu_timer = app_timer_register(1, cpu_handler, NULL);

	tamalib_is_late = 1;

	while (tamalib_is_late) {
		tamalib_step();
	}

}

static void render_handler(void *context) {
	layer_mark_dirty(s_canvas_layer);

	s_render_timer = app_timer_register(1000/FRAMERATE, render_handler, NULL);
}

static void prv_init(void) {
	s_window = window_create();
	window_set_click_config_provider(s_window, prv_click_config_provider);
	window_set_window_handlers(s_window, (WindowHandlers) {
		.load = prv_window_load,
		.unload = prv_window_unload,
	});
	const bool animated = true;
	window_stack_push(s_window, animated);

	s_cpu_timer = app_timer_register(1, cpu_handler, NULL);
	s_render_timer = app_timer_register(1000/FRAMERATE, render_handler, NULL);

	tamalib_register_hal(&hal);
	tamalib_init(g_program, NULL, 1000000);
	state_load();
}

static void prv_deinit(void) {
	state_save();
	tamalib_release();

	window_destroy(s_window);
}

int main(void) {
	prv_init();

	APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", s_window);

	app_event_loop();
	prv_deinit();
}
