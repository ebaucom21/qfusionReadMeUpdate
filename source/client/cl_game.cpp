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

#include "client.h"
#include "../qcommon/asyncstream.h"
#include "../ui/uisystem.h"

static void *cge = nullptr;

static void *module_handle;

static int cg_load_seq = 1;

//======================================================================

// CL_GameModule versions of the CM functions passed to the game module
// they only add sv.cms as the first parameter

//======================================================================

int CG_NumInlineModels( void ) {
	return CM_NumInlineModels( cl.cms );
}

int CG_TransformedPointContents( const vec3_t p, const cmodel_s *cmodel, const vec3_t origin, const vec3_t angles ) {
	return CM_TransformedPointContents( cl.cms, p, cmodel, origin, angles );
}

void CG_TransformedBoxTrace( trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
							 const cmodel_s *cmodel, int brushmask, const vec3_t origin, const vec3_t angles ) {
	CM_TransformedBoxTrace( cl.cms, tr, start, end, mins, maxs, cmodel, brushmask, origin, angles );
}

const cmodel_s *CG_InlineModel( int num ) {
	return CM_InlineModel( cl.cms, num );
}

void CG_InlineModelBounds( const cmodel_s *cmodel, vec3_t mins, vec3_t maxs ) {
	CM_InlineModelBounds( cl.cms, cmodel, mins, maxs );
}

const cmodel_s *CG_ModelForBBox( const vec3_t mins, const vec3_t maxs ) {
	return CM_ModelForBBox( cl.cms, mins, maxs );
}

const cmodel_s *CG_OctagonModelForBBox( const vec3_t mins, const vec3_t maxs ) {
	return CM_OctagonModelForBBox( cl.cms, mins, maxs );
}

bool CG_InPVS( const vec3_t p1, const vec3_t p2 ) {
	return CM_InPVS( cl.cms, p1, p2 );
}

void NET_GetUserCmd( int frame, usercmd_t *cmd ) {
	if( cmd ) {
		if( frame < 0 ) {
			frame = 0;
		}

		*cmd = cl.cmds[frame & CMD_MASK];
	}
}

int NET_GetCurrentUserCmdNum( void ) {
	return cls.ucmdHead;
}

void NET_GetCurrentState( int64_t *incomingAcknowledged, int64_t *outgoingSequence, int64_t *outgoingSent ) {
	if( incomingAcknowledged )
#ifdef TCP_ALLOW_CONNECT
	{ *incomingAcknowledged = cls.ucmdHead;}
#else
	{ *incomingAcknowledged = cls.ucmdAcknowledged;}
#endif
	if( outgoingSequence ) {
		*outgoingSequence = cls.ucmdHead;
	}
	if( outgoingSent ) {
		*outgoingSent = cls.ucmdSent;
	}
}

/*
* CL_GameModule_Init
*/
void CL_GameModule_Init( void ) {
	int64_t start;

	// stop all playing sounds
	SoundSystem::instance()->stopAllSounds( SoundSystem::StopAndClear | SoundSystem::StopMusic );

	CL_GameModule_Shutdown();

	SCR_EnableQuickMenu( false );

	start = Sys_Milliseconds();
	CG_Init( cls.servername, cl.playernum,
			   viddef.width, viddef.height, VID_GetPixelRatio(),
			   cls.demoPlayer.playing, cls.demoPlayer.playing ? cls.demoPlayer.filename : "",
			   cls.sv_pure, cl.snapFrameTime, APP_PROTOCOL_VERSION, APP_DEMO_EXTENSION_STR,
			   cls.mediaRandomSeed, cl.gamestart );

	cge = (void *)1;

	Com_DPrintf( "CL_GameModule_Init: %.2f seconds\n", (float)( Sys_Milliseconds() - start ) * 0.001f );

	cl.gamestart = false;
	cls.cgameActive = true;
}

/*
* CL_GameModule_Reset
*/
void CL_GameModule_Reset( void ) {
	if( cge ) {
		CG_Reset();
	}
}

/*
* CL_GameModule_Shutdown
*/
void CL_GameModule_Shutdown( void ) {
	if( !cge ) {
		return;
	}

	cg_load_seq++;
	cls.cgameActive = false;

	CG_Shutdown();
	Com_UnloadGameLibrary( &module_handle );
	cge = NULL;
}

/*
* CL_GameModule_EscapeKey
*/
void CL_GameModule_EscapeKey( void ) {
	if( cge ) {
		CG_EscapeKey();
	}
}

/*
* CL_GameModule_GetEntitySoundOrigin
*/
void CL_GameModule_GetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity ) {
	if( cge ) {
		CG_GetEntitySpatilization( entNum, origin, velocity );
	}
}

/*
* CL_GameModule_ConfigString
*/
void CL_GameModule_ConfigString( int number, const wsw::StringView &string ) {
	if( cge ) {
		CG_ConfigString( number, string );
	}
}

/*
* CL_GameModule_NewSnapshot
*/
bool CL_GameModule_NewSnapshot( int pendingSnapshot ) {
	snapshot_t *currentSnap, *newSnap;

	if( cge ) {
		currentSnap = ( cl.currentSnapNum <= 0 ) ? NULL : &cl.snapShots[cl.currentSnapNum & UPDATE_MASK];
		newSnap = &cl.snapShots[pendingSnapshot & UPDATE_MASK];
		return CG_NewFrameSnap( newSnap, currentSnap );
	}

	return false;
}

/*
* CL_GameModule_RenderView
*/
void CL_GameModule_RenderView() {
	if( cge && cls.cgameActive ) {
		unsigned extrapolationTime = cl_extrapolate->integer && !cls.demoPlayer.playing ? cl_extrapolationTime->integer : 0;
		CG_RenderView( cls.frametime, cls.realFrameTime, cls.realtime, cl.serverTime, extrapolationTime );
	}
}

/*
* CL_GameModule_InputFrame
*/
void CL_GameModule_InputFrame( int frameTime ) {
	if( cge ) {
		CG_InputFrame( frameTime );
	}
}

/*
* CL_GameModule_ClearInputState
*/
void CL_GameModule_ClearInputState( void ) {
	if( cge ) {
		CG_ClearInputState();
	}
}

/*
* CL_GameModule_GetButtonBits
*/
unsigned CL_GameModule_GetButtonBits( void ) {
	if( cge ) {
		return CG_GetButtonBits();
	}
	return 0;
}

/*
* CL_GameModule_AddViewAngles
*/
void CL_GameModule_AddViewAngles( vec3_t viewAngles ) {
	if( cge ) {
		CG_AddViewAngles( viewAngles );
	}
}

/*
* CL_GameModule_AddMovement
*/
void CL_GameModule_AddMovement( vec3_t movement ) {
	if( cge ) {
		CG_AddMovement( movement );
	}
}

/*
* CL_GameModule_MouseMove
*/
void CL_GameModule_MouseMove( int dx, int dy ) {
	if( cge ) {
		CG_MouseMove( dx, dy );
	}
}

bool CG_HasKeyboardFocus() {
	return cge && !Con_HasKeyboardFocus() && !wsw::ui::UISystem::instance()->requestsKeyboardFocus();
}