/*
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
#include "../qcommon/qcommon.h"
#include "../qcommon/cmdargs.h"
#include "../qcommon/cmdcompat.h"
#include "../client/client.h"

using wsw::operator""_asView;

// Thanks to Xavatar (xavatar2004@hotmail.com) for the path spline implementation

int64_t demo_initial_timestamp;
int64_t demo_time;

static bool CamIsFree;

#define CG_DemoCam_UpdateDemoTime() ( demo_time = cg.time - demo_initial_timestamp )

typedef struct cg_democam_s
{
	int type;
	int64_t timeStamp;
	int trackEnt;
	vec3_t origin;
	vec3_t angles;
	int fov;
	vec3_t tangent;
	vec3_t angles_tangent;
	float speed;
	struct cg_democam_s *next;
} cg_democam_t;

cg_democam_t *cg_cams_headnode = NULL;
cg_democam_t *currentcam, *nextcam;

static vec3_t cam_origin, cam_angles, cam_velocity;
static float cam_fov = 90;
static int cam_viewtype;
static int cam_POVent;
static bool cam_3dPerson;

/*
* CG_DemoCam_GetViewType
*/
int CG_DemoCam_GetViewType( void ) {
	return cam_viewtype;
}

/*
* CG_DemoCam_GetThirdPerson
*/
bool CG_DemoCam_GetThirdPerson( void ) {
	if( !currentcam ) {
		return ( chaseCam.mode == CAM_THIRDPERSON );
	}
	return ( cam_viewtype == VIEWDEF_PLAYERVIEW && cam_3dPerson );
}

/*
* CG_DemoCam_GetViewDef
*/
void CG_DemoCam_GetViewDef( cg_viewdef_t *view ) {
	view->POVent = cam_POVent;
	view->thirdperson = cam_3dPerson;
	view->playerPrediction = false;
	view->drawWeapon = false;
	view->draw2D = false;
}

/*
* CG_DemoCam_GetOrientation
*/
float CG_DemoCam_GetOrientation( vec3_t origin, vec3_t angles, vec3_t velocity ) {
	VectorCopy( cam_angles, angles );
	VectorCopy( cam_origin, origin );
	VectorCopy( cam_velocity, velocity );

	if( !currentcam || !currentcam->fov ) {
		return bound( MIN_FOV, cg_fov->value, MAX_FOV );
	}

	return cam_fov;
}

static short freecam_delta_angles[3];

/*
* CG_DemoCam_FreeFly
*/
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

/*
* CG_Democam_CalcView
*/
static int CG_Democam_CalcView( void ) {
	VectorClear( cam_velocity );
	return VIEWDEF_PLAYERVIEW;
}

/*
* CG_DemoCam_Update
*/
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

/*
* CG_DemoCam_IsFree
*/
bool CG_DemoCam_IsFree( void ) {
	return CamIsFree;
}

/*
* CG_DemoFreeFly_Cmd_f
*/
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

/*
* CG_CamSwitch_Cmd_f
*/
static void CG_CamSwitch_Cmd_f( const CmdArgs & ) {

}

/*
* CG_DemocamInit
*/
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

/*
* CG_DemocamShutdown
*/
void CG_DemocamShutdown( void ) {
	if( !cgs.demoPlaying ) {
		return;
	}

	// remove console commands
	CL_Cmd_Unregister( "demoFreeFly"_asView );
	CL_Cmd_Unregister( "camswitch"_asView );
}

/*
* CG_DemocamReset
*/
void CG_DemocamReset( void ) {
	demo_time = 0;
	demo_initial_timestamp = 0;
}
