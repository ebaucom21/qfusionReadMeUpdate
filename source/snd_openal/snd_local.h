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
// snd_local.h -- private OpenAL sound functions

#ifndef SND_OPENAL_LOCAL_H
#define SND_OPENAL_LOCAL_H

//#define VORBISLIB_RUNTIME // enable this define for dynamic linked vorbis libraries

#include "../gameshared/q_arch.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_cvar.h"
#include "../qcommon/qcommon.h"
#include "../client/snd_public.h"
#include "snd_cmdque.h"

#define AL_ALEXT_PROTOTYPES

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include <algorithm>
#include <functional>

#define MAX_SRC 128

typedef struct sfx_s {
	char filename[MAX_QPATH];
	int64_t used;           // Time last used
	int id;
	int registration_sequence;
	ALuint buffer;      // OpenAL buffer
	ALuint stereoBuffer;
	float qualityHint;  // Assumed to be in [0, 1] range for majority of sounds
						// (but values exceeding this range are allowed),
						// spammy sounds like ricochets, plasma explosions,
						// laser impact sounds should have importance close to zero.
						// Should not be treated as generic gameplay importance of the sound,
						// but as a hint allowing lowering quality of sound processing for saving performance
						// (the sound will remain playing but in low quality, without effects, etc).
	bool inMemory;
	bool isLocked;
} sfx_t;

extern cvar_t *s_volume;
extern cvar_t *s_musicvolume;
extern cvar_t *s_sources;
extern cvar_t *s_stereo2mono;

extern cvar_t *s_doppler;
extern cvar_t *s_sound_velocity;
extern cvar_t *s_environment_effects;
extern cvar_t *s_environment_sampling_quality;
extern cvar_t *s_effects_number_threshold;
extern cvar_t *s_hrtf;

extern int s_attenuation_model;
extern float s_attenuation_maxdistance;
extern float s_attenuation_refdistance;

extern ALCdevice *alDevice;
extern ALCcontext *alContext;

#define SRCPRI_AMBIENT  0   // Ambient sound effects
#define SRCPRI_LOOP 1   // Looping (not ambient) sound effects
#define SRCPRI_ONESHOT  2   // One-shot sounds
#define SRCPRI_LOCAL    3   // Local sounds
#define SRCPRI_STREAM   4   // Streams (music, cutscenes)

void S_Clear( void );
void S_Activate( bool active );

void S_SetAttenuationModel( int model, float maxdistance, float refdistance );

// playing
struct sfx_s *S_RegisterSound( const char *sample );

void S_StartFixedSound( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
void S_StartRelativeSound( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation );
void S_StartGlobalSound( struct sfx_s *sfx, int channel, float fvol );

void S_StartLocalSound( sfx_t *sfx, float fvol );

void S_AddLoopSound( struct sfx_s *sfx, int entnum, float fvol, float attenuation );

// cinema
void S_RawSamples( unsigned int samples, unsigned int rate,
				   unsigned short width, unsigned short channels, const uint8_t *data, bool music );
void S_RawSamples2( unsigned int samples, unsigned int rate,
					unsigned short width, unsigned short channels, const uint8_t *data, bool music, float fvol );
void S_PositionedRawSamples( int entnum, float fvol, float attenuation,
							 unsigned int samples, unsigned int rate,
							 unsigned short width, unsigned short channels, const uint8_t *data );

// music
void S_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void S_StopBackgroundTrack( void );
void S_PrevBackgroundTrack( void );
void S_NextBackgroundTrack( void );
void S_PauseBackgroundTrack( void );
void S_LockBackgroundTrack( bool lock );

/*
* Util (snd_al.c)
*/
ALuint S_SoundFormat( int width, int channels );
const char *S_ErrorMessage( ALenum error );
ALuint S_GetBufferLength( ALuint buffer );
void *S_BackgroundUpdateProc( void *param );

/*
* Buffer management
*/
void S_InitBuffers( void );
void S_ShutdownBuffers( void );
void S_SoundList_f( void );
void S_UseBuffer( sfx_t *sfx );
sfx_t *S_FindBuffer( const char *filename );
void S_MarkBufferFree( sfx_t *sfx );

// TODO: Should provide iterators instead
void S_ForEachBuffer( const std::function<void( sfx_t *)> &callback );

sfx_t *S_GetBufferById( int id );
bool S_LoadBuffer( sfx_t *sfx );
bool S_UnloadBuffer( sfx_t *sfx );

typedef struct {
	float quality;
	unsigned numSamples;
	uint16_t valueIndex;
} samplingProps_t;

struct PanningUpdateState {
	static constexpr auto MAX_POINTS = 80;
	vec3_t reflectionPoints[MAX_POINTS];
	unsigned numPassedSecondaryRays;
};

class Effect;

typedef struct envUpdateState_s {
	sfx_t *parent;

	int64_t nextEnvUpdateAt;
	int64_t lastEnvUpdateAt;

	Effect *oldEffect;
	Effect *effect;

	samplingProps_t directObstructionSamplingProps;

	vec3_t lastUpdateOrigin;
	vec3_t lastUpdateVelocity;

	int leafNum;

	int entNum;
	float attenuation;

	float priorityInQueue;

	bool isInLiquid;
	bool needsInterpolation;
} envUpdateState_t;

/*
* Source management
*/
typedef struct src_s {
	ALuint source;

	ALuint directFilter;
	ALuint effect;
	ALuint effectSlot;

	sfx_t *sfx;

	cvar_t *volumeVar;

	int64_t lastUse;    // Last time used
	int priority;
	int entNum;
	int channel;

	float fvol; // volume modifier, for s_volume updating
	float attenuation;

	bool isActive;
	bool isLocked;
	bool isLooping;
	bool isTracking;
	bool keepAlive;
	bool isLingering;

	int64_t lingeringTimeoutAt;

	envUpdateState_t envUpdateState;
	PanningUpdateState panningUpdateState;

	vec3_t origin, velocity; // for local culling
} src_t;

#define QF_METERS_PER_UNIT ( 1.7f / 64.0f )

extern src_t srclist[MAX_SRC];
extern int src_count;

bool S_InitSources( int maxEntities, bool verbose );
void S_ShutdownSources( void );
void S_UpdateSources( void );
src_t *S_AllocSource( int priority, int entnum, int channel );
src_t *S_FindSource( int entnum, int channel );
void S_LockSource( src_t *src );
void S_UnlockSource( src_t *src );
void S_StopAllSources( void );
ALuint S_GetALSource( const src_t *src );
src_t *S_AllocRawSource( int entNum, float fvol, float attenuation, cvar_t *volumeVar );
void S_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity );

