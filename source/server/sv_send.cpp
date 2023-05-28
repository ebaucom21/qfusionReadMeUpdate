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
// sv_main.c -- server main program

#include "server.h"
#include "../qcommon/wswstaticstring.h"

// shared message buffer to be used for occasional messages
msg_t tmpMessage;
uint8_t tmpMessageData[MAX_MSGLEN];



//=============================================================================
//
//Com_Printf redirection
//
//=============================================================================

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];
void SV_FlushRedirect( int sv_redirected, const char *outputbuf, const void *extra ) {
	const flush_params_t *params = ( const flush_params_t * )extra;

	if( sv_redirected == RD_PACKET ) {
		Netchan_OutOfBandPrint( params->socket, params->address, "print\n%s", outputbuf );
	}
}


//=============================================================================
//
//EVENT MESSAGES
//
//=============================================================================

/*
* SV_AddGameCommand
*/
void SV_AddGameCommand( client_t *client, const char *cmd ) {
	int index;

	if( !client ) {
		return;
	}

	client->gameCommandCurrent++;
	index = client->gameCommandCurrent & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->gameCommands[index].command, cmd, sizeof( client->gameCommands[index].command ) );
	if( client->lastSentFrameNum ) {
		client->gameCommands[index].framenum = client->lastSentFrameNum + 1;
	} else {
		client->gameCommands[index].framenum = sv.framenum;
	}
}

/*
* SV_AddServerCommand
*
* The given command will be transmitted to the client, and is guaranteed to
* not have future snapshot_t executed before it is executed
*/
void SV_AddServerCommand( client_t *client, const wsw::StringView &cmd ) {
	if( !client ) {
		return;
	}

	if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		return;
	}

	const auto len = cmd.length();
	if( !len ) {
		return;
	}

	if( len + 1 > client->reliableCommands[0].capacity() ) {
		// This is an server error, not the client one.
		Com_Error( ERR_DROP, "A server command %s is too long\n", cmd.data() );
	}

	// ch : To avoid overflow of messages from excessive amount of configstrings
	// we batch them here. On incoming "cs" command, we'll trackback the queue
	// to find a pending "cs" command that has space in it. If we'll find one,
	// we'll batch this there, if not, we'll create a new one.
	const wsw::StringView csPrefix( "cs ", 3 );
	if( cmd.startsWith( csPrefix ) ) {
		for( auto i = client->reliableSequence; i > client->reliableSent; i-- ) {
			auto &otherCmd = client->reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )];
			if( otherCmd.startsWith( csPrefix ) ) {
				if( ( otherCmd.length() + len ) < otherCmd.capacity() ) {
					otherCmd.append( cmd.drop( 2 ) );
					return;
				}
			}
		}
	}

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged, we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient() doesn't cause a recursive drop client
	if( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Error: Too many pending reliable server commands" );
		return;
	}

	const auto index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	client->reliableCommands[index].assign( cmd );
}

/*
* SV_SendServerCommand
*
* Sends a reliable command string to be interpreted by
* the client: "cs", "changing", "disconnect", etc
* A NULL client will broadcast to all clients
*/
void SV_SendServerCommand( client_t *cl, const char *format, ... ) {
	char buffer[MAX_MSGLEN];

	va_list argptr;
	va_start( argptr, format );
	int charsWritten = Q_vsnprintfz( buffer, sizeof( buffer ), format, argptr );
	va_end( argptr );

	if( (size_t)charsWritten >= sizeof( buffer ) ) {
		Com_Error( ERR_DROP, "Server command overflow" );
	}

	const wsw::StringView cmd( buffer, (size_t)charsWritten );
	if( cl != NULL ) {
		if( cl->state < CS_CONNECTING ) {
			return;
		}
		SV_AddServerCommand( cl, cmd );
		return;
	}

	// send the data to all relevant clients
	const auto maxClients = sv_maxclients->integer;
	for( int i = 0; i < maxClients; ++i ) {
		auto *const client = svs.clients + i;
		if( client->state < CS_CONNECTING ) {
			continue;
		}
		SV_AddServerCommand( client, cmd );
	}

	// add to demo
	if( svs.demo.file ) {
		SV_AddServerCommand( &svs.demo.client, cmd );
	}
}

