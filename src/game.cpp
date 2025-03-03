/*
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

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

#include "game.h"

#include <iomanip>
#include "camera.h"
#include "client.h"
#include "client/inputhandler.h"
#include "client/tile.h"     // For TextureSource
#include "client/keys.h"
#include "client/joystick_controller.h"
#include "clientmap.h"
#include "clouds.h"
#include "config.h"
#include "content_cao.h"
#include "drawscene.h"
#include "event_manager.h"
#include "fontengine.h"
#include "itemdef.h"
#include "log.h"
#include "filesys.h"
#include "gettext.h"
#include "guiChatConsole.h"
#include "guiFormSpecMenu.h"
#include "guiKeyChangeMenu.h"
#include "guiPasswordChange.h"
#include "guiVolumeChange.h"
#include "mainmenumanager.h"
#include "mapblock.h"
#include "nodedef.h"         // Needed for determining pointing to nodes
#include "nodemetadata.h"
#include "particles.h"
#include "profiler.h"
#include "quicktune_shortcutter.h"
#include "server.h"
#include "settings.h"
#include "sky.h"
#include "subgame.h"
#include "tool.h"
#include "util/basic_macros.h"
#include "util/directiontables.h"
#include "util/pointedthing.h"
#include "irrlicht_changes/static_text.h"
#include "version.h"

#include "database.h"

#include "script/scripting_client.h"

#if USE_SOUND
	#include "sound_openal.h"
#endif

extern Settings *g_settings;
extern Profiler *g_profiler;

/*
	Text input system
*/

struct TextDestNodeMetadata : public TextDest
{
	TextDestNodeMetadata(v3s16 p, Client *client)
	{
		m_p = p;
		m_client = client;
	}
	// This is deprecated I guess? -celeron55
	void gotText(const std::wstring &text)
	{
		std::string ntext = wide_to_utf8(text);
		infostream << "Submitting 'text' field of node at (" << m_p.X << ","
			   << m_p.Y << "," << m_p.Z << "): " << ntext << std::endl;
		StringMap fields;
		fields["text"] = ntext;
		m_client->sendNodemetaFields(m_p, "", fields);
	}
	void gotText(const StringMap &fields)
	{
		m_client->sendNodemetaFields(m_p, "", fields);
	}

	v3s16 m_p;
	Client *m_client;
};

struct TextDestPlayerInventory : public TextDest
{
	TextDestPlayerInventory(Client *client)
	{
		m_client = client;
		m_formname = "";
	}
	TextDestPlayerInventory(Client *client, const std::string &formname)
	{
		m_client = client;
		m_formname = formname;
	}
	void gotText(const StringMap &fields)
	{
		m_client->sendInventoryFields(m_formname, fields);
	}

	Client *m_client;
};

struct LocalFormspecHandler : public TextDest
{
	LocalFormspecHandler(const std::string &formname):
		m_client(NULL)
	{
		m_formname = formname;
	}

	LocalFormspecHandler(const std::string &formname, Client *client):
		m_client(client)
	{
		m_formname = formname;
	}

	void gotText(const StringMap &fields)
	{
		if (m_formname == "MT_PAUSE_MENU") {
			if (fields.find("btn_sound") != fields.end()) {
				g_gamecallback->changeVolume();
				return;
			}

			if (fields.find("btn_key_config") != fields.end()) {
				g_gamecallback->keyConfig();
				return;
			}

			if (fields.find("btn_exit_menu") != fields.end()) {
				g_gamecallback->disconnect();
				return;
			}

			if (fields.find("btn_exit_os") != fields.end()) {
				g_gamecallback->exitToOS();
				return;
			}

			if (fields.find("btn_change_password") != fields.end()) {
				g_gamecallback->changePassword();
				return;
			}

			if (fields.find("quit") != fields.end()) {
				return;
			}

			if (fields.find("btn_continue") != fields.end()) {
				return;
			}
		}

#ifdef DISABLE_CSM
		if (m_formname == "MT_DEATH_SCREEN") {
			assert(m_client);

			if (fields.find("btn_respawn") != fields.end() ||
					fields.find("quit") != fields.end()) {
				m_client->sendRespawn();
				return;
			}
		}
#endif

		// Don't disable this part when modding is disabled, it's used in builtin
		if (m_client && m_client->getScript())
			m_client->getScript()->on_formspec_input(m_formname, fields);
	}

	Client *m_client;
};

/* Form update callback */

class NodeMetadataFormSource: public IFormSource
{
public:
	NodeMetadataFormSource(ClientMap *map, v3s16 p):
		m_map(map),
		m_p(p)
	{
	}
	std::string getForm()
	{
		NodeMetadata *meta = m_map->getNodeMetadata(m_p);

		if (!meta)
			return "";

		return meta->getString("formspec");
	}

	virtual std::string resolveText(const std::string &str)
	{
		NodeMetadata *meta = m_map->getNodeMetadata(m_p);

		if (!meta)
			return str;

		return meta->resolveString(str);
	}

	ClientMap *m_map;
	v3s16 m_p;
};

class PlayerInventoryFormSource: public IFormSource
{
public:
	PlayerInventoryFormSource(Client *client):
		m_client(client)
	{
	}
	std::string getForm()
	{
		LocalPlayer *player = m_client->getEnv().getLocalPlayer();
		return player->inventory_formspec;
	}

	Client *m_client;
};

/* Profiler display */

void update_profiler_gui(gui::IGUIStaticText *guitext_profiler, FontEngine *fe,
		u32 show_profiler, u32 show_profiler_max, s32 screen_height)
{
	if (show_profiler == 0) {
		guitext_profiler->setVisible(false);
	} else {

		std::ostringstream os(std::ios_base::binary);
		g_profiler->printPage(os, show_profiler, show_profiler_max);
		std::wstring text = utf8_to_wide(os.str());
		setStaticText(guitext_profiler, text.c_str());
		guitext_profiler->setVisible(true);

		s32 w = fe->getTextWidth(text.c_str());

		if (w < 400)
			w = 400;

		unsigned text_height = fe->getTextHeight();

		core::position2di upper_left, lower_right;

		upper_left.X  = 6;
		upper_left.Y  = (text_height + 5) * 2;
		lower_right.X = 12 + w;
		lower_right.Y = upper_left.Y + (text_height + 1) * MAX_PROFILER_TEXT_ROWS;

		if (lower_right.Y > screen_height * 2 / 3)
			lower_right.Y = screen_height * 2 / 3;

		core::rect<s32> rect(upper_left, lower_right);

		guitext_profiler->setRelativePosition(rect);
		guitext_profiler->setVisible(true);
	}
}

class ProfilerGraph
{
private:
	struct Piece {
		Profiler::GraphValues values;
	};
	struct Meta {
		float min;
		float max;
		video::SColor color;
		Meta(float initial = 0,
			video::SColor color = video::SColor(255, 255, 255, 255)):
			min(initial),
			max(initial),
			color(color)
		{}
	};
	std::deque<Piece> m_log;
public:
	u32 m_log_max_size;

	ProfilerGraph():
		m_log_max_size(200)
	{}

	void put(const Profiler::GraphValues &values)
	{
		Piece piece;
		piece.values = values;
		m_log.push_back(piece);

		while (m_log.size() > m_log_max_size)
			m_log.erase(m_log.begin());
	}

	void draw(s32 x_left, s32 y_bottom, video::IVideoDriver *driver,
		  gui::IGUIFont *font) const
	{
		// Do *not* use UNORDERED_MAP here as the order needs
		// to be the same for each call to prevent flickering
		std::map<std::string, Meta> m_meta;

		for (std::deque<Piece>::const_iterator k = m_log.begin();
				k != m_log.end(); ++k) {
			const Piece &piece = *k;

			for (Profiler::GraphValues::const_iterator i = piece.values.begin();
					i != piece.values.end(); ++i) {
				const std::string &id = i->first;
				const float &value = i->second;
				std::map<std::string, Meta>::iterator j = m_meta.find(id);

				if (j == m_meta.end()) {
					m_meta[id] = Meta(value);
					continue;
				}

				if (value < j->second.min)
					j->second.min = value;

				if (value > j->second.max)
					j->second.max = value;
			}
		}

		// Assign colors
		static const video::SColor usable_colors[] = {
			video::SColor(255, 255, 100, 100),
			video::SColor(255, 90, 225, 90),
			video::SColor(255, 100, 100, 255),
			video::SColor(255, 255, 150, 50),
			video::SColor(255, 220, 220, 100)
		};
		static const u32 usable_colors_count =
			sizeof(usable_colors) / sizeof(*usable_colors);
		u32 next_color_i = 0;

		for (std::map<std::string, Meta>::iterator i = m_meta.begin();
				i != m_meta.end(); ++i) {
			Meta &meta = i->second;
			video::SColor color(255, 200, 200, 200);

			if (next_color_i < usable_colors_count)
				color = usable_colors[next_color_i++];

			meta.color = color;
		}

		s32 graphh = 50;
		s32 textx = x_left + m_log_max_size + 15;
		s32 textx2 = textx + 200 - 15;
		s32 meta_i = 0;

		for (std::map<std::string, Meta>::const_iterator i = m_meta.begin();
				i != m_meta.end(); ++i) {
			const std::string &id = i->first;
			const Meta &meta = i->second;
			s32 x = x_left;
			s32 y = y_bottom - meta_i * 50;
			float show_min = meta.min;
			float show_max = meta.max;

			if (show_min >= -0.0001 && show_max >= -0.0001) {
				if (show_min <= show_max * 0.5)
					show_min = 0;
			}

			s32 texth = 15;
			char buf[10];
			snprintf(buf, 10, "%.3g", show_max);
			font->draw(utf8_to_wide(buf).c_str(),
					core::rect<s32>(textx, y - graphh,
						   textx2, y - graphh + texth),
					meta.color);
			snprintf(buf, 10, "%.3g", show_min);
			font->draw(utf8_to_wide(buf).c_str(),
					core::rect<s32>(textx, y - texth,
						   textx2, y),
					meta.color);
			font->draw(utf8_to_wide(id).c_str(),
					core::rect<s32>(textx, y - graphh / 2 - texth / 2,
						   textx2, y - graphh / 2 + texth / 2),
					meta.color);
			s32 graph1y = y;
			s32 graph1h = graphh;
			bool relativegraph = (show_min != 0 && show_min != show_max);
			float lastscaledvalue = 0.0;
			bool lastscaledvalue_exists = false;

			for (std::deque<Piece>::const_iterator j = m_log.begin();
					j != m_log.end(); ++j) {
				const Piece &piece = *j;
				float value = 0;
				bool value_exists = false;
				Profiler::GraphValues::const_iterator k =
					piece.values.find(id);

				if (k != piece.values.end()) {
					value = k->second;
					value_exists = true;
				}

				if (!value_exists) {
					x++;
					lastscaledvalue_exists = false;
					continue;
				}

				float scaledvalue = 1.0;

				if (show_max != show_min)
					scaledvalue = (value - show_min) / (show_max - show_min);

				if (scaledvalue == 1.0 && value == 0) {
					x++;
					lastscaledvalue_exists = false;
					continue;
				}

				if (relativegraph) {
					if (lastscaledvalue_exists) {
						s32 ivalue1 = lastscaledvalue * graph1h;
						s32 ivalue2 = scaledvalue * graph1h;
						driver->draw2DLine(v2s32(x - 1, graph1y - ivalue1),
								   v2s32(x, graph1y - ivalue2), meta.color);
					}

					lastscaledvalue = scaledvalue;
					lastscaledvalue_exists = true;
				} else {
					s32 ivalue = scaledvalue * graph1h;
					driver->draw2DLine(v2s32(x, graph1y),
							   v2s32(x, graph1y - ivalue), meta.color);
				}

				x++;
			}

			meta_i++;
		}
	}
};

class NodeDugEvent: public MtEvent
{
public:
	v3s16 p;
	MapNode n;

	NodeDugEvent(v3s16 p, MapNode n):
		p(p),
		n(n)
	{}
	const char *getType() const
	{
		return "NodeDug";
	}
};

class SoundMaker
{
	ISoundManager *m_sound;
	INodeDefManager *m_ndef;
public:
	bool makes_footstep_sound;
	float m_player_step_timer;

	SimpleSoundSpec m_player_step_sound;
	SimpleSoundSpec m_player_leftpunch_sound;
	SimpleSoundSpec m_player_rightpunch_sound;

	SoundMaker(ISoundManager *sound, INodeDefManager *ndef):
		m_sound(sound),
		m_ndef(ndef),
		makes_footstep_sound(true),
		m_player_step_timer(0)
	{
	}

	void playPlayerStep()
	{
		if (m_player_step_timer <= 0 && m_player_step_sound.exists()) {
			m_player_step_timer = 0.03;
			if (makes_footstep_sound)
				m_sound->playSound(m_player_step_sound, false);
		}
	}

	static void viewBobbingStep(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->playPlayerStep();
	}

	static void playerRegainGround(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->playPlayerStep();
	}

	static void playerJump(MtEvent *e, void *data)
	{
		//SoundMaker *sm = (SoundMaker*)data;
	}

	static void cameraPunchLeft(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->m_sound->playSound(sm->m_player_leftpunch_sound, false);
	}

	static void cameraPunchRight(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->m_sound->playSound(sm->m_player_rightpunch_sound, false);
	}

	static void nodeDug(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		NodeDugEvent *nde = (NodeDugEvent *)e;
		sm->m_sound->playSound(sm->m_ndef->get(nde->n).sound_dug, false);
	}

	static void playerDamage(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->m_sound->playSound(SimpleSoundSpec("player_damage", 1.0), false);
	}

	static void playerFallingDamage(MtEvent *e, void *data)
	{
		SoundMaker *sm = (SoundMaker *)data;
		sm->m_sound->playSound(SimpleSoundSpec("player_falling_damage", 0.5), false);
	}

	void registerReceiver(MtEventManager *mgr)
	{
		mgr->reg("ViewBobbingStep", SoundMaker::viewBobbingStep, this);
		mgr->reg("PlayerRegainGround", SoundMaker::playerRegainGround, this);
		mgr->reg("PlayerJump", SoundMaker::playerJump, this);
		mgr->reg("CameraPunchLeft", SoundMaker::cameraPunchLeft, this);
		mgr->reg("CameraPunchRight", SoundMaker::cameraPunchRight, this);
		mgr->reg("NodeDug", SoundMaker::nodeDug, this);
		mgr->reg("PlayerDamage", SoundMaker::playerDamage, this);
		mgr->reg("PlayerFallingDamage", SoundMaker::playerFallingDamage, this);
	}

	void step(float dtime)
	{
		m_player_step_timer -= dtime;
	}
};

// Locally stored sounds don't need to be preloaded because of this
class GameOnDemandSoundFetcher: public OnDemandSoundFetcher
{
	std::set<std::string> m_fetched;
private:
	void paths_insert(std::set<std::string> &dst_paths,
		const std::string &base,
		const std::string &name)
	{
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".0.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".1.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".2.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".3.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".4.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".5.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".6.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".7.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".8.ogg");
		dst_paths.insert(base + DIR_DELIM + "sounds" + DIR_DELIM + name + ".9.ogg");
	}
public:
	void fetchSounds(const std::string &name,
		std::set<std::string> &dst_paths,
		std::set<std::string> &dst_datas)
	{
		if (m_fetched.count(name))
			return;

		m_fetched.insert(name);

		paths_insert(dst_paths, porting::path_share, name);
		paths_insert(dst_paths, porting::path_user,  name);
	}
};


// before 1.8 there isn't a "integer interface", only float
#if (IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR < 8)
typedef f32 SamplerLayer_t;
#else
typedef s32 SamplerLayer_t;
#endif


class GameGlobalShaderConstantSetter : public IShaderConstantSetter
{
	Sky *m_sky;
	bool *m_force_fog_off;
	f32 *m_fog_range;
	bool m_fog_enabled;
	CachedPixelShaderSetting<float, 4> m_sky_bg_color;
	CachedPixelShaderSetting<float> m_fog_distance;
	CachedVertexShaderSetting<float> m_animation_timer_vertex;
	CachedPixelShaderSetting<float> m_animation_timer_pixel;
	CachedPixelShaderSetting<float, 3> m_day_light;
	CachedPixelShaderSetting<float, 3> m_eye_position_pixel;
	CachedVertexShaderSetting<float, 3> m_eye_position_vertex;
	CachedPixelShaderSetting<float, 3> m_minimap_yaw;
	CachedPixelShaderSetting<SamplerLayer_t> m_base_texture;
	CachedPixelShaderSetting<SamplerLayer_t> m_normal_texture;
	CachedPixelShaderSetting<SamplerLayer_t> m_texture_flags;
	Client *m_client;

public:
	void onSettingsChange(const std::string &name)
	{
		if (name == "enable_fog")
			m_fog_enabled = g_settings->getBool("enable_fog");
	}

	static void settingsCallback(const std::string &name, void *userdata)
	{
		reinterpret_cast<GameGlobalShaderConstantSetter*>(userdata)->onSettingsChange(name);
	}

	void setSky(Sky *sky) { m_sky = sky; }

	GameGlobalShaderConstantSetter(Sky *sky, bool *force_fog_off,
			f32 *fog_range, Client *client) :
		m_sky(sky),
		m_force_fog_off(force_fog_off),
		m_fog_range(fog_range),
		m_sky_bg_color("skyBgColor"),
		m_fog_distance("fogDistance"),
		m_animation_timer_vertex("animationTimer"),
		m_animation_timer_pixel("animationTimer"),
		m_day_light("dayLight"),
		m_eye_position_pixel("eyePosition"),
		m_eye_position_vertex("eyePosition"),
		m_minimap_yaw("yawVec"),
		m_base_texture("baseTexture"),
		m_normal_texture("normalTexture"),
		m_texture_flags("textureFlags"),
		m_client(client)
	{
		g_settings->registerChangedCallback("enable_fog", settingsCallback, this);
		m_fog_enabled = g_settings->getBool("enable_fog");
	}

	~GameGlobalShaderConstantSetter()
	{
		g_settings->deregisterChangedCallback("enable_fog", settingsCallback, this);
	}

	virtual void onSetConstants(video::IMaterialRendererServices *services,
			bool is_highlevel)
	{
		if (!is_highlevel)
			return;

		// Background color
		video::SColor bgcolor = m_sky->getBgColor();
		video::SColorf bgcolorf(bgcolor);
		float bgcolorfa[4] = {
			bgcolorf.r,
			bgcolorf.g,
			bgcolorf.b,
			bgcolorf.a,
		};
		m_sky_bg_color.set(bgcolorfa, services);

		// Fog distance
		float fog_distance = 10000 * BS;

		if (m_fog_enabled && !*m_force_fog_off)
			fog_distance = *m_fog_range;

		m_fog_distance.set(&fog_distance, services);

		u32 daynight_ratio = (float)m_client->getEnv().getDayNightRatio();
		video::SColorf sunlight;
		get_sunlight_color(&sunlight, daynight_ratio);
		float dnc[3] = {
			sunlight.r,
			sunlight.g,
			sunlight.b };
		m_day_light.set(dnc, services);

		u32 animation_timer = porting::getTimeMs() % 100000;
		float animation_timer_f = (float)animation_timer / 100000.f;
		m_animation_timer_vertex.set(&animation_timer_f, services);
		m_animation_timer_pixel.set(&animation_timer_f, services);

		float eye_position_array[3];
		v3f epos = m_client->getEnv().getLocalPlayer()->getEyePosition();
#if (IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR < 8)
		eye_position_array[0] = epos.X;
		eye_position_array[1] = epos.Y;
		eye_position_array[2] = epos.Z;
#else
		epos.getAs3Values(eye_position_array);
#endif
		m_eye_position_pixel.set(eye_position_array, services);
		m_eye_position_vertex.set(eye_position_array, services);

		if (m_client->getMinimap()) {
			float minimap_yaw_array[3];
			v3f minimap_yaw = m_client->getMinimap()->getYawVec();
#if (IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR < 8)
			minimap_yaw_array[0] = minimap_yaw.X;
			minimap_yaw_array[1] = minimap_yaw.Y;
			minimap_yaw_array[2] = minimap_yaw.Z;
#else
			minimap_yaw.getAs3Values(minimap_yaw_array);
#endif
			m_minimap_yaw.set(minimap_yaw_array, services);

		}

		SamplerLayer_t base_tex = 0,
				normal_tex = 1,
				flags_tex = 2;
		m_base_texture.set(&base_tex, services);
		m_normal_texture.set(&normal_tex, services);
		m_texture_flags.set(&flags_tex, services);
	}
};


