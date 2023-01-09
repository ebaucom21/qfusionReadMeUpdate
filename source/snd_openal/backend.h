#ifndef WSW_3d1517a5_58dc_4c2e_b388_4ed859c532e2_H
#define WSW_3d1517a5_58dc_4c2e_b388_4ed859c532e2_H

// TODO: lift it to the top level
#include "../game/ai/vec3.h"

#include <array>

namespace wsw::snd {

// TODO: Discover how can we avoid making all arguments to be const references so methods can be used by PipeAdapter
class Backend {
	friend class ALSoundSystem;
public:
	void init( const bool &verbose );
	void shutdown( const bool &verbose );

	void clear();
	void stopAllSounds( const unsigned &flags );

	void processFrameUpdates();

	void freeSound( const int &id );
	void loadSound( const int &id );

	void setEntitySpatialParams( const int &entNum, const Vec3 &origin, const Vec3 &velocity );
	void setListener( const Vec3 &origin, const Vec3 &velocity, const std::array<Vec3, 3> &axis );

	void startLocalSound( const int &sfx, const float &volume );
	void startFixedSound( const int &sfx, const Vec3 &origin, const int &channel, const float &volume, const float &attenuation );
	void startGlobalSound( const int &sfx, const int &channel, const float &volume );
	void startRelativeSound( const int &sfx, const int &entNum, const int &channel, const float &volume, const float &attenuation );
	void addLoopSound( const int &sfx, const int &entNum, const float &volume, const float &attenuation );

	// TODO: Discover how to send pointers, see also the general note
	void startBackgroundTrack( const uintptr_t &introNameAddress, const uintptr_t &loopNameAddress, const int &mode );
	void stopBackgroundTrack();
	void lockBackgroundTrack( const bool &lock );
	void advanceBackgroundTrack( const int &value );
	void activate( const bool &active );

private:
	bool m_initialized { false };
};

}

#endif