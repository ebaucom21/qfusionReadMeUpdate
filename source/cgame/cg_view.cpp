/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2003 Victor Luchits
Copyright (C) 2007 Daniel Lindenfelser
Copyright (C) 2009 German Garcia Fernandez ("Jal")

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

#include "cg_local.h"
#include "../client/client.h"
#include "../ui/uisystem.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/cmdargs.h"
#include "../qcommon/cmdcompat.h"
#include "../qcommon/wswfs.h"

using wsw::operator""_asView;

extern cvar_t *cg_showHUD;
extern cvar_t *cg_showTeamInfo;
extern cvar_t *cg_showTimer;
extern cvar_t *cg_showPlayerNames;
extern cvar_t *cg_showPlayerNames_alpha;
extern cvar_t *cg_showPlayerNames_barWidth;
extern cvar_t *cg_showPlayerNames_zfar;
extern cvar_t *cg_showPointedPlayer;

extern cvar_t *cg_viewSize;
extern cvar_t *cg_showFPS;
extern cvar_t *cg_draw2D;

extern cvar_t *cg_showZoomEffect;

extern cvar_t *cg_showViewBlends;

static vrect_t scr_vrect;

static int64_t demo_initial_timestamp;
static int64_t demo_time;

static bool CamIsFree;

#define CG_DemoCam_UpdateDemoTime() ( demo_time = cg.time - demo_initial_timestamp )

typedef struct cg_democam_s
{
	int type;
	int64_t timeStamp;
	vec3_t origin;
	vec3_t angles;
	int fov;
	float speed;
	struct cg_democam_s *next;
} cg_democam_t;

cg_democam_t *currentcam;

static vec3_t cam_origin, cam_angles, cam_velocity;
static float cam_fov = 90;
static int cam_viewtype;
static int cam_POVent;
static bool cam_3dPerson;

static short freecam_delta_angles[3];

cg_chasecam_t chaseCam;

static bool postmatchsilence_set = false, demostream = false, background = false;
static unsigned lastSecond = 0;

static int oldState = -1;
static int oldAlphaScore, oldBetaScore;
static bool scoresSet = false;

int CG_DemoCam_GetViewType( void ) {
	return cam_viewtype;
}

bool CG_DemoCam_GetThirdPerson( void ) {
	if( !currentcam ) {
		return ( chaseCam.mode == CAM_THIRDPERSON );
	}
	return ( cam_viewtype == VIEWDEF_PLAYERVIEW && cam_3dPerson );
}

void CG_DemoCam_GetViewDef( cg_viewdef_t *view ) {
	view->POVent = cam_POVent;
	view->thirdperson = cam_3dPerson;
	view->playerPrediction = false;
	view->drawWeapon = false;
	view->draw2D = false;
}

float CG_DemoCam_GetOrientation( vec3_t origin, vec3_t angles, vec3_t velocity ) {
	VectorCopy( cam_angles, angles );
	VectorCopy( cam_origin, origin );
	VectorCopy( cam_velocity, velocity );

	if( !currentcam || !currentcam->fov ) {
		return bound( MIN_FOV, cg_fov->value, MAX_FOV );
	}

	return cam_fov;
}

// TODO: Should it belong to the same place where prediction gets executed?
int CG_DemoCam_FreeFly( void ) {
	usercmd_t cmd;
	const float SPEED = 500;

	if( cgs.demoPlaying && CamIsFree ) {
		vec3_t wishvel, wishdir, forward, right, up, moveangles;
		float fmove, smove, upmove, wishspeed, maxspeed;
		int i;

		maxspeed = 250;

		// run frame
		NET_GetUserCmd( NET_GetCurrentUserCmdNum() - 1, &cmd );
		cmd.msec = cg.realFrameTime;

		for( i = 0; i < 3; i++ )
			moveangles[i] = SHORT2ANGLE( cmd.angles[i] ) + SHORT2ANGLE( freecam_delta_angles[i] );

		AngleVectors( moveangles, forward, right, up );
		VectorCopy( moveangles, cam_angles );

		fmove = cmd.forwardmove * SPEED / 127.0f;
		smove = cmd.sidemove * SPEED / 127.0f;
		upmove = cmd.upmove * SPEED / 127.0f;
		if( cmd.buttons & BUTTON_SPECIAL ) {
			maxspeed *= 2;
		}

		for( i = 0; i < 3; i++ )
			wishvel[i] = forward[i] * fmove + right[i] * smove;
		wishvel[2] += upmove;

		wishspeed = VectorNormalize2( wishvel, wishdir );
		if( wishspeed > maxspeed ) {
			wishspeed = maxspeed / wishspeed;
			VectorScale( wishvel, wishspeed, wishvel );
			wishspeed = maxspeed;
		}

		VectorMA( cam_origin, (float)cg.realFrameTime * 0.001f, wishvel, cam_origin );

		cam_POVent = 0;
		cam_3dPerson = false;
		return VIEWDEF_CAMERA;
	}

	return VIEWDEF_PLAYERVIEW;
}

static void CG_Democam_SetCameraPositionFromView( void ) {
	if( cg.view.type == VIEWDEF_PLAYERVIEW ) {
		VectorCopy( cg.view.origin, cam_origin );
		VectorCopy( cg.view.angles, cam_angles );
		VectorCopy( cg.view.velocity, cam_velocity );
		cam_fov = cg.view.refdef.fov_x;
	}

	if( !CamIsFree ) {
		int i;
		usercmd_t cmd;

		NET_GetUserCmd( NET_GetCurrentUserCmdNum() - 1, &cmd );

		for( i = 0; i < 3; i++ )
			freecam_delta_angles[i] = ANGLE2SHORT( cam_angles[i] ) - cmd.angles[i];
	}
}

static int CG_Democam_CalcView( void ) {
	VectorClear( cam_velocity );
	return VIEWDEF_PLAYERVIEW;
}

bool CG_DemoCam_Update( void ) {
	if( !cgs.demoPlaying ) {
		return false;
	}

	if( !demo_initial_timestamp && cg.frame.valid ) {
		demo_initial_timestamp = cg.time;
	}

	CG_DemoCam_UpdateDemoTime();

	cam_3dPerson = false;
	cam_viewtype = VIEWDEF_PLAYERVIEW;
	cam_POVent = cg.frame.playerState.POVnum;

	if( CamIsFree ) {
		cam_viewtype = CG_DemoCam_FreeFly();
	} else if( currentcam ) {
		cam_viewtype = CG_Democam_CalcView();
	}

	CG_Democam_SetCameraPositionFromView();

	return true;
}

bool CG_DemoCam_IsFree( void ) {
	return CamIsFree;
}

static void CG_DemoFreeFly_Cmd_f( const CmdArgs &cmdArgs ) {
	if( Cmd_Argc() > 1 ) {
		if( !Q_stricmp( Cmd_Argv( 1 ), "on" ) ) {
			CamIsFree = true;
		} else if( !Q_stricmp( Cmd_Argv( 1 ), "off" ) ) {
			CamIsFree = false;
		}
	} else {
		CamIsFree = !CamIsFree;
	}

	VectorClear( cam_velocity );
}

static void CG_CamSwitch_Cmd_f( const CmdArgs & ) {

}

void CG_DemocamInit( void ) {
	demo_time = 0;
	demo_initial_timestamp = 0;

	if( !cgs.demoPlaying ) {
		return;
	}

	if( !*cgs.demoName ) {
		CG_Error( "CG_DemocamInit: no demo name string\n" );
	}

	// add console commands
	CL_Cmd_Register( "demoFreeFly"_asView, CG_DemoFreeFly_Cmd_f );
	CL_Cmd_Register( "camswitch"_asView, CG_CamSwitch_Cmd_f );
}

void CG_DemocamShutdown( void ) {
	if( !cgs.demoPlaying ) {
		return;
	}

	// remove console commands
	CL_Cmd_Unregister( "demoFreeFly"_asView );
	CL_Cmd_Unregister( "camswitch"_asView );
}

void CG_DemocamReset( void ) {
	demo_time = 0;
	demo_initial_timestamp = 0;
}

int CG_LostMultiviewPOV( void );

/*
* CG_ChaseStep
*
* Returns whether the POV was actually requested to be changed.
*/
bool CG_ChaseStep( int step ) {
	int index, checkPlayer, i;

	if( cg.frame.multipov ) {
		// find the playerState containing our current POV, then cycle playerStates
		index = -1;
		for( i = 0; i < cg.frame.numplayers; i++ ) {
			if( cg.frame.playerStates[i].playerNum < (unsigned)gs.maxclients && cg.frame.playerStates[i].playerNum == cg.multiviewPlayerNum ) {
				index = i;
				break;
			}
		}

		// the POV was lost, find the closer one (may go up or down, but who cares)
		if( index == -1 ) {
			checkPlayer = CG_LostMultiviewPOV();
		} else {
			checkPlayer = index;
			for( i = 0; i < cg.frame.numplayers; i++ ) {
				checkPlayer += step;
				if( checkPlayer < 0 ) {
					checkPlayer = cg.frame.numplayers - 1;
				} else if( checkPlayer >= cg.frame.numplayers ) {
					checkPlayer = 0;
				}

				if( ( checkPlayer != index ) && cg.frame.playerStates[checkPlayer].stats[STAT_REALTEAM] == TEAM_SPECTATOR ) {
					continue;
				}
				break;
			}
		}

		if( index < 0 ) {
			return false;
		}

		cg.multiviewPlayerNum = cg.frame.playerStates[checkPlayer].playerNum;
		return true;
	}
	
	if( !cgs.demoPlaying ) {
		CL_Cmd_ExecuteNow( step > 0 ? "chasenext" : "chaseprev" );
		return true;
	}

	return false;
}