/*
* Music
*/
void S_UpdateMusic( void );

/*
* Stream
*/
void S_UpdateStreams( void );
void S_StopStreams( void );
void S_StopRawSamples( void );

/*
* Decoder
*/
typedef struct snd_info_s {
	int rate;
	int width;
	int channels;
	int samples;
	int size;
} snd_info_t;

typedef struct snd_decoder_s snd_decoder_t;
typedef struct snd_stream_s {
	snd_decoder_t *decoder;
	bool isUrl;
	snd_info_t info; // TODO: Change to AL_FORMAT?
	void *ptr; // decoder specific stuff
} snd_stream_t;

typedef struct bgTrack_s {
	char *filename;
	bool ignore;
	bool loop;
	bool muteOnPause;
	snd_stream_t *stream;

	struct bgTrack_s *next; // the next track to be played, the looping part aways points to itself
	struct bgTrack_s *prev; // previous track in the playlist
	struct bgTrack_s *anext; // allocation linked list
} bgTrack_t;

bool S_InitDecoders( bool verbose );
void S_ShutdownDecoders( bool verbose );
void *S_LoadSound( const char *filename, snd_info_t *info );
snd_stream_t *S_OpenStream( const char *filename, bool *delay );
bool S_ContOpenStream( snd_stream_t *stream );
int S_ReadStream( snd_stream_t *stream, int bytes, void *buffer );
void S_CloseStream( snd_stream_t *stream );
bool S_ResetStream( snd_stream_t *stream );
bool S_EoStream( snd_stream_t *stream );
int S_SeekSteam( snd_stream_t *stream, int ofs, int whence );

void S_BeginAviDemo( void );
void S_StopAviDemo( void );

//====================================================================

unsigned S_GetRawSamplesLength( void );
unsigned S_GetPositionedRawSamplesLength( int entnum );

void SF_StopBackgroundTrack( void );
void SF_PrevBackgroundTrack( void );
void SF_NextBackgroundTrack( void );
void SF_PauseBackgroundTrack( void );

class ALSoundSystem : public SoundSystem {
	friend class SoundSystem;

	qbufPipe_s *pipe;
	qthread_s *thread;
	bool useVerboseShutdown { false };

	template <typename> friend class SingletonHolder;

	static ALSoundSystem *TryCreate( client_state_s *client, void *hWnd, bool verbose );

	ALSoundSystem( client_state_s *client_, qbufPipe_s *pipe_, qthread_s *thread_ )
		: SoundSystem( client_ ), pipe( pipe_ ), thread( thread_ ) {}

	~ALSoundSystem() override;
public:
	void DeleteSelf( bool verbose ) override;

	void PostInit() override;

	void BeginRegistration() override;
	void EndRegistration() override;

	void StopAllSounds( unsigned flags ) override;

	void Clear() override;
	void Update( const float *origin, const float *velocity, const mat3_t axis ) override;
	void Activate( bool isActive ) override;

	void SetEntitySpatialization( int entNum, const float *origin, const float *velocity ) override;

	sfx_s *RegisterSound( const char *name ) override;
	void StartFixedSound( sfx_s *sfx, const float *origin, int channel, float fvol, float attenuation ) override;
	void StartRelativeSound( sfx_s *sfx, int entNum, int channel, float fvol, float attenuation ) override;
	void StartGlobalSound( sfx_s *sfx, int channel, float fvol ) override;
	void StartLocalSound( const char *name, float fvol ) override;
	void StartLocalSound( sfx_s *sfx, float fvol ) override;
	void AddLoopSound( sfx_s *sfx, int entNum, float fvol, float attenuation ) override;

	void StartBackgroundTrack( const char *intro, const char *loop, int mode ) override;
	void StopBackgroundTrack() override;
	void LockBackgroundTrack( bool lock ) override;

	void ListSounds();
	void ListDevices();

	void PrevBackgroundTrack();
	void NextBackgroundTrack();
	void PauseBackgroundTrack();
};

// This stuff is used by the sound system implementation and is defined in the client code

void S_Trace( trace_s *tr, const float *start, const float *end, const float *mins,
			  const float *maxs, int mask, int topNodeHint = 0 );

wsw::StringView S_ShaderrefName( int shaderNum );

int S_PointContents( const float *p, int topNodeHint = 0 );
int S_PointLeafNum( const float *p, int topNodeHint = 0 );

int S_NumLeafs();

const vec3_t *S_GetLeafBounds( int leafnum );
bool S_LeafsInPVS( int leafNum1, int leafNum2 );

int S_FindTopNodeForBox( const float *mins, const float *maxs );
int S_FindTopNodeForSphere( const float *center, float radius );

const char *S_GetConfigString( int index );

#endif