/*
 * obs-retroize: video filter implementation
 * Copyright (c) 2026 Bailey 'monokrome' Stoner
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <obs-module.h>
#include <graphics/vec2.h>
#include <plugin-support.h>

#define S_PRESET   "preset"
#define S_TARGET_W "target_w"
#define S_TARGET_H "target_h"
#define S_BITS     "bits"
#define S_DITHER   "dither"

#define P_NES     "NES"
#define P_SNES    "SNES"
#define P_GENESIS "Genesis"
#define P_GAMEBOY "GameBoy"
#define P_CUSTOM  "Custom"

struct preset_def {
	const char *id;
	const char *technique;
	int width;
	int height;
};

static const struct preset_def presets[] = {
	{P_NES,     "NES",     256, 240},
	{P_SNES,    "SNES",    256, 224},
	{P_GENESIS, "Genesis", 320, 224},
	{P_GAMEBOY, "GameBoy", 160, 144},
	{P_CUSTOM,  "Custom",  256, 240},
};
#define PRESET_COUNT (sizeof(presets) / sizeof(presets[0]))

#define NES_PALETTE_COUNT 54

// 64-entry NES 2C02-style palette as RGBA8. Black entries pad to 64 so the
// shader can use a fixed loop bound; nes_count limits the effective range.
static const uint8_t nes_palette_rgba[64 * 4] = {
	 85, 85, 85,255,   0, 30,116,255,   7,  7,138,255,  59,  0,130,255,
	103,  0, 88,255, 119,  0, 29,255, 107,  6,  0,255,  74, 29,  0,255,
	 40, 56,  0,255,   6, 74,  0,255,   0, 80,  0,255,   0, 74, 28,255,
	  0, 60, 89,255,   0,  0,  0,255,   0,  0,  0,255,   0,  0,  0,255,

	156,156,156,255,   0, 88,202,255,  50, 50,225,255, 112, 19,216,255,
	159,  7,159,255, 180, 10, 72,255, 171, 42, 10,255, 132, 76,  0,255,
	 89,107,  0,255,  35,128,  0,255,   0,135, 21,255,   0,128, 86,255,
	  0,111,148,255,   0,  0,  0,255,   0,  0,  0,255,   0,  0,  0,255,

	247,247,247,255,  62,159,255,255, 119,125,255,255, 180, 91,255,255,
	232, 76,245,255, 255, 79,159,255, 255,101, 86,255, 227,133, 35,255,
	180,168,  7,255, 119,196, 23,255,  62,206, 77,255,  35,201,141,255,
	 38,187,198,255, 100,100,100,255,   0,  0,  0,255,   0,  0,  0,255,

	247,247,247,255, 168,207,255,255, 195,196,255,255, 222,184,255,255,
	245,177,247,255, 255,180,212,255, 255,190,184,255, 245,206,157,255,
	222,222,141,255, 195,234,143,255, 168,239,168,255, 154,237,201,255,
	154,232,232,255, 189,189,189,255,   0,  0,  0,255,   0,  0,  0,255
};

// 4x4 Bayer ordered-dither matrix as R8 in [0,255].
static const uint8_t bayer_r8[16] = {
	  0*16, 8*16, 2*16,10*16,
	 12*16, 4*16,14*16, 6*16,
	  3*16,11*16, 1*16, 9*16,
	 15*16, 7*16,13*16, 5*16
};

struct retroize_data {
	obs_source_t *context;
	gs_effect_t  *effect;
	gs_texture_t *nes_tex;
	gs_texture_t *bayer_tex;

	char *preset;
	int   target_w;
	int   target_h;
	int   bits;
	bool  dither;

	gs_eparam_t *p_source_size;
	gs_eparam_t *p_target_size;
	gs_eparam_t *p_bits;
	gs_eparam_t *p_dither;
	gs_eparam_t *p_nes_palette;
	gs_eparam_t *p_nes_count;
	gs_eparam_t *p_bayer;
};

static const struct preset_def *find_preset(const char *id)
{
	if (!id) return &presets[0];
	for (size_t i = 0; i < PRESET_COUNT; i++) {
		if (strcmp(id, presets[i].id) == 0)
			return &presets[i];
	}
	return &presets[0];
}

static const char *retroize_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Retroize");
}

static void retroize_update(void *data, obs_data_t *settings)
{
	struct retroize_data *f = data;

	bfree(f->preset);
	f->preset = bstrdup(obs_data_get_string(settings, S_PRESET));
	f->target_w = (int)obs_data_get_int(settings, S_TARGET_W);
	f->target_h = (int)obs_data_get_int(settings, S_TARGET_H);
	f->bits     = (int)obs_data_get_int(settings, S_BITS);
	f->dither   = obs_data_get_bool(settings, S_DITHER);
}

static void retroize_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_PRESET, P_NES);
	obs_data_set_default_int(settings, S_TARGET_W, 256);
	obs_data_set_default_int(settings, S_TARGET_H, 240);
	obs_data_set_default_int(settings, S_BITS, 4);
	obs_data_set_default_bool(settings, S_DITHER, false);
}

static bool preset_modified(obs_properties_t *props, obs_property_t *p,
			    obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	const char *id = obs_data_get_string(settings, S_PRESET);
	bool custom = (strcmp(id, P_CUSTOM) == 0);

	obs_property_set_visible(obs_properties_get(props, S_TARGET_W), custom);
	obs_property_set_visible(obs_properties_get(props, S_TARGET_H), custom);
	obs_property_set_visible(obs_properties_get(props, S_BITS), custom);
	return true;
}

static obs_properties_t *retroize_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *preset = obs_properties_add_list(
		props, S_PRESET, obs_module_text("Retroize.Preset"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset, obs_module_text("Retroize.Preset.NES"),     P_NES);
	obs_property_list_add_string(preset, obs_module_text("Retroize.Preset.SNES"),    P_SNES);
	obs_property_list_add_string(preset, obs_module_text("Retroize.Preset.Genesis"), P_GENESIS);
	obs_property_list_add_string(preset, obs_module_text("Retroize.Preset.GameBoy"), P_GAMEBOY);
	obs_property_list_add_string(preset, obs_module_text("Retroize.Preset.Custom"),  P_CUSTOM);
	obs_property_set_modified_callback(preset, preset_modified);

	obs_properties_add_int_slider(props, S_TARGET_W,
		obs_module_text("Retroize.TargetWidth"), 16, 1920, 1);
	obs_properties_add_int_slider(props, S_TARGET_H,
		obs_module_text("Retroize.TargetHeight"), 16, 1080, 1);
	obs_properties_add_int_slider(props, S_BITS,
		obs_module_text("Retroize.Bits"), 1, 8, 1);
	obs_properties_add_bool(props, S_DITHER, obs_module_text("Retroize.Dither"));

	return props;
}

static void retroize_destroy(void *data)
{
	struct retroize_data *f = data;
	obs_enter_graphics();
	if (f->effect)    gs_effect_destroy(f->effect);
	if (f->nes_tex)   gs_texture_destroy(f->nes_tex);
	if (f->bayer_tex) gs_texture_destroy(f->bayer_tex);
	obs_leave_graphics();
	bfree(f->preset);
	bfree(f);
}

static void *retroize_create(obs_data_t *settings, obs_source_t *context)
{
	struct retroize_data *f = bzalloc(sizeof(*f));
	f->context = context;

	char *path = obs_module_file("retroize.effect");
	obs_enter_graphics();
	f->effect = gs_effect_create_from_file(path, NULL);

	const uint8_t *nes_levels[1] = { nes_palette_rgba };
	f->nes_tex = gs_texture_create(64, 1, GS_RGBA, 1, nes_levels, 0);

	const uint8_t *bayer_levels[1] = { bayer_r8 };
	f->bayer_tex = gs_texture_create(4, 4, GS_R8, 1, bayer_levels, 0);
	obs_leave_graphics();
	bfree(path);

	if (!f->effect || !f->nes_tex || !f->bayer_tex) {
		obs_log(LOG_ERROR, "failed to initialize retroize resources");
		retroize_destroy(f);
		return NULL;
	}

	f->p_source_size = gs_effect_get_param_by_name(f->effect, "source_size");
	f->p_target_size = gs_effect_get_param_by_name(f->effect, "target_size");
	f->p_bits        = gs_effect_get_param_by_name(f->effect, "bits_per_channel");
	f->p_dither      = gs_effect_get_param_by_name(f->effect, "dither_strength");
	f->p_nes_palette = gs_effect_get_param_by_name(f->effect, "nes_palette");
	f->p_nes_count   = gs_effect_get_param_by_name(f->effect, "nes_count");
	f->p_bayer       = gs_effect_get_param_by_name(f->effect, "bayer");

	retroize_update(f, settings);
	return f;
}

static void retroize_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct retroize_data *f = data;
	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target || !f->effect) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);
	if (!w || !h) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	const struct preset_def *p = find_preset(f->preset);
	int tw = p->width;
	int th = p->height;
	if (strcmp(p->id, P_CUSTOM) == 0) {
		tw = f->target_w > 0 ? f->target_w : 1;
		th = f->target_h > 0 ? f->target_h : 1;
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	struct vec2 ss = {(float)w, (float)h};
	struct vec2 ts = {(float)tw, (float)th};
	gs_effect_set_vec2(f->p_source_size, &ss);
	gs_effect_set_vec2(f->p_target_size, &ts);
	gs_effect_set_float(f->p_bits, (float)f->bits);
	gs_effect_set_float(f->p_dither, f->dither ? 1.0f : 0.0f);
	gs_effect_set_float(f->p_nes_count, (float)NES_PALETTE_COUNT);
	gs_effect_set_texture(f->p_nes_palette, f->nes_tex);
	gs_effect_set_texture(f->p_bayer, f->bayer_tex);

	obs_source_process_filter_tech_end(f->context, f->effect, w, h,
					   p->technique);
}

struct obs_source_info retroize_filter_info = {
	.id             = "retroize_filter",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = retroize_get_name,
	.create         = retroize_create,
	.destroy        = retroize_destroy,
	.update         = retroize_update,
	.get_defaults   = retroize_defaults,
	.get_properties = retroize_properties,
	.video_render   = retroize_render,
};