static void CG_AddLocalSounds( void ) {
	// add local announces
	if( GS_Countdown() ) {
		if( GS_MatchDuration() ) {
			int64_t duration, curtime;
			unsigned remainingSeconds;
			float seconds;

			curtime = GS_MatchPaused() ? cg.frame.serverTime : cg.time;
			duration = GS_MatchDuration();

			if( duration + GS_MatchStartTime() < curtime ) {
				duration = curtime - GS_MatchStartTime(); // avoid negative results

			}
			seconds = (float)( GS_MatchStartTime() + duration - curtime ) * 0.001f;
			remainingSeconds = (unsigned int)seconds;

			if( remainingSeconds != lastSecond ) {
				if( 1 + remainingSeconds < 4 ) {
					struct sfx_s *sound = SoundSystem::instance()->registerSound( va( S_ANNOUNCER_COUNTDOWN_COUNT_1_to_3_SET_1_to_2, 1 + remainingSeconds, 1 ) );
					CG_AddAnnouncerEvent( sound, false );
				}

				lastSecond = remainingSeconds;
			}
		}
	} else {
		lastSecond = 0;
	}

	// add sounds from announcer
	CG_ReleaseAnnouncerEvents();

	// Stop background music in postmatch state
	if( GS_MatchState() >= MATCH_STATE_POSTMATCH ) {
		if( !postmatchsilence_set && !demostream ) {
			SoundSystem::instance()->stopBackgroundTrack();
			postmatchsilence_set = true;
			background = false;
		}
	} else {
		if( cgs.demoPlaying && cgs.demoAudioStream && !demostream ) {
			SoundSystem::instance()->startBackgroundTrack( cgs.demoAudioStream, NULL, 0 );
			demostream = true;
		}

		if( postmatchsilence_set ) {
			postmatchsilence_set = false;
			background = false;
		}

		if( ( !postmatchsilence_set && !demostream ) && !background ) {
			CG_StartBackgroundTrack();
			background = true;
		}
	}
}

/*
* CG_FlashGameWindow
*
* Flashes game window in case of important events (match state changes, etc) for user to notice
*/
static void CG_FlashGameWindow( void ) {
	int newState;
	bool flash = false;

	// notify player of important match states
	newState = GS_MatchState();
	if( oldState != newState ) {
		switch( newState ) {
			case MATCH_STATE_COUNTDOWN:
			case MATCH_STATE_PLAYTIME:
			case MATCH_STATE_POSTMATCH:
				flash = true;
				break;
			default:
				break;
		}

		oldState = newState;
	}

	// notify player of teams scoring in team-based gametypes
	if( !scoresSet ||
		( oldAlphaScore != cg.predictedPlayerState.stats[STAT_TEAM_ALPHA_SCORE] || oldBetaScore != cg.predictedPlayerState.stats[STAT_TEAM_BETA_SCORE] ) ) {
		oldAlphaScore = cg.predictedPlayerState.stats[STAT_TEAM_ALPHA_SCORE];
		oldBetaScore = cg.predictedPlayerState.stats[STAT_TEAM_BETA_SCORE];

		flash = scoresSet && GS_TeamBasedGametype() && !GS_IndividualGameType();
		scoresSet = true;
	}

	if( flash ) {
		VID_FlashWindow( cg_flashWindowCount->integer );
	}
}

/*
* CG_GetSensitivityScale
* Scale sensitivity for different view effects
*/
float CG_GetSensitivityScale( float sens, float zoomSens ) {
	float sensScale = 1.0f;

	if( !cgs.demoPlaying && sens && ( cg.predictedPlayerState.pmove.stats[PM_STAT_ZOOMTIME] > 0 ) ) {
		if( zoomSens ) {
			return zoomSens / sens;
		}

		return cg_zoomfov->value / cg_fov->value;
	}

	return sensScale;
}

void CG_AddKickAngles( vec3_t viewangles ) {
	float time;
	float uptime;
	float delta;
	int i;

	for( i = 0; i < MAX_ANGLES_KICKS; i++ ) {
		if( cg.time > cg.kickangles[i].timestamp + cg.kickangles[i].kicktime ) {
			continue;
		}

		time = (float)( ( cg.kickangles[i].timestamp + cg.kickangles[i].kicktime ) - cg.time );
		uptime = ( (float)cg.kickangles[i].kicktime ) * 0.5f;
		delta = 1.0f - ( fabs( time - uptime ) / uptime );

		//Com_Printf("Kick Delta:%f\n", delta );
		if( delta > 1.0f ) {
			delta = 1.0f;
		}
		if( delta <= 0.0f ) {
			continue;
		}

		viewangles[PITCH] += cg.kickangles[i].v_pitch * delta;
		viewangles[ROLL] += cg.kickangles[i].v_roll * delta;
	}
}

static float CG_CalcViewFov( void ) {
	float frac;
	float fov, zoomfov;

	fov = cg_fov->value;
	zoomfov = cg_zoomfov->value;

	if( !cg.predictedPlayerState.pmove.stats[PM_STAT_ZOOMTIME] ) {
		return fov;
	}

	frac = (float)cg.predictedPlayerState.pmove.stats[PM_STAT_ZOOMTIME] / (float)ZOOMTIME;
	return fov - ( fov - zoomfov ) * frac;
}

static void CG_CalcViewBob( void ) {
	float bobMove, bobTime, bobScale;

	if( !cg.view.drawWeapon ) {
		return;
	}

	// calculate speed and cycle to be used for all cyclic walking effects
	cg.xyspeed = sqrt( cg.predictedPlayerState.pmove.velocity[0] * cg.predictedPlayerState.pmove.velocity[0] + cg.predictedPlayerState.pmove.velocity[1] * cg.predictedPlayerState.pmove.velocity[1] );

	bobScale = 0;
	if( cg.xyspeed < 5 ) {
		cg.oldBobTime = 0;  // start at beginning of cycle again
	} else if( cg_gunbob->integer ) {
		if( !ISVIEWERENTITY( cg.view.POVent ) ) {
			bobScale = 0.0f;
		} else if( CG_PointContents( cg.view.origin ) & MASK_WATER ) {
			bobScale =  0.75f;
		} else {
			centity_t *cent;
			vec3_t mins, maxs;
			trace_t trace;

			cent = &cg_entities[cg.view.POVent];
			GS_BBoxForEntityState( &cent->current, mins, maxs );
			maxs[2] = mins[2];
			mins[2] -= ( 1.6f * STEPSIZE );

			CG_Trace( &trace, cg.predictedPlayerState.pmove.origin, mins, maxs, cg.predictedPlayerState.pmove.origin, cg.view.POVent, MASK_PLAYERSOLID );
			if( trace.startsolid || trace.allsolid ) {
				if( cg.predictedPlayerState.pmove.stats[PM_STAT_CROUCHTIME] ) {
					bobScale = 1.5f;
				} else {
					bobScale = 2.5f;
				}
			}
		}
	}

	bobMove = cg.frameTime * bobScale * 0.001f;
	bobTime = ( cg.oldBobTime += bobMove );

	cg.bobCycle = (int)bobTime;
	cg.bobFracSin = fabs( sin( bobTime * M_PI ) );
}

void CG_ResetKickAngles( void ) {
	memset( cg.kickangles, 0, sizeof( cg.kickangles ) );
}

void CG_StartKickAnglesEffect( vec3_t source, float knockback, float radius, int time ) {
	float kick;
	float side;
	float dist;
	float delta;
	float ftime;
	vec3_t forward, right, v;
	int i, kicknum = -1;
	vec3_t playerorigin;

	if( knockback <= 0 || time <= 0 || radius <= 0.0f ) {
		return;
	}

	// if spectator but not in chasecam, don't get any kick
	if( cg.frame.playerState.pmove.pm_type == PM_SPECTATOR ) {
		return;
	}

	// not if dead
	if( cg_entities[cg.view.POVent].current.type == ET_CORPSE || cg_entities[cg.view.POVent].current.type == ET_GIB ) {
		return;
	}

	// predictedPlayerState is predicted only when prediction is enabled, otherwise it is interpolated
	VectorCopy( cg.predictedPlayerState.pmove.origin, playerorigin );

	VectorSubtract( source, playerorigin, v );
	dist = VectorNormalize( v );
	if( dist > radius ) {
		return;
	}

	delta = 1.0f - ( dist / radius );
	if( delta > 1.0f ) {
		delta = 1.0f;
	}
	if( delta <= 0.0f ) {
		return;
	}

	kick = fabs( knockback ) * delta;
	if( kick ) { // kick of 0 means no view adjust at all
		//find first free kick spot, or the one closer to be finished
		for( i = 0; i < MAX_ANGLES_KICKS; i++ ) {
			if( cg.time > cg.kickangles[i].timestamp + cg.kickangles[i].kicktime ) {
				kicknum = i;
				break;
			}
		}

		// all in use. Choose the closer to be finished
		if( kicknum == -1 ) {
			int remaintime;
			int best = ( cg.kickangles[0].timestamp + cg.kickangles[0].kicktime ) - cg.time;
			kicknum = 0;
			for( i = 1; i < MAX_ANGLES_KICKS; i++ ) {
				remaintime = ( cg.kickangles[i].timestamp + cg.kickangles[i].kicktime ) - cg.time;
				if( remaintime < best ) {
					best = remaintime;
					kicknum = i;
				}
			}
		}

		AngleVectors( cg.frame.playerState.viewangles, forward, right, NULL );

		if( kick < 1.0f ) {
			kick = 1.0f;
		}

		side = DotProduct( v, right );
		cg.kickangles[kicknum].v_roll = kick * side * 0.3;
		Q_clamp( cg.kickangles[kicknum].v_roll, -20, 20 );

		side = -DotProduct( v, forward );
		cg.kickangles[kicknum].v_pitch = kick * side * 0.3;
		Q_clamp( cg.kickangles[kicknum].v_pitch, -20, 20 );

		cg.kickangles[kicknum].timestamp = cg.time;
		ftime = (float)time * delta;
		if( ftime < 100 ) {
			ftime = 100;
		}
		cg.kickangles[kicknum].kicktime = ftime;
	}
}