static_assert( kMaxNonFragmentedConfigStringLen < MAX_STRING_CHARS );
static_assert( kMaxNonFragmentedConfigStringLen > kMaxConfigStringFragmentLen );

static void SV_AddFragmentedConfigString( client_t *cl, int index, const wsw::StringView &string ) {
	wsw::StaticString<MAX_STRING_CHARS> buffer;

	const size_t len = string.length();
	// Don't use for transmission of shorter config strings
	assert( len >= kMaxNonFragmentedConfigStringLen );

	const size_t numFragments = len / kMaxConfigStringFragmentLen + ( len % kMaxConfigStringFragmentLen ? 1 : 0 );
	assert( numFragments >= 2 );

	wsw::StringView view( string );
	for( size_t i = 0; i < numFragments; ++i ) {
		wsw::StringView fragment = view.take( kMaxConfigStringFragmentLen );
		assert( ( i + 1 != numFragments && fragment.length() == kMaxConfigStringFragmentLen ) || !fragment.empty() );

		buffer.clear();
		buffer << wsw::StringView( "csf ", 4 );
		buffer << index << ' ' << i << ' ' << numFragments << ' ' << fragment.length() << ' ';
		buffer << '"' << fragment << '"';
		SV_AddServerCommand( cl, buffer.asView() );

		view = view.drop( fragment.length() );
		assert( !view.empty() || i + 1 == numFragments );
	}
}

void SV_SendConfigString( client_t *cl, int index, const wsw::StringView &string ) {
	if( string.length() + 16 > MAX_MSGLEN ) {
		Com_Error( ERR_DROP, "Configstring overflow: #%d len=%d\n", index, (int)string.length() );
	}

	if( string.length() < kMaxNonFragmentedConfigStringLen ) {
		assert( string.isZeroTerminated() );
		SV_SendServerCommand( cl, "cs %i \"%s\"", index, string.data() );
		return;
	}

	if( cl ) {
		if( cl->state >= CS_CONNECTING ) {
			SV_AddFragmentedConfigString( cl, index, string );
		}
		return;
	}

	const auto maxClients = sv_maxclients->integer;
	for( int i = 0; i < maxClients; ++i ) {
		auto *const client = svs.clients + i;
		if( client->state >= CS_CONNECTING ) {
			SV_AddFragmentedConfigString( client, index, string );
		}
	}

	if( svs.demo.file ) {
		SV_AddFragmentedConfigString( &svs.demo.client, index, string );
	}
}

/*
* SV_AddReliableCommandsToMessage
*
* (re)send all server commands the client hasn't acknowledged yet
*/
void SV_AddReliableCommandsToMessage( client_t *client, msg_t *msg ) {
	unsigned int i;

	if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		return;
	}

	if( sv_debug_serverCmd->integer ) {
		Com_Printf( "sv_cl->reliableAcknowledge: %" PRIi64 " sv_cl->reliableSequence:%" PRIi64 "\n",
			client->reliableAcknowledge, client->reliableSequence );
	}

	// write any unacknowledged serverCommands
	for( i = client->reliableAcknowledge + 1; i <= client->reliableSequence; i++ ) {
		const auto &cmd = client->reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )];
		if( cmd.empty() ) {
			continue;
		}
		MSG_WriteUint8( msg, svc_servercmd );
		if( !client->reliable ) {
			MSG_WriteInt32( msg, i );
		}
		MSG_WriteString( msg, cmd.data() );
		if( sv_debug_serverCmd->integer ) {
			Com_Printf( "SV_AddServerCommandsToMessage(%i):%s\n", i, cmd.data() );
		}
	}
	client->reliableSent = client->reliableSequence;
	if( client->reliable ) {
		client->reliableAcknowledge = client->reliableSent;
	}
}

//=============================================================================
//
//EVENT MESSAGES
//
//=============================================================================

