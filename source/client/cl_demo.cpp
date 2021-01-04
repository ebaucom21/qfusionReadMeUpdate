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
// cl_demo.c  -- demo recording

#include "client.h"
#include "../ref/frontend.h"
#include "../ui/uisystem.h"

using wsw::operator""_asView;

static void CL_PauseDemo( bool paused );

/*
* CL_WriteDemoMessage
*
* Dumps the current net message, prefixed by the length
*/
void CL_WriteDemoMessage( msg_t *msg ) {
	if( cls.demoRecorder.file <= 0 ) {
		cls.demoRecorder.recording = false;
		return;
	}

	// the first eight bytes are just packet sequencing stuff
	SNAP_RecordDemoMessage( cls.demoRecorder.file, msg, 8 );
}


/*
* CL_Stop_f
*
* stop recording a demo
*/
void CL_Stop_f( void ) {
	int arg;
	bool silent, cancel;

	// look through all the args
	silent = false;
	cancel = false;
	for( arg = 1; arg < Cmd_Argc(); arg++ ) {
		if( !Q_stricmp( Cmd_Argv( arg ), "silent" ) ) {
			silent = true;
		} else if( !Q_stricmp( Cmd_Argv( arg ), "cancel" ) ) {
			cancel = true;
		}
	}

	if( !cls.demoRecorder.recording ) {
		if( !silent ) {
			Com_Printf( "Not recording a demo.\n" );
		}
		return;
	}

	// finish up
	SNAP_StopDemoRecording( cls.demoRecorder.file );

	char metadata[SNAP_MAX_DEMO_META_DATA_SIZE];
	wsw::DemoMetadataWriter writer( metadata );

	// write some meta information about the match/demo
	writer.write( "hostname"_asView, cl.configStrings.getHostName().value() );
	writer.write( "localtime"_asView, wsw::StringView( va( "%" PRIu64, (uint64_t)cls.demoRecorder.localtime ) ) );
	writer.write( "multipov"_asView, "0"_asView );
	writer.write( "duration"_asView, wsw::StringView( va( "%u", (int)ceil( cls.demoRecorder.duration / 1000.0f ) ) ) );
	writer.write( "mapname"_asView, cl.configStrings.getMapName().value() );
	writer.write( "gametype"_asView, cl.configStrings.getGametypeName().value() );
	writer.write( "levelname"_asView, cl.configStrings.getMessage().value() );

	FS_FCloseFile( cls.demoRecorder.file );

	const auto [metadataSize, wasComplete] = writer.resultSoFar();
	if( !wasComplete ) {
		Com_Printf( S_COLOR_YELLOW "The demo metadata was truncated\n" );
	}

	SNAP_WriteDemoMetaData( cls.demoRecorder.filename, metadata, metadataSize );

	// cancel the demos
	if( cancel ) {
		// remove the file that correspond to cls.demoRecorder.file
		if( !silent ) {
			Com_Printf( "Canceling demo: %s\n", cls.demoRecorder.filename );
		}
		if( !FS_RemoveFile( cls.demoRecorder.filename ) && !silent ) {
			Com_Printf( "Error canceling demo." );
		}
	}

	if( !silent ) {
		Com_Printf( "Stopped demo: %s\n", cls.demoRecorder.filename );
	}

	cls.demoRecorder.file = 0; // file id
	Q_free( cls.demoRecorder.filename );
	Q_free( cls.demoRecorder.name );
	cls.demoRecorder.filename = NULL;
	cls.demoRecorder.name = NULL;
	cls.demoRecorder.recording = false;
}

