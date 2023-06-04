/*
Copyright (C) 2015 SiPlus, Chasseur de bots

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

/**
 * Warsow-specific input code.
 */

#include "cg_local.h"
#include "../qcommon/qcommon.h"
#include "../client/input.h"
#include "../client/keys.h"
#include "../client/client.h"
#include "../qcommon/cmdargs.h"
#include "../qcommon/cmdcompat.h"

using wsw::operator""_asView;

static int64_t cg_inputTime;
static int cg_inputFrameTime;
static bool cg_inputCenterView;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition


Key_Event (int key, bool down, int64_t time);

===============================================================================
*/

typedef struct {
	int down[2];            // key nums holding it down
	int64_t downtime;       // msec timestamp
	unsigned msec;          // msec down this frame
	int state;
} kbutton_t;

static kbutton_t in_klook;
static kbutton_t in_left, in_right, in_forward, in_back;
static kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
static kbutton_t in_strafe, in_speed, in_use, in_attack;
static kbutton_t in_up, in_down;
static kbutton_t in_special;
static kbutton_t in_zoom;

static cvar_t *cl_yawspeed;
static cvar_t *cl_pitchspeed;

static cvar_t *cl_run;

static cvar_t *cl_anglespeedkey;

/*
* CG_KeyDown
*/
static void CG_KeyDown( kbutton_t *b, const CmdArgs &cmdArgs ) {
	int k;
	const char *c;

	c = Cmd_Argv( 1 );
	if( c[0] ) {
		k = atoi( c );
	} else {
		k = -1; // typed manually at the console for continuous down

	}
	if( k == b->down[0] || k == b->down[1] ) {
		return; // repeating key

	}
	if( !b->down[0] ) {
		b->down[0] = k;
	} else if( !b->down[1] ) {
		b->down[1] = k;
	} else {
		cgWarning() << "Three keys down for a button!";
		return;
	}

	if( b->state & 1 ) {
		return; // still down

	}
	// save timestamp
	c = Cmd_Argv( 2 );
	b->downtime = atoi( c );
	if( !b->downtime ) {
		b->downtime = cg_inputTime - 100;
	}

	b->state |= 1 + 2; // down + impulse down
}

/*
* CG_KeyUp
*/
static void CG_KeyUp( kbutton_t *b, const CmdArgs &cmdArgs ) {
	int k;
	const char *c;
	int uptime;

	c = Cmd_Argv( 1 );
	if( c[0] ) {
		k = atoi( c );
	} else { // typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->state = 4; // impulse up
		return;
	}

	if( b->down[0] == k ) {
		b->down[0] = 0;
	} else if( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return; // key up without corresponding down (menu pass through)
	}
	if( b->down[0] || b->down[1] ) {
		return; // some other key is still holding it down

	}
	if( !( b->state & 1 ) ) {
		return; // still up (this should not happen)

	}
	// save timestamp
	c = Cmd_Argv( 2 );
	uptime = atoi( c );
	if( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += 10;
	}

	b->state &= ~1; // now up
	b->state |= 4;  // impulse up
}

static void IN_KLookDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_klook, cmdArgs ); }
static void IN_KLookUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_klook, cmdArgs ); }
static void IN_UpDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_up, cmdArgs ); }
static void IN_UpUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_up, cmdArgs ); }
static void IN_DownDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_down, cmdArgs ); }
static void IN_DownUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_down, cmdArgs ); }
static void IN_LeftDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_left, cmdArgs ); }
static void IN_LeftUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_left, cmdArgs ); }
static void IN_RightDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_right, cmdArgs ); }
static void IN_RightUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_right, cmdArgs ); }
static void IN_ForwardDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_forward, cmdArgs ); }
static void IN_ForwardUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_forward, cmdArgs ); }
static void IN_BackDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_back, cmdArgs ); }
static void IN_BackUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_back, cmdArgs ); }
static void IN_LookupDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_lookup, cmdArgs ); }
static void IN_LookupUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_lookup, cmdArgs ); }
static void IN_LookdownDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_lookdown, cmdArgs ); }
static void IN_LookdownUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_lookdown, cmdArgs ); }
static void IN_MoveleftDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_moveleft, cmdArgs ); }
static void IN_MoveleftUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_moveleft, cmdArgs ); }
static void IN_MoverightDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_moveright, cmdArgs ); }
static void IN_MoverightUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_moveright, cmdArgs ); }
static void IN_SpeedDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_speed, cmdArgs ); }
static void IN_SpeedUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_speed, cmdArgs ); }
static void IN_StrafeDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_strafe, cmdArgs ); }
static void IN_StrafeUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_strafe, cmdArgs ); }
static void IN_AttackDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_attack, cmdArgs ); }
static void IN_AttackUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_attack, cmdArgs ); }
static void IN_UseDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_use, cmdArgs ); }
static void IN_UseUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_use, cmdArgs ); }
static void IN_SpecialDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_special, cmdArgs ); }
static void IN_SpecialUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_special, cmdArgs ); }
static void IN_ZoomDown( const CmdArgs &cmdArgs ) { CG_KeyDown( &in_zoom, cmdArgs ); }
static void IN_ZoomUp( const CmdArgs &cmdArgs ) { CG_KeyUp( &in_zoom, cmdArgs ); }


