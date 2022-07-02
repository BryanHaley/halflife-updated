#include "cl_dll.h"
#include "cl_util.h"
#include "extdll.h"
#include "hud.h"
#include "parsemsg.h"
#include "fmod_api.h"
#include "FMOD/fmod_errors.h"

#include <fstream>
#include <iostream>

namespace HLFMOD
{
	FMOD::System *fmod_system;
	Fmod_Group fmod_mp3_group;
	Fmod_Group fmod_sfx_group;

	//Fmod_Sound fmod_main_menu_music;

	std::unordered_map<std::string, FMOD::Sound*> fmod_cached_sounds;
	std::unordered_map<std::string, FMOD::Channel*> fmod_channels;
	std::unordered_map<std::string, FMOD::Sound*> fmod_tracks;

	std::vector<FMOD::Reverb3D*> fmod_reverb_spheres;

	FMOD::Channel* fmod_current_track;

	const float DEFAULT_MIN_ATTEN = 40;
	const float DEFAULT_MAX_ATTEN = 40000;

	FMOD_REVERB_PROPERTIES fmod_reverb_properties[] = {
		FMOD_PRESET_OFF,				//  0
		FMOD_PRESET_GENERIC,			//  1
		FMOD_PRESET_PADDEDCELL,			//  2
		FMOD_PRESET_ROOM,				//  3
		FMOD_PRESET_BATHROOM,			//  4
		FMOD_PRESET_LIVINGROOM,			//  5
		FMOD_PRESET_STONEROOM,			//  6
		FMOD_PRESET_AUDITORIUM,			//  7
		FMOD_PRESET_CONCERTHALL,		//  8
		FMOD_PRESET_CAVE,				//  9
		FMOD_PRESET_ARENA,				// 10
		FMOD_PRESET_HANGAR,				// 11
		FMOD_PRESET_CARPETTEDHALLWAY,	// 12
		FMOD_PRESET_HALLWAY,			// 13
		FMOD_PRESET_STONECORRIDOR,		// 14
		FMOD_PRESET_ALLEY,				// 15
		FMOD_PRESET_FOREST,				// 16
		FMOD_PRESET_CITY,				// 17
		FMOD_PRESET_MOUNTAINS,			// 18
		FMOD_PRESET_QUARRY,				// 19
		FMOD_PRESET_PLAIN,				// 20
		FMOD_PRESET_PARKINGLOT,			// 21
		FMOD_PRESET_SEWERPIPE,			// 22
		FMOD_PRESET_UNDERWATER			// 23
	};

	bool Fmod_Init(void)
	{
		FMOD_RESULT result;
		//fmod_main_menu_music = NULL;
		fmod_current_track = NULL;

		// Create the main system object.
		result = FMOD::System_Create(&fmod_system);
		if (!_Fmod_Result_OK(&result)) return false;

		// Initialize FMOD.
		result = fmod_system->init(512, FMOD_INIT_NORMAL, 0);
		if (!_Fmod_Result_OK(&result)) return false;

		// Create mp3 channel group
		result = fmod_system->createChannelGroup("MP3", &fmod_mp3_group);
		if (!_Fmod_Result_OK(&result)) return false;
		fmod_mp3_group->setVolume(1.0f);

		// Create sfx channel group
		result = fmod_system->createChannelGroup("SFX", &fmod_sfx_group);
		if (!_Fmod_Result_OK(&result)) return false;
		fmod_sfx_group->setVolume(1.0f);

		_Fmod_Update_Volume();

		// TODO: Allow customizing doppler and rolloff scale in a cfg
		fmod_system->set3DSettings(1.0f, 40, 1.0f);

		_Fmod_LoadTracks();

		return true;
	}