/*
* CL_Record_f
*
* record <demoname>
*
* Begins recording a demo from the current position
*/
void CL_Record_f( void ) {
	char *name;
	size_t name_size;
	bool silent;
	const char *demoname;

	if( cls.state != CA_ACTIVE ) {
		Com_Printf( "You must be in a level to record.\n" );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "record <demoname>\n" );
		return;
	}

	if( Cmd_Argc() > 2 && !Q_stricmp( Cmd_Argv( 2 ), "silent" ) ) {
		silent = true;
	} else {
		silent = false;
	}

	if( cls.demoPlayer.playing ) {
		if( !silent ) {
			Com_Printf( "You can't record from another demo.\n" );
		}
		return;
	}

	if( cls.demoRecorder.recording ) {
		if( !silent ) {
			Com_Printf( "Already recording.\n" );
		}
		return;
	}

	//
	// open the demo file
	//
	demoname = Cmd_Argv( 1 );
	name_size = sizeof( char ) * ( strlen( "demos/" ) + strlen( demoname ) + strlen( APP_DEMO_EXTENSION_STR ) + 1 );
	name = (char *)Q_malloc( name_size );

	Q_snprintfz( name, name_size, "demos/%s", demoname );
	COM_SanitizeFilePath( name );
	COM_DefaultExtension( name, APP_DEMO_EXTENSION_STR, name_size );

	if( !COM_ValidateRelativeFilename( name ) ) {
		if( !silent ) {
			Com_Printf( "Invalid filename.\n" );
		}
		Q_free( name );
		return;
	}

	if( FS_FOpenFile( name, &cls.demoRecorder.file, FS_WRITE | SNAP_DEMO_GZ ) == -1 ) {
		Com_Printf( "Error: Couldn't create the demo file.\n" );
		Q_free( name );
		return;
	}

	if( !silent ) {
		Com_Printf( "Recording demo: %s\n", name );
	}

	// store the name in case we need it later
	cls.demoRecorder.filename = name;
	cls.demoRecorder.recording = true;
	cls.demoRecorder.basetime = cls.demoRecorder.duration = cls.demoRecorder.time = 0;
	cls.demoRecorder.name = Q_strdup( demoname );

	// don't start saving messages until a non-delta compressed message is received
	CL_AddReliableCommand( "nodelta" ); // request non delta compressed frame from server
	cls.demoRecorder.waiting = true;
}



/*
* CL_DemoCompleted
*
* Close the demo file and disable demo state. Called from disconnection proccess
*/
void CL_DemoCompleted( void ) {
	if( cls.demoPlayer.demofilehandle ) {
		FS_FCloseFile( cls.demoPlayer.demofilehandle );
		cls.demoPlayer.demofilehandle = 0;
	}
	cls.demoPlayer.demofilelen = cls.demoPlayer.demofilelentotal = 0;

	cls.demoPlayer.playing = false;
	cls.demoPlayer.basetime = cls.demoPlayer.duration = cls.demoPlayer.time = 0;
	Q_free( cls.demoPlayer.filename );
	cls.demoPlayer.filename = NULL;
	Q_free( cls.demoPlayer.name );
	cls.demoPlayer.name = NULL;

	Com_SetDemoPlaying( false );

	CL_PauseDemo( false );

	Com_Printf( "Demo completed\n" );

	memset( &cls.demoPlayer, 0, sizeof( cls.demoPlayer ) );
}

/*
* CL_ReadDemoMessage
*
* Read a packet from the demo file and send it to the messages parser
*/
static void CL_ReadDemoMessage( void ) {
	static uint8_t msgbuf[MAX_MSGLEN];
	static msg_t demomsg;
	static bool init = true;
	int read;

	if( !cls.demoPlayer.demofilehandle ) {
		CL_Disconnect( NULL );
		return;
	}

	if( init ) {
		MSG_Init( &demomsg, msgbuf, sizeof( msgbuf ) );
		init = false;
	}

	read = SNAP_ReadDemoMessage( cls.demoPlayer.demofilehandle, &demomsg );
	if( read == -1 ) {
		if( cls.demoPlayer.pause_on_stop ) {
			cls.demoPlayer.paused = true;
		} else {
			CL_Disconnect( NULL );
		}
		return;
	}

	CL_ParseServerMessage( &demomsg );
}

/*
* CL_ReadDemoPackets
*
* See if it's time to read a new demo packet
*/
void CL_ReadDemoPackets( void ) {
	if( cls.demoPlayer.paused ) {
		return;
	}

	while( cls.demoPlayer.playing && ( cl.receivedSnapNum <= 0 || !cl.snapShots[cl.receivedSnapNum & UPDATE_MASK].valid || cl.snapShots[cl.receivedSnapNum & UPDATE_MASK].serverTime < cl.serverTime ) ) {
		CL_ReadDemoMessage();
		if( cls.demoPlayer.paused ) {
			return;
		}
	}

	cls.demoPlayer.time = cls.gametime;
	cls.demoPlayer.play_jump = false;
}