/*
* CG_KeyState
*/
static float CG_KeyState( kbutton_t *key ) {
	float val;
	int msec;

	key->state &= 1; // clear impulses

	msec = key->msec;
	key->msec = 0;

	if( key->state ) {
		// still down
		msec += cg_inputTime - key->downtime;
		key->downtime = cg_inputTime;
	}

	if( !cg_inputFrameTime )
		return 0;

	val = (float) msec / (float)cg_inputFrameTime;

	return bound( 0, val, 1 );
}

/*
* CG_AddKeysViewAngles
*/
static void CG_AddKeysViewAngles( vec3_t viewAngles ) {
	float speed;

	if( in_speed.state & 1 ) {
		speed = ( (float)cg_inputFrameTime * 0.001f ) * cl_anglespeedkey->value;
	} else {
		speed = (float)cg_inputFrameTime * 0.001f;
	}

	if( !( in_strafe.state & 1 ) ) {
		viewAngles[YAW] -= speed * cl_yawspeed->value * CG_KeyState( &in_right );
		viewAngles[YAW] += speed * cl_yawspeed->value * CG_KeyState( &in_left );
	}
	if( in_klook.state & 1 ) {
		viewAngles[PITCH] -= speed * cl_pitchspeed->value * CG_KeyState( &in_forward );
		viewAngles[PITCH] += speed * cl_pitchspeed->value * CG_KeyState( &in_back );
	}

	viewAngles[PITCH] -= speed * cl_pitchspeed->value * CG_KeyState( &in_lookup );
	viewAngles[PITCH] += speed * cl_pitchspeed->value * CG_KeyState( &in_lookdown );
}

/*
* CG_AddKeysMovement
*/
static void CG_AddKeysMovement( vec3_t movement ) {
	float down;

	if( in_strafe.state & 1 ) {
		movement[0] += CG_KeyState( &in_right );
		movement[0] -= CG_KeyState( &in_left );
	}

	movement[0] += CG_KeyState( &in_moveright );
	movement[0] -= CG_KeyState( &in_moveleft );

	if( !( in_klook.state & 1 ) ) {
		movement[1] += CG_KeyState( &in_forward );
		movement[1] -= CG_KeyState( &in_back );
	}

	movement[2] += CG_KeyState( &in_up );
	down = CG_KeyState( &in_down );
	if( down > movement[2] ) {
		movement[2] -= down;
	}
}

/*
* CG_GetButtonBitsFromKeys
*/
unsigned int CG_GetButtonBitsFromKeys( void ) {
	int buttons = 0;

	// figure button bits

	if( in_attack.state & 3 ) {
		buttons |= BUTTON_ATTACK;
	}
	in_attack.state &= ~2;

	if( in_special.state & 3 ) {
		buttons |= BUTTON_SPECIAL;
	}
	in_special.state &= ~2;

	if( in_use.state & 3 ) {
		buttons |= BUTTON_USE;
	}
	in_use.state &= ~2;

	if( ( in_speed.state & 1 ) ^ !cl_run->integer ) {
		buttons |= BUTTON_WALK;
	}

	if( in_zoom.state & 3 ) {
		buttons |= BUTTON_ZOOM;
	}
	in_zoom.state &= ~2;

	return buttons;
}

/*
===============================================================================

MOUSE

===============================================================================
*/

static cvar_t *sensitivity;
static cvar_t *zoomsens;
static cvar_t *m_accel;
static cvar_t *m_accelStyle;
static cvar_t *m_accelOffset;
static cvar_t *m_accelPow;
static cvar_t *m_filter;
static cvar_t *m_sensCap;

