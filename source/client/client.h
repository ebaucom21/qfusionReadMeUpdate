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
// client.h -- primary header for client

#include "../qcommon/qcommon.h"
#include "../qcommon/configstringstorage.h"
#include "../ref/ref.h"
#include "../cgame/cg_public.h"
#include "../ftlib/ftlib.h"
#include "../qcommon/mmrating.h"
#include "snd_public.h"
#include "../qcommon/steam.h"

#include "vid.h"
#include "input.h"
#include "keys.h"
#include "console.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

typedef struct shader_s shader_t;
typedef struct qfontface_s qfontface_t;

//=============================================================================

#define MAX_TIMEDELTAS_BACKUP 8
#define MASK_TIMEDELTAS_BACKUP ( MAX_TIMEDELTAS_BACKUP - 1 )

typedef struct {
	int frames;
	int64_t startTime;
	int64_t lastTime;
	int counts[100];
} cl_timedemo_t;

struct cin_yuv_s;

typedef struct {
	void *h;
	int width, height;
	bool keepRatio;
	bool allowConsole;
	bool redraw;
	bool paused;
	int pause_cnt;
	bool yuv;
	int64_t startTime;
	int64_t pauseTime;
	uint8_t *pic;
	int aspect_numerator, aspect_denominator;
	cin_yuv_s *cyuv;
	float framerate;
} cl_cintematics_t;

//
// the client_state_t structure is wiped completely at every
// server map change
//
typedef struct client_state_s {
	int timeoutcount;

	cl_timedemo_t timedemo;

	int cmdNum;                     // current cmd
	usercmd_t *cmds;                // [CMD_BACKUP] each mesage will send several old cmds
	int *cmd_time;                  // [CMD_BACKUP] time sent, for calculating pings

	int receivedSnapNum;
	int pendingSnapNum;
	int currentSnapNum;
	int previousSnapNum;
	int suppressCount;              // number of messages rate suppressed
	snapshot_t *snapShots;          // [CMD_BACKUP]
	uint8_t *frames_areabits;

	cmodel_state_t *cms;

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  It is cleared to 0 upon entering each level.
	// the server sends a delta each frame which is added to the locally
	// tracked view angles to account for standing on rotating objects,
	// and teleport direction changes
	vec3_t viewangles;

	int serverTimeDeltas[MAX_TIMEDELTAS_BACKUP];
	int newServerTimeDelta;         // the time difference with the server time, or at least our best guess about it
	int serverTimeDelta;            // the time difference with the server time, or at least our best guess about it
	int64_t serverTime;             // the best match we can guess about current time in the server
	unsigned int snapFrameTime;

	//
	// non-gameserver information
	cl_cintematics_t cin;

	//
	// server state information
	//
	int servercount;        // server identification for prespawns
	int playernum;
	bool gamestart;

	char servermessage[MAX_STRING_CHARS];

	int configStringFragmentIndex;
	int configStringFragmentNum;
	int configStringNumFragments;

	char configStringFragmentsBuffer[MAX_MSGLEN];

	wsw::ConfigStringStorage configStrings;
} client_state_t;

extern client_state_t cl;

/*
==================================================================

the client_static_t structure is persistant through an arbitrary number
of server connections

==================================================================
*/

typedef struct download_list_s download_list_t;

struct download_list_s {
	char *filename;
	download_list_t *next;
};

typedef struct {
	// for request
	char *requestname;              // file we requested from the server (NULL if none requested)
	bool requestnext;           // whether to request next download after this, for precaching
	bool requestpak;            // whether to only allow .pk3/.pak or only allow normal file
	int64_t timeout;
	int64_t timestart;

	// both downloads
	char *name;                     // name of the file in download, relative to base path
	char *origname;                 // name of the file in download as originally passed by the server
	char *tempname;                 // temporary location, relative to base path
	size_t size;
	unsigned checksum;

	double percent;
	int successCount;               // so we know to restart media
	download_list_t *list;          // list of all tried downloads, so we don't request same pk3 twice

	// server download
	int filenum;
	size_t offset;
	int retries;
	size_t baseoffset;              // for download speed calculation when resuming downloads

	// web download
	bool web;
	bool web_official;
	bool web_official_only;
	char *web_url;                  // download URL, passed by the server
	bool web_local_http;

	bool disconnect;            // set when user tries to disconnect, to allow cleaning up webdownload
	bool pending_reconnect;     // set when we ignored a map change command to avoid stopping the download
	bool cancelled;             // to allow cleaning up of temporary download file
} download_t;

struct DemoPlayer {
	char *name;

	bool playing;
	bool paused;        // A boolean to test if demo is paused -- PLX

	int demofilehandle;
	int demofilelen, demofilelentotal;

	char *filename;

	int64_t time;           // milliseconds passed since the start of the demo

	bool play_jump;
	bool play_jump_latched;
	int64_t play_jump_time;
	bool play_ignore_next_frametime;

	bool pause_on_stop;

	char meta_data[SNAP_MAX_DEMO_META_DATA_SIZE];
	size_t meta_data_realsize;
};

