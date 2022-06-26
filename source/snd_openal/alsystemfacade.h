#include "../gameshared/q_math.h"
#include "../client/snd_public.h"

#include "backend.h"
#include "../qcommon/pipeadapter.h"
#include "snd_local.h"

namespace wsw::snd {

class ALSoundSystem : public SoundSystem, public PipeAdapter {
public:
	struct ThreadProcArg {
		qbufPipe_s *pipe;
		ALSoundSystem *instance;
	};

	[[nodiscard]]
	static auto tryCreate( client_state_s *client, bool verbose ) -> ALSoundSystem *;

	ALSoundSystem( client_state_s *client, qbufPipe_s *pipe, qthread_s *thread, bool verbose );
	~ALSoundSystem() override;

	void deleteSelf( bool verbose ) override;

	void postInit() override;

	void beginRegistration() override;
	void endRegistration() override;

	void stopAllSounds( unsigned flags ) override {
		m_stopAllSoundsCall.exec( flags );
	}

	void clear() override {
		m_clearCall.exec();
	}

	void updateListener( const float *origin, const float *velocity, const mat3_t axis ) override;

	void activate( bool isActive ) override;

	void setEntitySpatialParams( int entNum, const float *origin, const float *velocity ) override {
		m_setEntitySpatialParamsCall.exec( entNum, Vec3( origin ), Vec3( velocity ) );
	}

	[[nodiscard]]
	auto registerSound( const char *name ) -> sfx_s * override;

	void startFixedSound( sfx_s *sfx, const float *origin, int channel, float volume, float attenuation ) override {
		if( sfx ) {
			m_startFixedSoundCall.exec( sfx->id, Vec3( origin ), channel, volume, attenuation );
		}
	}

	void startRelativeSound( sfx_s *sfx, int entNum, int channel, float volume, float attenuation ) override {
		if( sfx ) {
			m_startRelativeSoundCall.exec( sfx->id, entNum, channel, volume, attenuation );
		}
	}

	void startGlobalSound( sfx_s *sfx, int channel, float volume ) override {
		if( sfx ) {
			m_startGlobalSoundCall.exec( sfx->id, channel, volume );
		}
	}

	void startLocalSound( const char *name, float volume ) override {
		startLocalSound( registerSound( name ), volume );
	}

	void startLocalSound( sfx_s *sfx, float volume ) override {
		if( sfx ) {
			m_startLocalSoundCall.exec( sfx->id, volume );
		}
	}

	void addLoopSound( sfx_s *sfx, int entNum, float volume, float attenuation ) override {
		if( sfx ) {
			m_addLoopSoundCall.exec( sfx->id, entNum, volume, attenuation );
		}
	}

	void startBackgroundTrack( const char *intro, const char *loop, int mode ) override;

	void stopBackgroundTrack() override {
		m_stopBackgroundTrackCall.exec();
	}

	void prevBackgroundTrack() override {
		m_advanceBackgroundTrackCall.exec( -1 );
	}

	void nextBackgroundTrack() override {
		m_advanceBackgroundTrackCall.exec( +1 );
	}

	void pauseBackgroundTrack() override {
		m_advanceBackgroundTrackCall.exec( 0 );
	}
private:
	wsw::String m_tmpPathBuffer1;
	wsw::String m_tmpPathBuffer2;

	qthread_s *m_thread { nullptr };
	bool m_useVerboseShutdown { false };

	Backend m_backend;

	InterThreadCall1<Backend, bool> m_initCall { this, "init", &m_backend, &Backend::init };
	InterThreadCall1<Backend, bool> m_shutdownCall { this, "shutdown", &m_backend, &Backend::shutdown };

	InterThreadCall0<Backend> m_clearCall { this, "clear", &m_backend, &Backend::clear };
	InterThreadCall1<Backend, unsigned> m_stopAllSoundsCall {
		this, "stopAllSounds", &m_backend, &Backend::stopAllSounds };

	InterThreadCall1<Backend, bool> m_activateCall { this, "activate", &m_backend, &Backend::activate };

	InterThreadCall1<Backend, int> m_freeSfxCall { this, "freeSound", &m_backend, &Backend::freeSound };
	InterThreadCall1<Backend, int> m_loadSfxCall { this, "loadSound", &m_backend, &Backend::loadSound };

	BatchedInterThreadCall3<Backend, int, Vec3, Vec3, 8> m_setEntitySpatialParamsCall {
		this, "setEntitySpatialParams", &m_backend, &Backend::setEntitySpatialParams };
	InterThreadCall3<Backend, Vec3, Vec3, std::array<Vec3, 3>> m_setListenerCall {
		this, "setListener", &m_backend, &Backend::setListener };

	InterThreadCall2<Backend, int, float> m_startLocalSoundCall {
		this, "startLocalSound", &m_backend, &Backend::startLocalSound };
	InterThreadCall5<Backend, int, Vec3, int, float, float> m_startFixedSoundCall {
		this, "startFixedSound", &m_backend, &Backend::startFixedSound };
	InterThreadCall3<Backend, int, int, float> m_startGlobalSoundCall {
		this, "startGlobalSound", &m_backend, &Backend::startGlobalSound };
	InterThreadCall5<Backend, int, int, int, float, float> m_startRelativeSoundCall {
		this, "startRelativeSound", &m_backend, &Backend::startRelativeSound };
	InterThreadCall4<Backend, int, int, float, float> m_addLoopSoundCall {
		this, "addLoopSound", &m_backend, &Backend::addLoopSound };

	InterThreadCall3<Backend, uintptr_t, uintptr_t, int> m_startBackgroundTrackCall {
		this, "startBackgroundTrack", &m_backend, &Backend::startBackgroundTrack };
	InterThreadCall0<Backend> m_stopBackgroundTrackCall {
		this, "stopBackgroundTrack", &m_backend, &Backend::stopBackgroundTrack };
	InterThreadCall1<Backend, bool> m_lockBackgroundTackCall {
		this, "lockBackgroundTrack", &m_backend, &Backend::lockBackgroundTrack };
	InterThreadCall1<Backend, int> m_advanceBackgroundTrackCall {
		this, "advanceBackgroundTrack", &m_backend, &Backend::advanceBackgroundTrack };

	TerminatePipeCall m_terminatePipeCall { this };
};

}