	void Fmod_Update(void)
	{
		FMOD_RESULT result;

		// Update FMOD
		result = fmod_system->update();
		_Fmod_Result_OK(&result);

		_Fmod_Update_Volume();

		// Get the forward and up vector
		Vector playerForward, playerUp;
		gEngfuncs.pfnAngleVectors(gHUD.playerAngles, playerForward, nullptr, playerUp);

		// Converting these Vectors into FMOD vectors
		FMOD_VECTOR FMODPlayerPosition	= _Fmod_HLVecToFmodVec(gHUD.playerOrigin);
		FMOD_VECTOR FMODPlayerVelocity	= _Fmod_HLVecToFmodVec(gHUD.playerSpeed);
		FMOD_VECTOR FMODPlayerForward	= _Fmod_HLVecToFmodVec(playerForward);
		FMOD_VECTOR FMODPlayerUp		= _Fmod_HLVecToFmodVec(playerUp);

		// update position
		Fmod_Update_Listener_Position(&FMODPlayerPosition, &FMODPlayerVelocity, &FMODPlayerForward, &FMODPlayerUp);
	}

	void Fmod_Think(struct ref_params_s* pparams)
	{
		if (pparams->paused)
		{
			fmod_sfx_group->setPaused(true);
			fmod_mp3_group->setPaused(true);
		}
		else
		{
			fmod_sfx_group->setPaused(false);
			fmod_mp3_group->setPaused(false);
		}
	}

	void _Fmod_LoadTracks(void)
	{
		// TODO: This and CHudFmodPlayer::MsgFunc_FmodCache are almost identical. Maybe consolidate into a single function.
		std::string gamedir = gEngfuncs.pfnGetGameDirectory();
		std::string tracks_txt_path = gamedir + "/" + "tracks.txt";

		std::ifstream tracks_txt_file;
		tracks_txt_file.open(tracks_txt_path);

		if (!tracks_txt_file.is_open())
		{
			_Fmod_Report("WARNING", "Could not open soundcache file " + tracks_txt_path + ". No sounds were precached!");
			return;
		}
		else _Fmod_Report("INFO", "Precaching tracks from file: " + tracks_txt_path);

		std::string filename;
		while (std::getline(tracks_txt_file, filename))
		{
			FMOD::Sound* sound = Fmod_CacheSound(filename.c_str(), true);
			if (!sound)
			{
				_Fmod_Report("ERROR", "Error occured during precaching tracks. Tried precaching: " + filename + ". Precaching stopped.");
				return;
			}
		}

		if (!tracks_txt_file.eof())
			_Fmod_Report("WARNING", "Stopped reading soundcache file " + tracks_txt_path + " before the end of file due to error.");

		tracks_txt_file.close();
	}

	void Fmod_Update_Listener_Position(FMOD_VECTOR *pos, FMOD_VECTOR *vel, FMOD_VECTOR *forward, FMOD_VECTOR *up)
	{
		FMOD_RESULT result;

		result = fmod_system->set3DListenerAttributes(0, pos, vel, forward, up);
		_Fmod_Result_OK(&result);
	}

	void Fmod_Release_Sounds(void)
	{
		auto sounds_it = fmod_cached_sounds.begin();

		while (sounds_it != fmod_cached_sounds.end())
		{
			sounds_it->second->release();
			sounds_it++;
		}

		fmod_cached_sounds.clear();
	}

	void Fmod_Release_Channels(void)
	{
		auto channels_it = fmod_channels.begin();

		while (channels_it != fmod_channels.end())
		{
			channels_it->second->stop();
			channels_it++;
		}

		fmod_channels.clear();
	}

	FMOD::Sound* Fmod_CacheSound(const char* path, const bool is_track)
	{
		return Fmod_CacheSound(path, is_track, false);
	}

	FMOD::Sound* Fmod_CacheSound(const char* path, const bool is_track, const bool play_everywhere)
	{
		FMOD_RESULT result;
		FMOD::Sound *sound = NULL;

		std::string gamedir = gEngfuncs.pfnGetGameDirectory();
		std::string full_path = gamedir + "/" + path; 

		// Create the sound/stream from the file on disk
		if (is_track)
		{
			result = fmod_system->createStream(full_path.c_str(), FMOD_2D, NULL, &sound);
			if (!_Fmod_Result_OK(&result)) return NULL; // TODO: investigate if a failure here might create a memory leak

			// If all went okay, insert the track into the cache
			fmod_tracks.insert(std::pair(path, sound));
		}
		else
		{
			if (play_everywhere)
				result = fmod_system->createSound(full_path.c_str(), FMOD_2D, NULL, &sound);
			else
				result = fmod_system->createSound(full_path.c_str(), FMOD_3D, NULL, &sound);

			if (!_Fmod_Result_OK(&result)) return NULL; // TODO: investigate if a failure here might create a memory leak

			// If all went okay, insert the sound into the cache
			fmod_cached_sounds.insert(std::pair(path, sound));
		}

		return sound;
	}