/*
* CL_LatchedDemoJump
*
* See if it's time to read a new demo packet
*/
void CL_LatchedDemoJump( void ) {
	if( cls.demoPlayer.paused || !cls.demoPlayer.play_jump_latched ) {
		return;
	}

	cls.gametime = cls.demoPlayer.play_jump_time;

	if( cl.serverTime < cl.snapShots[cl.receivedSnapNum & UPDATE_MASK].serverTime ) {
		cl.pendingSnapNum = 0;
	}

	CL_AdjustServerTime( 1 );

	if( cl.serverTime < cl.snapShots[cl.receivedSnapNum & UPDATE_MASK].serverTime ) {
		cls.demoPlayer.demofilelen = cls.demoPlayer.demofilelentotal;
		FS_Seek( cls.demoPlayer.demofilehandle, 0, FS_SEEK_SET );
		cl.currentSnapNum = cl.receivedSnapNum = 0;
	}

	cls.demoPlayer.play_jump = true;
	cls.demoPlayer.play_jump_latched = false;
}

/*
* CL_StartDemo
*/
static void CL_StartDemo( const char *demoname, bool pause_on_stop ) {
	size_t name_size;
	char *name, *servername;
	const char *filename = NULL;
	int tempdemofilehandle = 0, tempdemofilelen = -1;

	// have to copy the argument now, since next actions will lose it
	servername = Q_strdup( demoname );
	COM_SanitizeFilePath( servername );

	name_size = sizeof( char ) * ( strlen( "demos/" ) + strlen( servername ) + strlen( APP_DEMO_EXTENSION_STR ) + 1 );
	name = (char *)Q_malloc( name_size );

	Q_snprintfz( name, name_size, "demos/%s", servername );
	COM_DefaultExtension( name, APP_DEMO_EXTENSION_STR, name_size );

	if( COM_ValidateRelativeFilename( name ) ) {
		filename = name;
	}

	if( filename ) {
		tempdemofilelen = FS_FOpenFile( filename, &tempdemofilehandle, FS_READ | SNAP_DEMO_GZ );  // open the demo file
	}

	if( !tempdemofilehandle ) {
		// relative filename didn't work, try launching a demo from absolute path
		Q_snprintfz( name, name_size, "%s", servername );
		COM_DefaultExtension( name, APP_DEMO_EXTENSION_STR, name_size );
		tempdemofilelen = FS_FOpenAbsoluteFile( name, &tempdemofilehandle, FS_READ | SNAP_DEMO_GZ );
	}

	if( !tempdemofilehandle ) {
		Com_Printf( "No valid demo file found\n" );
		FS_FCloseFile( tempdemofilehandle );
		Q_free( name );
		Q_free( servername );
		return;
	}

	// make sure a local server is killed
	Cbuf_ExecuteText( EXEC_NOW, "killserver\n" );
	CL_Disconnect( NULL );
	// wsw: Medar: fix for menu getting stuck on screen when starting demo, but maybe there is better fix out there?
	wsw::ui::UISystem::instance()->forceMenuOff();

	memset( &cls.demoPlayer, 0, sizeof( cls.demoPlayer ) );

	cls.demoPlayer.demofilehandle = tempdemofilehandle;
	cls.demoPlayer.demofilelentotal = tempdemofilelen;
	cls.demoPlayer.demofilelen = cls.demoPlayer.demofilelentotal;

	cls.servername = Q_strdup( COM_FileBase( servername ) );
	COM_StripExtension( cls.servername );

	CL_SetClientState( CA_HANDSHAKE );
	Com_SetDemoPlaying( true );
	cls.demoPlayer.playing = true;
	cls.demoPlayer.basetime = cls.demoPlayer.duration = cls.demoPlayer.time = 0;

	cls.demoPlayer.pause_on_stop = pause_on_stop;
	cls.demoPlayer.play_ignore_next_frametime = false;
	cls.demoPlayer.play_jump = false;
	cls.demoPlayer.filename = Q_strdup( name );
	cls.demoPlayer.name = Q_strdup( servername );

	CL_PauseDemo( false );

	// set up for timedemo settings
	memset( &cl.timedemo, 0, sizeof( cl.timedemo ) );

	Q_free( name );
	Q_free( servername );
}

/*
* CL_DemoComplete
*/
char **CL_DemoComplete( const char *partial ) {
	return Cmd_CompleteFileList( partial, "demos", APP_DEMO_EXTENSION_STR, true );
}

/*
* CL_PlayDemo_f
*
* demo <demoname>
*/
void CL_PlayDemo_f( void ) {
	if( Cmd_Argc() < 2 ) {
		Com_Printf( "demo <demoname> [pause_on_stop]\n" );
		return;
	}
	CL_StartDemo( Cmd_Argv( 1 ), atoi( Cmd_Argv( 2 ) ) != 0 );
}

