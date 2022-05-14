/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "snd_local.h"
#include "snd_cmdque.h"
#include "snd_env_sampler.h"
#include "../client/client.h"
#include "../qcommon/singletonholder.h"

class NullSoundSystem : public SoundSystem {
public:
	explicit NullSoundSystem( client_state_s *client_ ) : SoundSystem( client_ ) {}

	void DeleteSelf( bool ) override;

	void PostInit() override {}

	void BeginRegistration() override {}
	void EndRegistration() override {}

	void StopAllSounds( unsigned ) override {}

	void Clear() override {}
	void Update( const float *, const float *, const mat3_t ) override {}
	void Activate( bool ) override {}

	void SetEntitySpatialization( int, const float *, const float * ) override {};

	sfx_s *RegisterSound( const char * ) override { return nullptr; }
	void StartFixedSound( sfx_s *, const float *, int, float, float ) override {}
	void StartRelativeSound( sfx_s *, int, int, float, float ) override {}
	void StartGlobalSound( sfx_s *, int, float ) override {}
	void StartLocalSound( const char *, float ) override {}
	void StartLocalSound( sfx_s *, float ) override {}
	void AddLoopSound( sfx_s *, int, float, float ) override {}

	void StartBackgroundTrack( const char *, const char *, int ) override {}
	void StopBackgroundTrack() override {}
	void LockBackgroundTrack( bool lock ) override {}
};

static SingletonHolder<ALSoundSystem> alSoundSystemHolder;
static SingletonHolder<NullSoundSystem> nullSoundSystemHolder;

void NullSoundSystem::DeleteSelf( bool ) {
	::nullSoundSystemHolder.shutdown();
}

cvar_t *s_volume;
cvar_t *s_musicvolume;
cvar_t *s_openAL_device;

cvar_t *s_doppler;
cvar_t *s_sound_velocity;
cvar_t *s_environment_effects;
cvar_t *s_environment_sampling_quality;
cvar_t *s_effects_number_threshold;
cvar_t *s_hrtf;
cvar_t *s_realistic_obstruction;
cvar_t *s_stereo2mono;
static cvar_t *s_globalfocus;

static int s_registration_sequence = 1;
static bool s_registering;

// batch entity spatializations
static unsigned s_num_ent_spats;
static smdCmdSpatialization_t s_ent_spats[SND_SPATIALIZE_ENTS_MAX];
static const unsigned s_max_ent_spats = sizeof( s_ent_spats ) / sizeof( s_ent_spats[0] );

/*
* Commands
*/

#ifdef ENABLE_PLAY
static void SF_Play_f( void ) {
	int i;
	char name[MAX_QPATH];

	i = 1;
	while( i < trap_Cmd_Argc() ) {
		Q_strncpyz( name, trap_Cmd_Argv( i ), sizeof( name ) );

		S_StartLocalSound( name );
		i++;
	}
}
#endif // ENABLE_PLAY

/*
* SF_Music
*/
static void SF_Music_f( void ) {
	if( Cmd_Argc() == 2 ) {
		::alSoundSystemHolder.instance()->StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 1 ), 0 );
	} else if( Cmd_Argc() == 3 ) {
		::alSoundSystemHolder.instance()->StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), 0 );
	} else {
		Com_Printf( "music <intro|playlist> [loop|shuffle]\n" );
		return;
	}
}

/*
* SF_SoundList
*/
static void SF_SoundList_f() {
	::alSoundSystemHolder.instance()->ListSounds();
}

void ALSoundSystem::ListSounds() {
	S_IssueStuffCmd( pipe, "soundlist" );
}

/*
* SF_ListDevices_f
*/
static void SF_ListDevices_f() {
	::alSoundSystemHolder.instance()->ListDevices();
}

void ALSoundSystem::ListDevices() {
	S_IssueStuffCmd( pipe, "devicelist" );
}

bool SoundSystem::Init( client_state_t *client, void *hWnd, const InitOptions &options ) {
	s_volume = Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	s_musicvolume = Cvar_Get( "s_musicvolume", "0.05", CVAR_ARCHIVE );
	s_doppler = Cvar_Get( "s_doppler", "1.0", CVAR_ARCHIVE );
	s_sound_velocity = Cvar_Get( "s_sound_velocity", "8500", CVAR_DEVELOPER );
	s_stereo2mono = Cvar_Get( "s_stereo2mono", "0", CVAR_ARCHIVE );
	s_globalfocus = Cvar_Get( "s_globalfocus", "0", CVAR_ARCHIVE );

	if( !options.useNullSystem ) {
		instance = ALSoundSystem::TryCreate( client, hWnd, options.verbose );
		if( instance ) {
			instance->PostInit();
			return true;
		}
	}

	::nullSoundSystemHolder.init( client );
	instance = nullSoundSystemHolder.instance();
	instance->PostInit();
	return options.useNullSystem;
}