	FMOD::Reverb3D* Fmod_CreateReverbSphere(const FMOD_REVERB_PROPERTIES* properties, const FMOD_VECTOR* pos, const float min_distance, const float max_distance)
	{
		FMOD_RESULT result;
		FMOD::Reverb3D* reverb_sphere = NULL;

		result = fmod_system->createReverb3D(&reverb_sphere);
		if (!_Fmod_Result_OK(&result)) return NULL; // TODO: investigate if a failure here might create a memory leak

		reverb_sphere->setProperties(properties);
		reverb_sphere->set3DAttributes(pos, min_distance, max_distance);

		fmod_reverb_spheres.push_back(reverb_sphere);

		return reverb_sphere;
	}

	FMOD::Channel* Fmod_CreateChannel(FMOD::Sound* sound, const char* name, const Fmod_Group &group, const bool loop, const float volume)
	{
		FMOD_RESULT result;
		FMOD::Channel* channel = NULL;

		// Create the sound channel
		// We "play" the sound to create the channel, but it's starting paused so we can set properties on it before it /actually/ plays.
		result = fmod_system->playSound(sound, group, true, &channel);
		if (!_Fmod_Result_OK(&result)) return NULL; // TODO: investigate if a failure here might create a memory leak

		// Set channel properties
		channel->setVolume(volume);
		if (loop)
		{
			// TODO: See why this doesn't work as expected
			/* FMOD_MODE mode;
			channel->getMode(&mode);
			channel->setMode(mode | FMOD_LOOP_NORMAL);*/

			channel->setMode(FMOD_LOOP_NORMAL);
		}

		// If all went okay, stick it in the channel list. Only looping sounds store a reference.
		fmod_channels.insert(std::pair(name, channel));

		return channel;
	}

	FMOD::Sound* Fmod_GetCachedSound(const char* sound_path)
	{
		FMOD::Sound* sound = nullptr;

		auto sound_iter = fmod_cached_sounds.find(sound_path);

		if (sound_iter == fmod_cached_sounds.end())
		{
			_Fmod_Report("WARNING", "Trying to play uncached sound " + std::string(sound_path) +
										". Add the sound to your [MAPNAME].bsp_soundcache.txt file.");
			_Fmod_Report("INFO", "Attempting to cache and play sound " + std::string(sound_path));
			sound = Fmod_CacheSound(sound_path, false);
		}
		else
			sound = sound_iter->second;

		return sound;
	}

	FMOD::Channel* Fmod_EmitSound(const char* sound_path, float volume)
	{
		FMOD::Sound* sound = Fmod_GetCachedSound(sound_path);

		Vector dummyVec;
		return Fmod_EmitSound(sound, "NONAME", false, volume, dummyVec, DEFAULT_MIN_ATTEN, DEFAULT_MAX_ATTEN, 1.0f);
	}
	FMOD::Channel* Fmod_EmitSound(FMOD::Sound* sound, float volume)
	{
		Vector dummyVec;
		return Fmod_EmitSound(sound, "NONAME", false, volume, dummyVec, DEFAULT_MIN_ATTEN, DEFAULT_MAX_ATTEN, 1.0f);
	}

	FMOD::Channel* Fmod_EmitSound(FMOD::Sound* sound, const char* channel_name, float volume, const Vector& pos)
	{
		return Fmod_EmitSound(sound, channel_name, false, volume, pos, DEFAULT_MIN_ATTEN, DEFAULT_MAX_ATTEN, 1.0f);
	}