/*
* SV_BroadcastCommand
*
* Sends a command to all connected clients. Ignores client->state < CS_SPAWNED check
*/
void SV_BroadcastCommand( const char *format, ... ) {
	client_t *client;
	int i;
	va_list argptr;
	char string[1024];

	if( !sv.state ) {
		return;
	}

	va_start( argptr, format );
	Q_vsnprintfz( string, sizeof( string ), format, argptr );
	va_end( argptr );

	for( i = 0, client = svs.clients; i < sv_maxclients->integer; i++, client++ ) {
		if( client->state < CS_CONNECTING ) {
			continue;
		}
		SV_SendServerCommand( client, string );
	}
}

//===============================================================================
//
//FRAME UPDATES
//
//===============================================================================

/*
* SV_SendClientsFragments
*/
bool SV_SendClientsFragments( void ) {
	client_t *client;
	int i;
	bool sent = false;

	// send a message to each connected client
	for( i = 0, client = svs.clients; i < sv_maxclients->integer; i++, client++ ) {
		if( client->state == CS_FREE || client->state == CS_ZOMBIE ) {
			continue;
		}
		if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
			continue;
		}
		if( !client->netchan.unsentFragments ) {
			continue;
		}

		if( !Netchan_TransmitNextFragment( &client->netchan ) ) {
			Com_Printf( "Error sending fragment to %s: %s\n", NET_AddressToString( &client->netchan.remoteAddress ),
						NET_ErrorString() );
			if( client->reliable ) {
				SV_DropClient( client, DROP_TYPE_GENERAL, "Error sending fragment: %s\n", NET_ErrorString() );
			}
			continue;
		}

		sent = true;
	}

	return sent;
}

/*
* SV_Netchan_Transmit
*/
bool SV_Netchan_Transmit( netchan_t *netchan, msg_t *msg ) {
	int zerror;

	// if we got here with unsent fragments, fire them all now
	if( !Netchan_PushAllFragments( netchan ) ) {
		return false;
	}

	if( sv_compresspackets->integer ) {
		zerror = Netchan_CompressMessage( msg );
		if( zerror < 0 ) { // it's compression error, just send uncompressed
			Com_DPrintf( "SV_Netchan_Transmit (ignoring compression): Compression error %i\n", zerror );
		}
	}

	return Netchan_Transmit( netchan, msg );
}

/*
* SV_InitClientMessage
*/
void SV_InitClientMessage( client_t *client, msg_t *msg, uint8_t *data, size_t size ) {
	if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		return;
	}

	if( data && size ) {
		MSG_Init( msg, data, size );
	}
	MSG_Clear( msg );

	// write the last client-command we received so it's acknowledged
	if( !client->reliable ) {
		MSG_WriteUint8( msg, svc_clcack );
		MSG_WriteUintBase128( msg, client->clientCommandExecuted );
		MSG_WriteUintBase128( msg, client->UcmdReceived ); // acknowledge the last ucmd
	}
}

/*
* SV_SendMessageToClient
*/
bool SV_SendMessageToClient( client_t *client, msg_t *msg ) {
	assert( client );

	if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		return true;
	}

	// transmit the message data
	client->lastPacketSentTime = svs.realtime;
	return SV_Netchan_Transmit( &client->netchan, msg );
}

/*
* SV_ResetClientFrameCounters
* This is used for a temporary sanity check I'm doing.
*/
void SV_ResetClientFrameCounters( void ) {
	int i;
	client_t *client;
	for( i = 0, client = svs.clients; i < sv_maxclients->integer; i++, client++ ) {
		if( !client->state ) {
			continue;
		}
		if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
			continue;
		}

		client->lastSentFrameNum = 0;
	}
}

/*
* SV_WriteFrameSnapToClient
*/
void SV_WriteFrameSnapToClient( client_t *client, msg_t *msg ) {
	SNAP_WriteFrameSnapToClient( &sv.gi, client, msg, sv.framenum, svs.gametime, sv.baselines,
								 &svs.client_entities, 0, NULL, NULL );
}