static cvar_t *m_pitch;
static cvar_t *m_yaw;

static float mouse_x = 0, mouse_y = 0;

/*
* CG_MouseMove
*/
void CG_MouseMove( int mx, int my ) {
	static float old_mouse_x = 0, old_mouse_y = 0;
	float accelSensitivity;

	// mouse filtering
	switch( m_filter->integer ) {
	case 1:
	{
		mouse_x = ( mx + old_mouse_x ) * 0.5;
		mouse_y = ( my + old_mouse_y ) * 0.5;
	}
	break;

	default: // no filtering
		mouse_x = mx;
		mouse_y = my;
		break;
	}

	old_mouse_x = mx;
	old_mouse_y = my;

	accelSensitivity = sensitivity->value;

	if( m_accel->value != 0.0f && cg_inputFrameTime != 0 ) {
		float rate;

		// QuakeLive-style mouse acceleration, ported from ioquake3
		// original patch by Gabriel Schnoering and TTimo
		if( m_accelStyle->integer == 1 ) {
			float base[2];
			float power[2];

			// sensitivity remains pretty much unchanged at low speeds
			// m_accel is a power value to how the acceleration is shaped
			// m_accelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			base[0] = (float) ( abs( mx ) ) / (float) cg_inputFrameTime;
			base[1] = (float) ( abs( my ) ) / (float) cg_inputFrameTime;
			power[0] = powf( base[0] / m_accelOffset->value, m_accel->value );
			power[1] = powf( base[1] / m_accelOffset->value, m_accel->value );

			mouse_x = ( mouse_x + ( ( mouse_x < 0 ) ? -power[0] : power[0] ) * m_accelOffset->value );
			mouse_y = ( mouse_y + ( ( mouse_y < 0 ) ? -power[1] : power[1] ) * m_accelOffset->value );
		} else if( m_accelStyle->integer == 2 ) {
			float accelOffset, accelPow;

			// ch : similar to normal acceleration with offset and variable pow mechanisms

			// sanitize values
			accelPow = m_accelPow->value > 1.0 ? m_accelPow->value : 2.0;
			accelOffset = m_accelOffset->value >= 0.0 ? m_accelOffset->value : 0.0;

			rate = sqrt( mouse_x * mouse_x + mouse_y * mouse_y ) / (float)cg_inputFrameTime;
			rate -= accelOffset;
			if( rate < 0 ) {
				rate = 0.0;
			}
			// ch : TODO sens += pow( rate * m_accel->value, m_accelPow->value - 1.0 )
			accelSensitivity += pow( rate * m_accel->value, accelPow - 1.0 );

			// TODO : move this outside of this branch?
			if( m_sensCap->value > 0 && accelSensitivity > m_sensCap->value ) {
				accelSensitivity = m_sensCap->value;
			}
		} else {
			rate = sqrt( mouse_x * mouse_x + mouse_y * mouse_y ) / (float)cg_inputFrameTime;
			accelSensitivity += rate * m_accel->value;
		}
	}

	accelSensitivity *= CG_GetSensitivityScale( sensitivity->value, zoomsens->value );

	mouse_x *= accelSensitivity;
	mouse_y *= accelSensitivity;
}

/**
* Adds view rotation from mouse.
*
* @param viewAngles view angles to modify
*/
static void CG_AddMouseViewAngles( vec3_t viewAngles ) {
	if( !mouse_x && !mouse_y ) {
		return;
	}

	// add mouse X/Y movement to cmd
	viewAngles[YAW] -= m_yaw->value * mouse_x;
	viewAngles[PITCH] += m_pitch->value * mouse_y;
}

/*
===============================================================================

COMMON

===============================================================================
*/

/*
* CG_CenterView
*/
static void CG_CenterView( const CmdArgs & ) {
	cg_inputCenterView = true;
}