void CG_StartFallKickEffect( int bounceTime ) {
	if( !cg_viewBob->integer ) {
		cg.fallEffectTime = 0;
		cg.fallEffectRebounceTime = 0;
		return;
	}

	if( cg.fallEffectTime > cg.time ) {
		cg.fallEffectRebounceTime = 0;
	}

	bounceTime += 200;
	clamp_high( bounceTime, 400 );

	cg.fallEffectTime = cg.time + bounceTime;
	if( cg.fallEffectRebounceTime ) {
		cg.fallEffectRebounceTime = cg.time - ( ( cg.time - cg.fallEffectRebounceTime ) * 0.5 );
	} else {
		cg.fallEffectRebounceTime = cg.time;
	}
}

void CG_ResetColorBlend( void ) {
	memset( cg.colorblends, 0, sizeof( cg.colorblends ) );
}

void CG_StartColorBlendEffect( float r, float g, float b, float a, int time ) {
	int i, bnum = -1;

	if( a <= 0.0f || time <= 0 ) {
		return;
	}

	//find first free colorblend spot, or the one closer to be finished
	for( i = 0; i < MAX_COLORBLENDS; i++ ) {
		if( cg.time > cg.colorblends[i].timestamp + cg.colorblends[i].blendtime ) {
			bnum = i;
			break;
		}
	}

	// all in use. Choose the closer to be finished
	if( bnum == -1 ) {
		int remaintime;
		int best = ( cg.colorblends[0].timestamp + cg.colorblends[0].blendtime ) - cg.time;
		bnum = 0;
		for( i = 1; i < MAX_COLORBLENDS; i++ ) {
			remaintime = ( cg.colorblends[i].timestamp + cg.colorblends[i].blendtime ) - cg.time;
			if( remaintime < best ) {
				best = remaintime;
				bnum = i;
			}
		}
	}

	// assign the color blend
	cg.colorblends[bnum].blend[0] = r;
	cg.colorblends[bnum].blend[1] = g;
	cg.colorblends[bnum].blend[2] = b;
	cg.colorblends[bnum].blend[3] = a;

	cg.colorblends[bnum].timestamp = cg.time;
	cg.colorblends[bnum].blendtime = time;
}

void CG_DamageIndicatorAdd( int damage, const vec3_t dir ) {
	int i;
	int64_t damageTime;
	vec3_t playerAngles;
	mat3_t playerAxis;

// epsilons are 30 degrees
#define INDICATOR_EPSILON 0.5f
#define INDICATOR_EPSILON_UP 0.85f
#define TOP_BLEND 0
#define RIGHT_BLEND 1
#define BOTTOM_BLEND 2
#define LEFT_BLEND 3
	float blends[4];
	float forward, side;

	if( !cg_damage_indicator->integer ) {
		return;
	}

	playerAngles[PITCH] = 0;
	playerAngles[YAW] = cg.predictedPlayerState.viewangles[YAW];
	playerAngles[ROLL] = 0;

	Matrix3_FromAngles( playerAngles, playerAxis );

	if( cg_damage_indicator_time->value < 0 ) {
		Cvar_SetValue( "cg_damage_indicator_time", 0 );
	}

	Vector4Set( blends, 0, 0, 0, 0 );
	damageTime = damage * cg_damage_indicator_time->value;

	// up and down go distributed equally to all blends and assumed when no dir is given
	if( !dir || VectorCompare( dir, vec3_origin ) || cg_damage_indicator->integer == 2 || GS_Instagib() ||
		( fabs( DotProduct( dir, &playerAxis[AXIS_UP] ) ) > INDICATOR_EPSILON_UP ) ) {
		blends[RIGHT_BLEND] += damageTime;
		blends[LEFT_BLEND] += damageTime;
		blends[TOP_BLEND] += damageTime;
		blends[BOTTOM_BLEND] += damageTime;
	} else {
		side = DotProduct( dir, &playerAxis[AXIS_RIGHT] );
		if( side > INDICATOR_EPSILON ) {
			blends[LEFT_BLEND] += damageTime;
		} else if( side < -INDICATOR_EPSILON ) {
			blends[RIGHT_BLEND] += damageTime;
		}

		forward = DotProduct( dir, &playerAxis[AXIS_FORWARD] );
		if( forward > INDICATOR_EPSILON ) {
			blends[BOTTOM_BLEND] += damageTime;
		} else if( forward < -INDICATOR_EPSILON ) {
			blends[TOP_BLEND] += damageTime;
		}
	}

	for( i = 0; i < 4; i++ ) {
		if( cg.damageBlends[i] < cg.time + blends[i] ) {
			cg.damageBlends[i] = cg.time + blends[i];
		}
	}
#undef TOP_BLEND
#undef RIGHT_BLEND
#undef BOTTOM_BLEND
#undef LEFT_BLEND
#undef INDICATOR_EPSILON
#undef INDICATOR_EPSILON_UP
}

void CG_ResetDamageIndicator( void ) {
	int i;

	for( i = 0; i < 4; i++ )
		cg.damageBlends[i] = 0;
}


void CG_AddEntityToScene( entity_t *ent, DrawSceneRequest *drawSceneRequest ) {
	if( ent->model && ( !ent->boneposes || !ent->oldboneposes ) ) {
		if( R_SkeletalGetNumBones( ent->model, NULL ) ) {
			CG_SetBoneposesForTemporaryEntity( ent );
		}
	}

	drawSceneRequest->addEntity( ent );
}

int CG_SkyPortal( void ) {
	float fov = 0;
	float scale = 0;
	int noents = 0;
	float pitchspeed = 0, yawspeed = 0, rollspeed = 0;
	skyportal_t *sp = &cg.view.refdef.skyportal;

	auto maybeConfigString = cgs.configStrings.getSkyBox();
	if( !maybeConfigString ) {
		return 0;
	}

	if( sscanf( maybeConfigString->data(), "%f %f %f %f %f %i %f %f %f",
				&sp->vieworg[0], &sp->vieworg[1], &sp->vieworg[2], &fov, &scale,
				&noents,
				&pitchspeed, &yawspeed, &rollspeed ) >= 3 ) {
		float off = cg.view.refdef.time * 0.001f;

		sp->fov = fov;
		sp->noEnts = ( noents ? true : false );
		sp->scale = scale ? 1.0f / scale : 0;
		VectorSet( sp->viewanglesOffset, anglemod( off * pitchspeed ), anglemod( off * yawspeed ), anglemod( off * rollspeed ) );
		return RDF_SKYPORTALINVIEW;
	}

	return 0;
}

static int CG_RenderFlags( void ) {
	int rdflags, contents;

	rdflags = 0;

	// set the RDF_UNDERWATER and RDF_CROSSINGWATER bitflags
	contents = CG_PointContents( cg.view.origin );
	if( contents & MASK_WATER ) {
		rdflags |= RDF_UNDERWATER;

		// undewater, check above
		contents = CG_PointContents( tv( cg.view.origin[0], cg.view.origin[1], cg.view.origin[2] + 9 ) );
		if( !( contents & MASK_WATER ) ) {
			rdflags |= RDF_CROSSINGWATER;
		}
	} else {
		// look down a bit
		contents = CG_PointContents( tv( cg.view.origin[0], cg.view.origin[1], cg.view.origin[2] - 9 ) );
		if( contents & MASK_WATER ) {
			rdflags |= RDF_CROSSINGWATER;
		}
	}

	if( cg.oldAreabits ) {
		rdflags |= RDF_OLDAREABITS;
	}

	if( cg.portalInView ) {
		rdflags |= RDF_PORTALINVIEW;
	}

	if( cg_outlineWorld->integer ) {
		rdflags |= RDF_WORLDOUTLINES;
	}

	rdflags |= CG_SkyPortal();

	return rdflags;
}

