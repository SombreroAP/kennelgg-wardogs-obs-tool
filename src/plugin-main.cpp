/*
POVBridge for OBS - show a squad mate's POV while you are downed in WARDOGS.
Copyright (C) 2026 Sombrero / The Kennel

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QMainWindow>
#include "engine.h"
#include "ui/dock.h"
#include "ui/settings-dialog.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static Engine *g_engine = nullptr;
static Dock *g_dock = nullptr;
static obs_hotkey_id g_hkToggle = OBS_INVALID_HOTKEY_ID, g_hkCapture = OBS_INVALID_HOTKEY_ID;

static void hotkeyToggle(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(g_engine, "toggle", Qt::QueuedConnection);
}

static void hotkeyCapture(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(g_engine, "captureTemplate", Qt::QueuedConnection);
}

static void loadHotkeys()
{
	std::string path = Config::configFile("hotkeys.json");
	obs_data_t *d = obs_data_create_from_json_file(path.c_str());
	if (!d)
		return;
	obs_data_array_t *a = obs_data_get_array(d, "toggle");
	if (a) {
		obs_hotkey_load(g_hkToggle, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "capture");
	if (a) {
		obs_hotkey_load(g_hkCapture, a);
		obs_data_array_release(a);
	}
	obs_data_release(d);
}

static void saveHotkeys()
{
	obs_data_t *d = obs_data_create();
	obs_data_array_t *a = obs_hotkey_save(g_hkToggle);
	obs_data_set_array(d, "toggle", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkCapture);
	obs_data_set_array(d, "capture", a);
	obs_data_array_release(a);
	obs_data_save_json_safe(d, Config::configFile("hotkeys.json").c_str(), "tmp", "bak");
	obs_data_release(d);
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (g_engine)
			g_engine->start();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (g_engine)
			g_engine->reloadConfig();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		saveHotkeys();
		if (g_engine)
			g_engine->stop();
	}
}

bool obs_module_load(void)
{
	auto *main = (QMainWindow *)obs_frontend_get_main_window();
	g_engine = new Engine(main);
	g_dock = new Dock(g_engine);
	obs_frontend_add_dock_by_id("povbridge_dock", obs_module_text("POVBridge"), g_dock);
	obs_frontend_add_tools_menu_item(obs_module_text("POVBridge.Settings"), [](void *) {
		if (g_dock)
			g_dock->openSettings();
	}, nullptr);
	g_hkToggle = obs_hotkey_register_frontend("povbridge.toggle", obs_module_text("POVBridge.Hotkey.Toggle"), hotkeyToggle, nullptr);
	g_hkCapture = obs_hotkey_register_frontend("povbridge.capture", obs_module_text("POVBridge.Hotkey.Capture"), hotkeyCapture, nullptr);
	loadHotkeys();
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "POVBridge loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	if (g_hkToggle != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkToggle);
	if (g_hkCapture != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkCapture);
	if (g_engine)
		g_engine->stop();
	g_engine = nullptr; // owned by the main window
	g_dock = nullptr;
	obs_log(LOG_INFO, "POVBridge unloaded");
}
