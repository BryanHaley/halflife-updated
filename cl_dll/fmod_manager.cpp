#include "cl_dll.h"
#include "cl_util.h"
#include "extdll.h"
#include "hud.h"
#include "parsemsg.h"
#include "fmod_manager.h"
#include "FMOD/fmod_errors.h"

#include <fstream>
#include <iostream>

FMOD::System *fmod_system;
Fmod_Group fmod_mp3_group;
Fmod_Group fmod_sfx_group;

//Fmod_Sound fmod_main_menu_music;

std::unordered_map<std::string, FMOD::Sound*> fmod_cached_sounds;
std::unordered_map<std::string, FMOD::Channel*> fmod_channels;

bool Fmod_Init(void)
{
    FMOD_RESULT result;
    //fmod_main_menu_music = NULL;

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

    return true;
}

void Fmod_Think(void)
{
    FMOD_RESULT result;

    // Update FMOD
    result = fmod_system->update();
    _Fmod_Result_OK(&result);

    _Fmod_Update_Volume();
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

FMOD::Sound* Fmod_CacheSound(const char* path, bool is_track)
{
    FMOD_RESULT result;
    FMOD::Sound *sound = NULL;

    std::string gamedir = gEngfuncs.pfnGetGameDirectory();
	std::string full_path = gamedir + "/" + path; 

    // Create the sound/stream from the file on disk
    if (!is_track)
        result = fmod_system->createSound(full_path.c_str(), FMOD_3D, NULL, &sound);
	else
		result = fmod_system->createStream(full_path.c_str(), FMOD_DEFAULT, NULL, &sound);

    // TODO: investigate if a failure here might create a memory leak
	if (!_Fmod_Result_OK(&result)) return NULL;

    // If all went okay, insert the sound into the cache
	fmod_cached_sounds.insert(std::pair(path, sound));

    return sound;
}

FMOD::Channel* Fmod_CreateChannel(FMOD::Sound* sound, const char* name, Fmod_Group group, bool loop, float volume)
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
		channel->setMode(FMOD_LOOP_NORMAL);
	}

    // If all went okay, stick it in the channel list. Only looping sounds store a reference.
	fmod_channels.insert(std::pair(name, channel));

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
void _Fmod_Report(std::string report_type, std::string info)
{
	std::string msg = "FMOD " + report_type +": " + info + "\n";
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