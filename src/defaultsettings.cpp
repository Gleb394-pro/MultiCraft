/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "settings.h"
#include "porting.h"
#include "filesys.h"
#include "config.h"
#include "constants.h"
#include "util/string.h"

#ifdef __IOS__
#import <UIKit/UIKit.h>
#import "SDVersion.h"
#endif

void set_default_settings(Settings *settings) {
	// Client and server
	settings->setDefault("language", "");
	settings->setDefault("name", "");
	settings->setDefault("password", "");
	settings->setDefault("password_save", "false");
	settings->setDefault("bind_address", "");
	settings->setDefault("serverlist_url", "servers.multicraft.world");
	settings->setDefault("serverlist_url_2", "");

	// Client
	settings->setDefault("address", "");
	settings->setDefault("enable_sound", "true");
	settings->setDefault("sound_volume", "1");
	settings->setDefault("enable_mesh_cache", "false");
	settings->setDefault("mesh_generation_interval", "0");
	settings->setDefault("meshgen_block_cache_size", "20");
	settings->setDefault("enable_vbo", "true");
	settings->setDefault("free_move", "false");
	settings->setDefault("fast_move", "false");
	settings->setDefault("noclip", "false");
	settings->setDefault("screenshot_path", ".");
	settings->setDefault("screenshot_format", "png");
	settings->setDefault("screenshot_quality", "0");
	settings->setDefault("client_unload_unused_data_timeout", "600");
	settings->setDefault("client_mapblock_limit", "5000");
	settings->setDefault("enable_build_where_you_stand", "false");
	settings->setDefault("send_pre_v25_init", "false");
	settings->setDefault("curl_timeout", "5000");
	settings->setDefault("curl_parallel_limit", "8");
	settings->setDefault("curl_file_download_timeout", "300000");
	settings->setDefault("curl_verify_cert", "true");
	settings->setDefault("enable_remote_media_server", "true");
	settings->setDefault("enable_client_modding", "false");
	settings->setDefault("max_out_chat_queue_size", "20");
	settings->setDefault("connection_timeout", "20");

	// Keymap
	settings->setDefault("remote_port", "40000");
	settings->setDefault("keymap_forward", "KEY_KEY_W");
	settings->setDefault("keymap_autorun", "");
	settings->setDefault("keymap_backward", "KEY_KEY_S");
	settings->setDefault("keymap_left", "KEY_KEY_A");
	settings->setDefault("keymap_right", "KEY_KEY_D");
	settings->setDefault("keymap_jump", "KEY_SPACE");
	settings->setDefault("keymap_sneak", "KEY_LSHIFT");
	settings->setDefault("keymap_drop", "KEY_KEY_Q");
	settings->setDefault("keymap_zoom", "KEY_KEY_Z");
	settings->setDefault("keymap_inventory", "KEY_KEY_I");
	settings->setDefault("keymap_special1", "KEY_KEY_E");
	settings->setDefault("keymap_chat", "KEY_KEY_T");
	settings->setDefault("keymap_cmd", "/");
	settings->setDefault("keymap_cmd_local", ".");
	settings->setDefault("keymap_minimap", "KEY_F9");
	settings->setDefault("keymap_console", "KEY_F10");
	settings->setDefault("keymap_rangeselect", "KEY_KEY_R");
	settings->setDefault("keymap_freemove", "KEY_KEY_K");
	settings->setDefault("keymap_fastmove", "KEY_KEY_J");
	settings->setDefault("keymap_noclip", "KEY_KEY_H");
	settings->setDefault("keymap_hotbar_next", "KEY_KEY_N");
	settings->setDefault("keymap_hotbar_previous", "KEY_KEY_B");
	settings->setDefault("keymap_mute", "KEY_KEY_M");
	settings->setDefault("keymap_increase_volume", "");
	settings->setDefault("keymap_decrease_volume", "");
	settings->setDefault("keymap_cinematic", "");
	settings->setDefault("keymap_toggle_hud", "KEY_F1");
	settings->setDefault("keymap_toggle_chat", "KEY_F2");
	settings->setDefault("keymap_toggle_force_fog_off", "KEY_F3");
#if DEBUG
	settings->setDefault("keymap_toggle_update_camera", "KEY_F4");
#else
	settings->setDefault("keymap_toggle_update_camera", "");
#endif
	settings->setDefault("keymap_toggle_debug", "KEY_F5");
	settings->setDefault("keymap_toggle_profiler", "KEY_F6");
	settings->setDefault("keymap_camera_mode", "KEY_F7");
	settings->setDefault("keymap_screenshot", "KEY_F12");
	settings->setDefault("keymap_increase_viewing_range_min", "+");
	settings->setDefault("keymap_decrease_viewing_range_min", "-");
	// Some (temporary) keys for debugging
	settings->setDefault("keymap_print_debug_stacks", "KEY_KEY_P");
	settings->setDefault("keymap_quicktune_prev", "KEY_HOME");
	settings->setDefault("keymap_quicktune_next", "KEY_END");
	settings->setDefault("keymap_quicktune_dec", "KEY_NEXT");
	settings->setDefault("keymap_quicktune_inc", "KEY_PRIOR");

	// Visuals
#ifdef NDEBUG
	settings->setDefault("show_debug", "false");
#else
	settings->setDefault("show_debug", "true");
#endif
	settings->setDefault("fsaa", "0");
	settings->setDefault("undersampling", "0");
	settings->setDefault("enable_fog", "true");
	settings->setDefault("fog_start", "0.4");
	settings->setDefault("3d_mode", "none");
	settings->setDefault("3d_paralax_strength", "0.025");
	settings->setDefault("tooltip_show_delay", "400");
	settings->setDefault("zoom_fov", "15");
	settings->setDefault("fps_max", "60");
	settings->setDefault("pause_fps_max", "15");
	settings->setDefault("viewing_range", "100");
	settings->setDefault("near_plane", "0.1");
	settings->setDefault("screenW", "1024");
	settings->setDefault("screenH", "768");
	settings->setDefault("autosave_screensize", "true");
	settings->setDefault("fullscreen", "false");
	settings->setDefault("fullscreen_bpp", "24");
	settings->setDefault("vsync", "false");
	settings->setDefault("fov", "72");
	settings->setDefault("leaves_style", "fancy");
	settings->setDefault("connected_glass", "false");
	settings->setDefault("smooth_lighting", "true");
	settings->setDefault("display_gamma", "3.0");
	settings->setDefault("texture_path", "");
	settings->setDefault("shader_path", "");
	settings->setDefault("video_driver", "opengl");
	settings->setDefault("cinematic", "false");
	settings->setDefault("camera_smoothing", "0");
	settings->setDefault("cinematic_camera_smoothing", "0.7");
	settings->setDefault("enable_clouds", "true");
	settings->setDefault("view_bobbing_amount", "1.0");
	settings->setDefault("fall_bobbing_amount", "1.0");
	settings->setDefault("enable_3d_clouds", "true");
	settings->setDefault("cloud_height", "128");
	settings->setDefault("cloud_radius", "12");
	settings->setDefault("menu_clouds", "false");
	settings->setDefault("opaque_water", "false");
	settings->setDefault("console_height", "1.0");
	settings->setDefault("console_color", "(0,0,0)");
	settings->setDefault("console_alpha", "200");
	settings->setDefault("selectionbox_color", "(0,0,0)");
	settings->setDefault("selectionbox_width", "4");
	settings->setDefault("node_highlighting", "box");
	settings->setDefault("crosshair_color", "(255,255,255)");
	settings->setDefault("crosshair_alpha", "255");
	settings->setDefault("hud_scaling", "1.0");
	settings->setDefault("gui_scaling", "1.0");
	settings->setDefault("gui_scaling_filter", "false");
	settings->setDefault("gui_scaling_filter_txr2img", "true");
	settings->setDefault("desynchronize_mapblock_texture_animation", "true");
	settings->setDefault("hud_hotbar_max_width", "1.0");
	settings->setDefault("hud_move_upwards", "0");
	settings->setDefault("hud_small", "false");
	settings->setDefault("round_screen", "0");
	settings->setDefault("enable_local_map_saving", "false");
	settings->setDefault("show_entity_selectionbox", "false");
	settings->setDefault("texture_clean_transparent", "false");
	settings->setDefault("texture_min_size", "32");
	settings->setDefault("ambient_occlusion_gamma", "2.2");
	settings->setDefault("enable_shaders", "true");
	settings->setDefault("enable_particles", "true");
	settings->setDefault("screen_dpi", "72");

	settings->setDefault("enable_minimap", "true");
	settings->setDefault("minimap_shape_round", "true");
	settings->setDefault("minimap_double_scan_height", "false");

	// Effects
	settings->setDefault("directional_colored_fog", "true");
	settings->setDefault("inventory_items_animations", "false");
	settings->setDefault("mip_map", "false");
	settings->setDefault("anisotropic_filter", "false");
	settings->setDefault("bilinear_filter", "false");
	settings->setDefault("trilinear_filter", "false");
	settings->setDefault("tone_mapping", "false");
	settings->setDefault("enable_bumpmapping", "false");
	settings->setDefault("enable_parallax_occlusion", "false");
	settings->setDefault("generate_normalmaps", "false");
	settings->setDefault("normalmaps_strength", "0.6");
	settings->setDefault("normalmaps_smooth", "1");
	settings->setDefault("parallax_occlusion_mode", "1");
	settings->setDefault("parallax_occlusion_iterations", "4");
	settings->setDefault("parallax_occlusion_scale", "0.08");
	settings->setDefault("parallax_occlusion_bias", "0.04");
	settings->setDefault("enable_waving_water", "false");
	settings->setDefault("water_wave_height", "1.0");
	settings->setDefault("water_wave_length", "20.0");
	settings->setDefault("water_wave_speed", "5.0");
	settings->setDefault("enable_waving_leaves", "false");
	settings->setDefault("enable_waving_plants", "false");


	// Input
	settings->setDefault("invert_mouse", "false");
	settings->setDefault("mouse_sensitivity", "0.2");
	settings->setDefault("repeat_rightclick_time", "0.25");
	settings->setDefault("random_input", "false");
	settings->setDefault("aux1_descends", "false");
	settings->setDefault("doubletap_jump", "false");
	settings->setDefault("always_fly_fast", "true");
	settings->setDefault("continuous_forward", "false");
	settings->setDefault("enable_joysticks", "false");
	settings->setDefault("joystick_id", "0");
	settings->setDefault("joystick_type", "");
	settings->setDefault("repeat_joystick_button_time", "0.17");
	settings->setDefault("joystick_frustum_sensitivity", "170");

	// Main menu
	settings->setDefault("main_menu_path", "");
	settings->setDefault("main_menu_mod_mgr", "1");
	settings->setDefault("main_menu_game_mgr", "0");
	settings->setDefault("modstore_download_url", "https://forum.minetest.net/media/");
	settings->setDefault("modstore_listmods_url", "https://forum.minetest.net/mmdb/mods/");
	settings->setDefault("modstore_details_url", "https://forum.minetest.net/mmdb/mod/*/");
	settings->setDefault("serverlist_file", "favoriteservers.txt");

#if USE_FREETYPE
	settings->setDefault("freetype", "true");
	settings->setDefault("font_path", porting::getDataPath("fonts" DIR_DELIM "Retron2000.ttf"));
	settings->setDefault("font_shadow", "1");
	settings->setDefault("font_shadow_alpha", "127");
	settings->setDefault("mono_font_path",
	                     porting::getDataPath("fonts" DIR_DELIM "Cousine-Regular.ttf"));
	settings->setDefault("fallback_font_path",
	                     porting::getDataPath("fonts" DIR_DELIM "DroidSansFallback.ttf"));

	settings->setDefault("fallback_font_shadow", "1");
	settings->setDefault("fallback_font_shadow_alpha", "128");

	std::string font_size_str = std::to_string(TTF_DEFAULT_FONT_SIZE);

	settings->setDefault("fallback_font_size", font_size_str);
#else
	settings->setDefault("freetype", "false");
	settings->setDefault("font_path", porting::getDataPath("fonts" DIR_DELIM "mono_dejavu_sans"));
	settings->setDefault("mono_font_path", porting::getDataPath("fonts" DIR_DELIM "mono_dejavu_sans"));

	std::string font_size_str = std::to_string(DEFAULT_FONT_SIZE);
#endif
	settings->setDefault("font_size", font_size_str);
	settings->setDefault("mono_font_size", font_size_str);


	// Server
	settings->setDefault("disable_escape_sequences", "false");
	settings->setDefault("strip_color_codes", "false");

	// Network
	settings->setDefault("enable_ipv6", "true");
	settings->setDefault("ipv6_server", "false");
	settings->setDefault("workaround_window_size", "5");
	settings->setDefault("max_packets_per_iteration", "1024");
	settings->setDefault("port", "40000");
	settings->setDefault("strict_protocol_version_checking", "false");
	settings->setDefault("player_transfer_distance", "0");
	settings->setDefault("max_simultaneous_block_sends_per_client", "10");
	settings->setDefault("max_simultaneous_block_sends_server_total", "10000");
	settings->setDefault("time_send_interval", "5");

	settings->setDefault("default_game", "default");
	settings->setDefault("motd", "");
	settings->setDefault("max_users", "15");
	settings->setDefault("creative_mode", "false");
	settings->setDefault("show_statusline_on_connect", "true");
	settings->setDefault("enable_damage", "true");
	settings->setDefault("default_password", "");
	settings->setDefault("default_privs", "interact, shout");
	settings->setDefault("enable_pvp", "true");
	settings->setDefault("disallow_empty_password", "false");
	settings->setDefault("disable_anticheat", "false");
	settings->setDefault("enable_rollback_recording", "false");
#ifdef NDEBUG
	settings->setDefault("deprecated_lua_api_handling", "legacy");
#else
	settings->setDefault("deprecated_lua_api_handling", "log");
#endif

	settings->setDefault("kick_msg_shutdown", "Server shutting down.");
	settings->setDefault("kick_msg_crash",
	                     "This server has experienced an internal error. You will now be disconnected.");
	settings->setDefault("ask_reconnect_on_crash", "false");
	settings->setDefault("kamikaze", "false");

	settings->setDefault("profiler_print_interval", "0");
	settings->setDefault("active_object_send_range_blocks", "4");
	settings->setDefault("active_block_range", "3");
	//settings->setDefault("max_simultaneous_block_sends_per_client", "1");
	// This causes frametime jitter on client side, or does it?
	settings->setDefault("max_block_send_distance", "10");
	settings->setDefault("block_send_optimize_distance", "4");
	settings->setDefault("server_side_occlusion_culling", "true");
	settings->setDefault("max_clearobjects_extra_loaded_blocks", "4096");
	settings->setDefault("time_speed", "72");
	settings->setDefault("server_unload_unused_data_timeout", "29");
	settings->setDefault("max_objects_per_block", "16");
	settings->setDefault("server_map_save_interval", "5.3");
	settings->setDefault("chat_message_max_size", "500");
	settings->setDefault("chat_message_limit_per_10sec", "5.0");
	settings->setDefault("chat_message_limit_trigger_kick", "50");
	settings->setDefault("sqlite_synchronous", "2");
	settings->setDefault("full_block_send_enable_min_time_from_building", "2.0");
	settings->setDefault("dedicated_server_step", "0.1");
	settings->setDefault("active_block_mgmt_interval", "2.0");
	settings->setDefault("abm_interval", "1.0");
	settings->setDefault("nodetimer_interval", "0.2");
	settings->setDefault("ignore_world_load_errors", "false");
	settings->setDefault("remote_media", "");
	settings->setDefault("debug_log_level", "warning");
	settings->setDefault("emergequeue_limit_total", "512");
	settings->setDefault("emergequeue_limit_diskonly", "64");
	settings->setDefault("emergequeue_limit_generate", "64");
	settings->setDefault("num_emerge_threads", "1");
	settings->setDefault("secure.enable_security", "true");
	settings->setDefault("secure.trusted_mods", "");
	settings->setDefault("secure.http_mods", "");

	// Physics
	settings->setDefault("movement_acceleration_default", "3");
	settings->setDefault("movement_acceleration_air", "2");
	settings->setDefault("movement_acceleration_fast", "20");
	settings->setDefault("movement_speed_walk", "4");
	settings->setDefault("movement_speed_crouch", "1.35");
	settings->setDefault("movement_speed_fast", "20");
	settings->setDefault("movement_speed_climb", "3");
	settings->setDefault("movement_speed_jump", "6.5");
	settings->setDefault("movement_liquid_fluidity", "1");
	settings->setDefault("movement_liquid_fluidity_smooth", "0.5");
	settings->setDefault("movement_liquid_sink", "20");
	settings->setDefault("movement_gravity", "9.81");

	// Liquids
	settings->setDefault("liquid_loop_max", "100000");
	settings->setDefault("liquid_queue_purge_time", "0");
	settings->setDefault("liquid_update", "1.0");

	// Mapgen
	settings->setDefault("mg_name", "v7p");
	settings->setDefault("water_level", "1");
	settings->setDefault("mapgen_limit", "31000");
	settings->setDefault("chunksize", "5");
	settings->setDefault("mg_flags", "dungeons");
	settings->setDefault("mgflat_spflags", "nocaves");
	settings->setDefault("fixed_map_seed", "");
	settings->setDefault("max_block_generate_distance", "8");
	settings->setDefault("projecting_dungeons", "false");
	settings->setDefault("enable_mapgen_debug_info", "false");

	// Server list announcing
	settings->setDefault("server_announce", "false");
	settings->setDefault("server_url", "");
	settings->setDefault("server_address", "");
	settings->setDefault("server_name", "");
	settings->setDefault("server_description", "");

	settings->setDefault("high_precision_fpu", "true");
	settings->setDefault("enable_console", "false");

	settings->setDefault("mainmenu_last_selected_world", "1");

	// Mobile Platform
#if defined(__ANDROID__) || defined(__IOS__)
	settings->setDefault("screenW", "0");
	settings->setDefault("screenH", "0");
	settings->setDefault("fullscreen", "true");
	settings->setDefault("enable_shaders", "false");
	settings->setDefault("video_driver", "ogles1");
	settings->setDefault("touchtarget", "true");
	settings->setDefault("touchscreen_threshold", "20");
	settings->setDefault("doubletap_jump", "true");
	settings->setDefault("emergequeue_limit_diskonly", "16");
	settings->setDefault("emergequeue_limit_generate", "16");
	settings->setDefault("curl_verify_cert", "false");
	settings->setDefault("gui_scaling_filter_txr2img", "false");
	settings->setDefault("autosave_screensize", "false");
	char lang[3] = {0};

	// Set the optimal settings depending on the memory size [Android] | model [iOS]
#ifdef __ANDROID__
	// minimal settings for less than 2GB RAM
	if (porting::getMemoryMax() < 2) {
#elif __IOS__
	// minimal settings for 32-bit devices
	bool arm = false;
#if defined(__arm__)
	arm = true;
#endif
	if (arm) {
#endif
		settings->setDefault("client_unload_unused_data_timeout", "60");
		settings->setDefault("client_mapblock_limit", "50");
		settings->setDefault("fps_max", "30");
		settings->setDefault("pause_fps_max", "5");
		settings->setDefault("viewing_range", "25");
		settings->setDefault("smooth_lighting", "false");
		settings->setDefault("enable_clouds", "false");
		settings->setDefault("active_block_range", "1");
		settings->setDefault("dedicated_server_step", "0.2");
		settings->setDefault("abm_interval", "3.0");
		settings->setDefault("chunksize", "3");
		settings->setDefault("max_block_generate_distance", "1");
		settings->setDefault("enable_weather", "false");
#ifdef __ANDROID__
	// low settings for 2-4GB RAM
	} else if (porting::getMemoryMax() >= 2 && porting::getMemoryMax() < 4) {
#elif __IOS__
	// low settings
	} else if (([SDVersion deviceVersion] == iPhone5S) || ([SDVersion deviceVersion] == iPhone6) || ([SDVersion deviceVersion] == iPhone6Plus) || ([SDVersion deviceVersion] == iPodTouch6Gen) ||
			   ([SDVersion deviceVersion] == iPadMini2) || ([SDVersion deviceVersion] == iPadMini3)) {
#endif
		settings->setDefault("client_unload_unused_data_timeout", "120");
		settings->setDefault("client_mapblock_limit", "200");
		settings->setDefault("fps_max", "35");
		settings->setDefault("pause_fps_max", "5");
		settings->setDefault("viewing_range", "30");
		settings->setDefault("smooth_lighting", "false");
		settings->setDefault("enable_3d_clouds", "false");
		settings->setDefault("cloud_radius", "6");
		settings->setDefault("active_block_range", "1");
		settings->setDefault("dedicated_server_step", "0.2");
		settings->setDefault("abm_interval", "2.0");
		settings->setDefault("chunksize", "3");
		settings->setDefault("max_block_generate_distance", "2");
		settings->setDefault("enable_weather", "false");
#ifdef __ANDROID__
	// medium settings for 4.1-6GB RAM
	} else if (porting::getMemoryMax() >= 4 && porting::getMemoryMax() < 6) {
#elif __IOS__
	// medium settings
	} else if (([SDVersion deviceVersion] == iPhone6S) || ([SDVersion deviceVersion] == iPhone6SPlus) || ([SDVersion deviceVersion] == iPhoneSE) || ([SDVersion deviceVersion] == iPhone7) || ([SDVersion deviceVersion] == iPhone7Plus) ||
			   ([SDVersion deviceVersion] == iPadMini4) || ([SDVersion deviceVersion] == iPadAir)) {
#endif
		settings->setDefault("client_unload_unused_data_timeout", "300");
		settings->setDefault("client_mapblock_limit", "300");
		settings->setDefault("fps_max", "35");
		settings->setDefault("pause_fps_max", "10");
		settings->setDefault("viewing_range", "60");
		settings->setDefault("cloud_radius", "6");
		settings->setDefault("active_block_range", "2");
		settings->setDefault("max_block_generate_distance", "3");
	} else {
	// high settings
		settings->setDefault("client_mapblock_limit", "500");
		settings->setDefault("viewing_range", "80");
		settings->setDefault("max_block_generate_distance", "5");
	}

	// Android Settings
#ifdef __ANDROID__
	settings->setDefault("debug_log_level", "error");

	// Set font_path
	settings->setDefault("mono_font_path", "/system/fonts/DroidSansMono.ttf");
	settings->setDefault("fallback_font_path", "/system/fonts/DroidSans.ttf");

	// Auto-detect language on Android
	AConfiguration_getLanguage(porting::app_global->config, lang);

	// Check screen size
	double x_inches = (double) porting::getDisplaySize().X /
	                   (160 * porting::getDisplayDensity());
	std::string font_size_str_small = std::to_string(TTF_DEFAULT_FONT_SIZE - 1);

	if (x_inches <= 3.7) {
		// small 4" phones
		settings->setDefault("hud_scaling", "0.55");
		settings->setDefault("font_size", font_size_str_small);
		settings->setDefault("mouse_sensitivity", "0.3");
		settings->setDefault("hud_small", "true");
	} else if (x_inches > 3.7 && x_inches <= 4.5) {
		// medium phones
		settings->setDefault("hud_scaling", "0.6");
		settings->setDefault("font_size", font_size_str_small);
		settings->setDefault("hud_small", "true");
		settings->setDefault("selectionbox_width", "6");
	} else if (x_inches > 4.5 && x_inches <= 5) {
		// large 6" phones
		settings->setDefault("hud_scaling", "0.7");
		settings->setDefault("mouse_sensitivity", "0.15");
		settings->setDefault("hud_small", "true");
		settings->setDefault("selectionbox_width", "6");
	} else if (x_inches > 5 && x_inches <= 6) {
		// 7" tablets
		settings->setDefault("hud_scaling", "0.9");
		settings->setDefault("hud_small", "true");
		settings->setDefault("selectionbox_width", "6");
	}
#endif // Android

	// iOS Settings
#ifdef __IOS__
	settings->setDefault("enable_minimap", "false");
	settings->setDefault("debug_log_level", "none");
	settings->setDefault("password_save", "true");

	// Set font_path
	settings->setDefault("mono_font_path", g_settings->get("font_path"));
	settings->setDefault("fallback_font_path", g_settings->get("font_path"));

	// Auto-detect language on iOS
	NSString *syslang = [[NSLocale preferredLanguages] firstObject];
	[syslang getBytes:lang maxLength:2 usedLength:nil encoding:NSASCIIStringEncoding options:0 range:NSMakeRange(0, 2) remainingRange:nil];

	if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPhone)
		settings->setDefault("hud_small", "true");

	// Set the size of the elements depending on the screen size
	if ([SDVersion deviceVersion] == iPhone4S) {
		// 3.5" iPhone
		settings->setDefault("hud_scaling", "0.5");
	} else if SDVersion4Inch {
		// 4" iPhone and iPod Touch
		settings->setDefault("hud_scaling", "0.55");
		settings->setDefault("mouse_sensitivity", "0.33");
	} else if SDVersion4and7Inch {
		// 4.7" iPhone
		settings->setDefault("hud_scaling", "0.65");
		settings->setDefault("mouse_sensitivity", "0.27");
	} else if SDVersion5and5Inch {
		// 5.5" iPhone Plus
		settings->setDefault("hud_scaling", "0.7");
		settings->setDefault("mouse_sensitivity", "0.3");
	} else if (SDVersion5and8Inch || SDVersion6and1Inch || SDVersion6and5Inch) {
		// 5.8+" iPhones
		settings->setDefault("hud_scaling", "0.85");
		settings->setDefault("mouse_sensitivity", "0.35");
		settings->setDefault("selectionbox_width", "6");
	} else if SDVersion7and9Inch {
		// iPad mini
		settings->setDefault("hud_scaling", "0.9");
		settings->setDefault("mouse_sensitivity", "0.25");
		settings->setDefault("selectionbox_width", "6");
	} else {
		// iPad
		settings->setDefault("mouse_sensitivity", "0.3");
		settings->setDefault("selectionbox_width", "6");
	}

	// Settings for the Rounded Screen and Home Bar
	if SDVersionHomeBar {
		settings->setDefault("hud_move_upwards", "20");
		settings->setDefault("round_screen", "35");
	}
#endif // iOS

	settings->setDefault("language", lang);
#endif
}

void override_default_settings(Settings *settings, Settings *from) {
	std::vector<std::string> names = from->getNames();
	for (const auto & name : names)
		settings->setDefault(name, from->get(name));
}