class GameGlobalShaderConstantSetterFactory : public IShaderConstantSetterFactory
{
	Sky *m_sky;
	bool *m_force_fog_off;
	f32 *m_fog_range;
	Client *m_client;
	std::vector<GameGlobalShaderConstantSetter *> created_nosky;
public:
	GameGlobalShaderConstantSetterFactory(bool *force_fog_off,
			f32 *fog_range, Client *client) :
		m_sky(NULL),
		m_force_fog_off(force_fog_off),
		m_fog_range(fog_range),
		m_client(client)
	{}

	void setSky(Sky *sky) {
		m_sky = sky;
		for (size_t i = 0; i < created_nosky.size(); ++i) {
			created_nosky[i]->setSky(m_sky);
		}
		created_nosky.clear();
	}

	virtual IShaderConstantSetter* create()
	{
		GameGlobalShaderConstantSetter *scs = new GameGlobalShaderConstantSetter(
				m_sky, m_force_fog_off, m_fog_range, m_client);
		if (!m_sky)
			created_nosky.push_back(scs);
		return scs;
	}
};


bool nodePlacementPrediction(Client &client, const ItemDefinition &playeritem_def,
	const ItemStack &playeritem, v3s16 nodepos, v3s16 neighbourpos)
{
	std::string prediction = playeritem_def.node_placement_prediction;
	INodeDefManager *nodedef = client.ndef();
	ClientMap &map = client.getEnv().getClientMap();
	MapNode node;
	bool is_valid_position;

	node = map.getNodeNoEx(nodepos, &is_valid_position);
	if (!is_valid_position)
		return false;

	if (prediction != "" && !nodedef->get(node).rightclickable) {
		verbosestream << "Node placement prediction for "
			      << playeritem_def.name << " is "
			      << prediction << std::endl;
		v3s16 p = neighbourpos;

		// Place inside node itself if buildable_to
		MapNode n_under = map.getNodeNoEx(nodepos, &is_valid_position);
		if (is_valid_position)
		{
			if (nodedef->get(n_under).buildable_to)
				p = nodepos;
			else {
				node = map.getNodeNoEx(p, &is_valid_position);
				if (is_valid_position &&!nodedef->get(node).buildable_to)
					return false;
			}
		}

		// Find id of predicted node
		content_t id;
		bool found = nodedef->getId(prediction, id);

		if (!found) {
			errorstream << "Node placement prediction failed for "
				    << playeritem_def.name << " (places "
				    << prediction
				    << ") - Name not known" << std::endl;
			return false;
		}

		const ContentFeatures &predicted_f = nodedef->get(id);

		// Predict param2 for facedir and wallmounted nodes
		u8 param2 = 0;

		if (predicted_f.param_type_2 == CPT2_WALLMOUNTED ||
				predicted_f.param_type_2 == CPT2_COLORED_WALLMOUNTED) {
			v3s16 dir = nodepos - neighbourpos;

			if (abs(dir.Y) > MYMAX(abs(dir.X), abs(dir.Z))) {
				param2 = dir.Y < 0 ? 1 : 0;
			} else if (abs(dir.X) > abs(dir.Z)) {
				param2 = dir.X < 0 ? 3 : 2;
			} else {
				param2 = dir.Z < 0 ? 5 : 4;
			}
		}

		if (predicted_f.param_type_2 == CPT2_FACEDIR ||
				predicted_f.param_type_2 == CPT2_COLORED_FACEDIR) {
			v3s16 dir = nodepos - floatToInt(client.getEnv().getLocalPlayer()->getPosition(), BS);

			if (abs(dir.X) > abs(dir.Z)) {
				param2 = dir.X < 0 ? 3 : 1;
			} else {
				param2 = dir.Z < 0 ? 2 : 0;
			}
		}

		assert(param2 <= 5);

		//Check attachment if node is in group attached_node
		if (((ItemGroupList) predicted_f.groups)["attached_node"] != 0) {
			static v3s16 wallmounted_dirs[8] = {
				v3s16(0, 1, 0),
				v3s16(0, -1, 0),
				v3s16(1, 0, 0),
				v3s16(-1, 0, 0),
				v3s16(0, 0, 1),
				v3s16(0, 0, -1),
			};
			v3s16 pp;

			if (predicted_f.param_type_2 == CPT2_WALLMOUNTED ||
					predicted_f.param_type_2 == CPT2_COLORED_WALLMOUNTED)
				pp = p + wallmounted_dirs[param2];
			else
				pp = p + v3s16(0, -1, 0);

			if (!nodedef->get(map.getNodeNoEx(pp)).walkable)
				return false;
		}

		// Apply color
		if ((predicted_f.param_type_2 == CPT2_COLOR
				|| predicted_f.param_type_2 == CPT2_COLORED_FACEDIR
				|| predicted_f.param_type_2 == CPT2_COLORED_WALLMOUNTED)) {
			const std::string &indexstr = playeritem.metadata.getString(
				"palette_index", 0);
			if (!indexstr.empty()) {
				s32 index = mystoi(indexstr);
				if (predicted_f.param_type_2 == CPT2_COLOR) {
					param2 = index;
				} else if (predicted_f.param_type_2
						== CPT2_COLORED_WALLMOUNTED) {
					// param2 = pure palette index + other
					param2 = (index & 0xf8) | (param2 & 0x07);
				} else if (predicted_f.param_type_2
						== CPT2_COLORED_FACEDIR) {
					// param2 = pure palette index + other
					param2 = (index & 0xe0) | (param2 & 0x1f);
				}
			}
		}

		// Add node to client map
		MapNode n(id, 0, param2);

		try {
			LocalPlayer *player = client.getEnv().getLocalPlayer();

			// Dont place node when player would be inside new node
			// NOTE: This is to be eventually implemented by a mod as client-side Lua
			if (!nodedef->get(n).walkable ||
					g_settings->getBool("enable_build_where_you_stand") ||
					(client.checkPrivilege("noclip") && g_settings->getBool("noclip")) ||
					(nodedef->get(n).walkable &&
					 neighbourpos != player->getStandingNodePos() + v3s16(0, 1, 0) &&
					 neighbourpos != player->getStandingNodePos() + v3s16(0, 2, 0))) {

				// This triggers the required mesh update too
				client.addNode(p, n);
				return true;
			}
		} catch (InvalidPositionException &e) {
			errorstream << "Node placement prediction failed for "
				    << playeritem_def.name << " (places "
				    << prediction
				    << ") - Position not loaded" << std::endl;
		}
	}

	return false;
}

static inline void create_formspec_menu(GUIFormSpecMenu **cur_formspec,
		Client *client, IrrlichtDevice *device, JoystickController *joystick,
		IFormSource *fs_src, TextDest *txt_dest)
{

	if (*cur_formspec == 0) {
		*cur_formspec = new GUIFormSpecMenu(device, joystick,
			guiroot, -1, &g_menumgr, client, client->getTextureSource(),
			fs_src, txt_dest);
		(*cur_formspec)->doPause = false;

		/*
			Caution: do not call (*cur_formspec)->drop() here --
			the reference might outlive the menu, so we will
			periodically check if *cur_formspec is the only
			remaining reference (i.e. the menu was removed)
			and delete it in that case.
		*/

	} else {
		(*cur_formspec)->setFormSource(fs_src);
		(*cur_formspec)->setTextDest(txt_dest);
	}

}

#define SIZE_TAG "size[11,5.5]"

/******************************************************************************/
static void updateChat(Client &client, f32 dtime, bool show_debug,
		const v2u32 &screensize, bool show_chat, u32 show_profiler,
		ChatBackend &chat_backend, gui::IGUIStaticText *guitext_chat)
{
	// Add chat log output for errors to be shown in chat
	static LogOutputBuffer chat_log_error_buf(g_logger, LL_ERROR);

	// Get new messages from error log buffer
	while (!chat_log_error_buf.empty()) {
		std::wstring error_message = utf8_to_wide(chat_log_error_buf.get());
		if (!g_settings->getBool("disable_escape_sequences")) {
			error_message = L"\x1b(c@red)" + error_message + L"\x1b(c@white)";
		}
		chat_backend.addMessage(L"", error_message);
	}

	// Get new messages from client
	std::wstring message;

	while (client.getChatMessage(message)) {
		chat_backend.addUnparsedMessage(message);
	}

	// Remove old messages
	chat_backend.step(dtime);

	// Display all messages in a static text element
	unsigned int recent_chat_count = chat_backend.getRecentBuffer().getLineCount();
	EnrichedString recent_chat     = chat_backend.getRecentChat();
	unsigned int line_height       = g_fontengine->getLineHeight();

	setStaticText(guitext_chat, recent_chat);

	// Update gui element size and position
	s32 chat_y = 5 + line_height;

	if (show_debug)
		chat_y += line_height;

	// first pass to calculate height of text to be set
	s32 width = std::min(g_fontengine->getTextWidth(recent_chat.c_str()) + 10,
			     porting::getWindowSize().X - 20);
	core::rect<s32> rect(10, chat_y, width, chat_y + porting::getWindowSize().Y);
	guitext_chat->setRelativePosition(rect);

	//now use real height of text and adjust rect according to this size
	rect = core::rect<s32>(10, chat_y, width,
			       chat_y + guitext_chat->getTextHeight());


	guitext_chat->setRelativePosition(rect);
	// Don't show chat if disabled or empty or profiler is enabled
	guitext_chat->setVisible(
		show_chat && recent_chat_count != 0 && !show_profiler);
}


/****************************************************************************
 Fast key cache for main game loop
 ****************************************************************************/

/* This is faster than using getKeySetting with the tradeoff that functions
 * using it must make sure that it's initialised before using it and there is
 * no error handling (for example bounds checking). This is really intended for
 * use only in the main running loop of the client (the_game()) where the faster
 * (up to 10x faster) key lookup is an asset. Other parts of the codebase
 * (e.g. formspecs) should continue using getKeySetting().
 */
struct KeyCache {

	KeyCache()
	{
		handler = NULL;
		populate();
		populate_nonchanging();
	}

	void populate();

	// Keys that are not settings dependent
	void populate_nonchanging();

	KeyPress key[KeyType::INTERNAL_ENUM_COUNT];
	InputHandler *handler;
};

void KeyCache::populate_nonchanging()
{
	key[KeyType::ESC] = EscapeKey;
}

void KeyCache::populate()
{
	key[KeyType::FORWARD]      = getKeySetting("keymap_forward");
	key[KeyType::BACKWARD]     = getKeySetting("keymap_backward");
	key[KeyType::LEFT]         = getKeySetting("keymap_left");
	key[KeyType::RIGHT]        = getKeySetting("keymap_right");
	key[KeyType::JUMP]         = getKeySetting("keymap_jump");
	key[KeyType::SPECIAL1]     = getKeySetting("keymap_special1");
	key[KeyType::SNEAK]        = getKeySetting("keymap_sneak");

	key[KeyType::AUTORUN]      = getKeySetting("keymap_autorun");

	key[KeyType::DROP]         = getKeySetting("keymap_drop");
	key[KeyType::INVENTORY]    = getKeySetting("keymap_inventory");
	key[KeyType::CHAT]         = getKeySetting("keymap_chat");
	key[KeyType::CMD]          = getKeySetting("keymap_cmd");
	key[KeyType::CMD_LOCAL]    = getKeySetting("keymap_cmd_local");
	key[KeyType::CONSOLE]      = getKeySetting("keymap_console");
	key[KeyType::MINIMAP]      = getKeySetting("keymap_minimap");
	key[KeyType::FREEMOVE]     = getKeySetting("keymap_freemove");
	key[KeyType::FASTMOVE]     = getKeySetting("keymap_fastmove");
	key[KeyType::NOCLIP]       = getKeySetting("keymap_noclip");
	key[KeyType::HOTBAR_PREV]  = getKeySetting("keymap_hotbar_previous");
	key[KeyType::HOTBAR_NEXT]  = getKeySetting("keymap_hotbar_next");
	key[KeyType::MUTE]         = getKeySetting("keymap_mute");
	key[KeyType::INC_VOLUME]   = getKeySetting("keymap_increase_volume");
	key[KeyType::DEC_VOLUME]   = getKeySetting("keymap_decrease_volume");
	key[KeyType::CINEMATIC]    = getKeySetting("keymap_cinematic");
	key[KeyType::SCREENSHOT]   = getKeySetting("keymap_screenshot");
	key[KeyType::TOGGLE_HUD]   = getKeySetting("keymap_toggle_hud");
	key[KeyType::TOGGLE_CHAT]  = getKeySetting("keymap_toggle_chat");
	key[KeyType::TOGGLE_FORCE_FOG_OFF]
			= getKeySetting("keymap_toggle_force_fog_off");
	key[KeyType::TOGGLE_UPDATE_CAMERA]
			= getKeySetting("keymap_toggle_update_camera");
	key[KeyType::TOGGLE_DEBUG]
			= getKeySetting("keymap_toggle_debug");
	key[KeyType::TOGGLE_PROFILER]
			= getKeySetting("keymap_toggle_profiler");
	key[KeyType::CAMERA_MODE]
			= getKeySetting("keymap_camera_mode");
	key[KeyType::INCREASE_VIEWING_RANGE]
			= getKeySetting("keymap_increase_viewing_range_min");
	key[KeyType::DECREASE_VIEWING_RANGE]
			= getKeySetting("keymap_decrease_viewing_range_min");
	key[KeyType::RANGESELECT]
			= getKeySetting("keymap_rangeselect");
	key[KeyType::ZOOM] = getKeySetting("keymap_zoom");

	key[KeyType::QUICKTUNE_NEXT] = getKeySetting("keymap_quicktune_next");
	key[KeyType::QUICKTUNE_PREV] = getKeySetting("keymap_quicktune_prev");
	key[KeyType::QUICKTUNE_INC]  = getKeySetting("keymap_quicktune_inc");
	key[KeyType::QUICKTUNE_DEC]  = getKeySetting("keymap_quicktune_dec");

	key[KeyType::DEBUG_STACKS]   = getKeySetting("keymap_print_debug_stacks");

	if (handler) {
		// First clear all keys, then re-add the ones we listen for
		handler->dontListenForKeys();
		for (size_t i = 0; i < KeyType::INTERNAL_ENUM_COUNT; i++) {
			handler->listenForKey(key[i]);
		}
		handler->listenForKey(EscapeKey);
		handler->listenForKey(CancelKey);
		for (size_t i = 0; i < 10; i++) {
			handler->listenForKey(NumberKey[i]);
		}
	}
}


/****************************************************************************

 ****************************************************************************/

const float object_hit_delay = 0.2;

struct FpsControl {
	u32 last_time, busy_time, sleep_time;
};


/* The reason the following structs are not anonymous structs within the
 * class is that they are not used by the majority of member functions and
 * many functions that do require objects of thse types do not modify them
 * (so they can be passed as a const qualified parameter)
 */
struct CameraOrientation {
	f32 camera_yaw;    // "right/left"
	f32 camera_pitch;  // "up/down"
};

struct GameRunData {
	u16 dig_index;
	u16 new_playeritem;
	PointedThing pointed_old;
	bool digging;
	bool ldown_for_dig;
	bool dig_instantly;
	bool left_punch;
	bool update_wielded_item_trigger;
	bool reset_jump_timer;
	float nodig_delay_timer;
	float noplace_delay_timer;
	float dig_time;
	float dig_time_complete;
	float repeat_rightclick_timer;
	float object_hit_delay_timer;
	float time_from_last_punch;
	float pause_game_timer;
	ClientActiveObject *selected_object;

	float jump_timer;
	float damage_flash;
	float update_draw_list_timer;
	float statustext_time;

	f32 fog_range;

	v3f update_draw_list_last_cam_dir;

	u32 profiler_current_page;
	u32 profiler_max_page;     // Number of pages

	float time_of_day;
	float time_of_day_smooth;
};

struct Jitter {
	f32 max, min, avg, counter, max_sample, min_sample, max_fraction;
};

struct RunStats {
	u32 drawtime;

	Jitter dtime_jitter, busy_time_jitter;
};

/****************************************************************************
 THE GAME
 ****************************************************************************/

/* This is not intended to be a public class. If a public class becomes
 * desirable then it may be better to create another 'wrapper' class that
 * hides most of the stuff in this class (nothing in this class is required
 * by any other file) but exposes the public methods/data only.
 */
class Game {
public:
	Game();
	~Game();

	bool startup(bool *kill,
			bool random_input,
			InputHandler *input,
			IrrlichtDevice *device,
			const std::string &map_dir,
			const std::string &playername,
			const std::string &password,
			// If address is "", local server is used and address is updated
			std::string *address,
			u16 port,
			std::string &error_message,
			bool *reconnect,
			ChatBackend *chat_backend,
			const SubgameSpec &gamespec,    // Used for local game
			bool simple_singleplayer_mode);

	void run();
	void shutdown();
#if defined(__ANDROID__) || defined(__IOS__)
	void pauseGame();
#endif

#ifdef __IOS__
	void customStatustext(const std::wstring &text, float time);
#endif
	void pauseAnimation(bool is_paused);

protected:

	void extendedResourceCleanup();

	// Basic initialisation
	bool init(const std::string &map_dir, std::string *address,
			u16 port,
			const SubgameSpec &gamespec);
	bool initSound();
	bool createSingleplayerServer(const std::string &map_dir,
			const SubgameSpec &gamespec, u16 port, std::string *address);

	// Client creation
	bool createClient(const std::string &playername,
			const std::string &password, std::string *address, u16 port);
	bool initGui();

	// Client connection
	bool connectToServer(const std::string &playername,
			const std::string &password, std::string *address, u16 port,
			bool *connect_ok, bool *aborted);
	bool getServerContent(bool *aborted);

	// Main loop

	void updateInteractTimers(f32 dtime);
	bool checkConnection();
	bool handleCallbacks();
	void processQueues();
	void updateProfilers(const RunStats &stats, const FpsControl &draw_times, f32 dtime);
	void addProfilerGraphs(const RunStats &stats, const FpsControl &draw_times, f32 dtime);
	void updateStats(RunStats *stats, const FpsControl &draw_times, f32 dtime);

	// Input related
	void processUserInput(f32 dtime);
	void processKeyInput();
	void processItemSelection(u16 *new_playeritem);

