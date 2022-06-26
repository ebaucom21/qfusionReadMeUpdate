/*
Copyright (C) 2002-2003 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef SOUND_PUBLIC_H
#define SOUND_PUBLIC_H

// snd_public.h -- sound dll information visible to engine

#define ATTN_NONE 0

//===============================================================

#include "../qcommon/wswstdtypes.h"

struct sfx_s;
struct client_state_s;

class SoundSystem {
	static SoundSystem *instance;

	client_state_s *const client;

#ifdef WIN32
	/**
	 * It's a good idea to limit the access to the {@code InstanceOrNull()}
	 * method only to this function to prevent spreading of this hack over the codebase
	 */
	friend void AppActivate( int, int, int );

	/**
	 * A hack that makes the instance state accessible for the Win32-specific code
	 */
	static SoundSystem *InstanceOrNull() { return instance; }
#endif
protected:
	[[nodiscard]]
	static auto getPathForName( const char *name, wsw::String *reuse ) -> const char *;

	explicit SoundSystem( client_state_s *client_ ) : client( client_ ) {}
public:
	struct InitOptions {
		bool verbose { false };
		bool useNullSystem { false };
	};

	static bool Init( client_state_s *client, void *hWnd, const InitOptions &options );
	static void Shutdown( bool verbose );

	static SoundSystem *Instance() {
		assert( instance );
		return instance;
	}

	client_state_s *GetClient() { return client; }
	const client_state_s *GetClient() const { return client; }

	virtual ~SoundSystem() = default;

	virtual void DeleteSelf( bool verbose ) = 0;

	/**
	 * @todo this is just to break a circular dependency. Refactor global objects into SoundSystem member fields.
	 */
	virtual void PostInit() = 0;

	virtual void BeginRegistration() = 0;
	virtual void EndRegistration() = 0;

	enum StopFlags : unsigned {
		StopAndClear = 0x1,
		StopMusic = 0x2
	};

	virtual void StopAllSounds( unsigned flags = 0 ) = 0;

	virtual void Clear() = 0;
	virtual void Update( const float *origin, const float *velocity, const mat3_t axis ) = 0;
	virtual void Activate( bool isActive ) = 0;

	virtual void SetEntitySpatialization( int entNum, const float *origin, const float *velocity ) = 0;

	virtual sfx_s *RegisterSound( const char *name ) = 0;
	virtual void StartFixedSound( sfx_s *sfx, const float *origin, int channel, float fvol, float attenuation ) = 0;
	virtual void StartRelativeSound( sfx_s *sfx, int entNum, int channel, float fvol, float attenuation ) = 0;
	virtual void StartGlobalSound( sfx_s *sfx, int channel, float fvol ) = 0;

	void StartLocalSound( const char *name ) {
		StartLocalSound( name, 1.0f );
	}

	virtual void StartLocalSound( const char *name, float fvol ) = 0;
	virtual void StartLocalSound( sfx_s *sfx, float fvol ) = 0;
	virtual void AddLoopSound( sfx_s *sfx, int entNum, float fvol, float attenuation ) = 0;

	virtual void StartBackgroundTrack( const char *intro, const char *loop, int mode ) = 0;
	virtual void StopBackgroundTrack() = 0;
	virtual void NextBackgroundTrack() = 0;
	virtual void PrevBackgroundTrack() = 0;
	virtual void PauseBackgroundTrack() = 0;
};

#endif