	FMOD::Channel* Fmod_EmitSound(FMOD::Sound* sound, const char* channel_name, float volume, bool looping, const Vector& pos, float min_atten, float max_atten, float pitch)
	{
		FMOD_VECTOR fmod_pos = _Fmod_HLVecToFmodVec(pos);
		FMOD_VECTOR vel;
		vel.x = 0;
		vel.y = 0;
		vel.z = 0;

		FMOD::Channel* channel = NULL;

		// Always create a new channel for EmitSound
		channel = Fmod_CreateChannel(sound, channel_name, fmod_sfx_group, looping, volume);
		if (!channel) return nullptr; // TODO: Report warning about failure to play here

		channel->set3DAttributes(&fmod_pos, &vel);
		channel->set3DMinMaxDistance(min_atten, max_atten);
		channel->setPitch(pitch);
		channel->setPaused(false);

		return channel;
	}

	/* void Fmod_PlayMainMenuMusic(void)
	{
		std::string gamedir = gEngfuncs.pfnGetGameDirectory();
		std::string cfg_path = gamedir + FMOD_PATH_SEP + "menu_music.cfg";

		std::ifstream cfg_file;
		cfg_file.open(cfg_path);

		if (cfg_file.fail())
		{
			std::string error_msg = "FMOD ERROR: Could not open menu_music.cfg\n";
			fprintf(stderr, error_msg.c_str());
			ConsolePrint(error_msg.c_str());
			return;
		}

		std::string music_file_path = "";
		cfg_file >> music_file_path;

		if (music_file_path.compare("") != 0)
		{
			// Get volume from cfg file
			std::string volume_str = "";
			float volume = 1.0f;
			cfg_file >> volume_str;

			if (volume_str.compare("") != 0)
				volume = std::stof(volume_str);
			else
				volume = 1.0f;
        
			fmod_main_menu_music = Fmod_LoadSound(music_file_path.c_str());
			Fmod_PlaySound(fmod_main_menu_music, fmod_mp3_group, true, volume);
		}

		cfg_file.close();
	}*/

	void Fmod_Shutdown(void)
	{
		FMOD_RESULT result;

		result = fmod_system->release();
		_Fmod_Result_OK(&result);
	}

	void _Fmod_Update_Volume(void)
	{
		// Check if user has changed volumes and update accordingly
		// TODO: See if this can be done in an event-based way instead
		float new_mp3_vol = CVAR_GET_FLOAT("MP3Volume"); // TODO: Get volume slider value somehow
		float old_mp3_vol = 1.0f;
		fmod_mp3_group->getVolume(&old_mp3_vol);
		if (new_mp3_vol != old_mp3_vol)
			fmod_mp3_group->setVolume(new_mp3_vol);

		float new_sfx_vol = CVAR_GET_FLOAT("volume"); // TODO: Get volume slider value somehow
		float old_sfx_vol = 1.0f;
		fmod_sfx_group->getVolume(&old_sfx_vol);
		if (new_sfx_vol != old_sfx_vol)
			fmod_sfx_group->setVolume(new_sfx_vol);
	}

	// Check result and return true if OK or false if not OK
	// Sometimes return value is ignored and this is used for throwing warnings only
	bool _Fmod_Result_OK (FMOD_RESULT *result)
	{
		if (*result != FMOD_OK)
		{
			std::string fmod_error_str = FMOD_ErrorString(*result);
			std::string error_msg = "FMOD ERROR: " + *result + std::string(": ") + fmod_error_str + "\n";
			fprintf(stderr, error_msg.c_str());
			ConsolePrint(error_msg.c_str());
			return false;
		}

		return true;
	}

	// Shortcut to send message to both stderr and console
	void _Fmod_Report(const std::string &report_type, const std::string &info)
	{
		std::string msg = "FMOD " + report_type + ": " + info + "\n";
		fprintf(stderr, msg.c_str());
		ConsolePrint(msg.c_str());
	}

	// Convert HL's coordinate system to Fmod
	FMOD_VECTOR _Fmod_HLVecToFmodVec(const Vector &vec)
	{
		FMOD_VECTOR FMODVector;
		FMODVector.z = vec.x;
		FMODVector.x = -vec.y;
		FMODVector.y = vec.z;

		return FMODVector;
	}
}