static void CG_InterpolatePlayerState( player_state_t *playerState ) {
	int i;
	player_state_t *ps, *ops;
	bool teleported;

	ps = &cg.frame.playerState;
	ops = &cg.oldFrame.playerState;

	*playerState = *ps;

	teleported = ( ps->pmove.pm_flags & PMF_TIME_TELEPORT ) ? true : false;

	if( abs( (int)( ops->pmove.origin[0] - ps->pmove.origin[0] ) ) > 256
		|| abs( (int)( ops->pmove.origin[1] - ps->pmove.origin[1] ) ) > 256
		|| abs( (int)( ops->pmove.origin[2] - ps->pmove.origin[2] ) ) > 256 ) {
		teleported = true;
	}

	// if the player entity was teleported this frame use the final position
	if( !teleported ) {
		for( i = 0; i < 3; i++ ) {
			playerState->pmove.origin[i] = ops->pmove.origin[i] + cg.lerpfrac * ( ps->pmove.origin[i] - ops->pmove.origin[i] );
			playerState->pmove.velocity[i] = ops->pmove.velocity[i] + cg.lerpfrac * ( ps->pmove.velocity[i] - ops->pmove.velocity[i] );
			playerState->viewangles[i] = LerpAngle( ops->viewangles[i], ps->viewangles[i], cg.lerpfrac );
		}
	}

	// interpolate fov and viewheight
	if( !teleported ) {
		playerState->viewheight = ops->viewheight + cg.lerpfrac * ( ps->viewheight - ops->viewheight );
		playerState->pmove.stats[PM_STAT_ZOOMTIME] = ops->pmove.stats[PM_STAT_ZOOMTIME] + cg.lerpfrac * ( ps->pmove.stats[PM_STAT_ZOOMTIME] - ops->pmove.stats[PM_STAT_ZOOMTIME] );
	}
}

static void CG_ThirdPersonOffsetView( cg_viewdef_t *view ) {
	float dist, f, r;
	vec3_t dest, stop;
	vec3_t chase_dest;
	trace_t trace;
	vec3_t mins = { -4, -4, -4 };
	vec3_t maxs = { 4, 4, 4 };

	if( !cg_thirdPersonAngle || !cg_thirdPersonRange ) {
		cg_thirdPersonAngle = Cvar_Get( "cg_thirdPersonAngle", "0", CVAR_ARCHIVE );
		cg_thirdPersonRange = Cvar_Get( "cg_thirdPersonRange", "70", CVAR_ARCHIVE );
	}

	// calc exact destination
	VectorCopy( view->origin, chase_dest );
	r = DEG2RAD( cg_thirdPersonAngle->value );
	f = -cos( r );
	r = -sin( r );
	VectorMA( chase_dest, cg_thirdPersonRange->value * f, &view->axis[AXIS_FORWARD], chase_dest );
	VectorMA( chase_dest, cg_thirdPersonRange->value * r, &view->axis[AXIS_RIGHT], chase_dest );
	chase_dest[2] += 8;

	// find the spot the player is looking at
	VectorMA( view->origin, 512, &view->axis[AXIS_FORWARD], dest );
	CG_Trace( &trace, view->origin, mins, maxs, dest, view->POVent, MASK_SOLID );

	// calculate pitch to look at the same spot from camera
	VectorSubtract( trace.endpos, view->origin, stop );
	dist = sqrt( stop[0] * stop[0] + stop[1] * stop[1] );
	if( dist < 1 ) {
		dist = 1;
	}
	view->angles[PITCH] = RAD2DEG( -atan2( stop[2], dist ) );
	view->angles[YAW] -= cg_thirdPersonAngle->value;
	Matrix3_FromAngles( view->angles, view->axis );

	// move towards destination
	CG_Trace( &trace, view->origin, mins, maxs, chase_dest, view->POVent, MASK_SOLID );

	if( trace.fraction != 1.0 ) {
		VectorCopy( trace.endpos, stop );
		stop[2] += ( 1.0 - trace.fraction ) * 32;
		CG_Trace( &trace, view->origin, mins, maxs, stop, view->POVent, MASK_SOLID );
		VectorCopy( trace.endpos, chase_dest );
	}

	VectorCopy( chase_dest, view->origin );
}

void CG_ViewSmoothPredictedSteps( vec3_t vieworg ) {
	int timeDelta;

	// smooth out stair climbing
	timeDelta = cg.realTime - cg.predictedStepTime;
	if( timeDelta < PREDICTED_STEP_TIME ) {
		vieworg[2] -= cg.predictedStep * ( PREDICTED_STEP_TIME - timeDelta ) / PREDICTED_STEP_TIME;
	}
}

float CG_ViewSmoothFallKick( void ) {
	// fallkick offset
	if( cg.fallEffectTime > cg.time ) {
		float fallfrac = (float)( cg.time - cg.fallEffectRebounceTime ) / (float)( cg.fallEffectTime - cg.fallEffectRebounceTime );
		float fallkick = -1.0f * sin( DEG2RAD( fallfrac * 180 ) ) * ( ( cg.fallEffectTime - cg.fallEffectRebounceTime ) * 0.01f );
		return fallkick;
	} else {
		cg.fallEffectTime = cg.fallEffectRebounceTime = 0;
	}
	return 0.0f;
}

bool CG_SwitchChaseCamMode( void ) {
	bool chasecam = ( cg.frame.playerState.pmove.pm_type == PM_CHASECAM )
					&& ( cg.frame.playerState.POVnum != (unsigned)( cgs.playerNum + 1 ) );
	bool realSpec = cgs.demoPlaying || ISREALSPECTATOR();

	if( ( cg.frame.multipov || chasecam ) && !CG_DemoCam_IsFree() ) {
		if( chasecam ) {
			if( realSpec ) {
				if( ++chaseCam.mode >= CAM_MODES ) {
					// if exceeds the cycle, start free fly
					CL_Cmd_ExecuteNow( "camswitch" );
					chaseCam.mode = 0;
				}
				return true;
			}
			return false;
		}

		chaseCam.mode = ( ( chaseCam.mode != CAM_THIRDPERSON ) ? CAM_THIRDPERSON : CAM_INEYES );
		return true;
	}

	if( realSpec && ( CG_DemoCam_IsFree() || cg.frame.playerState.pmove.pm_type == PM_SPECTATOR ) ) {
		CL_Cmd_ExecuteNow( "camswitch" );
		return true;
	}

	return false;
}

void CG_ClearChaseCam() {
	memset( &chaseCam, 0, sizeof( chaseCam ) );
}

static void CG_UpdateChaseCam( void ) {
	bool chasecam = ( cg.frame.playerState.pmove.pm_type == PM_CHASECAM )
					&& ( cg.frame.playerState.POVnum != (unsigned)( cgs.playerNum + 1 ) );

	if( !( cg.frame.multipov || chasecam ) || CG_DemoCam_IsFree() ) {
		chaseCam.mode = CAM_INEYES;
	}

	if( cg.time > chaseCam.cmd_mode_delay ) {
		const int delay = 250;

		usercmd_t cmd;
		NET_GetUserCmd( NET_GetCurrentUserCmdNum() - 1, &cmd );

		if( cmd.buttons & BUTTON_ATTACK ) {
			if( CG_SwitchChaseCamMode() ) {
				chaseCam.cmd_mode_delay = cg.time + delay;
			}
		}

		int chaseStep = 0;
		if( cmd.upmove > 0 || cmd.buttons & BUTTON_SPECIAL ) {
			chaseStep = 1;
		} else if( cmd.upmove < 0 ) {
			chaseStep = -1;
		}
		if( chaseStep ) {
			if( CG_ChaseStep( chaseStep ) ) {
				chaseCam.cmd_mode_delay = cg.time + delay;
			}
		}
	}
}