/*
* CG_InputInit
*/
void CG_InitInput( void ) {
	CL_Cmd_Register( "+moveup"_asView, IN_UpDown );
	CL_Cmd_Register( "-moveup"_asView, IN_UpUp );
	CL_Cmd_Register( "+movedown"_asView, IN_DownDown );
	CL_Cmd_Register( "-movedown"_asView, IN_DownUp );
	CL_Cmd_Register( "+left"_asView, IN_LeftDown );
	CL_Cmd_Register( "-left"_asView, IN_LeftUp );
	CL_Cmd_Register( "+right"_asView, IN_RightDown );
	CL_Cmd_Register( "-right"_asView, IN_RightUp );
	CL_Cmd_Register( "+forward"_asView, IN_ForwardDown );
	CL_Cmd_Register( "-forward"_asView, IN_ForwardUp );
	CL_Cmd_Register( "+back"_asView, IN_BackDown );
	CL_Cmd_Register( "-back"_asView, IN_BackUp );
	CL_Cmd_Register( "+lookup"_asView, IN_LookupDown );
	CL_Cmd_Register( "-lookup"_asView, IN_LookupUp );
	CL_Cmd_Register( "+lookdown"_asView, IN_LookdownDown );
	CL_Cmd_Register( "-lookdown"_asView, IN_LookdownUp );
	CL_Cmd_Register( "+strafe"_asView, IN_StrafeDown );
	CL_Cmd_Register( "-strafe"_asView, IN_StrafeUp );
	CL_Cmd_Register( "+moveleft"_asView, IN_MoveleftDown );
	CL_Cmd_Register( "-moveleft"_asView, IN_MoveleftUp );
	CL_Cmd_Register( "+moveright"_asView, IN_MoverightDown );
	CL_Cmd_Register( "-moveright"_asView, IN_MoverightUp );
	CL_Cmd_Register( "+speed"_asView, IN_SpeedDown );
	CL_Cmd_Register( "-speed"_asView, IN_SpeedUp );
	CL_Cmd_Register( "+attack"_asView, IN_AttackDown );
	CL_Cmd_Register( "-attack"_asView, IN_AttackUp );
	CL_Cmd_Register( "+use"_asView, IN_UseDown );
	CL_Cmd_Register( "-use"_asView, IN_UseUp );
	CL_Cmd_Register( "+klook"_asView, IN_KLookDown );
	CL_Cmd_Register( "-klook"_asView, IN_KLookUp );
	// wsw
	CL_Cmd_Register( "+special"_asView, IN_SpecialDown );
	CL_Cmd_Register( "-special"_asView, IN_SpecialUp );
	CL_Cmd_Register( "+zoom"_asView, IN_ZoomDown );
	CL_Cmd_Register( "-zoom"_asView, IN_ZoomUp );

	CL_Cmd_Register( "centerview"_asView, CG_CenterView );
}

void CG_InitInputVars() {
	cl_yawspeed =  Cvar_Get( "cl_yawspeed", "140", 0 );
	cl_pitchspeed = Cvar_Get( "cl_pitchspeed", "150", 0 );
	cl_anglespeedkey = Cvar_Get( "cl_anglespeedkey", "1.5", 0 );

	cl_run = Cvar_Get( "cl_run", "1", CVAR_ARCHIVE );

	sensitivity = Cvar_Get( "sensitivity", "3", CVAR_ARCHIVE );
	zoomsens = Cvar_Get( "zoomsens", "0", CVAR_ARCHIVE );
	m_accel = Cvar_Get( "m_accel", "0", CVAR_ARCHIVE );
	m_accelStyle = Cvar_Get( "m_accelStyle", "0", CVAR_ARCHIVE );
	m_accelOffset = Cvar_Get( "m_accelOffset", "0", CVAR_ARCHIVE );
	m_accelPow = Cvar_Get( "m_accelPow", "2", CVAR_ARCHIVE );
	m_filter = Cvar_Get( "m_filter", "0", CVAR_ARCHIVE );
	m_pitch = Cvar_Get( "m_pitch", "0.022", CVAR_ARCHIVE );
	m_yaw = Cvar_Get( "m_yaw", "0.022", CVAR_ARCHIVE );
	m_sensCap = Cvar_Get( "m_sensCap", "0", CVAR_ARCHIVE );
}

