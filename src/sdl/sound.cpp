/***********************************************************************
 *
 * Copyright (C) 2010, 2011, 2012 Graeme Gott <graeme@gottcode.org>
 *
 * SDL and SDL_mixer
 *  Copyright (C) 1997-2009 Sam Lantinga
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "../sound.h"

#include <QFile>
#include <QHash>
#include <QLibrary>
#include <QVector>

//-----------------------------------------------------------------------------

#ifndef SDLCALL
# if defined(__WIN32__) && !defined(__GNUC__)
#  define SDLCALL __cdecl
# elif defined(__OS2__)
#  if defined (__GNUC__) && __GNUC__ < 4
	/* Added support for GCC-EMX <v4.x */
	/* this is needed for XFree86/OS2 developement */
	/* F. Ambacher(anakor@snafu.de) 05.2008 */
#   define SDLCALL _cdecl
#  else
	/* On other compilers on OS/2, we use the _System calling convention */
	/* to be compatible with every compiler */
#   define SDLCALL _System
#  endif
# else
#  define SDLCALL
# endif
#endif

#ifndef SDLPTR
#define SDLPTR SDLCALL *
#endif

namespace
{
	// SDL functions
	const quint32 SDL_INIT_AUDIO = 0x00000010;

	typedef int (SDLPTR func_SDL_Init)(quint32 flags);
	func_SDL_Init sdl_Init;

	typedef void (SDLPTR func_SDL_Quit)(void);
	func_SDL_Quit sdl_Quit;

	typedef char* (SDLPTR func_SDL_GetError)(void);
	func_SDL_GetError sdl_GetError;
	inline char* SDLCALL mix_GetError() { return sdl_GetError(); }

	struct SDLRWops;
	typedef SDLRWops* (SDLPTR func_SDL_RWFromFile)(const char* file, const char* mode);
	func_SDL_RWFromFile sdl_RWFromFile;


	// SDL_mixer functions
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
	const quint16 MIX_DEFAULT_FORMAT = 0x8010;
#else
	const quint16 MIX_DEFAULT_FORMAT = 0x9010;
#endif

	typedef int (SDLPTR func_Mix_OpenAudio)(int frequency, quint16 format, int channels, int chunksize);
	func_Mix_OpenAudio mix_OpenAudio;

	typedef void (SDLPTR func_Mix_CloseAudio)(void);
	func_Mix_CloseAudio mix_CloseAudio;

	struct MixChunk;
	typedef MixChunk* (SDLPTR func_Mix_LoadWAV_RW)(SDLRWops* src, int freesrc);
	func_Mix_LoadWAV_RW mix_LoadWAV_RW;
	inline MixChunk* SDLCALL mix_LoadWAV(const char* file) { return mix_LoadWAV_RW(sdl_RWFromFile(file, "rb"), 1); }

	typedef void (SDLPTR func_Mix_FreeChunk)(MixChunk* chunk);
	func_Mix_FreeChunk mix_FreeChunk;

	typedef int (SDLPTR func_Mix_PlayChannelTimed)(int channel, MixChunk* chunk, int loops, int ticks);
	func_Mix_PlayChannelTimed mix_PlayChannelTimed;
	inline int SDLCALL mix_PlayChannel(int channel, MixChunk* chunk, int loops) { return mix_PlayChannelTimed(channel, chunk, loops, -1); }


	// Shared data
	QString f_path;
	bool f_enabled = false;
	bool f_sdl_loaded = false;

	QVector<MixChunk*> f_chunks;
	int f_total_sounds = 0;
	QHash<QString, int> f_ids;
	QHash<int, Sound*> f_sound_objects;