static void CG_SetupViewDef( cg_viewdef_t *view, int type ) {
	memset( view, 0, sizeof( cg_viewdef_t ) );

	//
	// VIEW SETTINGS
	//

	view->type = type;

	if( view->type == VIEWDEF_PLAYERVIEW ) {
		view->POVent = cg.frame.playerState.POVnum;

		view->draw2D = true;

		// set up third-person
		if( cgs.demoPlaying ) {
			view->thirdperson = CG_DemoCam_GetThirdPerson();
		} else if( chaseCam.mode == CAM_THIRDPERSON ) {
			view->thirdperson = true;
		} else {
			view->thirdperson = ( cg_thirdPerson->integer != 0 );
		}

		if( cg_entities[view->POVent].serverFrame != cg.frame.serverFrame ) {
			view->thirdperson = false;
		}

		// check for drawing gun
		if( !view->thirdperson && view->POVent > 0 && view->POVent <= gs.maxclients ) {
			if( ( cg_entities[view->POVent].serverFrame == cg.frame.serverFrame ) &&
				( cg_entities[view->POVent].current.weapon != 0 ) ) {
				view->drawWeapon = ( cg_gun->integer != 0 ) && ( cg_gun_alpha->value > 0 );
			}
		}

		// check for chase cams
		if( !( cg.frame.playerState.pmove.pm_flags & PMF_NO_PREDICTION ) ) {
			if( (unsigned)view->POVent == cgs.playerNum + 1 ) {
				if( cg_predict->integer && !cgs.demoPlaying ) {
					view->playerPrediction = true;
				}
			}
		}
	} else if( view->type == VIEWDEF_CAMERA ) {
		CG_DemoCam_GetViewDef( view );
	} else {
		Com_Error( ERR_DROP, "CG_SetupView: Invalid view type %i\n", view->type );
	}

	//
	// SETUP REFDEF FOR THE VIEW SETTINGS
	//

	if( view->type == VIEWDEF_PLAYERVIEW ) {
		int i;
		vec3_t viewoffset;

		if( view->playerPrediction ) {
			CG_PredictMovement();

			// fixme: crouching is predicted now, but it looks very ugly
			VectorSet( viewoffset, 0.0f, 0.0f, cg.predictedPlayerState.viewheight );

			for( i = 0; i < 3; i++ ) {
				view->origin[i] = cg.predictedPlayerState.pmove.origin[i] + viewoffset[i] - ( 1.0f - cg.lerpfrac ) * cg.predictionError[i];
				view->angles[i] = cg.predictedPlayerState.viewangles[i];
			}

			CG_ViewSmoothPredictedSteps( view->origin ); // smooth out stair climbing

			if( cg_viewBob->integer && !cg_thirdPerson->integer ) {
				view->origin[2] += CG_ViewSmoothFallKick() * 6.5f;
			}
		} else {
			cg.predictingTimeStamp = cg.time;
			cg.predictFrom = 0;

			// we don't run prediction, but we still set cg.predictedPlayerState with the interpolation
			CG_InterpolatePlayerState( &cg.predictedPlayerState );

			VectorSet( viewoffset, 0.0f, 0.0f, cg.predictedPlayerState.viewheight );

			VectorAdd( cg.predictedPlayerState.pmove.origin, viewoffset, view->origin );
			VectorCopy( cg.predictedPlayerState.viewangles, view->angles );
		}

		view->refdef.fov_x = CG_CalcViewFov();

		CG_CalcViewBob();

		VectorCopy( cg.predictedPlayerState.pmove.velocity, view->velocity );
	} else if( view->type == VIEWDEF_CAMERA ) {
		view->refdef.fov_x = CG_DemoCam_GetOrientation( view->origin, view->angles, view->velocity );
	}

	Matrix3_FromAngles( view->angles, view->axis );

	// view rectangle size
	view->refdef.x = scr_vrect.x;
	view->refdef.y = scr_vrect.y;
	view->refdef.width = scr_vrect.width;
	view->refdef.height = scr_vrect.height;
	view->refdef.time = cg.time;
	view->refdef.areabits = cg.frame.areabits;
	view->refdef.scissor_x = scr_vrect.x;
	view->refdef.scissor_y = scr_vrect.y;
	view->refdef.scissor_width = scr_vrect.width;
	view->refdef.scissor_height = scr_vrect.height;

	view->refdef.fov_y = CalcFov( view->refdef.fov_x, view->refdef.width, view->refdef.height );

	AdjustFov( &view->refdef.fov_x, &view->refdef.fov_y, view->refdef.width, view->refdef.height, false );

	view->fracDistFOV = tan( view->refdef.fov_x * ( M_PI / 180 ) * 0.5f );

	view->refdef.minLight = 0.3f;

	if( view->thirdperson ) {
		CG_ThirdPersonOffsetView( view );
	}

	if( !view->playerPrediction ) {
		cg.predictedWeaponSwitch = 0;
	}

	VectorCopy( cg.view.origin, view->refdef.vieworg );
	Matrix3_Copy( cg.view.axis, view->refdef.viewaxis );
	VectorInverse( &view->refdef.viewaxis[AXIS_RIGHT] );

	view->refdef.colorCorrection = NULL;
	if( cg_colorCorrection->integer ) {
		int colorCorrection = GS_ColorCorrection();
		if( ( colorCorrection > 0 ) && ( colorCorrection < MAX_IMAGES ) ) {
			view->refdef.colorCorrection = cgs.imagePrecache[colorCorrection];
		}
	}
}

#define WAVE_AMPLITUDE  0.015   // [0..1]
#define WAVE_FREQUENCY  0.6     // [0..1]
void CG_RenderView( int frameTime, int realFrameTime, int64_t realTime, int64_t serverTime, unsigned extrapolationTime ) {
	refdef_t *rd = &cg.view.refdef;

	// update time
	cg.realTime = realTime;
	cg.frameTime = frameTime;
	cg.realFrameTime = realFrameTime;
	cg.frameCount++;
	cg.time = serverTime;

	if( !cgs.precacheDone || !cg.frame.valid ) {
		CG_Precache();
		return;
	}

	{
		int snapTime = ( cg.frame.serverTime - cg.oldFrame.serverTime );

		if( !snapTime ) {
			snapTime = cgs.snapFrameTime;
		}

		// moved this from CG_Init here
		cgs.extrapolationTime = extrapolationTime;

		if( cg.oldFrame.serverTime == cg.frame.serverTime ) {
			cg.lerpfrac = 1.0f;
		} else {
			cg.lerpfrac = ( (double)( cg.time - cgs.extrapolationTime ) - (double)cg.oldFrame.serverTime ) / (double)snapTime;
		}

		if( cgs.extrapolationTime ) {
			cg.xerpTime = 0.001f * ( (double)cg.time - (double)cg.frame.serverTime );
			cg.oldXerpTime = 0.001f * ( (double)cg.time - (double)cg.oldFrame.serverTime );

			if( cg.time >= cg.frame.serverTime ) {
				cg.xerpSmoothFrac = (double)( cg.time - cg.frame.serverTime ) / (double)( cgs.extrapolationTime );
				Q_clamp( cg.xerpSmoothFrac, 0.0f, 1.0f );
			} else {
				cg.xerpSmoothFrac = (double)( cg.frame.serverTime - cg.time ) / (double)( cgs.extrapolationTime );
				Q_clamp( cg.xerpSmoothFrac, -1.0f, 0.0f );
				cg.xerpSmoothFrac = 1.0f - cg.xerpSmoothFrac;
			}

			clamp_low( cg.xerpTime, -( cgs.extrapolationTime * 0.001f ) );

			//clamp( cg.xerpTime, -( cgs.extrapolationTime * 0.001f ), ( cgs.extrapolationTime * 0.001f ) );
			//clamp( cg.oldXerpTime, 0, ( ( snapTime + cgs.extrapolationTime ) * 0.001f ) );
		} else {
			cg.xerpTime = 0.0f;
			cg.xerpSmoothFrac = 0.0f;
		}
	}

	if( cg_showClamp->integer ) {
		if( cg.lerpfrac > 1.0f ) {
			Com_Printf( "high clamp %f\n", cg.lerpfrac );
		} else if( cg.lerpfrac < 0.0f ) {
			Com_Printf( "low clamp  %f\n", cg.lerpfrac );
		}
	}

	Q_clamp( cg.lerpfrac, 0.0f, 1.0f );

	if( cgs.configStrings.getWorldModel() == std::nullopt ) {
		CG_AddLocalSounds();

		R_DrawStretchPic( 0, 0, cgs.vidWidth, cgs.vidHeight, 0, 0, 1, 1, colorBlack, cgs.shaderWhite );

		SoundSystem::instance()->updateListener( vec3_origin, vec3_origin, axis_identity );

		return;
	}

	// bring up the game menu after reconnecting
	if( !cgs.demoPlaying ) {
		if( ISREALSPECTATOR() && !cg.firstFrame ) {
			if( !cgs.gameMenuRequested ) {
				CL_Cmd_ExecuteNow( "gamemenu\n" );
			}
			cgs.gameMenuRequested = true;
		}
	}

	if( !cg.viewFrameCount ) {
		cg.firstViewRealTime = cg.realTime;
	}

	if( cg_fov->modified ) {
		if( cg_fov->value < MIN_FOV ) {
			Cvar_ForceSet( cg_fov->name, STR_TOSTR( MIN_FOV ) );
		} else if( cg_fov->value > MAX_FOV ) {
			Cvar_ForceSet( cg_fov->name, STR_TOSTR( MAX_FOV ) );
		}
		cg_fov->modified = false;
	}

	if( cg_zoomfov->modified ) {
		if( cg_zoomfov->value < MIN_ZOOMFOV ) {
			Cvar_ForceSet( cg_zoomfov->name, STR_TOSTR( MIN_ZOOMFOV ) );
		} else if( cg_zoomfov->value > MAX_ZOOMFOV ) {
			Cvar_ForceSet( cg_zoomfov->name, STR_TOSTR( MAX_ZOOMFOV ) );
		}
		cg_zoomfov->modified = false;
	}

	CG_FlashGameWindow(); // notify player of important game events

	CG_CalcVrect(); // find sizes of the 3d drawing screen

	CG_UpdateChaseCam();

	CG_RunLightStyles();

	CG_ClearFragmentedDecals();

	if( CG_DemoCam_Update() ) {
		CG_SetupViewDef( &cg.view, CG_DemoCam_GetViewType() );
	} else {
		CG_SetupViewDef( &cg.view, VIEWDEF_PLAYERVIEW );
	}

	CG_LerpEntities();  // interpolate packet entities positions

	CG_CalcViewWeapon( &cg.weapon );

	CG_FireEvents( false );

	DrawSceneRequest *drawSceneRequest = CreateDrawSceneRequest( cg.view.refdef );

	CG_AddEntities( drawSceneRequest );
	CG_AddViewWeapon( &cg.weapon, drawSceneRequest );

	cg.effectsSystem.simulateFrameAndSubmit( cg.time, drawSceneRequest );
	// Run the particle system last (don't submit flocks that could be invalidated by the effect system this frame)
	cg.particleSystem.runFrame( cg.time, drawSceneRequest );
	cg.polyEffectsSystem.simulateFrameAndSubmit( cg.time, drawSceneRequest );
	cg.simulatedHullsSystem.simulateFrameAndSubmit( cg.time, drawSceneRequest );

	AnglesToAxis( cg.view.angles, rd->viewaxis );

	rd->rdflags = CG_RenderFlags();

	// warp if underwater
	if( rd->rdflags & RDF_UNDERWATER ) {
		float phase = rd->time * 0.001 * WAVE_FREQUENCY * M_TWOPI;
		float v = WAVE_AMPLITUDE * ( sin( phase ) - 1.0 ) + 1;
		rd->fov_x *= v;
		rd->fov_y *= v;
	}

	CG_AddLocalSounds();
	CG_SetSceneTeamColors(); // update the team colors in the renderer

	SubmitDrawSceneRequest( drawSceneRequest );

	cg.oldAreabits = true;

	SoundSystem::instance()->updateListener( cg.view.origin, cg.view.velocity, cg.view.axis );

	CG_Draw2D();

	CG_ResetTemporaryBoneposesCache(); // clear for next frame

	cg.viewFrameCount++;
}