struct DemoRecorder {
	char *name;

	bool recording;
	bool waiting;       // don't record until a non-delta message is received

	int file;
	char *filename;

	time_t localtime;       // time of day of demo recording
	int64_t time;           // milliseconds passed since the start of the demo
	int64_t duration, basetime;
};

typedef struct {
	connstate_t state;          // only set through CL_SetClientState
	bool quickmenu;

	int64_t framecount;
	int64_t realtime;               // always increasing, no clamping, etc
	int64_t gametime;               // always increasing, no clamping, etc
	int frametime;                  // milliseconds since last frame
	int realFrameTime;

	socket_t socket_loopback;
	socket_t socket_udp;
	socket_t socket_udp6;
#ifdef TCP_SUPPORT
	socket_t socket_tcp;
#endif

	// screen rendering information
	bool cgameActive;
	int mediaRandomSeed;
	bool mediaInitialized;

	unsigned int disable_screen;    // showing loading plaque between levels
	                                // or changing rendering dlls
	                                // if time gets > 30 seconds ahead, break it

	// connection information
	char *servername;               // name of server from original connect
	socket_type_t servertype;       // socket type used to connect to the server
	netadr_t serveraddress;         // address of that server
	int64_t connect_time;               // for connection retransmits
	int connect_count;

	socket_t *socket;               // socket used by current connection
	bool reliable;
	bool mv;

	netadr_t rconaddress;       // address where we are sending rcon messages, to ignore other print packets

	netadr_t httpaddress;           // address of the builtin HTTP server
	char *httpbaseurl;              // http://<httpaddress>/

	bool rejected;          // these are used when the server rejects our connection
	int rejecttype;
	char rejectmessage[80];

	netchan_t netchan;

	int challenge;              // from the server to use for connecting

	download_t download;

	bool registrationOpen;

	// demo recording info must be here, so it isn't cleared on level change
	DemoRecorder demoRecorder;
	DemoPlayer demoPlayer;

	// these shaders have nothing to do with media
	shader_t *whiteShader;
	shader_t *consoleShader;

	// system font
	qfontface_t *consoleFont;

	// these are our reliable messages that go to the server
	int64_t reliableSequence;          // the last one we put in the list to be sent
	int64_t reliableSent;              // the last one we sent to the server
	int64_t reliableAcknowledge;       // the last one the server has executed
	char reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];

	// reliable messages received from server
	int64_t lastExecutedServerCommand;          // last server command grabbed or executed with CL_GetServerCommand

	// ucmds buffer
	int64_t ucmdAcknowledged;
	int64_t ucmdHead;
	int64_t ucmdSent;

	// times when we got/sent last valid packets from/to server
	int64_t lastPacketSentTime;
	int64_t lastPacketReceivedTime;

	// pure list
	bool sv_pure;
	bool pure_restart;

	purelist_t *purelist;

	char session[MAX_INFO_VALUE];

	void *wakelock;
} client_static_t;

extern client_static_t cls;

namespace wsw::cl {

struct ChatMessage {
	const wsw::StringView name;
	const wsw::StringView text;
	/// This field is defined if the message is an own one.
	const std::optional<uint64_t> sendCommandNum;
};

}

//=============================================================================

extern cvar_t *cl_shownet;

extern cvar_t *cl_extrapolationTime;
extern cvar_t *cl_extrapolate;

extern cvar_t *cl_timedemo;

// wsw : debug netcode
extern cvar_t *cl_debug_serverCmd;
extern cvar_t *cl_debug_timeDelta;

extern cvar_t *cl_downloads;
extern cvar_t *cl_downloads_from_web;
extern cvar_t *cl_downloads_from_web_timeout;
extern cvar_t *cl_download_allow_modules;

// delta from this if not from a previous frame
extern entity_state_t cl_baselines[MAX_EDICTS];

//=============================================================================

//
// cl_main.c
//
void CL_Init( void );
void CL_Quit( void );

void CL_UpdateClientCommandsToServer( msg_t *msg );
void CL_AddReliableCommand( const char *cmd );
void CL_Netchan_Transmit( msg_t *msg );
void CL_SendMessagesToServer( bool sendNow );
void CL_RestartTimeDeltas( int newTimeDelta );
void CL_AdjustServerTime( unsigned int gamemsec );

char *CL_GetClipboardData( void );
void CL_SetClipboardData( const char *data );
void CL_FreeClipboardData( char *data );

bool CG_HasKeyboardFocus();

void CL_ResetServerCount( void );
void CL_SetClientState( int state );
connstate_t CL_GetClientState( void );  // wsw : aiwa : we need this information for graphical plugins (e.g. IRC)
void CL_ClearState( void );
void CL_ReadPackets( void );
void CL_Disconnect_f( void );
void CL_S_Restart( bool noVideo );

bool CL_IsBrowserAvailable( void );
void CL_OpenURLInBrowser( const char *url );