	void dropSelectedItem();
	void openInventory();
	void openConsole(float scale, const wchar_t *line=NULL);
	void toggleFreeMove();
	void toggleFreeMoveAlt();
	void toggleFast();
	void toggleNoClip();
	void toggleCinematic();
	void toggleAutorun();

	void toggleChat();
	void toggleHud();
	void toggleMinimap(bool shift_pressed);
	void toggleFog();
	void toggleDebug();
	void toggleUpdateCamera();
	void toggleProfiler();

	void increaseViewRange();
	void decreaseViewRange();
	void toggleFullViewRange();

	void updateCameraDirection(CameraOrientation *cam, float dtime);
	void updateCameraOrientation(CameraOrientation *cam, float dtime);
	void updatePlayerControl(const CameraOrientation &cam);
	void step(f32 *dtime);
	void processClientEvents(CameraOrientation *cam);
	void updateCamera(u32 busy_time, f32 dtime);
	void updateSound(f32 dtime);
	void processPlayerInteraction(f32 dtime, bool show_hud, bool show_debug);
	/*!
	 * Returns the object or node the player is pointing at.
	 * Also updates the selected thing in the Hud.
	 *
	 * @param[in]  shootline         the shootline, starting from
	 * the camera position. This also gives the maximal distance
	 * of the search.
	 * @param[in]  liquids_pointable if false, liquids are ignored
	 * @param[in]  look_for_object   if false, objects are ignored
	 * @param[in]  camera_offset     offset of the camera
	 * @param[out] selected_object   the selected object or
	 * NULL if not found
	 */
	PointedThing updatePointedThing(
			const core::line3d<f32> &shootline, bool liquids_pointable,
			bool look_for_object, const v3s16 &camera_offset);
	void handlePointingAtNothing(const ItemStack &playerItem);
	void handlePointingAtNode(const PointedThing &pointed,
		const ItemDefinition &playeritem_def, const ItemStack &playeritem,
		const ToolCapabilities &playeritem_toolcap, f32 dtime);
	void handlePointingAtObject(const PointedThing &pointed, const ItemStack &playeritem,
			const v3f &player_position, bool show_debug);
	void handleDigging(const PointedThing &pointed, const v3s16 &nodepos,
			const ToolCapabilities &playeritem_toolcap, f32 dtime);
	void updateFrame(ProfilerGraph *graph, RunStats *stats, f32 dtime,
			const CameraOrientation &cam);
	void updateGui(const RunStats &stats, f32 dtime, const CameraOrientation &cam);
	void updateProfilerGraphs(ProfilerGraph *graph);

	// Misc
	void limitFps(FpsControl *fps_timings, f32 *dtime);

	void showOverlayMessage(const wchar_t *msg, float dtime, int percent,
			bool draw_clouds = true);

	static void settingChangedCallback(const std::string &setting_name, void *data);
	void readSettings();

	inline bool getLeftClicked()
	{
		return input->getLeftClicked() ||
			input->joystick.getWasKeyDown(KeyType::MOUSE_L);
	}
	inline bool getRightClicked()
	{
		return input->getRightClicked() ||
			input->joystick.getWasKeyDown(KeyType::MOUSE_R);
	}
	inline bool isLeftPressed()
	{
		return input->getLeftState() ||
			input->joystick.isKeyDown(KeyType::MOUSE_L);
	}
	inline bool isRightPressed()
	{
		return input->getRightState() ||
			input->joystick.isKeyDown(KeyType::MOUSE_R);
	}
	inline bool getLeftReleased()
	{
		return input->getLeftReleased() ||
			input->joystick.wasKeyReleased(KeyType::MOUSE_L);
	}

	inline bool isKeyDown(GameKeyType k)
	{
		return input->isKeyDown(keycache.key[k]) || input->joystick.isKeyDown(k);
	}
	inline bool wasKeyDown(GameKeyType k)
	{
		return input->wasKeyDown(keycache.key[k]) || input->joystick.wasKeyDown(k);
	}

#if defined(__ANDROID__) || defined(__IOS__)
	void handleAndroidChatInput();
#endif

private:
	void showPauseMenu();
#ifdef DISABLE_CSM
	void showDeathScreen();
#endif

	InputHandler *input;

	Client *client;
	Server *server;

	IWritableTextureSource *texture_src;
	IWritableShaderSource *shader_src;

	// When created, these will be filled with data received from the server
	IWritableItemDefManager *itemdef_manager;
	IWritableNodeDefManager *nodedef_manager;

	GameOnDemandSoundFetcher soundfetcher; // useful when testing
	ISoundManager *sound;
	bool sound_is_dummy;
	SoundMaker *soundmaker;

	ChatBackend *chat_backend;

	GUIFormSpecMenu *current_formspec;
	//default: "". If other than "", empty show_formspec packets will only close the formspec when the formname matches
	std::string cur_formname;
	std::string wield_name;

	EventManager *eventmgr;
	QuicktuneShortcutter *quicktune;

	GUIChatConsole *gui_chat_console; // Free using ->Drop()
	MapDrawControl *draw_control;
	Camera *camera;
	Clouds *clouds;	                  // Free using ->Drop()
	Sky *sky;                         // Free using ->Drop()
	Inventory *local_inventory;
	Hud *hud;
	Minimap *mapper;

	GameRunData runData;
	GameUIFlags flags;

	/* 'cache'
	   This class does take ownership/responsibily for cleaning up etc of any of
	   these items (e.g. device)
	*/
	IrrlichtDevice *device;
	video::IVideoDriver *driver;
	scene::ISceneManager *smgr;
	bool *kill;
	std::string *error_message;
	bool *reconnect_requested;
	scene::ISceneNode *skybox;

	bool random_input;
	bool simple_singleplayer_mode;
	/* End 'cache' */

	/* Pre-calculated values
	 */
	int crack_animation_length;

	/* GUI stuff
	 */
	gui::IGUIStaticText *guitext;          // First line of debug text
	gui::IGUIStaticText *guitext2;         // Second line of debug text
	gui::IGUIStaticText *guitext_info;     // At the middle of the screen
	gui::IGUIStaticText *guitext_status;
	gui::IGUIStaticText *guitext_chat;	   // Chat text
	gui::IGUIStaticText *guitext_profiler; // Profiler text

	std::wstring infotext;
	std::wstring m_statustext;

	KeyCache keycache;

	IntervalLimiter profiler_interval;

	/*
	 * TODO: Local caching of settings is not optimal and should at some stage
	 *       be updated to use a global settings object for getting thse values
	 *       (as opposed to the this local caching). This can be addressed in
	 *       a later release.
	 */
	bool m_cache_doubletap_jump;
	bool m_cache_enable_clouds;
	bool m_cache_enable_joysticks;
	bool m_cache_enable_particles;
	bool m_cache_enable_fog;
	bool m_cache_enable_noclip;
	bool m_cache_enable_free_move;
	f32  m_cache_mouse_sensitivity;
	f32  m_cache_joystick_frustum_sensitivity;
	f32  m_repeat_right_click_time;
	f32  m_cache_cam_smoothing;
	f32  m_cache_fog_start;

	u16  m_round_screen;
	f32  m_hud_scaling;
	bool m_hud_small;

	bool m_invert_mouse;
	bool m_first_loop_after_window_activation;
	bool m_camera_offset_changed;

#if defined(__ANDROID__) || defined(__IOS__)
	bool m_cache_hold_aux1;
	bool m_android_chat_open;
#endif
	core::list<scene::ISceneNode *> m_anim_nodes;
};