void CG_CalcVrect( void ) {
	int size;

	// bound viewsize
	if( cg_viewSize->integer < 40 ) {
		Cvar_Set( cg_viewSize->name, "40" );
	} else if( cg_viewSize->integer > 100 ) {
		Cvar_Set( cg_viewSize->name, "100" );
	}

	size = cg_viewSize->integer;

	if( size == 100 ) {
		scr_vrect.width = cgs.vidWidth;
		scr_vrect.height = cgs.vidHeight;
		scr_vrect.x = scr_vrect.y = 0;
	} else {
		scr_vrect.width = cgs.vidWidth * size / 100;
		scr_vrect.width &= ~1;

		scr_vrect.height = cgs.vidHeight * size / 100;
		scr_vrect.height &= ~1;

		scr_vrect.x = ( cgs.vidWidth - scr_vrect.width ) / 2;
		scr_vrect.y = ( cgs.vidHeight - scr_vrect.height ) / 2;
	}
}

void CG_DrawRSpeeds( int x, int y, int align, struct qfontface_s *font, const vec4_t color ) {
	char msg[1024];

	RF_GetSpeedsMessage( msg, sizeof( msg ) );

	if( msg[0] ) {
		int height;
		const char *p, *start, *end;

		height = SCR_FontHeight( font );

		p = start = msg;
		do {
			end = strchr( p, '\n' );
			if( end ) {
				msg[end - start] = '\0';
			}

			SCR_DrawString( x, y, align,
							p, font, color );
			y += height;

			if( end ) {
				p = end + 1;
			} else {
				break;
			}
		} while( 1 );
	}
}

void CG_LoadingString( const char *str ) {
	Q_strncpyz( cgs.loadingstring, str, sizeof( cgs.loadingstring ) );
}

/*
* CG_LoadingItemName
*
* Allow at least one item per frame to be precached.
* Stop accepting new precaches after the timelimit for this frame has been reached.
*/
bool CG_LoadingItemName( const char *str ) {
	if( cgs.precacheCount > cgs.precacheStart && ( Sys_Milliseconds() > cgs.precacheStartMsec + 33 ) ) {
		return false;
	}
	cgs.precacheCount++;
	return true;
}

static void CG_AddBlend( float r, float g, float b, float a, float *v_blend ) {
	float a2, a3;

	if( a <= 0 ) {
		return;
	}
	a2 = v_blend[3] + ( 1 - v_blend[3] ) * a; // new total alpha
	a3 = v_blend[3] / a2; // fraction of color from old

	v_blend[0] = v_blend[0] * a3 + r * ( 1 - a3 );
	v_blend[1] = v_blend[1] * a3 + g * ( 1 - a3 );
	v_blend[2] = v_blend[2] * a3 + b * ( 1 - a3 );
	v_blend[3] = a2;
}

static void CG_CalcColorBlend( float *color ) {
	float time;
	float uptime;
	float delta;
	int i, contents;

	//clear old values
	for( i = 0; i < 4; i++ )
		color[i] = 0.0f;

	// Add colorblend based on world position
	contents = CG_PointContents( cg.view.origin );
	if( contents & CONTENTS_WATER ) {
		CG_AddBlend( 0.0f, 0.1f, 8.0f, 0.2f, color );
	}
	if( contents & CONTENTS_LAVA ) {
		CG_AddBlend( 1.0f, 0.3f, 0.0f, 0.6f, color );
	}
	if( contents & CONTENTS_SLIME ) {
		CG_AddBlend( 0.0f, 0.1f, 0.05f, 0.6f, color );
	}

	// Add colorblends from sfx
	for( i = 0; i < MAX_COLORBLENDS; i++ ) {
		if( cg.time > cg.colorblends[i].timestamp + cg.colorblends[i].blendtime ) {
			continue;
		}

		time = (float)( ( cg.colorblends[i].timestamp + cg.colorblends[i].blendtime ) - cg.time );
		uptime = ( (float)cg.colorblends[i].blendtime ) * 0.5f;
		delta = 1.0f - ( fabs( time - uptime ) / uptime );
		if( delta <= 0.0f ) {
			continue;
		}
		if( delta > 1.0f ) {
			delta = 1.0f;
		}

		CG_AddBlend( cg.colorblends[i].blend[0],
					 cg.colorblends[i].blend[1],
					 cg.colorblends[i].blend[2],
					 cg.colorblends[i].blend[3] * delta,
					 color );
	}
}

static void CG_SCRDrawViewBlend( void ) {
	vec4_t colorblend;

	if( !cg_showViewBlends->integer ) {
		return;
	}

	CG_CalcColorBlend( colorblend );
	if( colorblend[3] < 0.01f ) {
		return;
	}

	R_DrawStretchPic( 0, 0, cgs.vidWidth, cgs.vidHeight, 0, 0, 1, 1, colorblend, cgs.shaderWhite );
}

void CG_ClearPointedNum( void ) {
	cg.pointedNum = 0;
	cg.pointRemoveTime = 0;
	cg.pointedHealth = 0;
	cg.pointedArmor = 0;
}

static void CG_UpdatePointedNum( void ) {
	// disable cases
	if( CG_IsScoreboardShown() || cg.view.thirdperson || cg.view.type != VIEWDEF_PLAYERVIEW || !cg_showPointedPlayer->integer ) {
		CG_ClearPointedNum();
		return;
	}

	if( cg.predictedPlayerState.stats[STAT_POINTED_PLAYER] ) {
		bool mega = false;

		cg.pointedNum = cg.predictedPlayerState.stats[STAT_POINTED_PLAYER];
		cg.pointRemoveTime = cg.time + 150;

		cg.pointedHealth = 3.2 * ( cg.predictedPlayerState.stats[STAT_POINTED_TEAMPLAYER] & 0x1F );
		mega = cg.predictedPlayerState.stats[STAT_POINTED_TEAMPLAYER] & 0x20 ? true : false;
		cg.pointedArmor = 5 * ( cg.predictedPlayerState.stats[STAT_POINTED_TEAMPLAYER] >> 6 & 0x3F );
		if( mega ) {
			cg.pointedHealth += 100;
			if( cg.pointedHealth > 200 ) {
				cg.pointedHealth = 200;
			}
		}
	}

	if( cg.pointRemoveTime <= cg.time ) {
		CG_ClearPointedNum();
	}

	if( cg.pointedNum && cg_showPointedPlayer->integer == 2 ) {
		if( cg_entities[cg.pointedNum].current.team != cg.predictedPlayerState.stats[STAT_TEAM] ) {
			CG_ClearPointedNum();
		}
	}
}

int CG_HorizontalAlignForWidth( const int x, int align, int width ) {
	int nx = x;

	if( align % 3 == 0 ) { // left
		nx = x;
	}
	if( align % 3 == 1 ) { // center
		nx = x - width / 2;
	}
	if( align % 3 == 2 ) { // right
		nx = x - width;
	}

	return nx;
}

int CG_VerticalAlignForHeight( const int y, int align, int height ) {
	int ny = y;

	if( align / 3 == 0 ) { // top
		ny = y;
	} else if( align / 3 == 1 ) { // middle
		ny = y - height / 2;
	} else if( align / 3 == 2 ) { // bottom
		ny = y - height;
	}

	return ny;
}

static void CG_DrawHUDRect( int x, int y, int align, int w, int h, int val, int maxval, vec4_t color, struct shader_s *shader ) {
	float frac;
	vec2_t tc[2];

	if( val < 1 || maxval < 1 || w < 1 || h < 1 ) {
		return;
	}

	if( !shader ) {
		shader = cgs.shaderWhite;
	}

	if( val >= maxval ) {
		frac = 1.0f;
	} else {
		frac = (float)val / (float)maxval;
	}

	tc[0][0] = 0.0f;
	tc[0][1] = 1.0f;
	tc[1][0] = 0.0f;
	tc[1][1] = 1.0f;
	if( h > w ) {
		h = (int)( (float)h * frac + 0.5 );
		if( align / 3 == 0 ) { // top
			tc[1][1] = 1.0f * frac;
		} else if( align / 3 == 1 ) {   // middle
			tc[1][0] = ( 1.0f - ( 1.0f * frac ) ) * 0.5f;
			tc[1][1] = ( 1.0f * frac ) * 0.5f;
		} else if( align / 3 == 2 ) {   // bottom
			tc[1][0] = 1.0f - ( 1.0f * frac );
		}
	} else {
		w = (int)( (float)w * frac + 0.5 );
		if( align % 3 == 0 ) { // left
			tc[0][1] = 1.0f * frac;
		}
		if( align % 3 == 1 ) { // center
			tc[0][0] = ( 1.0f - ( 1.0f * frac ) ) * 0.5f;
			tc[0][1] = ( 1.0f * frac ) * 0.5f;
		}
		if( align % 3 == 2 ) { // right
			tc[0][0] = 1.0f - ( 1.0f * frac );
		}
	}

	x = CG_HorizontalAlignForWidth( x, align, w );
	y = CG_VerticalAlignForHeight( y, align, h );

	R_DrawStretchPic( x, y, w, h, tc[0][0], tc[1][0], tc[0][1], tc[1][1], color, shader );
}