void CL_Reconnect_f( void );
void CL_ServerReconnect_f( void );
void CL_Changing_f( void );
void CL_Precache_f( void );
void CL_ForwardToServer_f( void );
void CL_ServerDisconnect_f( void );

size_t CL_GetBaseServerURL( char *buffer, size_t buffer_size );

int CL_AddSessionHttpRequestHeaders( const char *url, const char **headers );
void CL_AsyncStreamRequest( const char *url, const char **headers, int timeout, int resumeFrom,
							size_t ( *read_cb )( const void *, size_t, float, int, const char *, void * ),
							void ( *done_cb )( int, const char *, void * ),
							void ( *header_cb )( const char *, void * ), void *privatep, bool urlencodeUnsafe );

//
// cl_game.c
//
void CL_GameModule_Init( void );
void CL_GameModule_Reset( void );
void CL_GameModule_Shutdown( void );

void CL_GameModule_ConfigString( int number, const wsw::StringView &s );

bool CL_GameModule_NewSnapshot( int pendingSnapshot );
void CL_GameModule_RenderView();
void CL_GameModule_InputFrame( int frameTime );
void CL_GameModule_ClearInputState( void );
unsigned CL_GameModule_GetButtonBits( void );
void CL_GameModule_AddViewAngles( vec3_t viewAngles );
void CL_GameModule_AddMovement( vec3_t movement );
void CL_GameModule_MouseMove( int dx, int dy );

//
// cl_sound.c
//
void CL_SoundModule_Init( bool verbose );
void CL_SoundModule_Shutdown( bool verbose );

//
// cl_input.c
//
void CL_InitInput( void );
void CL_ShutdownInput( void );
void CL_UserInputFrame( int realMsec );
void CL_WriteUcmdsToMessage( msg_t *msg );

/**
 * Resets the input state to the same as when no input is done,
 * mainly when the current input dest can't receive events anymore.
 */
void CL_ClearInputState( void );



//
// cl_demo.c
//
void CL_WriteDemoMessage( msg_t *msg );
void CL_DemoCompleted( void );
void CL_PlayDemo_f( void );
void CL_ReadDemoPackets( void );
void CL_LatchedDemoJump( void );
void CL_Stop_f( void );
void CL_Record_f( void );
void CL_PauseDemo_f( void );
void CL_DemoJump_f( void );
size_t CL_ReadDemoMetaData( const char *demopath, char *meta_data, size_t meta_data_size );
char **CL_DemoComplete( const char *partial );

//
// cl_parse.c
//
void CL_ParseServerMessage( msg_t *msg );
#define SHOWNET( msg,s ) _SHOWNET( msg,s,cl_shownet->integer );

void CL_FreeDownloadList( void );
bool CL_CheckOrDownloadFile( const char *filename );

bool CL_DownloadRequest( const char *filename, bool requestpak );
void CL_DownloadStatus_f( void );
void CL_DownloadCancel_f( void );
void CL_DownloadDone( void );
void CL_RequestNextDownload( void );
void CL_CheckDownloadTimeout( void );

//
// cl_screen.c
//
void SCR_InitScreen( void );
void SCR_ShutdownScreen( void );
void SCR_EnableQuickMenu( bool enable );
void SCR_UpdateScreen( void );
void SCR_BeginLoadingPlaque( void );
void SCR_EndLoadingPlaque( void );
void SCR_DebugGraph( float value, float r, float g, float b );
void SCR_RunConsole( int msec );
void SCR_RegisterConsoleMedia( void );
void SCR_ShutDownConsoleMedia( void );
void SCR_ResetSystemFontConsoleSize( void );
void SCR_ChangeSystemFontConsoleSize( int ch );
qfontface_t *SCR_RegisterFont( const char *family, int style, unsigned int size );
size_t SCR_FontHeight( qfontface_t *font );
size_t SCR_strWidth( const char *str, qfontface_t *font, size_t maxlen, int flags = 0 );
size_t SCR_StrlenForWidth( const char *str, qfontface_t *font, size_t maxwidth, int flags = 0 );
int SCR_DrawString( int x, int y, int align, const char *str, qfontface_t *font, const vec4_t color, int flags = 0 );
size_t SCR_DrawStringWidth( int x, int y, int align, const char *str, size_t maxwidth, qfontface_t *font, const vec4_t color, int flags = 0 );
void SCR_DrawClampString( int x, int y, const char *str, int xmin, int ymin, int xmax, int ymax, qfontface_t *font, const vec4_t color, int flags = 0 );
void SCR_DrawRawChar( int x, int y, wchar_t num, qfontface_t *font, const vec4_t color );
void SCR_DrawFillRect( int x, int y, int w, int h, const vec4_t color );

void CL_InitMedia( void );
void CL_ShutdownMedia( void );
void CL_RestartMedia( void );

void CL_AddNetgraph( void );

extern float scr_con_current;
extern float scr_conlines;       // lines of console to display

//
// sys import
//

/**
 * Initializes the parts of the platform module required to run the client.
 */
void CL_Sys_Init( void );

/**
 * Shuts down the client parts of the platform module.
 */
void CL_Sys_Shutdown( void );
