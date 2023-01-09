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

#include "../qcommon/wswstring.h"

struct sfx_s;
struct client_state_s;

class SoundSystem {
	static SoundSystem *s_instance;

	client_state_s *const m_client;

#ifdef WIN32
	/**
	 * It's a good idea to limit the access to the {@code InstanceOrNull()}
	 * method only to this function to prevent spreading of this hack over the codebase
	 */
	friend void AppActivate( int, int, int );

	/**
	 * A hack that makes the instance state accessible for the Win32-specific code
	 */
	static SoundSystem *instanceOrNull() { return s_instance; }
#endif
protected:
	[[nodiscard]]
	static auto getPathForName( const char *name, wsw::String *reuse ) -> const char *;

	explicit SoundSystem( client_state_s *client ) : m_client( client ) {}
public:
	struct InitOptions {
		bool verbose { false };
		bool useNullSystem { false };
	};

	[[nodiscard]]
	static bool init( client_state_s *client, const InitOptions &options );

	static void shutdown( bool verbose );

	[[nodiscard]]
	static auto instance() -> SoundSystem * {
		assert( s_instance );
		return s_instance;
	}

	// TODO: Build within the client code so we don't need this
	[[nodiscard]]
	auto getClient() -> client_state_s * { return m_client; }
	[[nodiscard]]
	auto getClient() const -> const client_state_s * { return m_client; }

	virtual ~SoundSystem() = default;

	// TODO: Fix this
	virtual void deleteSelf( bool verbose ) = 0;

	/**
	 * @todo this is just to break a circular dependency. Refactor global objects into SoundSystem member fields.
	 */
	virtual void postInit() = 0;

	virtual void beginRegistration() = 0;
	virtual void endRegistration() = 0;

	enum StopFlags : unsigned { StopAndClear = 0x1, StopMusic = 0x2 };

	virtual void stopAllSounds( unsigned flags = 0 ) = 0;

	virtual void clear() = 0;
	virtual void updateListener( const float *origin, const float *velocity, const mat3_t axis ) = 0;
	virtual void activate( bool isActive ) = 0;

	virtual void processFrameUpdates() = 0;

	virtual void setEntitySpatialParams( int entNum, const float *origin, const float *velocity ) = 0;

	[[nodiscard]]
	virtual auto registerSound( const char *name ) -> sfx_s * = 0;

	virtual void startFixedSound( sfx_s *sfx, const float *origin, int channel, float fvol, float attenuation ) = 0;
	virtual void startRelativeSound( sfx_s *sfx, int entNum, int channel, float fvol, float attenuation ) = 0;
	virtual void startGlobalSound( sfx_s *sfx, int channel, float fvol ) = 0;

	virtual void startLocalSound( const char *name, float fvol ) = 0;
	virtual void startLocalSound( sfx_s *sfx, float fvol ) = 0;
	virtual void addLoopSound( sfx_s *sfx, int entNum, float fvol, float attenuation ) = 0;

	virtual void startBackgroundTrack( const char *intro, const char *loop, int mode ) = 0;
	virtual void stopBackgroundTrack() = 0;
	virtual void nextBackgroundTrack() = 0;
	virtual void prevBackgroundTrack() = 0;
	virtual void pauseBackgroundTrack() = 0;
};

#endif