Game::Game() :
	client(NULL),
	server(NULL),
	texture_src(NULL),
	shader_src(NULL),
	itemdef_manager(NULL),
	nodedef_manager(NULL),
	sound(NULL),
	sound_is_dummy(false),
	soundmaker(NULL),
	chat_backend(NULL),
	current_formspec(NULL),
	cur_formname(""),
	eventmgr(NULL),
	quicktune(NULL),
	gui_chat_console(NULL),
	draw_control(NULL),
	camera(NULL),
	clouds(NULL),
	sky(NULL),
	local_inventory(NULL),
	hud(NULL),
	mapper(NULL),
	m_invert_mouse(false),
	m_first_loop_after_window_activation(false),
	m_camera_offset_changed(false)
{
	g_settings->registerChangedCallback("doubletap_jump",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("enable_clouds",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("doubletap_joysticks",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("enable_particles",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("enable_fog",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("mouse_sensitivity",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("joystick_frustum_sensitivity",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("repeat_rightclick_time",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("noclip",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("free_move",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("cinematic",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("cinematic_camera_smoothing",
		&settingChangedCallback, this);
	g_settings->registerChangedCallback("camera_smoothing",
		&settingChangedCallback, this);

	readSettings();

#if defined(__ANDROID__) || defined(__IOS__)
	m_cache_hold_aux1 = false;	// This is initialised properly later
#endif

}



/****************************************************************************
 MinetestApp Public
 ****************************************************************************/

Game::~Game()
{
	delete client;
	delete soundmaker;
	if (!sound_is_dummy)
		delete sound;

	delete server; // deleted first to stop all server threads

	delete hud;
	delete local_inventory;
	delete camera;
	delete quicktune;
	delete eventmgr;
	delete texture_src;
	delete shader_src;
	delete nodedef_manager;
	delete itemdef_manager;
	delete draw_control;

	extendedResourceCleanup();

	g_settings->deregisterChangedCallback("doubletap_jump",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("enable_clouds",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("enable_particles",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("enable_fog",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("mouse_sensitivity",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("repeat_rightclick_time",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("noclip",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("free_move",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("cinematic",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("cinematic_camera_smoothing",
		&settingChangedCallback, this);
	g_settings->deregisterChangedCallback("camera_smoothing",
		&settingChangedCallback, this);
}

bool Game::startup(bool *kill,
		bool random_input,
		InputHandler *input,
		IrrlichtDevice *device,
		const std::string &map_dir,
		const std::string &playername,
		const std::string &password,
		std::string *address,     // can change if simple_singleplayer_mode
		u16 port,
		std::string &error_message,
		bool *reconnect,
		ChatBackend *chat_backend,
		const SubgameSpec &gamespec,
		bool simple_singleplayer_mode)
{
	// "cache"
	this->device              = device;
	this->kill                = kill;
	this->error_message       = &error_message;
	this->reconnect_requested = reconnect;
	this->random_input        = random_input;
	this->input               = input;
	this->chat_backend        = chat_backend;
	this->simple_singleplayer_mode = simple_singleplayer_mode;

	keycache.handler = input;
	keycache.populate();

	driver              = device->getVideoDriver();
	smgr                = device->getSceneManager();

	smgr->getParameters()->setAttribute(scene::OBJ_LOADER_IGNORE_MATERIAL_FILES, true);

	memset(&runData, 0, sizeof(runData));
	runData.time_from_last_punch = 10.0;
	runData.profiler_max_page = 3;
	runData.update_wielded_item_trigger = true;

	memset(&flags, 0, sizeof(flags));
	flags.show_chat = true;
	flags.show_hud = true;
	flags.show_debug = g_settings->getBool("show_debug");
	m_invert_mouse = g_settings->getBool("invert_mouse");
	m_first_loop_after_window_activation = true;

	if (!init(map_dir, address, port, gamespec))
		return false;

	if (!createClient(playername, password, address, port))
		return false;

	return true;
}


void Game::run()
{
	ProfilerGraph graph;
	RunStats stats              = { 0 };
	CameraOrientation cam_view_target  = { 0 };
	CameraOrientation cam_view  = { 0 };
	FpsControl draw_times       = { 0 };
	f32 dtime; // in seconds

	/* Clear the profiler */
	Profiler::GraphValues dummyvalues;
	g_profiler->graphGet(dummyvalues);

	draw_times.last_time = device->getTimer()->getTime();

	set_light_table(g_settings->getFloat("display_gamma"));

#if defined(__ANDROID__) || defined(__IOS__)
	m_cache_hold_aux1 = g_settings->getBool("fast_move")
			&& client->checkPrivilege("fast");
#endif

	irr::core::dimension2d<u32> previous_screen_size(g_settings->getU16("screenW"),
		g_settings->getU16("screenH"));

	while (device->run()
			&& !(*kill || g_gamecallback->shutdown_requested
			|| (server && server->getShutdownRequested()))) {
#ifdef __IOS__
		if (device->isWindowMinimized())
			continue;
#endif

		const irr::core::dimension2d<u32> &current_screen_size =
			device->getVideoDriver()->getScreenSize();
		// Verify if window size has changed and save it if it's the case
		// Ensure evaluating settings->getBool after verifying screensize
		// First condition is cheaper
		if (previous_screen_size != current_screen_size &&
				current_screen_size != irr::core::dimension2d<u32>(0,0) &&
				g_settings->getBool("autosave_screensize")) {
			g_settings->setU16("screenW", current_screen_size.Width);
			g_settings->setU16("screenH", current_screen_size.Height);
			previous_screen_size = current_screen_size;
		}

		/* Must be called immediately after a device->run() call because it
		 * uses device->getTimer()->getTime()
		 */
		limitFps(&draw_times, &dtime);

		updateStats(&stats, draw_times, dtime);
		updateInteractTimers(dtime);

		if (!checkConnection())
			break;
		if (!handleCallbacks())
			break;

		processQueues();

		infotext = L"";
		hud->resizeHotbar();

		updateProfilers(stats, draw_times, dtime);
		processUserInput(dtime);
		// Update camera before player movement to avoid camera lag of one frame
		updateCameraDirection(&cam_view_target, dtime);
		cam_view.camera_yaw += (cam_view_target.camera_yaw -
				cam_view.camera_yaw) * m_cache_cam_smoothing;
		cam_view.camera_pitch += (cam_view_target.camera_pitch -
				cam_view.camera_pitch) * m_cache_cam_smoothing;
		updatePlayerControl(cam_view);
		step(&dtime);
		processClientEvents(&cam_view_target);
		updateCamera(draw_times.busy_time, dtime);
		updateSound(dtime);
		processPlayerInteraction(dtime, flags.show_hud, flags.show_debug);
		updateFrame(&graph, &stats, dtime, cam_view);
		updateProfilerGraphs(&graph);

		// Update if minimap has been disabled by the server
		flags.show_minimap &= client->shouldShowMinimap();
	}
}


void Game::shutdown()
{
#if IRRLICHT_VERSION_MAJOR == 1 && IRRLICHT_VERSION_MINOR <= 8
	if (g_settings->get("3d_mode") == "pageflip")
		driver->setRenderTarget(irr::video::ERT_STEREO_BOTH_BUFFERS);
#endif

	if (current_formspec)
		current_formspec->quitMenu();

#ifdef HAVE_TOUCHSCREENGUI
	g_touchscreengui->hide();
#endif

	showOverlayMessage(wgettext("Shutting down..."), 0, 0, false);

	if (clouds)
		clouds->drop();

	if (gui_chat_console)
		gui_chat_console->drop();

	if (sky)
		sky->drop();

	/* cleanup menus */
	while (g_menumgr.menuCount() > 0) {
		g_menumgr.m_stack.front()->setVisible(false);
		g_menumgr.deletingMenu(g_menumgr.m_stack.front());
	}

	if (current_formspec) {
		current_formspec->drop();
		current_formspec = NULL;
	}

	chat_backend->addMessage(L"", L"# Disconnected.");
	chat_backend->addMessage(L"", L"");

	if (client) {
		client->Stop();
		while (!client->isShutdown()) {
			assert(texture_src != NULL);
			assert(shader_src != NULL);
			texture_src->processQueue();
			shader_src->processQueue();
			sleep_ms(100);
		}
	}
}


/****************************************************************************/
/****************************************************************************
 Startup
 ****************************************************************************/
/****************************************************************************/

bool Game::init(
		const std::string &map_dir,
		std::string *address,
		u16 port,
		const SubgameSpec &gamespec)
{
	texture_src = createTextureSource(device);

	showOverlayMessage(wgettext("Loading..."), 0, 0);

	shader_src = createShaderSource(device);

	itemdef_manager = createItemDefManager();
	nodedef_manager = createNodeDefManager();

	eventmgr = new EventManager();
	quicktune = new QuicktuneShortcutter();

	if (!(texture_src && shader_src && itemdef_manager && nodedef_manager
			&& eventmgr && quicktune))
		return false;

	if (!initSound())
		return false;

	// Create a server if not connecting to an existing one
	if (*address == "") {
		if (!createSingleplayerServer(map_dir, gamespec, port, address))
			return false;
	}

	return true;
}

bool Game::initSound()
{
#if USE_SOUND
	if (g_settings->getBool("enable_sound")) {
		infostream << "Attempting to use OpenAL audio" << std::endl;
		sound = createOpenALSoundManager(&soundfetcher);
		if (!sound)
			infostream << "Failed to initialize OpenAL audio" << std::endl;
	} else
		infostream << "Sound disabled." << std::endl;
#endif

	if (!sound) {
		infostream << "Using dummy audio." << std::endl;
		sound = &dummySoundManager;
		sound_is_dummy = true;
	}

	soundmaker = new SoundMaker(sound, nodedef_manager);
	if (!soundmaker)
		return false;

	soundmaker->registerReceiver(eventmgr);

	return true;
}

bool Game::createSingleplayerServer(const std::string &map_dir,
		const SubgameSpec &gamespec, u16 port, std::string *address)
{
	showOverlayMessage(wgettext("Creating server..."), 0, 5);

	std::string bind_str = g_settings->get("bind_address");
	Address bind_addr(0, 0, 0, 0, port);

	if (g_settings->getBool("ipv6_server")) {
		bind_addr.setAddress((IPv6AddressBytes *) NULL);
	}

	try {
		bind_addr.Resolve(bind_str.c_str());
	} catch (ResolveError &e) {
		infostream << "Resolving bind address \"" << bind_str
			   << "\" failed: " << e.what()
			   << " -- Listening on all addresses." << std::endl;
	}

	if (bind_addr.isIPv6() && !g_settings->getBool("enable_ipv6")) {
		*error_message = "Unable to listen on " +
				bind_addr.serializeString() +
				" because IPv6 is disabled";
		errorstream << *error_message << std::endl;
		return false;
	}

	server = new Server(map_dir, gamespec, simple_singleplayer_mode,
			    bind_addr.isIPv6(), false);

	server->start(bind_addr);

	return true;
}

bool Game::createClient(const std::string &playername,
		const std::string &password, std::string *address, u16 port)
{
	showOverlayMessage(wgettext("Creating client..."), 0, 10);

	draw_control = new MapDrawControl;
	if (!draw_control)
		return false;

	bool could_connect, connect_aborted;

	if (!connectToServer(playername, password, address, port,
			&could_connect, &connect_aborted))
		return false;

	if (!could_connect) {
		if (error_message->empty() && !connect_aborted) {
			// Should not happen if error messages are set properly
			*error_message = "Connection failed for unknown reason";
			errorstream << *error_message << std::endl;
		}
		return false;
	}

	if (!getServerContent(&connect_aborted)) {
		if (error_message->empty() && !connect_aborted) {
			// Should not happen if error messages are set properly
			*error_message = "Connection failed for unknown reason";
			errorstream << *error_message << std::endl;
		}
		return false;
	}

	#if defined(__ANDROID__) || defined(__IOS__)
		porting::notifyServerConnect(!simple_singleplayer_mode);
	#endif

	GameGlobalShaderConstantSetterFactory *scsf = new GameGlobalShaderConstantSetterFactory(
			&flags.force_fog_off, &runData.fog_range, client);
	shader_src->addShaderConstantSetterFactory(scsf);

	// Update cached textures, meshes and materials
	client->afterContentReceived(device);

	/* Camera
	 */
	camera = new Camera(smgr, *draw_control, client);
	if (!camera || !camera->successfullyCreated(*error_message))
		return false;
	client->setCamera(camera);

	/* Clouds
	 */
	if (m_cache_enable_clouds) {
		clouds = new Clouds(smgr->getRootSceneNode(), smgr, -1, time(0));
		if (!clouds) {
			*error_message = "Memory allocation error (clouds)";
			errorstream << *error_message << std::endl;
			return false;
		}
	}

	/* Skybox
	 */
	sky = new Sky(smgr->getRootSceneNode(), smgr, -1, texture_src);
	scsf->setSky(sky);
	skybox = NULL;	// This is used/set later on in the main run loop

	local_inventory = new Inventory(itemdef_manager);

	if (!(sky && local_inventory)) {
		*error_message = "Memory allocation error (sky or local inventory)";
		errorstream << *error_message << std::endl;
		return false;
	}

	/* Pre-calculated values
	 */
	video::ITexture *t = texture_src->getTexture("crack_anylength.png");
	if (t) {
		v2u32 size = t->getOriginalSize();
		crack_animation_length = size.Y / size.X;
	} else {
		crack_animation_length = 5;
	}

	if (!initGui())
		return false;

	/* Set window caption
	 */
	std::wstring str = utf8_to_wide(PROJECT_NAME_C);
	str += L" ";
	str += utf8_to_wide(g_version_hash);
	str += L" [";
	str += driver->getName();
	str += L"]";
	device->setWindowCaption(str.c_str());

	LocalPlayer *player = client->getEnv().getLocalPlayer();
	player->hurt_tilt_timer = 0;
	player->hurt_tilt_strength = 0;

	hud = new Hud(driver, smgr, guienv, client, player, local_inventory);

	if (!hud) {
		*error_message = "Memory error: could not create HUD";
		errorstream << *error_message << std::endl;
		return false;
	}

	mapper = client->getMinimap();
	if (mapper)
		mapper->setMinimapMode(MINIMAP_MODE_OFF);

	return true;
}

bool Game::initGui()
{
	// First line of debug text
	guitext = addStaticText(guienv,
			utf8_to_wide(PROJECT_NAME_C).c_str(),
			core::rect<s32>(0, 0, 0, 0),
			false, false, guiroot);

	// Second line of debug text
	guitext2 = addStaticText(guienv,
			L"",
			core::rect<s32>(0, 0, 0, 0),
			false, false, guiroot);

	// At the middle of the screen
	// Object infos are shown in this
	guitext_info = addStaticText(guienv,
			L"",
			core::rect<s32>(0, 0, 400, g_fontengine->getTextHeight() * 5 + 5) + v2s32(100, 200),
			false, true, guiroot);

	// Status text (displays info when showing and hiding GUI stuff, etc.)
	guitext_status = addStaticText(guienv,
			L"<Status>",
			core::rect<s32>(0, 0, 0, 0),
			false, false, guiroot);
	guitext_status->setVisible(false);

	// Chat text
	guitext_chat = addStaticText(
			guienv,
			L"",
			core::rect<s32>(0, 0, 0, 0),
			//false, false); // Disable word wrap as of now
			false, true, guiroot);

	// Remove stale "recent" chat messages from previous connections
	chat_backend->clearRecentChat();

	// Chat backend and console
	gui_chat_console = new GUIChatConsole(guienv, guienv->getRootGUIElement(),
			-1, chat_backend, client, &g_menumgr);
	if (!gui_chat_console) {
		*error_message = "Could not allocate memory for chat console";
		errorstream << *error_message << std::endl;
		return false;
	}

	// Profiler text (size is updated when text is updated)
	guitext_profiler = addStaticText(guienv,
			L"<Profiler>",
			core::rect<s32>(0, 0, 0, 0),
			false, false, guiroot);
	guitext_profiler->setBackgroundColor(video::SColor(120, 0, 0, 0));
	guitext_profiler->setVisible(false);
	guitext_profiler->setWordWrap(true);

#ifdef HAVE_TOUCHSCREENGUI
	if (g_touchscreengui)
		g_touchscreengui->init(texture_src);
#endif

	return true;
}

bool Game::connectToServer(const std::string &playername,
		const std::string &password, std::string *address, u16 port,
		bool *connect_ok, bool *aborted)
{
	*connect_ok = false;	// Let's not be overly optimistic
	*aborted = false;
	bool local_server_mode = false;

	showOverlayMessage(wgettext("Resolving address..."), 0, 15);

	Address connect_address(0, 0, 0, 0, port);

	try {
		connect_address.Resolve(address->c_str());

		if (connect_address.isZero()) { // i.e. INADDR_ANY, IN6ADDR_ANY
			//connect_address.Resolve("localhost");
			if (connect_address.isIPv6()) {
				IPv6AddressBytes addr_bytes;
				addr_bytes.bytes[15] = 1;
				connect_address.setAddress(&addr_bytes);
			} else {
				connect_address.setAddress(127, 0, 0, 1);
			}
			local_server_mode = true;
		}
	} catch (ResolveError &e) {
		*error_message = std::string("Couldn't resolve address: ") + e.what();
		errorstream << *error_message << std::endl;
		return false;
	}

	if (connect_address.isIPv6() && !g_settings->getBool("enable_ipv6")) {
		*error_message = "Unable to connect to " +
				connect_address.serializeString() +
				" because IPv6 is disabled";
		errorstream << *error_message << std::endl;
		return false;
	}

	client = new Client(device,
			playername.c_str(), password, *address,
			*draw_control, texture_src, shader_src,
			itemdef_manager, nodedef_manager, sound, eventmgr,
			connect_address.isIPv6(), &flags);

	if (!client)
		return false;

	infostream << "Connecting to server at ";
	connect_address.print(&infostream);
	infostream << std::endl;

	client->connect(connect_address,
		simple_singleplayer_mode || local_server_mode);

	/*
		Wait for server to accept connection
	*/

	try {
		input->clear();

		FpsControl fps_control = { 0 };
		f32 dtime;
		f32 wait_time = 0; // in seconds

		fps_control.last_time = device->getTimer()->getTime();

		client->initMods();

		while (device->run()) {

			limitFps(&fps_control, &dtime);

			// Update client and server
			client->step(dtime);

			if (server != NULL)
				server->step(dtime);

			// End condition
			if (client->getState() == LC_Init) {
				*connect_ok = true;
				break;
			}

			// Break conditions
			if (client->accessDenied()) {
				*error_message = "Access denied. Reason: "
						+ client->accessDeniedReason();
				*reconnect_requested = client->reconnectRequested();
				errorstream << *error_message << std::endl;
				break;
			}

			if (wasKeyDown(KeyType::ESC) || input->wasKeyDown(CancelKey)) {
				*aborted = true;
				infostream << "Connect aborted [Escape]" << std::endl;
				break;
			}

			wait_time += dtime;
			// Only time out if we aren't waiting for the server we started
			if ((*address != "") && (wait_time > g_settings->getS32("connection_timeout"))) {
				bool sent_old_init = g_settings->getFlag("send_pre_v25_init");
				// If no pre v25 init was sent, and no answer was received,
				// but the low level connection could be established
				// (meaning that we have a peer id), then we probably wanted
				// to connect to a legacy server. In this case, tell the user
				// to enable the option to be able to connect.
				if (!sent_old_init &&
						(client->getProtoVersion() == 0) &&
						client->connectedToServer()) {
					*error_message = "Connection failure: init packet not "
					"recognized by server.";
				} else {
					*error_message = "Connection timed out.";
				}
				errorstream << *error_message << std::endl;
				break;
			}

			// Update status
			showOverlayMessage(wgettext("Connecting to server..."), dtime, 20);
		}
	} catch (con::PeerNotFoundException &e) {
		// TODO: Should something be done here? At least an info/error
		// message?
		return false;
	}

	return true;
}

bool Game::getServerContent(bool *aborted)
{
	input->clear();

	FpsControl fps_control = { 0 };
	f32 dtime; // in seconds

	fps_control.last_time = device->getTimer()->getTime();

	while (device->run()) {

		limitFps(&fps_control, &dtime);

		// Update client and server
		client->step(dtime);

		if (server != NULL)
			server->step(dtime);

		// End condition
		if (client->mediaReceived() && client->itemdefReceived() &&
				client->nodedefReceived()) {
			break;
		}

		// Error conditions
		if (!checkConnection())
			return false;

		if (client->getState() < LC_Init) {
			*error_message = "Client disconnected";
			errorstream << *error_message << std::endl;
			return false;
		}

		if (wasKeyDown(KeyType::ESC) || input->wasKeyDown(CancelKey)) {
			*aborted = true;
			infostream << "Connect aborted [Escape]" << std::endl;
			return false;
		}

		// Display status
		int progress = 25;

		if (!client->itemdefReceived()) {
			const wchar_t *text = wgettext("Item definitions...");
			progress = 25;
			draw_load_screen(text, device, guienv, texture_src,
				dtime, progress);
			delete[] text;
		} else if (!client->nodedefReceived()) {
			const wchar_t *text = wgettext("Node definitions...");
			progress = 30;
			draw_load_screen(text, device, guienv, texture_src,
				dtime, progress);
			delete[] text;
		} else {
			std::stringstream message;
			std::fixed(message);
			message.precision(0);
			float receive = client->mediaReceiveProgress() * 100;
			message << gettext("Media...");
			if (receive > 0)
				message << " " << receive << "%";
			message.precision(2);

			if ((USE_CURL == 0) ||
					(!g_settings->getBool("enable_remote_media_server"))) {
				float cur = client->getCurRate();
				std::string cur_unit = gettext("KiB/s");

				if (cur > 900) {
					cur /= 1024.0;
					cur_unit = gettext("MiB/s");
				}

				message << " (" << cur << ' ' << cur_unit << ")";
			}

			progress = 30 + client->mediaReceiveProgress() * 35 + 0.5;
			draw_load_screen(utf8_to_wide(message.str()), device,
					guienv, texture_src, dtime, progress);
		}
	}

	return true;
}


/****************************************************************************/
/****************************************************************************
 Run
 ****************************************************************************/
/****************************************************************************/

inline void Game::updateInteractTimers(f32 dtime)
{
	if (runData.nodig_delay_timer >= 0)
		runData.nodig_delay_timer -= dtime;

	if (runData.object_hit_delay_timer >= 0)
		runData.object_hit_delay_timer -= dtime;

	if (runData.noplace_delay_timer >= 0)
		runData.noplace_delay_timer -= dtime;

	runData.time_from_last_punch += dtime;
}


/* returns false if game should exit, otherwise true
 */
inline bool Game::checkConnection()
{
	if (client->accessDenied()) {
		*error_message = "Access denied. Reason: "
				+ client->accessDeniedReason();
		*reconnect_requested = client->reconnectRequested();
		errorstream << *error_message << std::endl;
		return false;
	}

	return true;
}


/* returns false if game should exit, otherwise true
 */
inline bool Game::handleCallbacks()
{
	if (g_gamecallback->disconnect_requested) {
		g_gamecallback->disconnect_requested = false;
		return false;
	}

	if (g_gamecallback->changepassword_requested) {
		(new GUIPasswordChange(guienv, guiroot, -1,
				       &g_menumgr, client))->drop();
		g_gamecallback->changepassword_requested = false;
	}

	if (g_gamecallback->changevolume_requested) {
		(new GUIVolumeChange(guienv, guiroot, -1,
				     &g_menumgr))->drop();
		g_gamecallback->changevolume_requested = false;
	}

	if (g_gamecallback->keyconfig_requested) {
		(new GUIKeyChangeMenu(guienv, guiroot, -1,
				      &g_menumgr))->drop();
		g_gamecallback->keyconfig_requested = false;
	}

	if (g_gamecallback->keyconfig_changed) {
		keycache.populate(); // update the cache with new settings
		g_gamecallback->keyconfig_changed = false;
	}

	return true;
}


void Game::processQueues()
{
	texture_src->processQueue();
	itemdef_manager->processQueue(client);
	shader_src->processQueue();
}


void Game::updateProfilers(const RunStats &stats, const FpsControl &draw_times, f32 dtime)
{
	float profiler_print_interval =
			g_settings->getFloat("profiler_print_interval");
	bool print_to_log = true;

	if (profiler_print_interval == 0) {
		print_to_log = false;
		profiler_print_interval = 5;
	}

	if (profiler_interval.step(dtime, profiler_print_interval)) {
		if (print_to_log) {
			infostream << "Profiler:" << std::endl;
			g_profiler->print(infostream);
		}

		update_profiler_gui(guitext_profiler, g_fontengine,
				runData.profiler_current_page, runData.profiler_max_page,
				driver->getScreenSize().Height);

		g_profiler->clear();
	}

	addProfilerGraphs(stats, draw_times, dtime);
}


void Game::addProfilerGraphs(const RunStats &stats,
		const FpsControl &draw_times, f32 dtime)
{
	g_profiler->graphAdd("mainloop_other",
			draw_times.busy_time / 1000.0f - stats.drawtime / 1000.0f);

	if (draw_times.sleep_time != 0)
		g_profiler->graphAdd("mainloop_sleep", draw_times.sleep_time / 1000.0f);
	g_profiler->graphAdd("mainloop_dtime", dtime);

	g_profiler->add("Elapsed time", dtime);
	g_profiler->avg("FPS", 1. / dtime);
}


void Game::updateStats(RunStats *stats, const FpsControl &draw_times,
		f32 dtime)
{

	f32 jitter;
	Jitter *jp;

	/* Time average and jitter calculation
	 */
	jp = &stats->dtime_jitter;
	jp->avg = jp->avg * 0.96 + dtime * 0.04;

	jitter = dtime - jp->avg;

	if (jitter > jp->max)
		jp->max = jitter;

	jp->counter += dtime;

	if (jp->counter > 0.0) {
		jp->counter -= 3.0;
		jp->max_sample = jp->max;
		jp->max_fraction = jp->max_sample / (jp->avg + 0.001);
		jp->max = 0.0;
	}

	/* Busytime average and jitter calculation
	 */
	jp = &stats->busy_time_jitter;
	jp->avg = jp->avg + draw_times.busy_time * 0.02;

	jitter = draw_times.busy_time - jp->avg;

	if (jitter > jp->max)
		jp->max = jitter;
	if (jitter < jp->min)
		jp->min = jitter;

	jp->counter += dtime;

	if (jp->counter > 0.0) {
		jp->counter -= 3.0;
		jp->max_sample = jp->max;
		jp->min_sample = jp->min;
		jp->max = 0.0;
		jp->min = 0.0;
	}
}



/****************************************************************************
 Input handling
 ****************************************************************************/

void Game::processUserInput(f32 dtime)
{
	// Reset input if window not active or some menu is active
	if (!device->isWindowActive() || isMenuActive() || guienv->hasFocus(gui_chat_console)) {
		input->clear();
#ifdef HAVE_TOUCHSCREENGUI
		g_touchscreengui->hide();
#endif
	}
#ifdef HAVE_TOUCHSCREENGUI
	else if (g_touchscreengui) {
		/* on touchscreengui step may generate own input events which ain't
		 * what we want in case we just did clear them */
		g_touchscreengui->step(dtime);
	}
#endif

	if (!guienv->hasFocus(gui_chat_console) && gui_chat_console->isOpen()) {
		gui_chat_console->closeConsoleAtOnce();
	}

	// Input handler step() (used by the random input generator)
	input->step(dtime);

#if defined(__ANDROID__) || defined(__IOS__)
	if (!porting::hasRealKeyboard()) {
		if (current_formspec != NULL)
			current_formspec->getAndroidUIInput();
		else
			handleAndroidChatInput();
	}
#endif

	// Increase timer for double tap of "keymap_jump"
	if (m_cache_doubletap_jump && runData.jump_timer <= 0.15f)
		runData.jump_timer += dtime;

	processKeyInput();
	processItemSelection(&runData.new_playeritem);
}


void Game::processKeyInput()
{
	if (wasKeyDown(KeyType::DROP)) {
		dropSelectedItem();
	} else if (wasKeyDown(KeyType::AUTORUN)) {
		toggleAutorun();
	} else if (wasKeyDown(KeyType::INVENTORY)) {
		openInventory();
	} else if (wasKeyDown(KeyType::ESC) || input->wasKeyDown(CancelKey)) {
		if (!gui_chat_console->isOpenInhibited()) {
			showPauseMenu();
		}
	} else if (wasKeyDown(KeyType::CHAT)) {
		openConsole(0.2, L"");
	} else if (wasKeyDown(KeyType::CMD)) {
		openConsole(0.2, L"/");
	} else if (wasKeyDown(KeyType::CMD_LOCAL)) {
		openConsole(0.2, L".");
	} else if (wasKeyDown(KeyType::CONSOLE)) {
		openConsole(core::clamp(g_settings->getFloat("console_height"), 0.1f, 1.0f));
	} else if (wasKeyDown(KeyType::FREEMOVE)) {
		toggleFreeMove();
	} else if (wasKeyDown(KeyType::JUMP)) {
		toggleFreeMoveAlt();
	} else if (wasKeyDown(KeyType::FASTMOVE)) {
		toggleFast();
	} else if (wasKeyDown(KeyType::NOCLIP)) {
		toggleNoClip();
	} else if (wasKeyDown(KeyType::MUTE)) {
		float volume = g_settings->getFloat("sound_volume");
		if (volume < 0.001f) {
			g_settings->setFloat("sound_volume", 1.0f);
			m_statustext = narrow_to_wide(gettext("Volume changed to 100%"));
		} else {
			g_settings->setFloat("sound_volume", 0.0f);
			m_statustext = narrow_to_wide(gettext("Volume changed to 0%"));
		}
		runData.statustext_time = 0;
	} else if (wasKeyDown(KeyType::INC_VOLUME)) {
		float new_volume = rangelim(g_settings->getFloat("sound_volume") + 0.1f, 0.0f, 1.0f);
		char buf[100];
		g_settings->setFloat("sound_volume", new_volume);
		snprintf(buf, sizeof(buf), gettext("Volume changed to %d%%"), myround(new_volume * 100));
		m_statustext = narrow_to_wide(buf);
		runData.statustext_time = 0;
	} else if (wasKeyDown(KeyType::DEC_VOLUME)) {
		float new_volume = rangelim(g_settings->getFloat("sound_volume") - 0.1f, 0.0f, 1.0f);
		char buf[100];
		g_settings->setFloat("sound_volume", new_volume);
		snprintf(buf, sizeof(buf), gettext("Volume changed to %d%%"), myround(new_volume * 100));
		m_statustext = narrow_to_wide(buf);
		runData.statustext_time = 0;
	} else if (wasKeyDown(KeyType::CINEMATIC)) {
		toggleCinematic();
	} else if (wasKeyDown(KeyType::SCREENSHOT)) {
		client->makeScreenshot(device);
	} else if (wasKeyDown(KeyType::TOGGLE_HUD)) {
		toggleHud();
	} else if (wasKeyDown(KeyType::MINIMAP)) {
		toggleMinimap(isKeyDown(KeyType::SNEAK));
	} else if (wasKeyDown(KeyType::TOGGLE_CHAT)) {
		toggleChat();
	} else if (wasKeyDown(KeyType::TOGGLE_FORCE_FOG_OFF)) {
		toggleFog();
	} else if (wasKeyDown(KeyType::TOGGLE_UPDATE_CAMERA)) {
		toggleUpdateCamera();
	} else if (wasKeyDown(KeyType::TOGGLE_DEBUG)) {
		toggleDebug();
	} else if (wasKeyDown(KeyType::TOGGLE_PROFILER)) {
		toggleProfiler();
	} else if (wasKeyDown(KeyType::INCREASE_VIEWING_RANGE)) {
		increaseViewRange();
	} else if (wasKeyDown(KeyType::DECREASE_VIEWING_RANGE)) {
		decreaseViewRange();
	} else if (wasKeyDown(KeyType::RANGESELECT)) {
		toggleFullViewRange();
	} else if (wasKeyDown(KeyType::QUICKTUNE_NEXT)) {
		quicktune->next();
	} else if (wasKeyDown(KeyType::QUICKTUNE_PREV)) {
		quicktune->prev();
	} else if (wasKeyDown(KeyType::QUICKTUNE_INC)) {
		quicktune->inc();
	} else if (wasKeyDown(KeyType::QUICKTUNE_DEC)) {
		quicktune->dec();
	} else if (wasKeyDown(KeyType::DEBUG_STACKS)) {
		// Print debug stacks
		dstream << "-----------------------------------------"
		        << std::endl;
		dstream << "Printing debug stacks:" << std::endl;
		dstream << "-----------------------------------------"
		        << std::endl;
		debug_stacks_print();
	}

	if (!isKeyDown(KeyType::JUMP) && runData.reset_jump_timer) {
		runData.reset_jump_timer = false;
		runData.jump_timer = 0.0f;
	}

	if (quicktune->hasMessage()) {
		m_statustext = utf8_to_wide(quicktune->getMessage());
		runData.statustext_time = 0.0f;
	}
}

void Game::processItemSelection(u16 *new_playeritem)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();

	/* Item selection using mouse wheel
	 */
	*new_playeritem = client->getPlayerItem();

	s32 wheel = input->getMouseWheel();
	u16 max_item = MYMIN(PLAYER_INVENTORY_SIZE - 1,
		    player->hud_hotbar_itemcount - 1);

	s32 dir = wheel;

	if (input->joystick.wasKeyDown(KeyType::SCROLL_DOWN) ||
			wasKeyDown(KeyType::HOTBAR_NEXT)) {
		dir = -1;
	}

	if (input->joystick.wasKeyDown(KeyType::SCROLL_UP) ||
			wasKeyDown(KeyType::HOTBAR_PREV)) {
		dir = 1;
	}

	if (dir < 0)
		*new_playeritem = *new_playeritem < max_item ? *new_playeritem + 1 : 0;
	else if (dir > 0)
		*new_playeritem = *new_playeritem > 0 ? *new_playeritem - 1 : max_item;
	// else dir == 0

	/* Item selection using keyboard
	 */
	for (u16 i = 0; i < 10; i++) {
		static const KeyPress *item_keys[10] = {
			NumberKey + 1, NumberKey + 2, NumberKey + 3, NumberKey + 4,
			NumberKey + 5, NumberKey + 6, NumberKey + 7, NumberKey + 8,
			NumberKey + 9, NumberKey + 0,
		};

		if (input->wasKeyDown(*item_keys[i])) {
			if (i < PLAYER_INVENTORY_SIZE && i < player->hud_hotbar_itemcount) {
				*new_playeritem = i;
				infostream << "Selected item: " << new_playeritem << std::endl;
			}
			break;
		}
	}
}


void Game::dropSelectedItem()
{
	IDropAction *a = new IDropAction();
	a->count = 0;
	a->from_inv.setCurrentPlayer();
	a->from_list = "main";
	a->from_i = client->getPlayerItem();
	client->inventoryAction(a);
}


void Game::openInventory()
{
	/*
	 * Don't permit to open inventory is CAO or player doesn't exists.
	 * This prevent showing an empty inventory at player load
	 */

	LocalPlayer *player = client->getEnv().getLocalPlayer();
	if (player == NULL || player->getCAO() == NULL)
		return;

	infostream << "the_game: " << "Launching inventory" << std::endl;

	PlayerInventoryFormSource *fs_src = new PlayerInventoryFormSource(client);
	TextDest *txt_dst = new TextDestPlayerInventory(client);

	create_formspec_menu(&current_formspec, client, device, &input->joystick, fs_src, txt_dst);
	cur_formname = "";

	InventoryLocation inventoryloc;
	inventoryloc.setCurrentPlayer();
	current_formspec->setFormSpec(fs_src->getForm(), inventoryloc);
}


void Game::openConsole(float scale, const wchar_t *line)
{
	assert(scale > 0.0f && scale <= 1.0f);

#if defined(__ANDROID__) || defined(__IOS__)
	if (!porting::hasRealKeyboard()) {
		porting::showInputDialog(gettext("OK"), "", "", 2);
		m_android_chat_open = true;
	} else {
#endif
	if (gui_chat_console->isOpenInhibited())
		return;
	gui_chat_console->openConsole(scale);
	if (line) {
		gui_chat_console->setCloseOnEnter(true);
		gui_chat_console->replaceAndAddToHistory(line);
	}
#if defined(__ANDROID__) || defined(__IOS__)
	}
#endif
}

#if defined(__ANDROID__) || defined(__IOS__)
void Game::handleAndroidChatInput()
{
	if (m_android_chat_open && porting::getInputDialogState() == 0) {
		std::string text = porting::getInputDialogValue();
		client->typeChatMessage(utf8_to_wide(text));
	}
}
#endif


void Game::toggleFreeMove()
{
	static const wchar_t *msg[] = { L"Fly mode disabled", L"Fly mode enabled" };

	bool free_move = !g_settings->getBool("free_move");
	g_settings->set("free_move", bool_to_cstr(free_move));

	runData.statustext_time = 0;
	m_statustext = msg[free_move];
	if (free_move && !client->checkPrivilege("fly"))
		m_statustext += L" (note: no 'fly' privilege)";
}


void Game::toggleFreeMoveAlt()
{
	if (m_cache_doubletap_jump && runData.jump_timer < 0.15f
			&& client->checkPrivilege("fly"))
		toggleFreeMove();

	runData.reset_jump_timer = true;
}


void Game::toggleFast()
{
	static const wchar_t *msg[] = { L"Fast move disabled", L"Fast move enabled" };
	bool fast_move = !g_settings->getBool("fast_move");
	g_settings->set("fast_move", bool_to_cstr(fast_move));

	runData.statustext_time = 0;
	m_statustext = msg[fast_move];

	bool has_fast_privs = client->checkPrivilege("fast");

	if (fast_move && !has_fast_privs)
		m_statustext += L" (note: no 'fast' privilege)";

#if defined(__ANDROID__) || defined(__IOS__)
	m_cache_hold_aux1 = fast_move && has_fast_privs;
#endif
}


void Game::toggleNoClip()
{
	static const wchar_t *msg[] = { L"noclip disabled", L"noclip enabled" };
	bool noclip = !g_settings->getBool("noclip");
	g_settings->set("noclip", bool_to_cstr(noclip));

	runData.statustext_time = 0;
	m_statustext = msg[noclip];

	if (noclip && !client->checkPrivilege("noclip"))
		m_statustext += L" (note: no 'noclip' privilege)";
}

void Game::toggleCinematic()
{
	static const wchar_t *msg[] = { L"cinematic disabled", L"cinematic enabled" };
	bool cinematic = !g_settings->getBool("cinematic");
	g_settings->set("cinematic", bool_to_cstr(cinematic));

	runData.statustext_time = 0;
	m_statustext = msg[cinematic];
}

// Add WoW-style autorun by toggling continuous forward.
void Game::toggleAutorun()
{
	static const wchar_t *msg[] = { L"autorun disabled", L"autorun enabled" };
	bool autorun_enabled = !g_settings->getBool("continuous_forward");
	g_settings->set("continuous_forward", bool_to_cstr(autorun_enabled));

	runData.statustext_time = 0;
	m_statustext = msg[autorun_enabled ? 1 : 0];
}

void Game::toggleChat()
{
	static const wchar_t *msg[] = { L"Chat hidden", L"Chat shown" };

	flags.show_chat = !flags.show_chat;
	runData.statustext_time = 0;
	m_statustext = msg[flags.show_chat];
}


void Game::toggleHud()
{
	static const wchar_t *msg[] = { L"HUD hidden", L"HUD shown" };

	flags.show_hud = !flags.show_hud;
	runData.statustext_time = 0;
	m_statustext = msg[flags.show_hud];
}

void Game::toggleMinimap(bool shift_pressed)
{
	if (!mapper || !flags.show_hud || !g_settings->getBool("enable_minimap"))
		return;

	if (shift_pressed) {
		mapper->toggleMinimapShape();
		return;
	}

	u32 hud_flags = client->getEnv().getLocalPlayer()->hud_flags;

	MinimapMode mode = MINIMAP_MODE_OFF;
	if (hud_flags & HUD_FLAG_MINIMAP_VISIBLE) {
		mode = mapper->getMinimapMode();
		mode = (MinimapMode)((int)mode + 1);
	}

	flags.show_minimap = true;
	switch (mode) {
		case MINIMAP_MODE_SURFACE:
			m_statustext = L"Minimap enabled";
			break;
		case MINIMAP_MODE_RADAR:
			m_statustext = L"Radar mode enabled";
			break;
		default:
			mode = MINIMAP_MODE_OFF;
			flags.show_minimap = false;
			m_statustext = (hud_flags & HUD_FLAG_MINIMAP_VISIBLE) ?
				L"Minimap disabled" : L"Minimap disabled by server";
	}

	runData.statustext_time = 0;
	mapper->setMinimapMode(mode);
}

void Game::toggleFog()
{
	static const wchar_t *msg[] = { L"Fog enabled", L"Fog disabled" };

	flags.force_fog_off = !flags.force_fog_off;
	runData.statustext_time = 0;
	m_statustext = msg[flags.force_fog_off];
}


void Game::toggleDebug()
{
	// Initial / 4x toggle: Chat only
	// 1x toggle: Debug text with chat
	// 2x toggle: Debug text with profiler graph
	// 3x toggle: Debug text and wireframe
	if (!flags.show_debug) {
		flags.show_debug = true;
		flags.show_profiler_graph = false;
		draw_control->show_wireframe = false;
		m_statustext = L"Debug info shown";
	} else if (!flags.show_profiler_graph && !draw_control->show_wireframe) {
		flags.show_profiler_graph = true;
		m_statustext = L"Profiler graph shown";
	} else if (!draw_control->show_wireframe && client->checkPrivilege("debug")) {
		flags.show_profiler_graph = false;
		draw_control->show_wireframe = true;
		m_statustext = L"Wireframe shown";
	} else {
		flags.show_debug = false;
		flags.show_profiler_graph = false;
		draw_control->show_wireframe = false;
		if (client->checkPrivilege("debug")) {
			m_statustext = L"Debug info, profiler graph, and wireframe hidden";
		} else {
			m_statustext = L"Debug info and profiler graph hidden";
		}
	}
	runData.statustext_time = 0;
}


void Game::toggleUpdateCamera()
{
	static const wchar_t *msg[] = {
		L"Camera update enabled",
		L"Camera update disabled"
	};

	flags.disable_camera_update = !flags.disable_camera_update;
	runData.statustext_time = 0;
	m_statustext = msg[flags.disable_camera_update];
}


void Game::toggleProfiler()
{
	runData.profiler_current_page =
		(runData.profiler_current_page + 1) % (runData.profiler_max_page + 1);

	// FIXME: This updates the profiler with incomplete values
	update_profiler_gui(guitext_profiler, g_fontengine, runData.profiler_current_page,
		runData.profiler_max_page, driver->getScreenSize().Height);

	if (runData.profiler_current_page != 0) {
		std::wstringstream sstr;
		sstr << "Profiler shown (page " << runData.profiler_current_page
		     << " of " << runData.profiler_max_page << ")";
		m_statustext = sstr.str();
	} else {
		m_statustext = L"Profiler hidden";
	}
	runData.statustext_time = 0;
}


void Game::increaseViewRange()
{
	s16 range = g_settings->getS16("viewing_range");
	s16 range_new = range + 10;

	if (range_new > 4000) {
		range_new = 4000;
		m_statustext = utf8_to_wide("Viewing range is at maximum: "
				+ itos(range_new));
	} else {
		m_statustext = utf8_to_wide("Viewing range changed to "
				+ itos(range_new));
	}
	g_settings->set("viewing_range", itos(range_new));
	runData.statustext_time = 0;
}


void Game::decreaseViewRange()
{
	s16 range = g_settings->getS16("viewing_range");
	s16 range_new = range - 10;

	if (range_new < 20) {
		range_new = 20;
		m_statustext = utf8_to_wide("Viewing range is at minimum: "
				+ itos(range_new));
	} else {
		m_statustext = utf8_to_wide("Viewing range changed to "
				+ itos(range_new));
	}
	g_settings->set("viewing_range", itos(range_new));
	runData.statustext_time = 0;
}


void Game::toggleFullViewRange()
{
#if defined(__ANDROID__) || defined(__IOS__)
	static const wchar_t *msg[] = {
		L"Disabled far viewing range",
		L"Enabled far viewing range"
	};
#else
	static const wchar_t *msg[] = {
		L"Normal view range",
		L"Infinite view range"
	};
#endif

	draw_control->range_all = !draw_control->range_all;
	infostream << msg[draw_control->range_all] << std::endl;
	m_statustext = msg[draw_control->range_all];
	runData.statustext_time = 0;
}


void Game::updateCameraDirection(CameraOrientation *cam, float dtime)
{
	if ((device->isWindowActive() && device->isWindowFocused()
			&& !isMenuActive()) || random_input) {

#if !defined(__ANDROID__) && !defined(__IOS__)
		if (!random_input) {
			// Mac OSX gets upset if this is set every frame
			if (device->getCursorControl()->isVisible())
				device->getCursorControl()->setVisible(false);
		}
#endif

		if (m_first_loop_after_window_activation)
			m_first_loop_after_window_activation = false;
		else
			updateCameraOrientation(cam, dtime);

		input->setMousePos((driver->getScreenSize().Width / 2),
				(driver->getScreenSize().Height / 2));
	} else {

#if !defined(__ANDROID__) && !defined(__IOS__)
		// Mac OSX gets upset if this is set every frame
		if (!device->getCursorControl()->isVisible())
			device->getCursorControl()->setVisible(true);
#endif

		m_first_loop_after_window_activation = true;

	}
}

void Game::updateCameraOrientation(CameraOrientation *cam, float dtime)
{
#ifdef HAVE_TOUCHSCREENGUI
	if (g_touchscreengui) {
		cam->camera_yaw   += g_touchscreengui->getYawChange();
		cam->camera_pitch  = g_touchscreengui->getPitch();
	} else {
#endif

		s32 dx = input->getMousePos().X - (driver->getScreenSize().Width / 2);
		s32 dy = input->getMousePos().Y - (driver->getScreenSize().Height / 2);

		if (m_invert_mouse || camera->getCameraMode() == CAMERA_MODE_THIRD_FRONT) {
			dy = -dy;
		}

		cam->camera_yaw   -= dx * m_cache_mouse_sensitivity;
		cam->camera_pitch += dy * m_cache_mouse_sensitivity;

#ifdef HAVE_TOUCHSCREENGUI
	}
#endif

	if (m_cache_enable_joysticks) {
		f32 c = m_cache_joystick_frustum_sensitivity * (1.f / 32767.f) * dtime;
		cam->camera_yaw -= input->joystick.getAxisWithoutDead(JA_FRUSTUM_HORIZONTAL) * c;
		cam->camera_pitch += input->joystick.getAxisWithoutDead(JA_FRUSTUM_VERTICAL) * c;
	}

	cam->camera_pitch = rangelim(cam->camera_pitch, -89.5, 89.5);
}


void Game::updatePlayerControl(const CameraOrientation &cam)
{
	//TimeTaker tt("update player control", NULL, PRECISION_NANO);

	// DO NOT use the isKeyDown method for the forward, backward, left, right
	// buttons, as the code that uses the controls needs to be able to
	// distinguish between the two in order to know when to use joysticks.

	PlayerControl control(
		input->isKeyDown(keycache.key[KeyType::FORWARD]),
		input->isKeyDown(keycache.key[KeyType::BACKWARD]),
		input->isKeyDown(keycache.key[KeyType::LEFT]),
		input->isKeyDown(keycache.key[KeyType::RIGHT]),
		isKeyDown(KeyType::JUMP),
		isKeyDown(KeyType::SPECIAL1),
		isKeyDown(KeyType::SNEAK),
		isKeyDown(KeyType::ZOOM),
		isLeftPressed(),
		isRightPressed(),
		cam.camera_pitch,
		cam.camera_yaw,
		input->joystick.getAxisWithoutDead(JA_SIDEWARD_MOVE),
		input->joystick.getAxisWithoutDead(JA_FORWARD_MOVE)
	);

	u32 keypress_bits =
			( (u32)(isKeyDown(KeyType::FORWARD)                       & 0x1) << 0) |
			( (u32)(isKeyDown(KeyType::BACKWARD)                      & 0x1) << 1) |
			( (u32)(isKeyDown(KeyType::LEFT)                          & 0x1) << 2) |
			( (u32)(isKeyDown(KeyType::RIGHT)                         & 0x1) << 3) |
			( (u32)(isKeyDown(KeyType::JUMP)                          & 0x1) << 4) |
			( (u32)(isKeyDown(KeyType::SPECIAL1)                      & 0x1) << 5) |
			( (u32)(isKeyDown(KeyType::SNEAK)                         & 0x1) << 6) |
			( (u32)(isLeftPressed()                                   & 0x1) << 7) |
			( (u32)(isRightPressed()                                  & 0x1) << 8
		);

#if defined(__ANDROID__) || defined(__IOS__)
	/* For Android, simulate holding down AUX1 (fast move) if the user has
	 * the fast_move setting toggled on. If there is an aux1 key defined for
	 * Android then its meaning is inverted (i.e. holding aux1 means walk and
	 * not fast)
	 */
	if (m_cache_hold_aux1 && !porting::hasRealKeyboard()) {
		control.aux1 = control.aux1 ^ true;
		keypress_bits ^= ((u32)(1U << 5));
	}
#endif

	client->setPlayerControl(control);
	LocalPlayer *player = client->getEnv().getLocalPlayer();
	player->keyPressed = keypress_bits;

	//tt.stop();
}


inline void Game::step(f32 *dtime)
{
#if defined(__ANDROID__) || defined(__IOS__)
	if (g_menumgr.pausesGame()) {
		runData.pause_game_timer += *dtime;
		if (runData.pause_game_timer > 120.0f) {
			g_gamecallback->disconnect();
			return;
		}
	}
#endif
	bool can_be_and_is_paused =
			(simple_singleplayer_mode && g_menumgr.pausesGame());

	if (can_be_and_is_paused) {	// This is for a singleplayer server
		*dtime = 0;             // No time passes
	} else {
		if (simple_singleplayer_mode && !m_anim_nodes.empty())
			pauseAnimation(false);

		if (server != NULL) {
			//TimeTaker timer("server->step(dtime)");
			server->step(*dtime);
		}

		//TimeTaker timer("client.step(dtime)");
		client->step(*dtime);
	}
}


void Game::processClientEvents(CameraOrientation *cam)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();

	while (client->hasClientEvents()) {
		ClientEvent event = client->getClientEvent();

		switch (event.type) {
		case CE_PLAYER_DAMAGE:
			if (client->getHP() == 0)
				break;
			if (client->moddingEnabled()) {
				client->getScript()->on_damage_taken(event.player_damage.amount);
			}

			runData.damage_flash += 95.0 + 3.2 * event.player_damage.amount;
			runData.damage_flash = MYMIN(runData.damage_flash, 127.0);

			player->hurt_tilt_timer = 1.5;
			player->hurt_tilt_strength =
				rangelim(event.player_damage.amount / 4, 1.0, 4.0);

			client->event()->put(new SimpleTriggerEvent("PlayerDamage"));
			break;

		case CE_PLAYER_FORCE_MOVE:
			cam->camera_yaw = event.player_force_move.yaw;
			cam->camera_pitch = event.player_force_move.pitch;
			break;

		case CE_DEATHSCREEN:
#ifdef DISABLE_CSM
			showDeathScreen();
			chat_backend->addMessage(L"", L"You died.");
#else
			// This should be enabled for death formspec in builtin
			client->getScript()->on_death();
#endif

			/* Handle visualization */
			runData.damage_flash = 0;
			player->hurt_tilt_timer = 0;
			player->hurt_tilt_strength = 0;
			break;

		case CE_SHOW_FORMSPEC:
			if (*(event.show_formspec.formspec) == "") {
				if (current_formspec && ( *(event.show_formspec.formname) == "" || *(event.show_formspec.formname) == cur_formname) ){
					current_formspec->quitMenu();
				}
			} else {
				FormspecFormSource *fs_src =
					new FormspecFormSource(*(event.show_formspec.formspec));
				TextDestPlayerInventory *txt_dst =
					new TextDestPlayerInventory(client, *(event.show_formspec.formname));

				create_formspec_menu(&current_formspec, client, device, &input->joystick,
					fs_src, txt_dst);
				cur_formname = *(event.show_formspec.formname);
			}

			delete event.show_formspec.formspec;
			delete event.show_formspec.formname;
			break;

		case CE_SHOW_LOCAL_FORMSPEC:
			{
				FormspecFormSource *fs_src = new FormspecFormSource(*event.show_formspec.formspec);
				LocalFormspecHandler *txt_dst = new LocalFormspecHandler(*event.show_formspec.formname, client);
				create_formspec_menu(&current_formspec, client, device, &input->joystick,
					fs_src, txt_dst);
			}
			delete event.show_formspec.formspec;
			delete event.show_formspec.formname;
			break;

		case CE_SPAWN_PARTICLE:
		case CE_ADD_PARTICLESPAWNER:
		case CE_DELETE_PARTICLESPAWNER:
			client->getParticleManager()->handleParticleEvent(&event, client,
					smgr, player);
			break;

		case CE_HUDADD:
			{
				u32 id = event.hudadd.id;

				HudElement *e = player->getHud(id);

				if (e != NULL) {
					delete event.hudadd.pos;
					delete event.hudadd.name;
					delete event.hudadd.scale;
					delete event.hudadd.text;
					delete event.hudadd.align;
					delete event.hudadd.offset;
					delete event.hudadd.world_pos;
					delete event.hudadd.size;
					continue;
				}

				e = new HudElement;
				e->type   = (HudElementType)event.hudadd.type;
				e->pos    = *event.hudadd.pos;
				e->name   = *event.hudadd.name;
				e->scale  = *event.hudadd.scale;
				e->text   = *event.hudadd.text;
				e->number = event.hudadd.number;
				e->item   = event.hudadd.item;
				e->dir    = event.hudadd.dir;
				e->align  = *event.hudadd.align;
				e->offset = *event.hudadd.offset;
				e->world_pos = *event.hudadd.world_pos;
				e->size = *event.hudadd.size;

				u32 new_id = player->addHud(e);
				//if this isn't true our huds aren't consistent
				sanity_check(new_id == id);
			}

			delete event.hudadd.pos;
			delete event.hudadd.name;
			delete event.hudadd.scale;
			delete event.hudadd.text;
			delete event.hudadd.align;
			delete event.hudadd.offset;
			delete event.hudadd.world_pos;
			delete event.hudadd.size;
			break;

		case CE_HUDRM:
			{
				HudElement *e = player->removeHud(event.hudrm.id);

				if (e != NULL)
					delete e;
			}
			break;

		case CE_HUDCHANGE:
			{
				u32 id = event.hudchange.id;
				HudElement *e = player->getHud(id);

				if (e == NULL) {
					delete event.hudchange.v3fdata;
					delete event.hudchange.v2fdata;
					delete event.hudchange.sdata;
					delete event.hudchange.v2s32data;
					continue;
				}

				switch (event.hudchange.stat) {
				case HUD_STAT_POS:
					e->pos = *event.hudchange.v2fdata;
					break;

				case HUD_STAT_NAME:
					e->name = *event.hudchange.sdata;
					break;

				case HUD_STAT_SCALE:
					e->scale = *event.hudchange.v2fdata;
					break;

				case HUD_STAT_TEXT:
					e->text = *event.hudchange.sdata;
					break;

				case HUD_STAT_NUMBER:
					e->number = event.hudchange.data;
					break;

				case HUD_STAT_ITEM:
					e->item = event.hudchange.data;
					break;

				case HUD_STAT_DIR:
					e->dir = event.hudchange.data;
					break;

				case HUD_STAT_ALIGN:
					e->align = *event.hudchange.v2fdata;
					break;

				case HUD_STAT_OFFSET:
					e->offset = *event.hudchange.v2fdata;
					break;

				case HUD_STAT_WORLD_POS:
					e->world_pos = *event.hudchange.v3fdata;
					break;

				case HUD_STAT_SIZE:
					e->size = *event.hudchange.v2s32data;
					break;
				}
			}

			delete event.hudchange.v3fdata;
			delete event.hudchange.v2fdata;
			delete event.hudchange.sdata;
			delete event.hudchange.v2s32data;
			break;

		case CE_SET_SKY:
			sky->setVisible(false);
			// Whether clouds are visible in front of a custom skybox
			sky->setCloudsEnabled(event.set_sky.clouds);

			if (skybox) {
				skybox->remove();
				skybox = NULL;
			}

			// Handle according to type
			if (*event.set_sky.type == "regular") {
				sky->setVisible(true);
				sky->setCloudsEnabled(true);
			} else if (*event.set_sky.type == "skybox" &&
					event.set_sky.params->size() == 6) {
				sky->setFallbackBgColor(*event.set_sky.bgcolor);
				skybox = smgr->addSkyBoxSceneNode(
						 texture_src->getTextureForMesh((*event.set_sky.params)[0]),
						 texture_src->getTextureForMesh((*event.set_sky.params)[1]),
						 texture_src->getTextureForMesh((*event.set_sky.params)[2]),
						 texture_src->getTextureForMesh((*event.set_sky.params)[3]),
						 texture_src->getTextureForMesh((*event.set_sky.params)[4]),
						 texture_src->getTextureForMesh((*event.set_sky.params)[5]));
			}
			// Handle everything else as plain color
			else {
				if (*event.set_sky.type != "plain")
					infostream << "Unknown sky type: "
						   << (*event.set_sky.type) << std::endl;

				sky->setFallbackBgColor(*event.set_sky.bgcolor);
			}

			delete event.set_sky.bgcolor;
			delete event.set_sky.type;
			delete event.set_sky.params;
			break;

		case CE_OVERRIDE_DAY_NIGHT_RATIO:
			client->getEnv().setDayNightRatioOverride(
					event.override_day_night_ratio.do_override,
					event.override_day_night_ratio.ratio_f * 1000);
			break;

		case CE_CLOUD_PARAMS:
			if (clouds) {
				clouds->setDensity(event.cloud_params.density);
				clouds->setColorBright(video::SColor(event.cloud_params.color_bright));
				clouds->setColorAmbient(video::SColor(event.cloud_params.color_ambient));
				clouds->setHeight(event.cloud_params.height);
				clouds->setThickness(event.cloud_params.thickness);
				clouds->setSpeed(v2f(
						event.cloud_params.speed_x,
						event.cloud_params.speed_y));
			}
			break;

		default:
			// unknown or unhandled type
			break;

		}
	}
}


void Game::updateCamera(u32 busy_time, f32 dtime)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();

	/*
		For interaction purposes, get info about the held item
		- What item is it?
		- Is it a usable item?
		- Can it point to liquids?
	*/
	ItemStack playeritem;
	{
		InventoryList *mlist = local_inventory->getList("main");

		if (mlist && client->getPlayerItem() < mlist->getSize())
			playeritem = mlist->getItem(client->getPlayerItem());
	}

	if (playeritem.getDefinition(itemdef_manager).name.empty()) { // override the hand
		InventoryList *hlist = local_inventory->getList("hand");
		if (hlist)
			playeritem = hlist->getItem(0);
	}


	ToolCapabilities playeritem_toolcap =
		playeritem.getToolCapabilities(itemdef_manager);

	v3s16 old_camera_offset = camera->getOffset();

	if (wasKeyDown(KeyType::CAMERA_MODE)) {
		GenericCAO *playercao = player->getCAO();

		// If playercao not loaded, don't change camera
		if (playercao == NULL)
			return;

		camera->toggleCameraMode();

		playercao->setVisible(camera->getCameraMode() > CAMERA_MODE_FIRST);
		playercao->setChildrenVisible(camera->getCameraMode() > CAMERA_MODE_FIRST);
	}

	float full_punch_interval = playeritem_toolcap.full_punch_interval;
	float tool_reload_ratio = runData.time_from_last_punch / full_punch_interval;

	tool_reload_ratio = MYMIN(tool_reload_ratio, 1.0);
	camera->update(player, dtime, busy_time / 1000.0f, tool_reload_ratio,
		      client->getEnv());
	camera->step(dtime);

	v3f camera_position = camera->getPosition();
	v3f camera_direction = camera->getDirection();
	f32 camera_fov = camera->getFovMax();
	v3s16 camera_offset = camera->getOffset();

	m_camera_offset_changed = (camera_offset != old_camera_offset);

	if (!flags.disable_camera_update) {
		client->getEnv().getClientMap().updateCamera(camera_position,
				camera_direction, camera_fov, camera_offset);

		if (m_camera_offset_changed) {
			client->updateCameraOffset(camera_offset);
			client->getEnv().updateCameraOffset(camera_offset);

			if (clouds)
				clouds->updateCameraOffset(camera_offset);
		}
	}
}


void Game::updateSound(f32 dtime)
{
	// Update sound listener
	v3s16 camera_offset = camera->getOffset();
	sound->updateListener(camera->getCameraNode()->getPosition() + intToFloat(camera_offset, BS),
			      v3f(0, 0, 0), // velocity
			      camera->getDirection(),
			      camera->getCameraNode()->getUpVector());

	// Check if volume is in the proper range, else fix it.
	float old_volume = g_settings->getFloat("sound_volume");
	float new_volume = rangelim(old_volume, 0.0f, 1.0f);
	sound->setListenerGain(new_volume);

	if (old_volume != new_volume) {
		g_settings->setFloat("sound_volume", new_volume);
	}

	LocalPlayer *player = client->getEnv().getLocalPlayer();

	// Tell the sound maker whether to make footstep sounds
	soundmaker->makes_footstep_sound = player->makes_footstep_sound;

	//	Update sound maker
	if (player->makes_footstep_sound)
		soundmaker->step(dtime);

	ClientMap &map = client->getEnv().getClientMap();
	MapNode n = map.getNodeNoEx(player->getFootstepNodePos());
	soundmaker->m_player_step_sound = nodedef_manager->get(n).sound_footstep;
}


void Game::processPlayerInteraction(f32 dtime, bool show_hud, bool show_debug)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();

	ItemStack playeritem;
	{
		InventoryList *mlist = local_inventory->getList("main");

		if (mlist && client->getPlayerItem() < mlist->getSize())
			playeritem = mlist->getItem(client->getPlayerItem());
	}

	const ItemDefinition &playeritem_def =
			playeritem.getDefinition(itemdef_manager);
	InventoryList *hlist = local_inventory->getList("hand");
	const ItemDefinition &hand_def =
		hlist ? hlist->getItem(0).getDefinition(itemdef_manager) : itemdef_manager->get("");

	v3f player_position  = player->getPosition();
	v3f camera_position  = camera->getPosition();
	v3f camera_direction = camera->getDirection();
	v3s16 camera_offset  = camera->getOffset();


	/*
		Calculate what block is the crosshair pointing to
	*/

	f32 d = playeritem_def.range; // max. distance
	f32 d_hand = hand_def.range;

	if (d < 0 && d_hand >= 0)
		d = d_hand;
	else if (d < 0)
		d = 4.0;

	core::line3d<f32> shootline;

	if (camera->getCameraMode() != CAMERA_MODE_THIRD_FRONT) {
		shootline = core::line3d<f32>(camera_position,
			camera_position + camera_direction * BS * d);
	} else {
	    // prevent player pointing anything in front-view
		shootline = core::line3d<f32>(camera_position,camera_position);
	}

#ifdef HAVE_TOUCHSCREENGUI

	if ((g_settings->getBool("touchtarget")) && (g_touchscreengui)) {
		shootline = g_touchscreengui->getShootline();
		// Scale shootline to the acual distance the player can reach
		shootline.end = shootline.start
			+ shootline.getVector().normalize() * BS * d;
		shootline.start += intToFloat(camera_offset, BS);
		shootline.end += intToFloat(camera_offset, BS);
	}

#endif

	PointedThing pointed = updatePointedThing(shootline,
			playeritem_def.liquids_pointable,
			!runData.ldown_for_dig,
			camera_offset);

	if (pointed != runData.pointed_old) {
		infostream << "Pointing at " << pointed.dump() << std::endl;
		hud->updateSelectionMesh(camera_offset);
	}

	/*
		Stop digging when
		- releasing left mouse button
		- pointing away from node
	*/
	if (runData.digging) {
		if (getLeftReleased()) {
			infostream << "Left button released"
			           << " (stopped digging)" << std::endl;
			runData.digging = false;
		} else if (pointed != runData.pointed_old) {
			if (pointed.type == POINTEDTHING_NODE
					&& runData.pointed_old.type == POINTEDTHING_NODE
					&& pointed.node_undersurface
							== runData.pointed_old.node_undersurface) {
				// Still pointing to the same node, but a different face.
				// Don't reset.
			} else {
				infostream << "Pointing away from node"
				           << " (stopped digging)" << std::endl;
				runData.digging = false;
				hud->updateSelectionMesh(camera_offset);
			}
		}

		if (!runData.digging) {
			client->interact(1, runData.pointed_old);
			client->setCrack(-1, v3s16(0, 0, 0));
			runData.dig_time = 0.0;
		}
	} else if (runData.dig_instantly && getLeftReleased()) {
		// Remove e.g. torches faster when clicking instead of holding LMB
		runData.nodig_delay_timer = 0;
		runData.dig_instantly = false;
	}

	if (!runData.digging && runData.ldown_for_dig && !isLeftPressed()) {
		runData.ldown_for_dig = false;
	}

	runData.left_punch = false;

	soundmaker->m_player_leftpunch_sound.name = "";

	if (isRightPressed())
		runData.repeat_rightclick_timer += dtime;
	else
		runData.repeat_rightclick_timer = 0;

	if (playeritem_def.usable && isLeftPressed()) {
		if (getLeftClicked() && (!client->moddingEnabled()
				|| !client->getScript()->on_item_use(playeritem, pointed)))
			client->interact(4, pointed);
	} else if (pointed.type == POINTEDTHING_NODE) {
		ToolCapabilities playeritem_toolcap =
				playeritem.getToolCapabilities(itemdef_manager);
		if (playeritem.name.empty() && hand_def.tool_capabilities != NULL) {
			playeritem_toolcap = *hand_def.tool_capabilities;
		}
		handlePointingAtNode(pointed, playeritem_def, playeritem,
			playeritem_toolcap, dtime);
	} else if (pointed.type == POINTEDTHING_OBJECT) {
		handlePointingAtObject(pointed, playeritem, player_position, show_debug);
	} else if (isLeftPressed()) {
		// When button is held down in air, show continuous animation
		runData.left_punch = true;
	} else if (getRightClicked()) {
		handlePointingAtNothing(playeritem);
	}

	runData.pointed_old = pointed;

	if (runData.left_punch || getLeftClicked())
		camera->setDigging(0); // left click animation

	input->resetLeftClicked();
	input->resetRightClicked();

	input->joystick.clearWasKeyDown(KeyType::MOUSE_L);
	input->joystick.clearWasKeyDown(KeyType::MOUSE_R);

	input->resetLeftReleased();
	input->resetRightReleased();

	input->joystick.clearWasKeyReleased(KeyType::MOUSE_L);
	input->joystick.clearWasKeyReleased(KeyType::MOUSE_R);
}


PointedThing Game::updatePointedThing(
	const core::line3d<f32> &shootline,
	bool liquids_pointable,
	bool look_for_object,
	const v3s16 &camera_offset)
{
	std::vector<aabb3f> *selectionboxes = hud->getSelectionBoxes();
	selectionboxes->clear();
	hud->setSelectedFaceNormal(v3f(0.0, 0.0, 0.0));
	static const bool show_entity_selectionbox = g_settings->getBool(
		"show_entity_selectionbox");

	ClientMap &map = client->getEnv().getClientMap();
	INodeDefManager *nodedef=client->getNodeDefManager();

	runData.selected_object = NULL;

	PointedThing result=client->getEnv().getPointedThing(
		shootline, liquids_pointable, look_for_object);
	if (result.type == POINTEDTHING_OBJECT) {
		runData.selected_object = client->getEnv().getActiveObject(result.object_id);
		if (show_entity_selectionbox && runData.selected_object->doShowSelectionBox()) {
			aabb3f *selection_box = runData.selected_object->getSelectionBox();

			// Box should exist because object was
			// returned in the first place

			assert(selection_box);

			v3f pos = runData.selected_object->getPosition();
			selectionboxes->push_back(aabb3f(
				selection_box->MinEdge, selection_box->MaxEdge));
			selectionboxes->push_back(
				aabb3f(selection_box->MinEdge, selection_box->MaxEdge));
			hud->setSelectionPos(pos, camera_offset);
		}
	} else if (result.type == POINTEDTHING_NODE) {
		// Update selection boxes
		MapNode n = map.getNodeNoEx(result.node_undersurface);
		std::vector<aabb3f> boxes;
		n.getSelectionBoxes(nodedef, &boxes,
			n.getNeighbors(result.node_undersurface, &map));

		f32 d = 0.002 * BS;
		for (std::vector<aabb3f>::const_iterator i = boxes.begin();
			i != boxes.end(); ++i) {
			aabb3f box = *i;
			box.MinEdge -= v3f(d, d, d);
			box.MaxEdge += v3f(d, d, d);
			selectionboxes->push_back(box);
		}
		hud->setSelectionPos(intToFloat(result.node_undersurface, BS),
			camera_offset);
		hud->setSelectedFaceNormal(v3f(
			result.intersection_normal.X,
			result.intersection_normal.Y,
			result.intersection_normal.Z));
	}

	// Update selection mesh light level and vertex colors
	if (selectionboxes->size() > 0) {
		v3f pf = hud->getSelectionPos();
		v3s16 p = floatToInt(pf, BS);

		// Get selection mesh light level
		MapNode n = map.getNodeNoEx(p);
		u16 node_light = getInteriorLight(n, -1, nodedef);
		u16 light_level = node_light;

		for (u8 i = 0; i < 6; i++) {
			n = map.getNodeNoEx(p + g_6dirs[i]);
			node_light = getInteriorLight(n, -1, nodedef);
			if (node_light > light_level)
				light_level = node_light;
		}

		u32 daynight_ratio = client->getEnv().getDayNightRatio();
		video::SColor c;
		final_color_blend(&c, light_level, daynight_ratio);

		// Modify final color a bit with time
		u32 timer = porting::getTimeMs() % 5000;
		float timerf = (float) (irr::core::PI * ((timer / 2500.0) - 0.5));
		float sin_r = 0.08 * sin(timerf);
		float sin_g = 0.08 * sin(timerf + irr::core::PI * 0.5);
		float sin_b = 0.08 * sin(timerf + irr::core::PI);
		c.setRed(core::clamp(core::round32(c.getRed() * (0.8 + sin_r)), 0, 255));
		c.setGreen(core::clamp(core::round32(c.getGreen() * (0.8 + sin_g)), 0, 255));
		c.setBlue(core::clamp(core::round32(c.getBlue() * (0.8 + sin_b)), 0, 255));

		// Set mesh final color
		hud->setSelectionMeshColor(c);
	}
	return result;
}


void Game::handlePointingAtNothing(const ItemStack &playerItem)
{
	infostream << "Right Clicked in Air" << std::endl;
	PointedThing fauxPointed;
	fauxPointed.type = POINTEDTHING_NOTHING;
	client->interact(5, fauxPointed);
}


void Game::handlePointingAtNode(const PointedThing &pointed,
	const ItemDefinition &playeritem_def, const ItemStack &playeritem,
	const ToolCapabilities &playeritem_toolcap, f32 dtime)
{
	v3s16 nodepos = pointed.node_undersurface;
	v3s16 neighbourpos = pointed.node_abovesurface;

	/*
		Check information text of node
	*/

	ClientMap &map = client->getEnv().getClientMap();

	bool digging = false;
	if (runData.nodig_delay_timer <= 0.0 && isLeftPressed()
			&& client->checkPrivilege("interact")) {
		handleDigging(pointed, nodepos, playeritem_toolcap, dtime);
		digging = true;
		runData.noplace_delay_timer = 1.0;
	}

	// This should be done after digging handling
	NodeMetadata *meta = map.getNodeMetadata(nodepos);

	if (meta) {
		infotext = unescape_enriched(utf8_to_wide(meta->getString("infotext")));
	} else {
		MapNode n = map.getNodeNoEx(nodepos);

		if (nodedef_manager->get(n).tiledef[0].name == "unknown_node.png") {
			infotext = L"Unknown node: ";
			infotext += utf8_to_wide(nodedef_manager->get(n).name);
		}
	}

	if ((getRightClicked() ||
				  runData.repeat_rightclick_timer >= m_repeat_right_click_time) &&
				  !digging && runData.noplace_delay_timer <= 0.0 &&
		client->checkPrivilege("interact")) {
		runData.repeat_rightclick_timer = 0;
		infostream << "Ground right-clicked" << std::endl;

		if (meta && meta->getString("formspec") != "" && !random_input
				&& !isKeyDown(KeyType::SNEAK)) {
			// Report right click to server
			if (nodedef_manager->get(map.getNodeNoEx(nodepos)).rightclickable) {
				client->interact(3, pointed);
			}

			infostream << "Launching custom inventory view" << std::endl;

			InventoryLocation inventoryloc;
			inventoryloc.setNodeMeta(nodepos);

			NodeMetadataFormSource *fs_src = new NodeMetadataFormSource(
				&client->getEnv().getClientMap(), nodepos);
			TextDest *txt_dst = new TextDestNodeMetadata(nodepos, client);

			create_formspec_menu(&current_formspec, client,
					device, &input->joystick, fs_src, txt_dst);
			cur_formname = "";

			current_formspec->setFormSpec(meta->getString("formspec"), inventoryloc);
		} else {
			// Report right click to server

			camera->setDigging(1);  // right click animation (always shown for feedback)

			// If the wielded item has node placement prediction,
			// make that happen
			bool placed = nodePlacementPrediction(*client,
					playeritem_def, playeritem,
					nodepos, neighbourpos);

			if (placed) {
				// Report to server
				client->interact(3, pointed);
				// Read the sound
				soundmaker->m_player_rightpunch_sound =
						playeritem_def.sound_place;

				if (client->moddingEnabled())
					client->getScript()->on_placenode(pointed, playeritem_def);
			} else {
				soundmaker->m_player_rightpunch_sound =
						SimpleSoundSpec();

				if (playeritem_def.node_placement_prediction == "" ||
						nodedef_manager->get(map.getNodeNoEx(nodepos)).rightclickable) {
					client->interact(3, pointed); // Report to server
				} else {
					soundmaker->m_player_rightpunch_sound =
						playeritem_def.sound_place_failed;
				}
			}
		}
	}
}


void Game::handlePointingAtObject(const PointedThing &pointed, const ItemStack &playeritem,
		const v3f &player_position, bool show_debug)
{
	infotext = unescape_enriched(
		utf8_to_wide(runData.selected_object->infoText()));

	if (show_debug) {
		if (infotext != L"") {
			infotext += L"\n";
		}
		infotext += unescape_enriched(utf8_to_wide(
			runData.selected_object->debugInfoText()));
	}

	const ItemDefinition &playeritem_def =
		playeritem.getDefinition(itemdef_manager);
	bool nohit_enabled = ((ItemGroupList) playeritem_def.groups)["nohit"] != 0;

#ifdef HAVE_TOUCHSCREENGUI
	if (input->getRightClicked() && !nohit_enabled) {
#else
	if (input->getLeftState() && !nohit_enabled) {
#endif
		bool do_punch = false;
		bool do_punch_damage = false;

		if (runData.object_hit_delay_timer <= 0.0) {
			do_punch = true;
			do_punch_damage = true;
			runData.object_hit_delay_timer = object_hit_delay;
		}

		if (getLeftClicked())
			do_punch = true;

		if (do_punch) {
			infostream << "Left-clicked object" << std::endl;
			runData.left_punch = true;
		}

		if (do_punch_damage) {
			// Report direct punch
			v3f objpos = runData.selected_object->getPosition();
			v3f dir = (objpos - player_position).normalize();
			ItemStack item = playeritem;
			if (playeritem.name.empty()) {
				InventoryList *hlist = local_inventory->getList("hand");
				if (hlist) {
					item = hlist->getItem(0);
				}
			}

			bool disable_send = runData.selected_object->directReportPunch(
					dir, &item, runData.time_from_last_punch);
			runData.time_from_last_punch = 0;

			if (!disable_send)
				client->interact(0, pointed);
		}
#ifdef HAVE_TOUCHSCREENGUI
	} else if (input->getLeftClicked() || (input->getRightClicked() && nohit_enabled)) {
#else
	} else if (input->getRightClicked() || (input->getLeftClicked() && nohit_enabled)) {
#endif
		infostream << "Right-clicked object" << std::endl;
		client->interact(3, pointed);  // place
	}
}


void Game::handleDigging(const PointedThing &pointed, const v3s16 &nodepos,
		const ToolCapabilities &playeritem_toolcap, f32 dtime)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();
	ClientMap &map = client->getEnv().getClientMap();
	MapNode n = client->getEnv().getClientMap().getNodeNoEx(nodepos);

	// NOTE: Similar piece of code exists on the server side for
	// cheat detection.
	// Get digging parameters
	DigParams params = getDigParams(nodedef_manager->get(n).groups,
			&playeritem_toolcap);

	// If can't dig, try hand
	if (!params.diggable) {
		InventoryList *hlist = local_inventory->getList("hand");
		const ItemDefinition &hand =
			hlist ? hlist->getItem(0).getDefinition(itemdef_manager) : itemdef_manager->get("");
		const ToolCapabilities *tp = hand.tool_capabilities;

		if (tp)
			params = getDigParams(nodedef_manager->get(n).groups, tp);
	}

	if (!params.diggable) {
		// I guess nobody will wait for this long
		runData.dig_time_complete = 10000000.0;
	} else {
		runData.dig_time_complete = params.time;

		if (m_cache_enable_particles) {
			const ContentFeatures &features =
					client->getNodeDefManager()->get(n);
			client->getParticleManager()->addPunchingParticles(client, smgr,
					player, nodepos, n, features);
		}
	}

	if (!runData.digging) {
		infostream << "Started digging" << std::endl;
		runData.dig_instantly = runData.dig_time_complete == 0;
		if (client->moddingEnabled() && client->getScript()->on_punchnode(nodepos, n))
			return;
		client->interact(0, pointed);
		runData.digging = true;
		runData.ldown_for_dig = true;
	}

	if (!runData.dig_instantly) {
		runData.dig_index = (float)crack_animation_length
				* runData.dig_time
				/ runData.dig_time_complete;
	} else {
		// This is for e.g. torches
		runData.dig_index = crack_animation_length;
	}

	SimpleSoundSpec sound_dig = nodedef_manager->get(n).sound_dig;

	if (sound_dig.exists() && params.diggable) {
		if (sound_dig.name == "__group") {
			if (params.main_group != "") {
				soundmaker->m_player_leftpunch_sound.gain = 0.5;
				soundmaker->m_player_leftpunch_sound.name =
						std::string("default_dig_") +
						params.main_group;
			}
		} else {
			soundmaker->m_player_leftpunch_sound = sound_dig;
		}
	}

	// Don't show cracks if not diggable
	if (runData.dig_time_complete >= 100000.0) {
	} else if (runData.dig_index < crack_animation_length) {
		//TimeTaker timer("client.setTempMod");
		//infostream<<"dig_index="<<dig_index<<std::endl;
		client->setCrack(runData.dig_index, nodepos);
	} else {
		infostream << "Digging completed" << std::endl;
		runData.noplace_delay_timer = 1.0;
		client->interact(2, pointed);
		client->setCrack(-1, v3s16(0, 0, 0));

		runData.dig_time = 0;
		runData.digging = false;

		runData.nodig_delay_timer =
				runData.dig_time_complete / (float)crack_animation_length;

		// We don't want a corresponding delay to very time consuming nodes
		// and nodes without digging time (e.g. torches) get a fixed delay.
		if (runData.nodig_delay_timer > 0.3)
			runData.nodig_delay_timer = 0.3;
		else if (runData.dig_instantly)
			runData.nodig_delay_timer = 0.15;

		bool is_valid_position;
		MapNode wasnode = map.getNodeNoEx(nodepos, &is_valid_position);
		if (is_valid_position) {
			if (client->moddingEnabled() &&
			    		client->getScript()->on_dignode(nodepos, wasnode)) {
				return;
			}
			client->removeNode(nodepos);
		}

		client->interact(2, pointed);

		if (m_cache_enable_particles) {
			const ContentFeatures &features =
				client->getNodeDefManager()->get(wasnode);
			client->getParticleManager()->addDiggingParticles(client, smgr,
				player, nodepos, wasnode, features);
		}


		// Send event to trigger sound
		MtEvent *e = new NodeDugEvent(nodepos, wasnode);
		client->event()->put(e);
	}

	if (runData.dig_time_complete < 100000.0) {
		runData.dig_time += dtime;
	} else {
		runData.dig_time = 0;
		client->setCrack(-1, nodepos);
	}

	camera->setDigging(0);  // left click animation
}


void Game::updateFrame(ProfilerGraph *graph, RunStats *stats, f32 dtime,
		const CameraOrientation &cam)
{
	LocalPlayer *player = client->getEnv().getLocalPlayer();

	/*
		Fog range
	*/

	if (draw_control->range_all) {
		#if defined(__ANDROID__) || defined(__IOS__)
			runData.fog_range = draw_control->wanted_range * 4 * BS;
		#else
			runData.fog_range = 100000 * BS;
		#endif
	} else {
		runData.fog_range = draw_control->wanted_range * BS;
	}

	/*
		Calculate general brightness
	*/
	u32 daynight_ratio = client->getEnv().getDayNightRatio();
	float time_brightness = decode_light_f((float)daynight_ratio / 1000.0);
	float direct_brightness;
	bool sunlight_seen;

	if (m_cache_enable_noclip && m_cache_enable_free_move) {
		direct_brightness = time_brightness;
		sunlight_seen = true;
	} else {
		ScopeProfiler sp(g_profiler, "Detecting background light", SPT_AVG);
		float old_brightness = sky->getBrightness();
		direct_brightness = client->getEnv().getClientMap()
				.getBackgroundBrightness(MYMIN(runData.fog_range * 1.2, 60 * BS),
					daynight_ratio, (int)(old_brightness * 255.5), &sunlight_seen)
				    / 255.0;
	}

	float time_of_day_smooth = runData.time_of_day_smooth;
	float time_of_day = client->getEnv().getTimeOfDayF();

	static const float maxsm = 0.05;
	static const float todsm = 0.05;

	if (fabs(time_of_day - time_of_day_smooth) > maxsm &&
			fabs(time_of_day - time_of_day_smooth + 1.0) > maxsm &&
			fabs(time_of_day - time_of_day_smooth - 1.0) > maxsm)
		time_of_day_smooth = time_of_day;

	if (time_of_day_smooth > 0.8 && time_of_day < 0.2)
		time_of_day_smooth = time_of_day_smooth * (1.0 - todsm)
				+ (time_of_day + 1.0) * todsm;
	else
		time_of_day_smooth = time_of_day_smooth * (1.0 - todsm)
				+ time_of_day * todsm;

	runData.time_of_day = time_of_day;
	runData.time_of_day_smooth = time_of_day_smooth;

	sky->update(time_of_day_smooth, time_brightness, direct_brightness,
			sunlight_seen, camera->getCameraMode(), player->getYaw(),
			player->getPitch());

	/*
		Update clouds
	*/
	if (clouds) {
		v3f player_position = player->getPosition();
		if (sky->getCloudsVisible()) {
			clouds->setVisible(true);
			clouds->step(dtime);
			clouds->update(v2f(player_position.X, player_position.Z),
				       sky->getCloudColor());
		} else {
			clouds->setVisible(false);
		}
	}

	/*
		Update particles
	*/
	client->getParticleManager()->step(dtime);

	/*
		Fog
	*/

	if (m_cache_enable_fog && !flags.force_fog_off) {
		driver->setFog(
				sky->getBgColor(),
				video::EFT_FOG_LINEAR,
				runData.fog_range * m_cache_fog_start,
				runData.fog_range * 1.0,
				0.01,
				false, // pixel fog
				true // range fog
		);
	} else {
		driver->setFog(
				sky->getBgColor(),
				video::EFT_FOG_LINEAR,
				100000 * BS,
				110000 * BS,
				0.01,
				false, // pixel fog
				false // range fog
		);
	}

	/*
		Get chat messages from client
	*/

	v2u32 screensize = driver->getScreenSize();

	updateChat(*client, dtime, flags.show_debug, screensize,
			flags.show_chat, runData.profiler_current_page,
			*chat_backend, guitext_chat);

	/*
		Inventory
	*/

	if (client->getPlayerItem() != runData.new_playeritem)
		client->selectPlayerItem(runData.new_playeritem);

	// Update local inventory if it has changed
	if (client->getLocalInventoryUpdated()) {
		//infostream<<"Updating local inventory"<<std::endl;
		client->getLocalInventory(*local_inventory);
		runData.update_wielded_item_trigger = true;
	}

	if (runData.update_wielded_item_trigger) {
		// Update wielded tool
		InventoryList *mlist = local_inventory->getList("main");

		if (mlist && (client->getPlayerItem() < mlist->getSize())) {
			ItemStack item = mlist->getItem(client->getPlayerItem());
			if (item.getDefinition(itemdef_manager).name.empty()) { // override the hand
				InventoryList *hlist = local_inventory->getList("hand");
				if (hlist)
					item = hlist->getItem(0);
			}
			camera->wield(item);

			// Show item description as statustext
			std::string item_desc = item.getDefinition(itemdef_manager).description;
			if (wield_name != item_desc) {
				m_statustext = utf8_to_wide(item_desc);
				runData.statustext_time = 0;
				wield_name = item_desc;
			}
		}

		runData.update_wielded_item_trigger = false;
	}

	/*
		Update block draw list every 200ms or when camera direction has
		changed much
	*/
	runData.update_draw_list_timer += dtime;

	v3f camera_direction = camera->getDirection();
	if (runData.update_draw_list_timer >= 0.2
			|| runData.update_draw_list_last_cam_dir.getDistanceFrom(camera_direction) > 0.2
			|| m_camera_offset_changed) {
		runData.update_draw_list_timer = 0;
		client->getEnv().getClientMap().updateDrawList(driver);
		runData.update_draw_list_last_cam_dir = camera_direction;
	}

	updateGui(*stats, dtime, cam);

	/*
	   make sure menu is on top
	   1. Delete formspec menu reference if menu was removed
	   2. Else, make sure formspec menu is on top
	*/
	if (current_formspec) {
		if (current_formspec->getReferenceCount() == 1) {
			current_formspec->drop();
			current_formspec = NULL;
		} else if (isMenuActive()) {
			guiroot->bringToFront(current_formspec);
		}
	}

	/*
		Drawing begins
	*/

	const video::SColor &skycolor = sky->getSkyColor();

	TimeTaker tt_draw("mainloop: draw");
	driver->beginScene(true, true, skycolor);

	draw_scene(driver, smgr, *camera, *client, player, *hud, mapper,
			guienv, screensize, skycolor, flags.show_hud,
			flags.show_minimap);

	/*
		Profiler graph
	*/
	if (flags.show_profiler_graph)
		graph->draw(10, screensize.Y - 10, driver, g_fontengine->getFont());

	/*
		Damage flash
	*/
	if (runData.damage_flash > 0.0) {
		video::SColor color(runData.damage_flash, 180, 0, 0);
		driver->draw2DRectangle(color,
					core::rect<s32>(0, 0, screensize.X, screensize.Y),
					NULL);

		runData.damage_flash -= 100.0 * dtime;
	}

	/*
		Damage camera tilt
	*/
	if (player->hurt_tilt_timer > 0.0) {
		player->hurt_tilt_timer -= dtime * 5;

		if (player->hurt_tilt_timer < 0)
			player->hurt_tilt_strength = 0;
	}

	/*
		Update minimap pos and rotation
	*/
	if (mapper && flags.show_minimap && flags.show_hud) {
		mapper->setPos(floatToInt(player->getPosition(), BS));
		mapper->setAngle(player->getYaw());
	}

	/*
		End scene
	*/
	driver->endScene();

	stats->drawtime = tt_draw.stop(true);
	g_profiler->graphAdd("mainloop_draw", stats->drawtime / 1000.0f);
}


inline static const char *yawToDirectionString(int yaw)
{
	static const char *direction[4] = {"North [+Z]", "West [-X]", "South [-Z]", "East [+X]"};

	yaw = wrapDegrees_0_360(yaw);
	yaw = (yaw + 45) % 360 / 90;

	return direction[yaw];
}


void Game::updateGui(const RunStats &stats, f32 dtime, const CameraOrientation &cam)
{
	v2u32 screensize = driver->getScreenSize();
	LocalPlayer *player = client->getEnv().getLocalPlayer();
	v3f player_position = player->getPosition();

	if (flags.show_debug) {
		static float drawtime_avg = 0;
		drawtime_avg = drawtime_avg * 0.95 + stats.drawtime * 0.05;

		u16 fps = 1.0 / stats.dtime_jitter.avg;

		std::ostringstream os(std::ios_base::binary);
		os << std::fixed
		   << PROJECT_NAME_C " " << g_version_hash
		   << " FPS = " << fps
		   << " (R: range_all=" << draw_control->range_all << ")"
		   << std::setprecision(0)
		   << " drawtime = " << drawtime_avg
		   << std::setprecision(1)
		   << ", dtime_jitter = "
		   << (stats.dtime_jitter.max_fraction * 100.0) << " %"
		   << std::setprecision(1)
		   << ", v_range = " << draw_control->wanted_range
		   << std::setprecision(3)
		   << ", RTT = " << client->getRTT();
		guitext->setText(utf8_to_wide(os.str()).c_str());
		guitext->setVisible(true);
	} else if (flags.show_hud || flags.show_chat) {
		std::ostringstream os(std::ios_base::binary);
		os << std::setprecision(1) << std::fixed
		   << "X: " << (player_position.X / BS)
		   << ", Y: " << (player_position.Y / BS)
		   << ", Z: " << (player_position.Z / BS);
		guitext->setText(utf8_to_wide(os.str()).c_str());
		guitext->setVisible(true);
	} else {
		guitext->setVisible(false);
	}

	if (guitext->isVisible()) {
		core::rect<s32> rect(
				5 + m_round_screen, 5,
				screensize.X, 5 + g_fontengine->getTextHeight()
		);
		guitext->setRelativePosition(rect);
	}

	if (flags.show_debug) {
		std::ostringstream os(std::ios_base::binary);
		os << std::setprecision(1) << std::fixed
		   << "(" << (player_position.X / BS)
		   << ", " << (player_position.Y / BS)
		   << ", " << (player_position.Z / BS)
		   << ") (yaw=" << (wrapDegrees_0_360(cam.camera_yaw))
		   << " " << yawToDirectionString(cam.camera_yaw)
		   << ") (seed = " << ((u64)client->getMapSeed())
		   << ")";

		if (runData.pointed_old.type == POINTEDTHING_NODE) {
			ClientMap &map = client->getEnv().getClientMap();
			const INodeDefManager *nodedef = client->getNodeDefManager();
			MapNode n = map.getNodeNoEx(runData.pointed_old.node_undersurface);
			if (n.getContent() != CONTENT_IGNORE && nodedef->get(n).name != "unknown") {
				const ContentFeatures &features = nodedef->get(n);
				os << " (pointing_at = " << nodedef->get(n).name
				   << " - " << features.tiledef[0].name.c_str()
				   << ")";
			}
		}

		setStaticText(guitext2, utf8_to_wide(os.str()).c_str());
		guitext2->setVisible(true);

		core::rect<s32> rect(
				5,             5 + g_fontengine->getTextHeight(),
				screensize.X,  5 + g_fontengine->getTextHeight() * 2
		);
		guitext2->setRelativePosition(rect);
	} else {
		guitext2->setVisible(false);
	}

	setStaticText(guitext_info, infotext.c_str());
	guitext_info->setVisible(flags.show_hud && g_menumgr.menuCount() == 0);

	float statustext_time_max = 2.0f;

	if (!m_statustext.empty()) {
		runData.statustext_time += dtime;

		if (runData.statustext_time >= statustext_time_max) {
			m_statustext = L"";
			runData.statustext_time = 0;
		}
	}

	setStaticText(guitext_status, m_statustext.c_str());
	guitext_status->setVisible(!m_statustext.empty());

	if (!m_statustext.empty()) {
		s32 status_width  = guitext_status->getTextWidth();
		s32 status_height = guitext_status->getTextHeight();
#if defined(__ANDROID__) || defined(__IOS__)
		s32 status_y = screensize.Y / 1.25;
		if (m_hud_small)
			status_y = (screensize.Y) / 1.5;
#else
		s32 status_y = screensize.Y - 150 * m_hud_scaling;
#endif
		s32 status_x = (screensize.X - status_width) / 2;
		core::rect<s32> rect(
				status_x,  status_y - status_height,
				status_x + status_width, status_y
		);
		guitext_status->setRelativePosition(rect);

		// Fade out
		video::SColor initial_color(255, 0, 0, 0);

		if (guienv->getSkin())
			initial_color = guienv->getSkin()->getColor(gui::EGDC_BUTTON_TEXT);

		video::SColor final_color = initial_color;
		final_color.setAlpha(0);
		video::SColor fade_color = initial_color.getInterpolated_quadratic(
				initial_color, final_color,
				pow(runData.statustext_time / statustext_time_max, 2.0f));
		guitext_status->setOverrideColor(fade_color);
		guitext_status->enableOverrideColor(true);
	}
}


/* Log times and stuff for visualization */
inline void Game::updateProfilerGraphs(ProfilerGraph *graph)
{
	Profiler::GraphValues values;
	g_profiler->graphGet(values);
	graph->put(values);
}



/****************************************************************************
 Misc
 ****************************************************************************/

/* On some computers framerate doesn't seem to be automatically limited
 */
inline void Game::limitFps(FpsControl *fps_timings, f32 *dtime)
{
	// not using getRealTime is necessary for wine
	device->getTimer()->tick(); // Maker sure device time is up-to-date
	u32 time = device->getTimer()->getTime();
	u32 last_time = fps_timings->last_time;

	if (time > last_time)  // Make sure time hasn't overflowed
		fps_timings->busy_time = time - last_time;
	else
		fps_timings->busy_time = 0;

	u32 frametime_min = 1000 / (g_menumgr.pausesGame()
			? g_settings->getFloat("pause_fps_max")
			: g_settings->getFloat("fps_max"));
#if defined(__ANDROID__) || defined(__IOS__)
	if (g_menumgr.pausesGame() && !device->isWindowFocused())
		frametime_min = 1000;
#endif

	if (fps_timings->busy_time < frametime_min) {
		fps_timings->sleep_time = frametime_min - fps_timings->busy_time;
		device->sleep(fps_timings->sleep_time);
	} else {
		fps_timings->sleep_time = 0;
	}

	/* Get the new value of the device timer. Note that device->sleep() may
	 * not sleep for the entire requested time as sleep may be interrupted and
	 * therefore it is arguably more accurate to get the new time from the
	 * device rather than calculating it by adding sleep_time to time.
	 */

	device->getTimer()->tick(); // Update device timer
	time = device->getTimer()->getTime();

	if (time > last_time)  // Make sure last_time hasn't overflowed
		*dtime = (time - last_time) / 1000.0;
	else
		*dtime = 0;

	fps_timings->last_time = time;
}

// Note: This will free (using delete[])! \p msg. If you want to use it later,
// pass a copy of it to this function
// Note: \p msg must be allocated using new (not malloc())
void Game::showOverlayMessage(const wchar_t *msg, float dtime,
		int percent, bool draw_clouds)
{
	draw_load_screen(msg, device, guienv, texture_src, dtime, percent,
		draw_clouds);
	delete[] msg;
}

void Game::settingChangedCallback(const std::string &setting_name, void *data)
{
	((Game *)data)->readSettings();
}

void Game::readSettings()
{
	m_cache_doubletap_jump               = g_settings->getBool("doubletap_jump");
	m_cache_enable_clouds                = g_settings->getBool("enable_clouds");
	m_cache_enable_joysticks             = g_settings->getBool("enable_joysticks");
	m_cache_enable_particles             = g_settings->getBool("enable_particles");
	m_cache_enable_fog                   = g_settings->getBool("enable_fog");
	m_cache_mouse_sensitivity            = g_settings->getFloat("mouse_sensitivity");
	m_cache_joystick_frustum_sensitivity = g_settings->getFloat("joystick_frustum_sensitivity");
	m_repeat_right_click_time            = g_settings->getFloat("repeat_rightclick_time");

	m_cache_enable_noclip                = g_settings->getBool("noclip");
	m_cache_enable_free_move             = g_settings->getBool("free_move");

	m_cache_fog_start                    = g_settings->getFloat("fog_start");

	m_round_screen = g_settings->getU16("round_screen");
	m_hud_scaling = g_settings->getFloat("hud_scaling");
	m_hud_small = g_settings->getBool("hud_small");

	m_cache_cam_smoothing = 0;
	if (g_settings->getBool("cinematic"))
		m_cache_cam_smoothing = 1 - g_settings->getFloat("cinematic_camera_smoothing");
	else
		m_cache_cam_smoothing = 1 - g_settings->getFloat("camera_smoothing");

	m_cache_fog_start = rangelim(m_cache_fog_start, 0.0f, 0.99f);
	m_cache_cam_smoothing = rangelim(m_cache_cam_smoothing, 0.01f, 1.0f);
	m_cache_mouse_sensitivity = rangelim(m_cache_mouse_sensitivity, 0.001, 100.0);

}

#if defined(__ANDROID__) || defined(__IOS__)
void Game::pauseGame()
{
	if (g_menumgr.pausesGame() || !hud)
		return;
	g_touchscreengui->handleReleaseAll();
	showPauseMenu();
	runData.pause_game_timer = 0;
}
#endif

#ifdef __IOS__
void Game::customStatustext(const std::wstring &text, float time)
{
	m_statustext = text;
	if (m_statustext == L"")
		runData.statustext_time = 0;
	else
		runData.statustext_time = time;
}
#endif

void Game::pauseAnimation(bool is_paused)
{
	core::list<scene::ISceneNode *> nodes = (is_paused) ?
			smgr->getRootSceneNode()->getChildren() : m_anim_nodes;

	for (auto it : nodes) {
		if (it && it->getType() == scene::ESNT_ANIMATED_MESH) {
			auto *node = (scene::IAnimatedMeshSceneNode*)it;
			if (is_paused) {
				if (node->getLoopMode()) {
					m_anim_nodes.push_back(node);
					node->setLoopMode(false);
				}
			} else {
				node->setLoopMode(true);
			}
		}
	}
	if (!is_paused)
		m_anim_nodes.clear();
}

/****************************************************************************/
/****************************************************************************
 Shutdown / cleanup
 ****************************************************************************/
/****************************************************************************/

void Game::extendedResourceCleanup()
{
	// Extended resource accounting
	infostream << "Irrlicht resources after cleanup:" << std::endl;
	infostream << "\tRemaining meshes   : "
	           << device->getSceneManager()->getMeshCache()->getMeshCount() << std::endl;
	infostream << "\tRemaining textures : "
	           << driver->getTextureCount() << std::endl;

	for (unsigned int i = 0; i < driver->getTextureCount(); i++) {
		irr::video::ITexture *texture = driver->getTextureByIndex(i);
		infostream << "\t\t" << i << ":" << texture->getName().getPath().c_str()
		           << std::endl;
	}

	clearTextureNameCache();
	infostream << "\tRemaining materials: "
               << driver-> getMaterialRendererCount()
		       << " (note: irrlicht doesn't support removing renderers)" << std::endl;
}

#define GET_KEY_NAME(KEY) gettext(getKeySetting(#KEY).name())
void Game::showPauseMenu()
{
#ifdef __ANDROID__
	static const std::string control_text = strgettext("Default Controls:\n"
		"No menu visible:\n"
		"- single tap: button activate\n"
		"- double tap: place/use\n"
		"- slide finger: look around\n"
		"Menu/Inventory visible:\n"
		"- double tap (outside):\n"
		" -->close\n"
		"- touch stack, touch slot:\n"
		" --> move stack\n"
		"- touch&drag, tap 2nd finger\n"
		" --> place single item to slot\n"
		);
#else
	static const std::string control_text_template = strgettext("Controls:\n"
		"- %s: move forwards\n"
		"- %s: move backwards\n"
		"- %s: move left\n"
		"- %s: move right\n"
		"- %s: jump/climb\n"
		"- %s: sneak/go down\n"
		"- %s: drop item\n"
		"- %s: inventory\n"
		"- Mouse: turn/look\n"
		"- Mouse left: dig/punch\n"
		"- Mouse right: place/use\n"
		"- Mouse wheel: select item\n"
		"- %s: chat\n"
	);

	 char control_text_buf[600];

	 snprintf(control_text_buf, ARRLEN(control_text_buf), control_text_template.c_str(),
			GET_KEY_NAME(keymap_forward),
			GET_KEY_NAME(keymap_backward),
			GET_KEY_NAME(keymap_left),
			GET_KEY_NAME(keymap_right),
			GET_KEY_NAME(keymap_jump),
			GET_KEY_NAME(keymap_sneak),
			GET_KEY_NAME(keymap_drop),
			GET_KEY_NAME(keymap_inventory),
			GET_KEY_NAME(keymap_chat)
			);

	std::string control_text = std::string(control_text_buf);
	str_formspec_escape(control_text);
#endif

#ifdef __IOS__
	float ypos = 1.5f;
#else
	float ypos = simple_singleplayer_mode ? 0.5f : 0.1f;
#endif
	std::ostringstream os;

	os << FORMSPEC_VERSION_STRING  << SIZE_TAG
		<< "bgcolor[#00000060;true]"
		<< "button_exit[3.5," << (ypos++) << ";4,0.5;btn_continue;"
		<< strgettext("Continue") << "]";

#if !defined(__ANDROID__) && !defined(__IOS__)
	if (!simple_singleplayer_mode) {
		os << "button_exit[3.5," << (ypos++) << ";4,0.5;btn_change_password;"
			<< strgettext("Change Password") << "]";
	}
	os		<< "button_exit[3.5," << (ypos++) << ";4,0.5;btn_sound;"
		<< strgettext("Sound Volume") << "]";
	os		<< "button_exit[3.5," << (ypos++) << ";4,0.5;btn_key_config;"
		<< strgettext("Change Keys")  << "]";
#endif
	os		<< "button_exit[3.5," << (ypos++) << ";4,0.5;btn_exit_menu;"
		<< strgettext("Save and Exit") << "]";
#ifndef __IOS__
	os		<< "button_exit[3.5," << (ypos++) << ";4,0.5;btn_exit_os;"
		<< strgettext("Close game")   << "]";
#endif

/*		<< "textarea[7.5,0.25;3.9,6.25;;" << control_text << ";]
		<< "textarea[0.4,0.25;3.9,6.25;;" << PROJECT_NAME_C " " VERSION_STRING "\n"
		<< "\n"
		<<  strgettext("Game info:") << "\n";
	const std::string &address = client->getAddressName();
	static const std::string mode = strgettext("- Mode: ");
	if (!simple_singleplayer_mode) {
		Address serverAddress = client->getServerAddress();
		if (address != "") {
			os << mode << strgettext("Remote server") << "\n"
					<< strgettext("- Address: ") << address;
		} else {
			os << mode << strgettext("Hosting server");
		}
		os << "\n" << strgettext("- Port: ") << serverAddress.getPort() << "\n";
	} else {
		os << mode << strgettext("Singleplayer") << "\n";
	}
	if (simple_singleplayer_mode || address == "") {
		static const std::string on = strgettext("On");
		static const std::string off = strgettext("Off");
		const std::string &damage = g_settings->getBool("enable_damage") ? on : off;
		const std::string &creative = g_settings->getBool("creative_mode") ? on : off;
		const std::string &announced = g_settings->getBool("server_announce") ? on : off;
		os << strgettext("- Damage: ") << damage << "\n"
				<< strgettext("- Creative Mode: ") << creative << "\n";
		if (!simple_singleplayer_mode) {
			const std::string &pvp = g_settings->getBool("enable_pvp") ? on : off;
			os << strgettext("- PvP: ") << pvp << "\n"
					<< strgettext("- Public: ") << announced << "\n";
			std::string server_name = g_settings->get("server_name");
			str_formspec_escape(server_name);
			if (announced == on && server_name != "")
				os << strgettext("- Server Name: ") << server_name;

		}
	}
	os << ";]";*/

	/* Create menu */
	/* Note: FormspecFormSource and LocalFormspecHandler  *
	 * are deleted by guiFormSpecMenu                     */
	FormspecFormSource *fs_src = new FormspecFormSource(os.str());
	LocalFormspecHandler *txt_dst = new LocalFormspecHandler("MT_PAUSE_MENU");

	create_formspec_menu(&current_formspec, client, device, &input->joystick, fs_src, txt_dst);
	current_formspec->setFocus("btn_continue");
	current_formspec->doPause = true;

	if (simple_singleplayer_mode)
		pauseAnimation(true);

	runData.pause_game_timer = 0;
}

#ifdef DISABLE_CSM
void Game::showDeathScreen()
{
	std::string formspec =
		std::string(FORMSPEC_VERSION_STRING) +
		SIZE_TAG
		"bgcolor[#320000b4;true]"
		"label[4.4,1.35;" + gettext("You died.") + "]"
		"button_exit[3.5,3;4,0.5;btn_respawn;" + gettext("Respawn") + "]"
		;

	/* Create menu */
	/* Note: FormspecFormSource and LocalFormspecHandler
	 * are deleted by guiFormSpecMenu                     */
	FormspecFormSource *fs_src = new FormspecFormSource(formspec);
	LocalFormspecHandler *txt_dst = new LocalFormspecHandler("MT_DEATH_SCREEN", client);

	create_formspec_menu(&current_formspec, client, device, &input->joystick, fs_src, txt_dst);
}
#endif

/****************************************************************************/
/****************************************************************************
 extern function for launching the game
 ****************************************************************************/
/****************************************************************************/

static Game *g_game = NULL;

void the_game(bool *kill,
		bool random_input,
		InputHandler *input,
		IrrlichtDevice *device,

		const std::string &map_dir,
		const std::string &playername,
		const std::string &password,
		const std::string &address,         // If empty local server is created
		u16 port,

		std::string &error_message,
		ChatBackend &chat_backend,
		bool *reconnect_requested,
		const SubgameSpec &gamespec,        // Used for local game
		bool simple_singleplayer_mode)
{
	Game game;
	g_game = &game;

	/* Make a copy of the server address because if a local singleplayer server
	 * is created then this is updated and we don't want to change the value
	 * passed to us by the calling function
	 */
	std::string server_address = address;

	try {

		if (game.startup(kill, random_input, input, device, map_dir,
				playername, password, &server_address, port, error_message,
				reconnect_requested, &chat_backend, gamespec,
				simple_singleplayer_mode)) {
			game.run();
		}

	} catch (SerializationError &e) {
		error_message = std::string("A serialization error occurred:\n")
				+ e.what() + "\n\nThe server is probably "
				" running a different version of " PROJECT_NAME_C ".";
		errorstream << error_message << std::endl;
	} catch (ServerError &e) {
		error_message = e.what();
		errorstream << "ServerError: " << error_message << std::endl;
	} catch (ModError &e) {
		error_message = e.what() + strgettext("\nCheck debug.txt for details.");
		errorstream << "ModError: " << error_message << std::endl;
	}
	game.shutdown();
	g_game = NULL;
}

#if defined(__ANDROID__) || defined(__IOS__)
void external_pause_game()
{
	if (!g_game)
		return;
	g_game->pauseGame();
}
#endif

#ifdef __IOS__
void external_statustext(const char *text, float duration)
{
	if (!g_game)
		return;
	std::wstring s = narrow_to_wide(std::string(text));
	g_game->customStatustext(s, duration);
}
#endif