ALSoundSystem *ALSoundSystem::TryCreate( client_state_s *client, void *hWnd, bool verbose ) {
	s_num_ent_spats = 0;

#ifdef OPENAL_RUNTIME
	if( !QAL_Init( ALDRIVER, verbose ) ) {
#ifdef ALDRIVER_ALT
		if( !QAL_Init( ALDRIVER_ALT, verbose ) )
#endif
		{
			Com_Printf( "Failed to load OpenAL library: %s\n", ALDRIVER );
			return false;
		}
	}
#endif

	s_environment_effects = Cvar_Get( "s_environment_effects", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );

	s_environment_sampling_quality = Cvar_Get( "s_environment_sampling_quality", "0.5", CVAR_ARCHIVE );
	if ( s_environment_sampling_quality->value < 0 || s_environment_sampling_quality->value > 1.0f ) {
		Cvar_ForceSet( s_environment_sampling_quality->name, "0.5" );
	}

	s_effects_number_threshold = Cvar_Get( "s_effects_number_threshold", "15", CVAR_ARCHIVE );
	s_hrtf = Cvar_Get( "s_hrtf", "1", CVAR_ARCHIVE | CVAR_LATCH_SOUND );
	s_realistic_obstruction = Cvar_Get( "s_realistic_obstruction", "1", CVAR_ARCHIVE );

#ifdef ENABLE_PLAY
	trap_Cmd_AddCommand( "play", SF_Play_f );
#endif
	Cmd_AddCommand( "music", SF_Music_f );
	Cmd_AddCommand( "stopmusic", SF_StopBackgroundTrack );
	Cmd_AddCommand( "prevmusic", SF_PrevBackgroundTrack );
	Cmd_AddCommand( "nextmusic", SF_NextBackgroundTrack );
	Cmd_AddCommand( "pausemusic", SF_PauseBackgroundTrack );
	Cmd_AddCommand( "soundlist", SF_SoundList_f );
	Cmd_AddCommand( "s_devices", SF_ListDevices_f );

	auto *pipe = S_CreateSoundCmdPipe();
	if( !pipe ) {
		return nullptr;
	}

	auto *thread = QThread_Create( S_BackgroundUpdateProc, pipe );

	S_IssueInitCmd( pipe, hWnd, MAX_EDICTS, verbose );

	S_FinishSoundCmdPipe( pipe );

	if( !alContext ) {
		return nullptr;
	}

	S_InitBuffers();

	::alSoundSystemHolder.init( client, pipe, thread );
	return ::alSoundSystemHolder.instance();
}

void ALSoundSystem::DeleteSelf( bool verbose ) {
	useVerboseShutdown = verbose;
	::alSoundSystemHolder.shutdown();
}

ALSoundSystem::~ALSoundSystem() {
	StopAllSounds( StopAndClear | StopMusic );

	// wake up the mixer
	Activate( true );

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( pipe );

	S_ShutdownBuffers();

	ENV_Shutdown();

	// shutdown backend
	S_IssueShutdownCmd( pipe, useVerboseShutdown );

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( pipe );

	// wait for the backend thread to die
	QThread_Join( thread );

	S_DestroySoundCmdPipe( &pipe );

#ifdef ENABLE_PLAY
	Cmd_RemoveCommand( "play" );
#endif
	Cmd_RemoveCommand( "music" );
	Cmd_RemoveCommand( "stopmusic" );
	Cmd_RemoveCommand( "prevmusic" );
	Cmd_RemoveCommand( "nextmusic" );
	Cmd_RemoveCommand( "pausemusic" );
	Cmd_RemoveCommand( "soundlist" );
	Cmd_RemoveCommand( "s_devices" );

	QAL_Shutdown();
}

