#include "alsystemfacade.h"

#include "../qcommon/singletonholder.h"
#include "../qcommon/pipeutils.h"

#include "snd_local.h"
#include "snd_env_sampler.h"

static SingletonHolder<wsw::snd::ALSoundSystem> alSoundSystemHolder;
static bool s_registering;
static int s_registration_sequence = 1;

extern cvar_s *s_globalfocus;

namespace wsw::snd {

wsw::snd::ALSoundSystem *wsw::snd::ALSoundSystem::tryCreate( client_state_s *client, bool verbose ) {
	auto *arg = (ThreadProcArg *)Q_malloc( sizeof( ThreadProcArg ) );
	if( !arg ) {
		return nullptr;
	}

	qbufPipe_s *pipe = QBufPipe_Create( 0x100000, 0 );
	if( !pipe ) {
		Q_free( arg );
		return nullptr;
	}

	arg->pipe     = pipe;
	// Pass the address of the instance.
	// It won't be accessed prior to the constructor call.
	// TODO: There should be guarantees regarding the object placement within the holder.
	arg->instance = (ALSoundSystem *)&::alSoundSystemHolder;

	// TODO: Pass both this instance and pipe via a heap-allocated argument
	qthread_s *thread = QThread_Create( S_BackgroundUpdateProc, arg );
	if( !thread ) {
		Q_free( arg );
		QBufPipe_Destroy( &pipe );
		return nullptr;
	}

	::alSoundSystemHolder.init( client, pipe, thread, verbose );
	ALSoundSystem *instance = ::alSoundSystemHolder.instance();
	assert( (void *)instance == (void *)&::alSoundSystemHolder );

	if( !instance->m_backend.m_initialized ) {
		// This should dispose the thread and the pipe
		instance->deleteSelf( verbose );
		return nullptr;
	}

	S_InitBuffers();

	return instance;
}

void ALSoundSystem::deleteSelf( bool verbose ) {
	m_useVerboseShutdown = verbose;
	::alSoundSystemHolder.shutdown();
}

ALSoundSystem::ALSoundSystem( client_state_s *client, qbufPipe_s *pipe, qthread_s *thread, bool verbose )
	: SoundSystem( client ) {
	m_pipe   = pipe;
	m_thread = thread;

	callMethodOverPipe( pipe, &m_backend, &Backend::init, verbose );
	QBufPipe_Finish( pipe );
}

ALSoundSystem::~ALSoundSystem() {
	stopAllSounds( StopAndClear | StopMusic );
	// wake up the mixer
	activate( true );

	// wait for the queue to be processed
	QBufPipe_Finish( m_pipe );

	S_ShutdownBuffers();
	ENV_Shutdown();

	// shutdown backend

	callMethodOverPipe( m_pipe, &m_backend, &Backend::shutdown, m_useVerboseShutdown );
	sendTerminateCmd( m_pipe );

	// wait for the queue to be terminated
	QBufPipe_Finish( m_pipe );

	// wait for the backend thread to die
	QThread_Join( m_thread );

	QBufPipe_Destroy( &m_pipe );
}

void ALSoundSystem::postInit() {
	ENV_Init();
}

void ALSoundSystem::beginRegistration() {
	s_registration_sequence++;
	if( !s_registration_sequence ) {
		s_registration_sequence = 1;
	}

	s_registering = true;

	// wait for the queue to be processed
	QBufPipe_Finish( m_pipe );
}

void ALSoundSystem::endRegistration() {
	// wait for the queue to be processed
	QBufPipe_Finish( m_pipe );

	S_ForEachBuffer( [this]( sfx_t *sfx ) {
		if( sfx->filename[0] && sfx->registration_sequence != s_registration_sequence ) {
			callMethodOverPipe( m_pipe, &m_backend, &Backend::freeSound, sfx->id );
		}
	});

	// wait for the queue to be processed
	QBufPipe_Finish( m_pipe );

	S_ForEachBuffer( []( sfx_t *sfx ) {
		if( sfx->registration_sequence && sfx->registration_sequence != s_registration_sequence ) {
			S_MarkBufferFree( sfx );
		}
	});

	s_registering = false;

	ENV_EndRegistration();
}

void ALSoundSystem::stopAllSounds( unsigned flags ) {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::stopAllSounds, flags );
}

void ALSoundSystem::clear() {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::clear );
}

sfx_t *ALSoundSystem::registerSound( const char *name ) {
	// TODO: All of that should just be sync...
	sfx_t *sfx = S_FindBuffer( getPathForName( name, &m_tmpPathBuffer1 ) );

	callMethodOverPipe( m_pipe, &m_backend, &Backend::loadSound, sfx->id );
	QBufPipe_Finish( m_pipe );

	if( sfx->buffer ) {
		sfx->used = Sys_Milliseconds();
		sfx->registration_sequence = s_registration_sequence;
		return sfx;
	}
	S_MarkBufferFree( sfx );
	return nullptr;
}

