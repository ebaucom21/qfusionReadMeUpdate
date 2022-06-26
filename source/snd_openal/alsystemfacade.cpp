#include "alsystemfacade.h"

#include "../qcommon/singletonholder.h"
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

	m_initCall.exec( verbose );
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
	m_shutdownCall.exec( m_useVerboseShutdown );
	m_terminatePipeCall.exec();
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

	S_ForEachBuffer( [=]( sfx_t *sfx ) {
		if( sfx->filename[0] && sfx->registration_sequence != s_registration_sequence ) {
			m_freeSfxCall.exec( sfx->id );
		}
	});

	// wait for the queue to be processed
	QBufPipe_Finish( m_pipe );

	S_ForEachBuffer( [=]( sfx_t *sfx ) {
		if( sfx->registration_sequence && sfx->registration_sequence != s_registration_sequence ) {
			S_MarkBufferFree( sfx );
		}
	});

	s_registering = false;

	ENV_EndRegistration();
}

sfx_t *ALSoundSystem::registerSound( const char *name ) {
	sfx_t *sfx = S_FindBuffer( getPathForName( name, &m_tmpPathBuffer1 ) );
	m_loadSfxCall.exec( sfx->id );
	sfx->used = Sys_Milliseconds();
	sfx->registration_sequence = s_registration_sequence;
	return sfx;
}

void ALSoundSystem::activate( bool active ) {
	if( !active && s_globalfocus->integer ) {
		return;
	}

	// TODO: Let the activate() backend call manage the track state?
	m_lockBackgroundTackCall.exec( !active );
	m_activateCall.exec( active );
}

void ALSoundSystem::startBackgroundTrack( const char *intro, const char *loop, int mode ) {
	const char *introPath = getPathForName( intro, &m_tmpPathBuffer1 );
	const char *loopPath  = getPathForName( loop, &m_tmpPathBuffer2 );

	char *boxedIntro = introPath ? Q_strdup( introPath ) : nullptr;
	char *boxedLoop  = loopPath ? Q_strdup( loopPath ) : nullptr;

	m_startBackgroundTrackCall.exec( (uintptr_t)boxedIntro, (uintptr_t)boxedLoop, mode );
}

void ALSoundSystem::updateListener( const vec3_t origin, const vec3_t velocity, const mat3_t axis ) {
	m_setEntitySpatialParamsCall.flush();

	std::array<Vec3, 3> argAxis {
		Vec3 { axis[0], axis[1], axis[2] },
		Vec3 { axis[3], axis[4], axis[5] },
		Vec3 { axis[6], axis[7], axis[8] }
	};

	m_setListenerCall.exec( Vec3( origin ), Vec3( velocity ), argAxis );
}

}