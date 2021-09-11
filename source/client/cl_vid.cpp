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

// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// qfusion refresh engine.
#include "client.h"
#include "xpm.h"
#include "../ui/uisystem.h"

cvar_t *vid_width, *vid_height;
cvar_t *vid_xpos;          // X coordinate of window position
cvar_t *vid_ypos;          // Y coordinate of window position
cvar_t *vid_fullscreen;
cvar_t *vid_borderless;
cvar_t *vid_displayfrequency;
cvar_t *vid_multiscreen_head;
cvar_t *vid_parentwid;      // parent window identifier
cvar_t *win_noalttab;
cvar_t *win_nowinkeys;

// Global variables used internally by this module
viddef_t viddef;             // global video state; used by other modules

static int vid_ref_prevwidth, vid_ref_prevheight;
static bool vid_ref_modified;
static bool vid_ref_verbose;
static bool vid_ref_sound_restart;
static bool vid_ref_active;
static bool vid_initialized;
static bool vid_app_active;
static bool vid_app_minimized;

// These are system specific functions
// wrapper around R_Init
rserr_t VID_Sys_Init( const char *applicationName, const char *screenshotsPrefix, int startupColor, const int *iconXPM,
					  void *parentWindow, bool verbose );
void VID_UpdateWindowPosAndSize( int x, int y );
void VID_EnableAltTab( bool enable );
void VID_EnableWinKeys( bool enable );

/*
** VID_Restart_f
*
* Console command to re-start the video mode and refresh DLL. We do this
* simply by setting the vid_ref_modified variable, which will
* cause the entire video mode and refresh DLL to be reset on the next frame.
*/
void VID_Restart( bool verbose, bool soundRestart ) {
	vid_ref_modified = true;
	vid_ref_verbose = verbose;
	vid_ref_sound_restart = soundRestart;
}

void VID_Restart_f( void ) {
	VID_Restart( ( Cmd_Argc() >= 2 ? true : false ), false );
}

static unsigned vid_num_modes;
static unsigned vid_max_width_mode_index;
static unsigned vid_max_height_mode_index;
static vidmode_t *vid_modes;

/*
** VID_GetModeInfo
*/
bool VID_GetModeInfo( int *width, int *height, unsigned int mode ) {
	if( mode >= vid_num_modes ) {
		return false;
	}

	*width  = vid_modes[mode].width;
	*height = vid_modes[mode].height;
	return true;
}

/*
** VID_ModeList_f
*/
static void VID_ModeList_f( void ) {
	unsigned int i;

	for( i = 0; i < vid_num_modes; i++ )
		Com_Printf( "* %ix%i\n", vid_modes[i].width, vid_modes[i].height );
}

/*
** VID_NewWindow
*/
static void VID_NewWindow( int width, int height ) {
	viddef.width  = width;
	viddef.height = height;
}

// Ignore conversion from a string literal to char * for XPM data
#ifndef _MSC_VER
#pragma GCC diagnostic push
#	ifdef __clang__
#	pragma GCC diagnostic ignored "-Wwritable-strings"
#	else
#	pragma GCC diagnostic ignored "-Wwrite-strings"
#	endif
#endif