static void CG_DrawTeamMates() {
	centity_t *cent;
	vec3_t dir, drawOrigin;
	vec2_t coords;
	vec4_t color;
	int i;
	int pic_size = 18 * cgs.vidHeight / 600;

	if( !cg_showTeamInfo->integer ) {
		return;
	}

	if( cg.predictedPlayerState.stats[STAT_TEAM] < TEAM_ALPHA ) {
		return;
	}

	for( i = 0; i < gs.maxclients; i++ ) {
		trace_t trace;

		if( !cgs.clientInfo[i].name[0] || ISVIEWERENTITY( i + 1 ) ) {
			continue;
		}

		cent = &cg_entities[i + 1];
		if( cent->serverFrame != cg.frame.serverFrame ) {
			continue;
		}

		if( cent->current.team != cg.predictedPlayerState.stats[STAT_TEAM] ) {
			continue;
		}

		VectorSet( drawOrigin, cent->ent.origin[0], cent->ent.origin[1], cent->ent.origin[2] + playerbox_stand_maxs[2] + 16 );
		VectorSubtract( drawOrigin, cg.view.origin, dir );

		// ignore, if not in view
		if( DotProduct( dir, &cg.view.axis[AXIS_FORWARD] ) < 0 ) {
			continue;
		}

		if( !cent->current.modelindex || !cent->current.solid ||
			cent->current.solid == SOLID_BMODEL || cent->current.team == TEAM_SPECTATOR ) {
			continue;
		}

		// find the 3d point in 2d screen
		RF_TransformVectorToScreen( &cg.view.refdef, drawOrigin, coords );
		if( ( coords[0] < 0 || coords[0] > cgs.vidWidth ) || ( coords[1] < 0 || coords[1] > cgs.vidHeight ) ) {
			continue;
		}

		CG_Trace( &trace, cg.view.origin, vec3_origin, vec3_origin, cent->ent.origin, cg.predictedPlayerState.POVnum, MASK_OPAQUE );
		if( cg_showTeamInfo->integer == 1 && trace.fraction == 1.0f ) {
			continue;
		}

		coords[0] -= pic_size / 2;
		coords[1] -= pic_size / 2;
		Q_clamp( coords[0], 0, cgs.vidWidth - pic_size );
		Q_clamp( coords[1], 0, cgs.vidHeight - pic_size );

		CG_TeamColor( cg.predictedPlayerState.stats[STAT_TEAM], color );

		shader_s *shader;
		if( cent->current.effects & EF_CARRIER ) {
			shader = cgs.media.shaderTeamCarrierIndicator;
		} else {
			shader = cgs.media.shaderTeamMateIndicator;
		}

		R_DrawStretchPic( coords[0], coords[1], pic_size, pic_size, 0, 0, 1, 1, color, shader );
	}
}

static void CG_DrawPlayerNames() {
	qfontface_s *font = cgs.fontSystemMedium;
	const float *color = colorWhite;
	static vec4_t alphagreen = { 0, 1, 0, 0 }, alphared = { 1, 0, 0, 0 }, alphayellow = { 1, 1, 0, 0 }, alphamagenta = { 1, 0, 1, 1 }, alphagrey = { 0.85, 0.85, 0.85, 1 };
	centity_t *cent;
	vec4_t tmpcolor;
	vec3_t dir, drawOrigin;
	vec2_t coords;
	float dist, fadeFrac;
	trace_t trace;
	int i;

	if( !cg_showPlayerNames->integer && !cg_showPointedPlayer->integer ) {
		return;
	}

	CG_UpdatePointedNum();

	for( i = 0; i < gs.maxclients; i++ ) {
		int pointed_health, pointed_armor;

		if( !cgs.clientInfo[i].name[0] || ISVIEWERENTITY( i + 1 ) ) {
			continue;
		}

		cent = &cg_entities[i + 1];
		if( cent->serverFrame != cg.frame.serverFrame ) {
			continue;
		}

		if( cent->current.effects & EF_PLAYER_HIDENAME ) {
			continue;
		}

		// only show the pointed player
		if( !cg_showPlayerNames->integer && ( cent->current.number != cg.pointedNum ) ) {
			continue;
		}

		if( ( cg_showPlayerNames->integer == 2 ) && ( cent->current.team != cg.predictedPlayerState.stats[STAT_TEAM] ) ) {
			continue;
		}

		if( !cent->current.modelindex || !cent->current.solid ||
			cent->current.solid == SOLID_BMODEL || cent->current.team == TEAM_SPECTATOR ) {
			continue;
		}

		// Kill if behind the view
		VectorSubtract( cent->ent.origin, cg.view.origin, dir );
		dist = VectorNormalize( dir ) * cg.view.fracDistFOV;

		if( DotProduct( dir, &cg.view.axis[AXIS_FORWARD] ) < 0 ) {
			continue;
		}

		Vector4Copy( color, tmpcolor );

		if( cent->current.number != cg.pointedNum ) {
			if( dist > cg_showPlayerNames_zfar->value ) {
				continue;
			}

			fadeFrac = ( cg_showPlayerNames_zfar->value - dist ) / ( cg_showPlayerNames_zfar->value * 0.25f );
			Q_clamp( fadeFrac, 0.0f, 1.0f );

			tmpcolor[3] = cg_showPlayerNames_alpha->value * color[3] * fadeFrac;
		} else {
			fadeFrac = (float)( cg.pointRemoveTime - cg.time ) / 150.0f;
			Q_clamp( fadeFrac, 0.0f, 1.0f );

			tmpcolor[3] = color[3] * fadeFrac;
		}

		if( tmpcolor[3] <= 0.0f ) {
			continue;
		}

		CG_Trace( &trace, cg.view.origin, vec3_origin, vec3_origin, cent->ent.origin, cg.predictedPlayerState.POVnum, MASK_OPAQUE );
		if( trace.fraction < 1.0f && trace.ent != cent->current.number ) {
			continue;
		}

		VectorSet( drawOrigin, cent->ent.origin[0], cent->ent.origin[1], cent->ent.origin[2] + playerbox_stand_maxs[2] + 16 );

		// find the 3d point in 2d screen
		RF_TransformVectorToScreen( &cg.view.refdef, drawOrigin, coords );
		if( ( coords[0] < 0 || coords[0] > cgs.vidWidth ) || ( coords[1] < 0 || coords[1] > cgs.vidHeight ) ) {
			continue;
		}

		SCR_DrawString( coords[0], coords[1], ALIGN_CENTER_BOTTOM, cgs.clientInfo[i].name, font, tmpcolor );

		// if not the pointed player we are done
		if( cent->current.number != cg.pointedNum ) {
			continue;
		}

		pointed_health = cg.pointedHealth;
		pointed_armor = cg.pointedArmor;

		// pointed player hasn't a health value to be drawn, so skip adding the bars
		if( pointed_health && cg_showPlayerNames_barWidth->integer > 0 ) {
			int x, y;
			int barwidth = SCR_strWidth( "_", font, 0 ) * cg_showPlayerNames_barWidth->integer; // size of 8 characters
			int barheight = SCR_FontHeight( font ) * 0.25; // quarter of a character height
			int barseparator = barheight * 0.333;

			alphagreen[3] = alphared[3] = alphayellow[3] = alphamagenta[3] = alphagrey[3] = tmpcolor[3];

			// soften the alpha of the box color
			tmpcolor[3] *= 0.4f;

			// we have to align first, then draw as left top, cause we want the bar to grow from left to right
			x = CG_HorizontalAlignForWidth( coords[0], ALIGN_CENTER_TOP, barwidth );
			y = CG_VerticalAlignForHeight( coords[1], ALIGN_CENTER_TOP, barheight );

			// draw the background box
			CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight * 3, 100, 100, tmpcolor, NULL );

			y += barseparator;

			if( pointed_health > 100 ) {
				alphagreen[3] = alphamagenta[3] = 1.0f;
				CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, 100, 100, alphagreen, NULL );
				CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, pointed_health - 100, 100, alphamagenta, NULL );
				alphagreen[3] = alphamagenta[3] = alphared[3];
			} else {
				if( pointed_health <= 33 ) {
					CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, pointed_health, 100, alphared, NULL );
				} else if( pointed_health <= 66 ) {
					CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, pointed_health, 100, alphayellow, NULL );
				} else {
					CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, pointed_health, 100, alphagreen, NULL );
				}
			}

			if( pointed_armor ) {
				y += barseparator + barheight;
				CG_DrawHUDRect( x, y, ALIGN_LEFT_TOP, barwidth, barheight, pointed_armor, 150, alphagrey, NULL );
			}
		}
	}
}

void CrosshairState::checkValueVar( cvar_t *var, Style style ) {
	if( const wsw::StringView name( var->string ); !name.empty() ) {
		bool found = false;
		for( const wsw::StringView &file: ( style == Strong ? getStrongCrosshairFiles() : getRegularCrosshairFiles() ) ) {
			if( file.equalsIgnoreCase( name ) ) {
				found = true;
				break;
			}
		}
		if( !found ) {
			Cvar_ForceSet( var->name, "" );
		}
	}
}