	void loadSDL()
	{
		// Initialize SDL
#ifndef Q_OS_WIN
		QLibrary mixer_lib("SDL2_mixer");
		if (!mixer_lib.load()) {
			mixer_lib.setFileName("SDL_mixer");
		}
		if (!mixer_lib.load()) {
			mixer_lib.setFileNameAndVersion("SDL2_mixer-2.0", "0");
		}
		if (!mixer_lib.load()) {
			mixer_lib.setFileNameAndVersion("SDL_mixer-1.2", "0");
		}
#else
		QLibrary mixer_lib("SDL");
#endif
		sdl_Init = (func_SDL_Init) mixer_lib.resolve("SDL_Init");
		sdl_Quit = (func_SDL_Quit) mixer_lib.resolve("SDL_Quit");
		sdl_GetError = (func_SDL_GetError) mixer_lib.resolve("SDL_GetError");
		sdl_RWFromFile = (func_SDL_RWFromFile) mixer_lib.resolve("SDL_RWFromFile");
		if ((sdl_Init == 0) || (sdl_Quit == 0) || (sdl_GetError == 0) || (sdl_RWFromFile == 0)) {
			qWarning("Unable to load SDL");
			return;
		}
		if (sdl_Init(SDL_INIT_AUDIO) != 0) {
			qWarning("Unable to initialize SDL: %s", sdl_GetError());
			return;
		}

		// Initialize SDL_mixer
#ifdef Q_OS_WIN
		mixer_lib.setFileName("SDL_mixer");
#endif
		mix_OpenAudio = (func_Mix_OpenAudio) mixer_lib.resolve("Mix_OpenAudio");
		mix_CloseAudio = (func_Mix_CloseAudio) mixer_lib.resolve("Mix_CloseAudio");
		mix_LoadWAV_RW = (func_Mix_LoadWAV_RW) mixer_lib.resolve("Mix_LoadWAV_RW");
		mix_FreeChunk = (func_Mix_FreeChunk) mixer_lib.resolve("Mix_FreeChunk");
		mix_PlayChannelTimed = (func_Mix_PlayChannelTimed) mixer_lib.resolve("Mix_PlayChannelTimed");
		if ((mix_OpenAudio == 0) || (mix_CloseAudio == 0) || (mix_LoadWAV_RW == 0) || (mix_FreeChunk == 0) || (mix_PlayChannelTimed == 0)) {
			qWarning("Unable to load SDL_mixer");
			sdl_Quit();
			return;
		}
		if (mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
			qWarning("Unable to initialize SDL_mixer: %s", mix_GetError());
			sdl_Quit();
			return;
		}

		f_sdl_loaded = true;
	}

	void unloadSDL()
	{
		// Free audio chunks
		int count = f_chunks.count();
		for (int i = 0; i < count; ++i) {
			mix_FreeChunk(f_chunks.at(i));
		}
		f_chunks.clear();

		// Disable SDL
		mix_CloseAudio();
		sdl_Quit();

		f_sdl_loaded = false;
	}
}

//-----------------------------------------------------------------------------

Sound::Sound(int name, const QString& filename, QObject* parent) :
	QObject(parent),
	m_id(-1),
	m_name(name)
{
	if (!f_sdl_loaded) {
		loadSDL();
	}
	f_total_sounds++;

	if (f_ids.contains(filename)) {
		m_id = f_ids.value(filename);
	} else if (f_sdl_loaded) {
		MixChunk* chunk = mix_LoadWAV(QFile::encodeName(f_path + "/" + filename).constData());
		if (chunk == 0) {
			qWarning("Unable to load WAV file: %s", mix_GetError());
			return;
		}

		m_id = f_chunks.count();
		f_chunks.append(chunk);
		f_ids[filename] = m_id;
	}

	f_sound_objects[m_name] = this;
}

//-----------------------------------------------------------------------------

Sound::~Sound()
{
	f_sound_objects[m_name] = 0;
	f_total_sounds--;
	if (f_total_sounds == 0) {
		if (f_sdl_loaded) {
			unloadSDL();
		}
		f_ids.clear();
	}
}

//-----------------------------------------------------------------------------

void Sound::play(int name)
{
	if (f_enabled) {
		Sound* sound = f_sound_objects.value(name);
		if (sound && sound->isValid() && (mix_PlayChannel(-1, f_chunks.at(sound->m_id), 0) == -1)) {
			qWarning("Unable to play WAV file: %s", mix_GetError());
		}
	}
}

//-----------------------------------------------------------------------------

void Sound::setEnabled(bool enabled)
{
	f_enabled = enabled;
}

//-----------------------------------------------------------------------------

void Sound::setPath(const QString& path)
{
	f_path = path;
}

//-----------------------------------------------------------------------------