/*
* CG_ShutdownInput
*/
void CG_ShutdownInput( void ) {
	CL_Cmd_Unregister( "+moveup"_asView );
	CL_Cmd_Unregister( "-moveup"_asView );
	CL_Cmd_Unregister( "+movedown"_asView );
	CL_Cmd_Unregister( "-movedown"_asView );
	CL_Cmd_Unregister( "+left"_asView );
	CL_Cmd_Unregister( "-left"_asView );
	CL_Cmd_Unregister( "+right"_asView );
	CL_Cmd_Unregister( "-right"_asView );
	CL_Cmd_Unregister( "+forward"_asView );
	CL_Cmd_Unregister( "-forward"_asView );
	CL_Cmd_Unregister( "+back"_asView );
	CL_Cmd_Unregister( "-back"_asView );
	CL_Cmd_Unregister( "+lookup"_asView );
	CL_Cmd_Unregister( "-lookup"_asView );
	CL_Cmd_Unregister( "+lookdown"_asView );
	CL_Cmd_Unregister( "-lookdown"_asView );
	CL_Cmd_Unregister( "+strafe"_asView );
	CL_Cmd_Unregister( "-strafe"_asView );
	CL_Cmd_Unregister( "+moveleft"_asView );
	CL_Cmd_Unregister( "-moveleft"_asView );
	CL_Cmd_Unregister( "+moveright"_asView );
	CL_Cmd_Unregister( "-moveright"_asView );
	CL_Cmd_Unregister( "+speed"_asView );
	CL_Cmd_Unregister( "-speed"_asView );
	CL_Cmd_Unregister( "+attack"_asView );
	CL_Cmd_Unregister( "-attack"_asView );
	CL_Cmd_Unregister( "+use"_asView );
	CL_Cmd_Unregister( "-use"_asView );
	CL_Cmd_Unregister( "+klook"_asView );
	CL_Cmd_Unregister( "-klook"_asView );
	// wsw
	CL_Cmd_Unregister( "+special"_asView );
	CL_Cmd_Unregister( "-special"_asView );
	CL_Cmd_Unregister( "+zoom"_asView );
	CL_Cmd_Unregister( "-zoom"_asView );

	CL_Cmd_Unregister( "centerview"_asView );
}

/*
* CG_GetButtonBits
*/
unsigned int CG_GetButtonBits( void ) {
	return CG_GetButtonBitsFromKeys();
}

/**
* Adds view rotation from all kinds of input devices.
*
* @param viewAngles view angles to modify
* @param flipped    horizontal flipping direction
*/
void CG_AddViewAngles( vec3_t viewAngles ) {
	vec3_t am { 0.0f, 0.0f, 0.0f };

	CG_AddKeysViewAngles( am );
	CG_AddMouseViewAngles( am );

	VectorAdd( viewAngles, am, viewAngles );

	if( cg_inputCenterView ) {
		viewAngles[PITCH] = -SHORT2ANGLE( cg.predictedPlayerState.pmove.delta_angles[PITCH] );
		cg_inputCenterView = false;
	}
}

/*
* CG_AddMovement
*/
void CG_AddMovement( vec3_t movement ) {
	vec3_t dm { 0.0f, 0.0f, 0.0f };

	CG_AddKeysMovement( dm );

	VectorAdd( movement, dm, movement );
}

/*
* CG_InputFrame
*/
void CG_InputFrame( int frameTime ) {
	cg_inputTime = Sys_Milliseconds();
	cg_inputFrameTime = frameTime;
}

/*
* CG_ClearInputState
*/
void CG_ClearInputState( void ) {
	cg_inputFrameTime = 0;
}

/*
* CG_GetBoundKeysString
*/
void CG_GetBoundKeysString( const char *cmd, char *keys, size_t keysSize ) {
	int key;
	int numKeys = 0;
	const char *keyNames[2];
	char charKeys[2][2];

	const wsw::StringView cmdView( cmd );
	memset( charKeys, 0, sizeof( charKeys ) );

	const auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	// TODO: If the routine turns to be really useful,
	// implement such functionality at the bindings system level
	// in an optimized fashion instead of doing a loop over all keys here.
	for( key = 0; key < 256; key++ ) {
		auto maybeBinding = bindingsSystem->getBindingForKey( key );
		if( !maybeBinding || !maybeBinding->equalsIgnoreCase( cmdView ) ) {
			continue;
		}

		if( ( key >= 'a' ) && ( key <= 'z' ) ) {
			charKeys[numKeys][0] = key - ( 'a' - 'A' );
			keyNames[numKeys] = charKeys[numKeys];
		} else {
			keyNames[numKeys] = bindingsSystem->getNameForKey( key )->data();
		}

		numKeys++;
		if( numKeys == 2 ) {
			break;
		}
	}

	if( !numKeys ) {
		keyNames[0] = "UNBOUND";
	}

	if( numKeys == 2 ) {
		Q_snprintfz( keys, keysSize, "%s or %s", keyNames[0], keyNames[1] );
	} else {
		Q_strncpyz( keys, keyNames[0], keysSize );
	}
}
