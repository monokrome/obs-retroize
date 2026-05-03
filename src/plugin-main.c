/*
 * obs-retroize
 * Copyright (c) 2026 Bailey "monokrome" Stoner
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info retroize_filter_info;

bool obs_module_load(void)
{
	obs_register_source(&retroize_filter_info);
	obs_log(LOG_INFO, "retroize loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "retroize unloaded");
}