void CrosshairState::checkSizeVar( cvar_t *var, const SizeProps &sizeProps ) {
	if( var->integer < (int)sizeProps.minSize || var->integer > (int)sizeProps.maxSize ) {
		char buffer[16];
		Cvar_ForceSet( var->name, va_r( buffer, sizeof( buffer ), "%d", (int)( sizeProps.defaultSize ) ) );
	}
}

void CrosshairState::checkColorVar( cvar_s *var, float *cachedColor, int *oldPackedColor ) {
	const int packedColor = COM_ReadColorRGBString( var->string );
	if( packedColor == -1 ) {
		constexpr const char *defaultString = "255 255 255";
		if( !Q_stricmp( var->string, defaultString ) ) {
			Cvar_ForceSet( var->name, defaultString );
		}
	}
	// Update cached color values if their addresses are supplied and the packed value has changed
	// (tracking the packed value allows using cheap comparisons of a single integer)
	if( !oldPackedColor || ( packedColor != *oldPackedColor ) ) {
		if( oldPackedColor ) {
			*oldPackedColor = packedColor;
		}
		if( cachedColor ) {
			float r = 1.0f, g = 1.0f, b = 1.0f;
			if( packedColor != -1 ) {
				constexpr float normalizer = 1.0f / 255.0f;
				r = COLOR_R( packedColor ) * normalizer;
				g = COLOR_G( packedColor ) * normalizer;
				b = COLOR_B( packedColor ) * normalizer;
			}
			Vector4Set( cachedColor, r, g, b, 1.0f );
		}
	}
}

static_assert( WEAP_NONE == 0 && WEAP_GUNBLADE == 1 );
static inline const char *kWeaponNames[WEAP_TOTAL - 1] = {
	"gb", "mg", "rg", "gl", "rl", "pg", "lg", "eb", "sw", "ig"
};

void CrosshairState::initPersistentState() {
	wsw::StaticString<64> varNameBuffer;
	varNameBuffer << "cg_crosshair_"_asView;
	const auto prefixLen = varNameBuffer.length();

	wsw::StaticString<8> sizeStringBuffer;
	(void)sizeStringBuffer.assignf( "%d", kRegularCrosshairSizeProps.defaultSize );

	for( int i = 0; i < WEAP_TOTAL - 1; ++i ) {
		assert( std::strlen( kWeaponNames[i] ) == 2 );
		const wsw::StringView weaponName( kWeaponNames[i], 2 );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << weaponName;
		s_valueVars[i] = Cvar_Get( varNameBuffer.data(), "1", CVAR_ARCHIVE );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "size_"_asView << weaponName;
		s_sizeVars[i] = Cvar_Get( varNameBuffer.data(), sizeStringBuffer.data(), CVAR_ARCHIVE );
		checkSizeVar( s_sizeVars[i], kRegularCrosshairSizeProps );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "color_"_asView << weaponName;
		s_colorVars[i] = Cvar_Get( varNameBuffer.data(), "255 255 255", CVAR_ARCHIVE );
		checkColorVar( s_colorVars[i] );
	}

	cg_crosshair = Cvar_Get( "cg_crosshair", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair, Regular );

	cg_crosshair_size = Cvar_Get( "cg_crosshair_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_size, kRegularCrosshairSizeProps );

	cg_crosshair_color = Cvar_Get( "cg_crosshair_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_color );

	cg_crosshair_strong = Cvar_Get( "cg_crosshair_strong", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair_strong, Strong );

	(void)sizeStringBuffer.assignf( "%d", kStrongCrosshairSizeProps.defaultSize );
	cg_crosshair_strong_size = Cvar_Get( "cg_crosshair_strong_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_strong_size, kStrongCrosshairSizeProps );

	cg_crosshair_strong_color = Cvar_Get( "cg_crosshair_strong_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_strong_color );

	cg_crosshair_damage_color = Cvar_Get( "cg_crosshair_damage_color", "255 0 0", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_damage_color );

	cg_separate_weapon_settings = Cvar_Get( "cg_separate_weapon_settings", "0", CVAR_ARCHIVE );
}

void CrosshairState::updateSharedPart() {
	checkColorVar( cg_crosshair_damage_color, s_damageColor, &s_oldPackedDamageColor );
}

void CrosshairState::update( [[maybe_unused]] unsigned weapon ) {
	assert( weapon > 0 && weapon < WEAP_TOTAL );

	const bool isStrong  = m_style == Strong;
	if( isStrong ) {
		m_sizeVar  = cg_crosshair_strong_size;
		m_colorVar = cg_crosshair_strong_color;
		m_valueVar = cg_crosshair_strong;
	} else {
		if( cg_separate_weapon_settings->integer ) {
			m_sizeVar  = s_sizeVars[weapon - 1];
			m_colorVar = s_colorVars[weapon - 1];
			m_valueVar = s_valueVars[weapon - 1];
		} else {
			m_sizeVar  = cg_crosshair_size;
			m_colorVar = cg_crosshair_color;
			m_valueVar = cg_crosshair;
		}
	}

	checkSizeVar( m_sizeVar, isStrong ? kStrongCrosshairSizeProps : kRegularCrosshairSizeProps );
	checkColorVar( m_colorVar, m_varColor, &m_oldPackedColor );
	checkValueVar( m_valueVar, m_style );

	m_decayTimeLeft = wsw::max( 0, m_decayTimeLeft - cg.frameTime );
}

void CrosshairState::clear() {
	m_decayTimeLeft  = 0;
	m_oldPackedColor = -1;
}

auto CrosshairState::getDrawingColor() -> const float * {
	if( m_decayTimeLeft > 0 ) {
		const float frac = 1.0f - Q_Sqrt( (float) m_decayTimeLeft * m_invDecayTime );
		assert( frac >= 0.0f && frac <= 1.0f );
		VectorLerp( s_damageColor, frac, m_varColor, m_drawColor );
		return m_drawColor;
	}
	return m_varColor;
}

[[nodiscard]]
auto CrosshairState::getDrawingMaterial() -> std::optional<std::tuple<shader_s *, unsigned, unsigned>> {
	if( const wsw::StringView name = wsw::StringView( m_valueVar->string ); !name.empty() ) {
		if( const auto size = (unsigned)m_sizeVar->integer ) {
			const bool isStrong   = m_style == Strong;
			const auto &sizeProps = isStrong ? kStrongCrosshairSizeProps : kRegularCrosshairSizeProps;
			if( size >= sizeProps.minSize && size <= sizeProps.maxSize ) {
				return isStrong ? getStrongCrosshairMaterial( name, size ) : getRegularCrosshairMaterial( name, size );
			}
		}
	}
	return std::nullopt;
}

void CG_ScreenCrosshairDamageUpdate() {
	cg.crosshairState.touchDamageState();
	cg.strongCrosshairState.touchDamageState();
}

static void drawCrosshair( CrosshairState *state ) {
	if( auto maybeMaterialAndDimensions = state->getDrawingMaterial() ) {
		auto [material, width, height] = *maybeMaterialAndDimensions;
		const int x = ( cgs.vidWidth - (int)width ) / 2;
		const int y = ( cgs.vidHeight - (int)height ) / 2;
		R_DrawStretchPic( x, y, (int)width, (int)height, 0, 0, 1, 1, state->getDrawingColor(), material );
	}
}

void CG_UpdateCrosshair() {
	CrosshairState::updateSharedPart();
	if( unsigned weapon = cg.predictedPlayerState.stats[STAT_WEAPON] ) {
		cg.crosshairState.update( weapon );
		cg.strongCrosshairState.update( weapon );
	} else {
		cg.crosshairState.clear();
		cg.strongCrosshairState.clear();
	}
}

void CG_DrawCrosshair() {
	const auto *const playerState = &cg.predictFromPlayerState;
	const auto weapon = playerState->stats[STAT_WEAPON];
	if( !weapon ) {
		return;
	}

	const auto *const firedef = GS_FiredefForPlayerState( playerState, weapon );
	if( !firedef ) {
		return;
	}

	if( firedef->fire_mode == FIRE_MODE_STRONG ) {
		::drawCrosshair( &cg.strongCrosshairState );
	}
	::drawCrosshair( &cg.crosshairState );
}

void CG_Draw2DView( void ) {
	CG_UpdateCrosshair();

	if( !cg.view.draw2D ) {
		return;
	}

	CG_SCRDrawViewBlend();

	if( cg.motd && ( cg.time > cg.motd_time ) ) {
		Q_free(   cg.motd );
		cg.motd = NULL;
	}


	if( cg_showHUD->integer ) {
		if( !CG_IsScoreboardShown() ) {
			CG_DrawTeamMates();
			CG_DrawPlayerNames();
		}

		// TODO: Does it work for chasers?
		if( cg.predictedPlayerState.pmove.pm_type == PM_NORMAL ) {
			if( !wsw::ui::UISystem::instance()->isShown() ) {
				CG_DrawCrosshair();
			}
		}
	}

	CG_DrawRSpeeds( cgs.vidWidth, cgs.vidHeight / 2 + 8 * cgs.vidHeight / 600,
					ALIGN_RIGHT_TOP, cgs.fontSystemSmall, colorWhite );
}

void CG_Draw2D() {
	// TODO: We still have to update some states even if these conditions do not hold
	if( cg_draw2D->integer && cg.view.draw2D ) {
		CG_Draw2DView();
	}
}