/*
* CL_PauseDemo
*/
static void CL_PauseDemo( bool paused ) {
	cls.demoPlayer.paused = paused;
}

/*
* CL_PauseDemo_f
*/
void CL_PauseDemo_f( void ) {
	if( !cls.demoPlayer.playing ) {
		Com_Printf( "Can only demopause when playing a demo.\n" );
		return;
	}

	if( Cmd_Argc() > 1 ) {
		if( !Q_stricmp( Cmd_Argv( 1 ), "on" ) ) {
			CL_PauseDemo( true );
		} else if( !Q_stricmp( Cmd_Argv( 1 ), "off" ) ) {
			CL_PauseDemo( false );
		}
		return;
	}

	CL_PauseDemo( !cls.demoPlayer.paused );
}

/*
* CL_DemoJump_f
*/
void CL_DemoJump_f( void ) {
	bool relative;
	int time;
	char *p;

	if( !cls.demoPlayer.playing ) {
		Com_Printf( "Can only demojump when playing a demo\n" );
		return;
	}

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: demojump <time>\n" );
		Com_Printf( "Time format is [minutes:]seconds\n" );
		Com_Printf( "Use '+' or '-' in front of the time to specify it in relation to current position\n" );
		return;
	}

	p = Cmd_Argv( 1 );

	if( Cmd_Argv( 1 )[0] == '+' || Cmd_Argv( 1 )[0] == '-' ) {
		relative = true;
		p++;
	} else {
		relative = false;
	}

	if( strchr( p, ':' ) ) {
		time = ( atoi( p ) * 60 + atoi( strchr( p, ':' ) + 1 ) ) * 1000;
	} else {
		time = atoi( p ) * 1000;
	}

	if( Cmd_Argv( 1 )[0] == '-' ) {
		time = -time;
	}

	if( relative ) {
		cls.demoPlayer.play_jump_time = cls.gametime + time;
	} else {
		cls.demoPlayer.play_jump_time = time; // gametime always starts from 0
	}
	cls.demoPlayer.play_jump_latched = true;
}

/*
* CL_ReadDemoMetaData
*/
size_t CL_ReadDemoMetaData( const char *demopath, char *meta_data, size_t meta_data_size ) {
	char *servername;
	size_t meta_data_realsize = 0;

	if( !demopath || !*demopath ) {
		return 0;
	}

	// have to copy the argument now, since next actions will lose it
	servername = Q_strdup( demopath );
	COM_SanitizeFilePath( servername );

	// hack:
	if( cls.demoPlayer.playing && !Q_stricmp( cls.demoPlayer.name, servername ) && cls.demoPlayer.meta_data_realsize > 0 ) {
		if( meta_data && meta_data_size ) {
			meta_data_realsize = cls.demoPlayer.meta_data_realsize;
			memcpy( meta_data, cls.demoPlayer.meta_data, std::min( meta_data_size, cls.demoPlayer.meta_data_realsize ) );
			meta_data[std::min( meta_data_size - 1, cls.demoPlayer.meta_data_realsize )] = '\0';
		}
	} else {
		char *name;
		size_t name_size;
		int demofile, demolength;

		name_size = sizeof( char ) * ( strlen( "demos/" ) + strlen( servername ) + strlen( APP_DEMO_EXTENSION_STR ) + 1 );
		name = (char *)Q_malloc( name_size );

		Q_snprintfz( name, name_size, "demos/%s", servername );
		COM_DefaultExtension( name, APP_DEMO_EXTENSION_STR, name_size );

		demolength = FS_FOpenFile( name, &demofile, FS_READ | SNAP_DEMO_GZ );

		if( !demofile || demolength < 1 ) {
			// relative filename didn't work, try launching a demo from absolute path
			Q_snprintfz( name, name_size, "%s", servername );
			COM_DefaultExtension( name, APP_DEMO_EXTENSION_STR, name_size );
			demolength = FS_FOpenAbsoluteFile( name, &demofile, FS_READ );
		}

		if( demolength > 0 ) {
			meta_data_realsize = SNAP_ReadDemoMetaData( demofile, meta_data, meta_data_size );
		}
		FS_FCloseFile( demofile );

		Q_free( name );
	}

	Q_free( servername );

	return meta_data_realsize;
}
