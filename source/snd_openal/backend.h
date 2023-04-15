#ifndef WSW_3d1517a5_58dc_4c2e_b388_4ed859c532e2_H
#define WSW_3d1517a5_58dc_4c2e_b388_4ed859c532e2_H

// TODO: lift it to the top level
#include "../game/ai/vec3.h"
#include "../qcommon/wswstring.h"
#include "../qcommon/wswstaticvector.h"
#include <array>

namespace wsw::snd {

class Backend {
	friend class ALSoundSystem;
public:
	void init( bool verbose );
	void shutdown( bool verbose );

	void clear();
	void stopAllSounds( unsigned flags );

	void processFrameUpdates();

	void freeSound( int id );
	void loadSound( int id );

	struct EntitySpatialParamsBatch {
		int entNums[8];
		vec3_t origins[8];
		vec3_t velocities[8];
		unsigned count { 0 };
	};

	void setEntitySpatialParams( const EntitySpatialParamsBatch &batch );

	void setListener( const Vec3 &origin, const Vec3 &velocity, const std::array<Vec3, 3> &axis );

	void startLocalSound( int id, float volume );
	void startFixedSound( int id, const Vec3 &origin, int channel, float volume, float attenuation );
	void startGlobalSound( int id, int channel, float volume );
	void startRelativeSound( int id, int entNum, int channel, float volume, float attenuation );
	void addLoopSound( int id, int entNum, uintptr_t identifyingToken, float volume, float attenuation );

	void startBackgroundTrack( char *intro, char *loop, int mode );
	void stopBackgroundTrack();
	void lockBackgroundTrack( bool lock );
	void advanceBackgroundTrack( int value );
	void activate( bool active );

private:
	bool m_initialized { false };
};

}

#endif