static rserr_t VID_Sys_Init_( void *parentWindow, bool verbose ) {
	rserr_t res;
#include APP_XPM_ICON
	int *xpm_icon;

	xpm_icon = XPM_ParseIcon( sizeof( app256x256_xpm ) / sizeof( app256x256_xpm[0] ), app256x256_xpm );

	res = VID_Sys_Init( APPLICATION_UTF8, APP_SCREENSHOTS_PREFIX, APP_STARTUP_COLOR, xpm_icon,
						parentWindow, verbose );

	free( xpm_icon );

	return res;
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

/*
** VID_AppActivate
*/
void VID_AppActivate( bool active, bool minimize, bool destroy ) {
	vid_app_active = active;
	vid_app_minimized = minimize;
	RF_AppActivate( active, minimize, destroy );
}

/*
** VID_AppIsActive
*/
bool VID_AppIsActive( void ) {
	return vid_app_active;
}

/*
** VID_AppIsMinimized
*/
bool VID_AppIsMinimized( void ) {
	return vid_app_minimized;
}

/*
** VID_RefreshIsActive
*/
bool VID_RefreshIsActive( void ) {
	return vid_ref_active;
}

/*
** VID_GetWindowWidth
*/
int VID_GetWindowWidth( void ) {
	return viddef.width;
}

/*
** VID_GetWindowHeight
*/
int VID_GetWindowHeight( void ) {
	return viddef.height;
}

/*
** VID_ChangeMode
*/
static rserr_t VID_ChangeMode( void ) {
	vid_fullscreen->modified = false;

	const int frequency = vid_displayfrequency->integer;

	VidModeFlags flags = VidModeFlags::None;
	if( vid_borderless->integer ) {
		flags = flags | VidModeFlags::Borderless;
	}
	if( vid_fullscreen->integer ) {
		flags = flags | VidModeFlags::Fullscreen;
	}

	int x, y, w, h;
	if( ( flags & VidModeFlags::BorderlessFullscreen ) == VidModeFlags::BorderlessFullscreen ) {
		x = 0, y = 0;
		if( !VID_GetDefaultMode( &w, &h ) ) {
			w = vid_modes[0].width;
			h = vid_modes[0].height;
		}
	} else {
		x = vid_xpos->integer;
		y = vid_ypos->integer;
		w = vid_width->integer;
		h = vid_height->integer;
	}

	if( vid_ref_active && ( w != (int)viddef.width || h != (int)viddef.height ) ) {
		return rserr_restart_required;
	}

	rserr_t err = R_TrySettingMode( x, y, w, h, frequency, (unsigned)flags );
	if( err == rserr_restart_required ) {
		return err;
	}

	if( err == rserr_ok ) {
		// store fallback mode
		vid_ref_prevwidth = w;
		vid_ref_prevheight = h;
	} else {
		/* Try to recover from all possible kinds of mode-related failures.
		 *
		 * rserr_invalid_fullscreen may be returned only if fullscreen is requested, but at this
		 * point the system may not be totally sure whether the requested mode is windowed-only
		 * or totally unsupported, so there's a possibility of rserr_invalid_mode as well.
		 *
		 * However, the previously working mode may be windowed-only, but the user may request
		 * fullscreen, so this case is handled too.
		 *
		 * In the end, in the worst case, the windowed safe mode will be selected, and the system
		 * should not return rserr_invalid_fullscreen or rserr_invalid_mode anymore.
		 */

		// TODO: Take the borderless flag into account (could it fail?)

		if( err == rserr_invalid_fullscreen ) {
			Com_Printf( "VID_ChangeMode() - fullscreen unavailable in this mode\n" );

			Cvar_ForceSet( vid_fullscreen->name, "0" );
			vid_fullscreen->modified = false;

			// Try again without the fullscreen flag
			flags = flags & ~VidModeFlags::Fullscreen;
			err = R_TrySettingMode( x, y, w, h, frequency, (unsigned)flags );
		}

		if( err == rserr_invalid_mode ) {
			Com_Printf( "VID_ChangeMode() - invalid mode\n" );

			// Try setting it back to something safe
			if( w != vid_ref_prevwidth || h != vid_ref_prevheight ) {
				w = vid_ref_prevwidth;
				Cvar_ForceSet( vid_width->name, va( "%i", w ) );
				h = vid_ref_prevheight;
				Cvar_ForceSet( vid_height->name, va( "%i", h ) );

				err = R_TrySettingMode( x, y, w, h, frequency, (unsigned)flags );
				if( err == rserr_invalid_fullscreen ) {
					Com_Printf( "VID_ChangeMode() - could not revert to safe fullscreen mode\n" );

					Cvar_ForceSet( vid_fullscreen->name, "0" );
					vid_fullscreen->modified = false;

					// Try again without the fullscreen flag
					flags = flags & ~VidModeFlags::Fullscreen;
					err = R_TrySettingMode( x, y, w, h, frequency, (unsigned)flags );
				}
			}

			if( err != rserr_ok ) {
				Com_Printf( "VID_ChangeMode() - could not revert to safe mode\n" );
			}
		}
	}

	if( err == rserr_ok ) {
		// let the sound and input subsystems know about the new window
		VID_NewWindow( w, h );
	}

	return err;
}

static void VID_UnloadRefresh( void ) {
	if( !vid_ref_active ) {
		return;
	}

	RF_Shutdown( false );
	vid_ref_active = false;
}

static bool VID_LoadRefresh() {
	VID_UnloadRefresh();

	Com_Printf( "\n" );
	return true;
}

[[nodiscard]]
static auto getBestFittingMode( int requestedWidth, int requestedHeight ) -> std::pair<int, int> {
	unsigned scanHeightFrom = 0;
	int width = vid_modes[0].width;
	int height = vid_modes[0].height;
	if( int minWidthDiff = std::abs( width - requestedWidth ) ) {
		for( unsigned i = 1; i < vid_num_modes; i++ ) {
			const vidmode_t &mode = vid_modes[i];
			const int diff = std::abs( mode.width - requestedWidth );
			// select the bigger mode if the diff from the smaller and the larger is equal - use < for the smaller one
			if( diff <= minWidthDiff ) {
				// don't advance firstForHeight when searching for the larger mode
				if( mode.width != width ) {
					scanHeightFrom = i;
					width = mode.width;
				}
				minWidthDiff = diff;
			}
			if( !diff || ( diff > minWidthDiff ) ) {
				break;
			}
		}
	}
	if( int minHeightDiff = std::abs( height - requestedHeight ) ) {
		for( unsigned i = scanHeightFrom + 1; i < vid_num_modes; i++ ) {
			const vidmode_t &mode = vid_modes[i];
			if( mode.width != width ) {
				break;
			}
			const int diff = std::abs( mode.height - requestedHeight );
			if( diff <= minHeightDiff ) {
				height = mode.height;
				minHeightDiff = diff;
			}
			if( !diff || ( diff > minHeightDiff ) ) {
				break;
			}
		}
	}

	return { width, height };
}

static void RestartVideoAndAllMedia( bool vid_ref_was_active, bool verbose ) {
	const bool cgameActive = cls.cgameActive;
	cls.disable_screen = 1;

	CL_ShutdownMedia();

	// stop and free all sounds
	CL_SoundModule_Shutdown( false );

	FTLIB_FreeFonts( false );

	Cvar_GetLatchedVars( CVAR_LATCH_VIDEO );

	// TODO: Eliminate this
	if( !VID_LoadRefresh() ) {
		Sys_Error( "VID_LoadRefresh() failed" );
	}

	char buffer[16];

	// handle vid size changes
	if( ( vid_width->integer <= 0 ) || ( vid_height->integer <= 0 ) ) {
		// set the mode to the default
		int w, h;
		if( !VID_GetDefaultMode( &w, &h ) ) {
			w = vid_modes[0].width;
			h = vid_modes[0].height;
		}
		Cvar_ForceSet( vid_width->name, va_r( buffer, sizeof( buffer ), "%d", w ) );
		Cvar_ForceSet( vid_height->name, va_r( buffer, sizeof( buffer ), "%d", h ) );
	}

	if( const auto &mode = vid_modes[vid_max_width_mode_index]; vid_width->integer > mode.width ) {
		Cvar_ForceSet( vid_width->name, va_r( buffer, sizeof( buffer ), "%d", mode.width ) );
	}
	if( const auto &mode = vid_modes[vid_max_height_mode_index]; vid_height->integer > mode.height ) {
		Cvar_ForceSet( vid_height->name, va_r( buffer, sizeof( buffer ), "%d", mode.width ) );
	}

	if( vid_fullscreen->integer ) {
		// snap to the closest fullscreen resolution, width has priority over height
		const int requestedWidth = vid_width->integer;
		const int requestedHeight = vid_height->integer;
		const auto [bestWidth, bestHeight] = getBestFittingMode( requestedWidth, requestedHeight );
		if( bestWidth != requestedWidth ) {
			Cvar_ForceSet( vid_width->name, va_r( buffer, sizeof( buffer ), "%d", bestWidth ) );
		}
		if( bestHeight != requestedHeight ) {
			Cvar_ForceSet( vid_height->name, va_r( buffer, sizeof( buffer ), "%d", bestHeight ) );
		}
	}

	if( rserr_t err = VID_Sys_Init_( STR_TO_POINTER( vid_parentwid->string ), vid_ref_verbose ); err != rserr_ok ) {
		Sys_Error( "VID_Init() failed with code %i", err );
	}
	if( rserr_t err = VID_ChangeMode(); err != rserr_ok ) {
		Sys_Error( "VID_ChangeMode() failed with code %i", err );
	}

	vid_ref_active = true;

	// stop and free all sounds
	CL_SoundModule_Init( verbose );

	RF_BeginRegistration();
	SoundSystem::Instance()->BeginRegistration();

	FTLIB_PrecacheFonts( verbose );

	if( vid_ref_was_active ) {
		IN_Restart();
	}

	CL_InitMedia();

	cls.disable_screen = 0;

	Con_Close();

	if( cgameActive ) {
		CL_GameModule_Init();
		Con_Close();
		wsw::ui::UISystem::instance()->forceMenuOff();
	} else {
		if( auto maybeInstance = wsw::ui::UISystem::maybeInstance() ) {
			( *maybeInstance )->forceMenuOn();
		}
	}

	RF_EndRegistration();
	SoundSystem::Instance()->EndRegistration();

	vid_ref_modified = false;
	vid_ref_verbose = true;
}

/*
** VID_CheckChanges
*
* This function gets called once just before drawing each frame, and its sole purpose in life
* is to check to see if any of the video mode parameters have changed, and if they have to
* update the rendering DLL and/or video mode to match.
*/
void VID_CheckChanges( void ) {
	const bool vid_ref_was_active = vid_ref_active;
	const bool verbose = vid_ref_verbose || vid_ref_sound_restart;

	if( win_noalttab->modified ) {
		VID_EnableAltTab( win_noalttab->integer ? false : true );
		win_noalttab->modified = false;
	}

	if( win_nowinkeys->modified ) {
		VID_EnableWinKeys( win_nowinkeys->integer ? false : true );
		win_nowinkeys->modified = false;
	}

	if( vid_fullscreen->modified ) {
		if( vid_ref_active ) {
			// try to change video mode without vid_restart
			if( const rserr_t err = VID_ChangeMode(); err == rserr_restart_required ) {
				vid_ref_modified = true;
			}
		}

		vid_fullscreen->modified = false;
	}

	if( vid_ref_modified ) {
		RestartVideoAndAllMedia( vid_ref_was_active, verbose );
	}

	if( vid_xpos->modified || vid_ypos->modified ) {
		if( !vid_fullscreen->integer && !vid_borderless->integer ) {
			VID_UpdateWindowPosAndSize( vid_xpos->integer, vid_ypos->integer );
		}
		vid_xpos->modified = false;
		vid_ypos->modified = false;
	}
}

static int VID_CompareModes( const vidmode_t *first, const vidmode_t *second ) {
	if( first->width == second->width ) {
		return first->height - second->height;
	}

	return first->width - second->width;
}

void VID_InitModes( void ) {
	unsigned int numModes, i;

	numModes = VID_GetSysModes( vid_modes );
	if( !numModes ) {
		Sys_Error( "Failed to get video modes" );
	}

	vid_modes = (vidmode_t *)Q_malloc( numModes * sizeof( vidmode_t ) );
	VID_GetSysModes( vid_modes );
	qsort( vid_modes, numModes, sizeof( vidmode_t ), ( int ( * )( const void *, const void * ) )VID_CompareModes );

	// Remove duplicate modes in case the sys code failed to do so.
	vid_num_modes = 0;
	vid_max_height_mode_index = 0;
	int prevWidth = 0, prevHeight = 0;
	for( i = 0; i < numModes; i++ ) {
		const int width = vid_modes[i].width;
		const int height = vid_modes[i].height;
		if( width != prevWidth || height != prevHeight ) {
			if( height > vid_modes[vid_max_height_mode_index].height ) {
				vid_max_height_mode_index = i;
			}
			vid_modes[vid_num_modes++] = vid_modes[i];
			prevWidth = width;
			prevHeight = height;
		}
	}

	vid_max_width_mode_index = vid_num_modes - 1;
}

/*
** VID_Init
*/
void VID_Init( void ) {
	if( vid_initialized ) {
		return;
	}

	VID_InitModes();

	vid_width = Cvar_Get( "vid_width", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	vid_height = Cvar_Get( "vid_height", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	vid_xpos = Cvar_Get( "vid_xpos", "0", CVAR_ARCHIVE );
	vid_ypos = Cvar_Get( "vid_ypos", "0", CVAR_ARCHIVE );
	vid_fullscreen = Cvar_Get( "vid_fullscreen", "1", CVAR_ARCHIVE );
	vid_borderless = Cvar_Get( "vid_borderless", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	vid_displayfrequency = Cvar_Get( "vid_displayfrequency", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	vid_multiscreen_head = Cvar_Get( "vid_multiscreen_head", "-1", CVAR_ARCHIVE );
	vid_parentwid = Cvar_Get( "vid_parentwid", "0", CVAR_NOSET );

	win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );
	win_nowinkeys = Cvar_Get( "win_nowinkeys", "0", CVAR_ARCHIVE );

	/* Add some console commands that we want to handle */
	Cmd_AddCommand( "vid_restart", VID_Restart_f );
	Cmd_AddCommand( "vid_modelist", VID_ModeList_f );

	/* Start the graphics mode and load refresh DLL */
	vid_ref_modified = true;
	vid_ref_active = false;
	vid_ref_verbose = true;
	vid_initialized = true;
	vid_ref_sound_restart = false;
	vid_fullscreen->modified = false;
	vid_borderless->modified = false;
	vid_ref_prevwidth = vid_modes[0].width; // the smallest mode is the "safe mode"
	vid_ref_prevheight = vid_modes[0].height;

	FTLIB_Init( true );

	VID_CheckChanges();
}

/*
** VID_Shutdown
*/
void VID_Shutdown( void ) {
	if( !vid_initialized ) {
		return;
	}

	VID_UnloadRefresh();

	FTLIB_Shutdown( true );

	Cmd_RemoveCommand( "vid_restart" );
	Cmd_RemoveCommand( "vid_modelist" );

	Q_free( vid_modes );

	vid_initialized = false;
}