/*
* SV_BuildClientFrameSnap
*/
void SV_BuildClientFrameSnap( client_t *client, int snapHintFlags ) {
	vec_t *skyorg = NULL, origin[3];

	if( auto maybeSkyBoxString = sv.configStrings.getSkyBox() ) {
		int noents = 0;
		float f1 = 0, f2 = 0;

		if( sscanf( maybeSkyBoxString->data(), "%f %f %f %f %f %i", &origin[0], &origin[1], &origin[2], &f1, &f2, &noents ) >= 3 ) {
			if( !noents ) {
				skyorg = origin;
			}
		}
	}

	const ReplicatedScoreboardData *scoreboardData;
	if( client->edict ) {
		scoreboardData = ge->GetScoreboardDataForClient( client->edict->s.number - 1 );
	} else {
		scoreboardData = ge->GetScoreboardDataForDemo();
	}

	svs.fatvis.skyorg = skyorg;     // HACK HACK HACK
	SNAP_BuildClientFrameSnap( svs.cms, &sv.gi, sv.framenum, svs.gametime, &svs.fatvis,
							   client, ge->GetGameState(), scoreboardData,
							   &svs.client_entities, snapHintFlags );
	svs.fatvis.skyorg = NULL;
}

/*
* SV_SendClientDatagram
*/
static bool SV_SendClientDatagram( client_t *client ) {
	if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
		return true;
	}

	SV_InitClientMessage( client, &tmpMessage, NULL, 0 );

	SV_AddReliableCommandsToMessage( client, &tmpMessage );

	// Set snap hint flags to client-specific flags set by the game module
	int snapHintFlags = client->edict->r.client->m_snapHintFlags;
	// Add server global snap hint flags
	if( sv_snap_aggressive_sound_culling->integer ) {
		snapHintFlags |= SNAP_HINT_CULL_SOUND_WITH_PVS;
	}
	if( sv_snap_raycast_players_culling->integer ) {
		snapHintFlags |= SNAP_HINT_USE_RAYCAST_CULLING;
	}
	if( sv_snap_aggressive_fov_culling->integer ) {
		snapHintFlags |= SNAP_HINT_USE_VIEW_DIR_CULLING;
	}
	if( sv_snap_shadow_events_data->integer ) {
		snapHintFlags |= SNAP_HINT_SHADOW_EVENTS_DATA;
	}

	// send over all the relevant entity_state_t
	// and the player_state_t
	SV_BuildClientFrameSnap( client, snapHintFlags );

	SV_WriteFrameSnapToClient( client, &tmpMessage );

	return SV_SendMessageToClient( client, &tmpMessage );
}

/*
* SV_SendClientMessages
*/
void SV_SendClientMessages( void ) {
	int i;
	client_t *client;

	// send a message to each connected client
	for( i = 0, client = svs.clients; i < sv_maxclients->integer; i++, client++ ) {
		if( client->state == CS_FREE || client->state == CS_ZOMBIE ) {
			continue;
		}

		if( client->edict && ( client->edict->r.svflags & SVF_FAKECLIENT ) ) {
			client->lastSentFrameNum = sv.framenum;
			continue;
		}

		SV_UpdateActivity();

		if( client->state == CS_SPAWNED ) {
			if( !SV_SendClientDatagram( client ) ) {
				Com_Printf( "Error sending message to %s: %s\n", client->name, NET_ErrorString() );
				if( client->reliable ) {
					SV_DropClient( client, DROP_TYPE_GENERAL, "Error sending message: %s\n", NET_ErrorString() );
				}
			}
		} else {
			// send pending reliable commands, or send heartbeats for not timing out
			if( client->reliableSequence > client->reliableAcknowledge ||
				svs.realtime - client->lastPacketSentTime > 1000 ) {
				SV_InitClientMessage( client, &tmpMessage, NULL, 0 );
				SV_AddReliableCommandsToMessage( client, &tmpMessage );
				if( !SV_SendMessageToClient( client, &tmpMessage ) ) {
					Com_Printf( "Error sending message to %s: %s\n", client->name, NET_ErrorString() );
					if( client->reliable ) {
						SV_DropClient( client, DROP_TYPE_GENERAL, "Error sending message: %s\n", NET_ErrorString() );
					}
				}
			}
		}
	}
}