void ALSoundSystem::activate( bool active ) {
	if( !active && s_globalfocus->integer ) {
		return;
	}

	// TODO: Let the activate() backend call manage the track state?
	callMethodOverPipe( m_pipe, &m_backend, &Backend::lockBackgroundTrack, !active );
	callMethodOverPipe( m_pipe, &m_backend, &Backend::activate, active );
}

void ALSoundSystem::startFixedSound( sfx_s *sfx, const float *origin, int channel, float volume, float attenuation ) {
	if( sfx ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::startFixedSound, sfx->id, Vec3( origin ), channel, volume, attenuation );
	}
}

void ALSoundSystem::startRelativeSound( sfx_s *sfx, int entNum, int channel, float volume, float attenuation ) {
	if( sfx ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::startRelativeSound, sfx->id, entNum, channel, volume, attenuation );
	}
}

void ALSoundSystem::startGlobalSound( sfx_s *sfx, int channel, float volume ) {
	if( sfx ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::startGlobalSound, sfx->id, channel, volume );
	}
}

void ALSoundSystem::startLocalSound( const char *name, float volume ) {
	startLocalSound( registerSound( name ), volume );
}

void ALSoundSystem::startLocalSound( sfx_s *sfx, float volume ) {
	if( sfx ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::startLocalSound, sfx->id, volume );
	}
}

void ALSoundSystem::addLoopSound( sfx_s *sfx, int entNum, uintptr_t identifyingToken, float volume, float attenuation ) {
	if( sfx ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::addLoopSound, sfx->id, entNum, identifyingToken, volume, attenuation );
	}
}

void ALSoundSystem::startBackgroundTrack( const char *intro, const char *loop, int mode ) {
	const char *introPath = getPathForName( intro, &m_tmpPathBuffer1 );
	const char *loopPath  = getPathForName( loop, &m_tmpPathBuffer2 );

	char *const boxedIntroPath = introPath ? Q_strdup( introPath ) : nullptr;
	char *const boxedLoopPath  = loopPath ? Q_strdup( loopPath ) : nullptr;

	callMethodOverPipe( m_pipe, &m_backend, &Backend::startBackgroundTrack, boxedIntroPath, boxedLoopPath, mode );
}

void ALSoundSystem::stopBackgroundTrack() {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::stopBackgroundTrack );
}

void ALSoundSystem::prevBackgroundTrack() {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::advanceBackgroundTrack, -1 );
}

void ALSoundSystem::nextBackgroundTrack() {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::advanceBackgroundTrack, +1 );
}

void ALSoundSystem::pauseBackgroundTrack() {
	callMethodOverPipe( m_pipe, &m_backend, &Backend::advanceBackgroundTrack, 0 );
}

void ALSoundSystem::updateListener( const vec3_t origin, const vec3_t velocity, const mat3_t axis ) {
	std::array<Vec3, 3> argAxis {
		Vec3 { axis[0], axis[1], axis[2] },
		Vec3 { axis[3], axis[4], axis[5] },
		Vec3 { axis[6], axis[7], axis[8] }
	};

	callMethodOverPipe( m_pipe, &m_backend, &Backend::setListener, Vec3( origin ), Vec3( velocity ), argAxis );
}

void ALSoundSystem::setEntitySpatialParams( int entNum, const float *origin, const float *velocity ) {
	if( m_spatialParamsBatch.count == std::size( m_spatialParamsBatch.entNums ) ) [[unlikely]] {
		flushEntitySpatialParams();
	}

	m_spatialParamsBatch.entNums[m_spatialParamsBatch.count] = entNum;
	VectorCopy( origin, m_spatialParamsBatch.origins[m_spatialParamsBatch.count] );
	VectorCopy( velocity, m_spatialParamsBatch.velocities[m_spatialParamsBatch.count] );
	m_spatialParamsBatch.count++;
}

void ALSoundSystem::processFrameUpdates() {
	flushEntitySpatialParams();
	callMethodOverPipe( m_pipe, &m_backend, &Backend::processFrameUpdates );
}

void ALSoundSystem::flushEntitySpatialParams() {
	if( m_spatialParamsBatch.count > 0 ) {
		callMethodOverPipe( m_pipe, &m_backend, &Backend::setEntitySpatialParams, m_spatialParamsBatch );
		m_spatialParamsBatch.count = 0;
	}
}

}