void ALSoundSystem::PostInit() {
	ENV_Init();

	int model = S_DEFAULT_ATTENUATION_MODEL;
	float maxDistance = S_DEFAULT_ATTENUATION_MAXDISTANCE;
	float refDistance = S_DEFAULT_ATTENUATION_REFDISTANCE;

#ifndef PUBLIC_BUILD
	constexpr const auto varFlags = CVAR_DEVELOPER | CVAR_LATCH_SOUND;
	cvar_t *modelVar = Cvar_Get( "s_attenuation_model", va( "%i", model ), varFlags );
	cvar_t *maxDistanceVar = Cvar_Get( "s_attenuation_maxdistance", va( "%f", maxDistance ), varFlags );
	cvar_t *refDistanceVar = Cvar_Get( "s_attenuation_refdistance", va( "%f", refDistance ), varFlags );

	model = modelVar->integer;
	maxDistance = maxDistanceVar->value;
	refDistance = refDistanceVar->value;
#endif

	S_IssueSetAttenuationCmd( pipe, model, maxDistance, refDistance );
}

void ALSoundSystem::BeginRegistration() {
	s_registration_sequence++;
	if( !s_registration_sequence ) {
		s_registration_sequence = 1;
	}
	s_registering = true;

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( pipe );
}

void ALSoundSystem::EndRegistration() {
	// wait for the queue to be processed
	S_FinishSoundCmdPipe( pipe );

	S_ForEachBuffer( [=]( sfx_t *sfx ) {
		if( sfx->filename[0] && sfx->registration_sequence != s_registration_sequence ) {
			S_IssueFreeSfxCmd( pipe, sfx->id );
		}
	});

	// wait for the queue to be processed
	S_FinishSoundCmdPipe( pipe );

	S_ForEachBuffer( [=]( sfx_t *sfx ) {
		if( sfx->registration_sequence && sfx->registration_sequence != s_registration_sequence ) {
			S_MarkBufferFree( sfx );
		}
	});

	s_registering = false;

	ENV_EndRegistration();
}

/*
* SF_RegisterSound
*/
sfx_t *ALSoundSystem::RegisterSound( const char *name ) {
	sfx_t *sfx = S_FindBuffer( PathForName( name, tmpString1 ) );
	S_IssueLoadSfxCmd( pipe, sfx->id );
	sfx->used = Sys_Milliseconds();
	sfx->registration_sequence = s_registration_sequence;
	return sfx;
}

void ALSoundSystem::Activate( bool active ) {
	if( !active && s_globalfocus->integer ) {
		return;
	}

	LockBackgroundTrack( !active );

	S_IssueActivateCmd( pipe, active );
}

void ALSoundSystem::StartBackgroundTrack( const char *intro, const char *loop, int mode ) {
	const char *introPath = PathForName( intro, tmpString1 );
	const char *loopPath = PathForName( intro, tmpString2 );
	// The path values are deeply copied by this call so using temporary objects is correct
	S_IssueStartBackgroundTrackCmd( pipe, introPath, loopPath, mode );
}

void ALSoundSystem::StopBackgroundTrack() {
	S_IssueStopBackgroundTrackCmd( pipe );
}

void ALSoundSystem::LockBackgroundTrack( bool lock ) {
	S_IssueLockBackgroundTrackCmd( pipe, lock );
}

void ALSoundSystem::StopAllSounds( unsigned flags ) {
	S_IssueStopAllSoundsCmd( pipe, flags );
}

void SF_StopBackgroundTrack() {
	::alSoundSystemHolder.instance()->StopBackgroundTrack();
}

void SF_PrevBackgroundTrack() {
	::alSoundSystemHolder.instance()->PrevBackgroundTrack();
}

void ALSoundSystem::PrevBackgroundTrack() {
	S_IssueAdvanceBackgroundTrackCmd( pipe, -1 );
}

void SF_NextBackgroundTrack() {
	::alSoundSystemHolder.instance()->NextBackgroundTrack();
}

void ALSoundSystem::NextBackgroundTrack() {
	S_IssueAdvanceBackgroundTrackCmd( pipe, 1 );
}

void SF_PauseBackgroundTrack() {
	::alSoundSystemHolder.instance()->PauseBackgroundTrack();
}

void ALSoundSystem::PauseBackgroundTrack() {
	S_IssueAdvanceBackgroundTrackCmd( pipe, 0 );
}

void ALSoundSystem::SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity ) {
	smdCmdSpatialization_t *spat;

	if( s_num_ent_spats == s_max_ent_spats ) {
		// flush all spatializations at once to free room
		S_IssueSetMulEntitySpatializationCmd( pipe, s_num_ent_spats, s_ent_spats );
		s_num_ent_spats = 0;
	}

	spat = &s_ent_spats[s_num_ent_spats++];
	spat->entnum = entnum;
	VectorCopy( origin, spat->origin );
	VectorCopy( velocity, spat->velocity );
}

void ALSoundSystem::StartFixedSound( sfx_t *sfx, const vec3_t origin, int channel, float fvol, float attenuation ) {
	if( sfx ) {
		S_IssueStartFixedSoundCmd( pipe, sfx->id, origin, channel, fvol, attenuation );
	}
}

void ALSoundSystem::StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation ) {
	if( sfx ) {
		S_IssueStartRelativeSoundCmd( pipe, sfx->id, entnum, channel, fvol, attenuation );
	}
}

void ALSoundSystem::StartGlobalSound( sfx_t *sfx, int channel, float fvol ) {
	if( sfx ) {
		S_IssueStartGlobalSoundCmd( pipe, sfx->id, channel, fvol );
	}
}

void ALSoundSystem::StartLocalSound( const char *sound, float fvol ) {
	sfx_t *sfx = RegisterSound( sound );
	if( !sfx ) {
		Com_Printf( "S_StartLocalSoundByName: can't cache %s\n", sound );
		return;
	}

	S_IssueStartLocalSoundCmd( pipe, sfx->id, fvol );
}

void ALSoundSystem::StartLocalSound( sfx_t *sfx, float fvol ) {
	if( sfx ) {
		S_IssueStartLocalSoundCmd( pipe, sfx->id, fvol );
	}
}

void ALSoundSystem::Clear() {
	S_IssueClearCmd( pipe );
}

void ALSoundSystem::AddLoopSound( sfx_s *sfx, int entNum, float fvol, float attenuation ) {
	if( sfx ) {
		S_IssueAddLoopSoundCmd( pipe, sfx->id, entNum, fvol, attenuation );
	}
}

void ALSoundSystem::Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis ) {
	if( s_num_ent_spats ) {
		S_IssueSetMulEntitySpatializationCmd( pipe, s_num_ent_spats, s_ent_spats );
		s_num_ent_spats = 0;
	}

	S_IssueSetListenerCmd( pipe, origin, velocity, axis );
}

void S_Trace( trace_t *tr, const vec3_t start,
			  const vec3_t end, const vec3_t mins,
			  const vec3_t maxs, int mask, int topNodeHint ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		CM_TransformedBoxTrace( cms, tr, start, end, mins, maxs, nullptr, mask, nullptr, nullptr, topNodeHint );
		return;
	}

	::memset( tr, 0, sizeof( trace_t ) );
	tr->fraction = 1.0f;
}

wsw::StringView S_ShaderrefName( int shaderNum ) {
	const char *s = nullptr;
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		s = CM_ShaderrefName( cms, shaderNum );
	}
	// Should really always return a valid name ref
	return s ? wsw::StringView( s ) : wsw::StringView();
}

int S_PointContents( const float *p, int topNodeHint ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_TransformedPointContents( cms, p, nullptr, nullptr, nullptr, topNodeHint );
	}
	return 0;
}

int S_PointLeafNum( const vec3_t p, int topNodeHint ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_PointLeafnum( cms, p, topNodeHint );
	}
	return 0;
}

int S_NumLeafs() {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_NumLeafs( cms );
	}
	return 0;
}

const vec3_t *S_GetLeafBounds( int leafnum ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_GetLeafBounds( cms, leafnum );
	}
	return nullptr;
}

bool S_LeafsInPVS( int leafNum1, int leafNum2 ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return ( leafNum1 == leafNum2 ) || CM_LeafsInPVS( cms, leafNum1, leafNum2 );
	}
	return true;
}

int S_FindTopNodeForBox( const vec3_t mins, const vec3_t maxs ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_FindTopNodeForBox( cms, mins, maxs );
	}
	return 0;
}

int S_FindTopNodeForSphere( const vec3_t center, float radius ) {
	if( const auto *cms = SoundSystem::Instance()->GetClient()->cms ) {
		return CM_FindTopNodeForSphere( cms, center, radius );
	}
	return 0;
}

const char *S_GetConfigString( int index ) {
	return cl.configStrings.get( index ).value_or( wsw::StringView() ).data();
}