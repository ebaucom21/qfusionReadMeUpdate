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

#include <time.h>       // just for dev

#include "server.h"
#include "sv_mm.h"
#include "../gameshared/q_shared.h"

#include "../qcommon/mmcommon.h"
#include "../qcommon/mmrating.h"
#include "../qcommon/mmquery.h"
#include "../qcommon/mmnetworktask.h"
#include "../qcommon/mmreliablepipe.h"

#include "../qcommon/singletonholder.h"

static SingletonHolder<SVStatsowFacade> instanceHolder;

void SVStatsowFacade::Init() {
	::instanceHolder.init();
}

void SVStatsowFacade::Shutdown() {
	::instanceHolder.shutdown();
}

SVStatsowFacade *SVStatsowFacade::Instance() {
	return ::instanceHolder.instance();
}

/**
 * A base class for all server-side descendants of {@code StatsowFacadeTask}
 * that provides some shared server-side-specific utilities.
 */
class SVStatsowTask : public StatsowFacadeTask<SVStatsowFacade> {
protected:
	SVStatsowTask( SVStatsowFacade *parent_, const char *name_, const char *resource_, unsigned retryDelay_ = 0 )
		: StatsowFacadeTask( parent_, name_, va( "server/%s", resource_ ), sv_ip->string ) {
		this->retryDelay = retryDelay_;
	}

	bool CheckResponseStatus( const char *methodTag ) const {
		return CheckParsedResponse( methodTag ) && CheckStatusField( methodTag );
	}

	bool CheckParsedResponse( const char *methodTag ) const;
	bool CheckStatusField( const char *methodTag ) const;
};

bool SVStatsowTask::CheckParsedResponse( const char *methodTag ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	if( query->ResponseJsonRoot() ) {
		return true;
	}
	PrintError( methodTag, "Failed to parse a JSON response" );
	return false;
}

bool SVStatsowTask::CheckStatusField( const char *methodTag ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	double status = query->GetRootDouble( "status", std::numeric_limits<double>::infinity() );
	if( !std::isfinite( status ) ) {
		PrintError( methodTag, "Can't find a numeric `status` field in the response" );
		return false;
	}
	if( status != 0 ) {
		return true;
	}
	const char *error = query->GetRootString( "error", "" );
	if( *error ) {
		PrintError( methodTag, "Request error at remote host: `%s`", error );
	} else {
		PrintError( methodTag, "Unspecified request error at remote host" );
	}
	return false;
}

class SVLoginTask : public SVStatsowTask {
public:
	explicit SVLoginTask( SVStatsowFacade *parent_ )
		: SVStatsowTask( parent_, "SVLoginTask", "login", 1000 ) {
		if( !query ) {
			return;
		}

		query->SetAuthKey( parent->sv_mm_authkey->string );
		query->SetPort( sv_port->integer );
		query->SetServerName( sv.configStrings.getHostName().value().data() );
		query->SetServerAddress( sv_ip->string );
		query->SetDemosBaseUrl( sv_uploads_demos_baseurl->string );
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;

	void OnAnyFailure();
};

class SVLogoutTask : public SVStatsowTask {
public:
	explicit SVLogoutTask( SVStatsowFacade *parent_ )
		: SVStatsowTask( parent_, "SVLogoutTask", "logout", 1000 ) {
		assert( parent->ourSession.IsValidSessionId() );
		if( query ) {
			query->SetServerSession( parent->ourSession );
		}
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class SVClientConnectTask : public SVStatsowTask {
	client_t *const client;
	bool userInfoChanged { false };

	bool CheckClientStillValid( const char *tag );
	bool TryReadingCredentials();
	void ReadRatings();
	void OnAnyOutcome();
	void OnAnyFailure();
public:
	explicit SVClientConnectTask( SVStatsowFacade *parent_,
								  client_t *client_,
								  const mm_uuid_t &session,
								  const mm_uuid_t &ticket,
								  const char *address )
		: SVStatsowTask( parent_, "SVClientConnectTask", "clientConnect", 333 ), client( client_ ) {
		if( !query ) {
			return;
		}
		query->SetServerSession( parent->ourSession );
		query->SetClientSession( session );
		query->SetTicket( ticket );
		query->SetClientAddress( address );
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class SVClientDisconnectTask : public SVStatsowTask {
	mm_uuid_t clientSession;
public:
	SVClientDisconnectTask( SVStatsowFacade *parent_, const mm_uuid_t &clientSession_ )
		: SVStatsowTask( parent_, "SVClientDisconnectTask", "clientDisconnect", 333 )
		, clientSession( clientSession_ ) {
		if( !query ) {
			return;
		}
		query->SetServerSession( parent->ourSession );
		query->SetClientSession( clientSession_ );
	}

	void OnQuerySuccess() override {
		char buffer[UUID_BUFFER_SIZE];
		PrintMessage( "OnQuerySuccess", "Acknowledged client %s disconnection", clientSession.ToString( buffer ) );
	}

	void OnQueryFailure() override {
		char buffer[UUID_BUFFER_SIZE];
		PrintError( "OnQueryFailure", "Failed to acknowledge client %s disconnection", clientSession.ToString( buffer ) );
	}
};

class SVFetchMatchUuidTask : public SVStatsowTask {
public:
	explicit SVFetchMatchUuidTask( SVStatsowFacade *parent_ )
		: SVStatsowTask( parent_, "SVFetchMatchUuidTask", "match/fetchId", 2500 ) {
		if( query ) {
			query->SetServerSession( parent->ourSession );
		}
	}

	bool QueryResultForbidsRetry() override {
		// Keep attempts to fetch a match ID in any case.
		// No error such as network error, server error, etc can stop it.
		// We hope these troubles get finally resolved at some attempt.
		return false;
	}

	bool IsRetryPermittedNow() override {
		return parent->continueFetchUuidTask;
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

SVLoginTask *SVStatsowFacade::NewLoginTask() {
	return NewTaskStub<SVLoginTask>( this );
}

SVLogoutTask *SVStatsowFacade::NewLogoutTask() {
	return NewTaskStub<SVLogoutTask>( this );
}

SVClientConnectTask *SVStatsowFacade::NewClientConnectTask( client_s *client,
	                                                        const mm_uuid_t &session,
	                                                        const mm_uuid_t &ticket,
	                                                        const char *address ) {
	return NewTaskStub<SVClientConnectTask>( this, client, session, ticket, address );
}

SVClientDisconnectTask *SVStatsowFacade::NewClientDisconnectTask( const mm_uuid_t &session ) {
	return NewTaskStub<SVClientDisconnectTask>( this, session );
}

SVFetchMatchUuidTask *SVStatsowFacade::NewFetchMatchUuidTask() {
	return NewTaskStub<SVFetchMatchUuidTask>( this );
}

bool SVStatsowFacade::SendGameQuery( QueryObject *query ) {
	query->SetServerSession( ourSession );
	return query->SendForStatusPolling();
}

void SVStatsowFacade::EnqueueMatchReport( QueryObject *query ) {
	if( reliablePipe ) {
		query->SetServerSession( ourSession );
		reliablePipe->EnqueueMatchReport( query );
		return;
	}

	// TODO: Disallow creation of queries for "match started" and "match aborted" events
	// if they are not expected by Statsow for this match (as well as if the server session is not valid)
	Com_Printf( S_COLOR_YELLOW "SVStatsowFacade::EnqueueMatchReport(): this event won't be sent\n" );
	QueryObject::DeleteQuery( query );
}

void SVStatsowFacade::CheckMatchUuid() {
	if( sv.configStrings.getMatchUuid() != std::nullopt ) {
		// Cancel tasks if any
		continueFetchUuidTask = false;
		return;
	}

	// Wait for logging instead of assigning a dummy UUID
	if( isLoggingIn || isLoggingOut ) {
		return;
	}

	// Provide an arbitrary non-empty UUID for the game module in this case
	if( !ourSession.IsValidSessionId() ) {
		assert( !continueFetchUuidTask );

		// Set another dummy UUID (aside from zero one that is always used at server spawn)
		// so we do not confuse valid UUIDs supplied by the Statsow server and dummy ones
		wsw::StringView dummyUuid( "ffffffff-ffff-ffff-ffff-ffffffffffff" );
		sv.configStrings.setMatchUuid( dummyUuid );
		Com_Printf( "SVStatsowFacade::CheckMatchUuid(): Using dummy UUID %s\n", dummyUuid.data() );
		return;
	}

	// Check whether the task is already running
	if( continueFetchUuidTask ) {
		return;
	}

	// Set this prior to launching the task
	continueFetchUuidTask = true;
	if( TryStartingTask( NewFetchMatchUuidTask() ) ) {
		return;
	}

	// Try again next frame
	continueFetchUuidTask = false;
}

void SVFetchMatchUuidTask::OnQuerySuccess() {
	const char *tag = "OnQuerySuccess";

	ScopeGuard scopeGuard( [=]() { parent->continueFetchUuidTask = false; } );

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const char *idString = query->GetRootString( "id", "" );
	if( !*idString ) {
		PrintError( tag, "Can't find the `id` response field" );
		return;
	}

	mm_uuid_t tmp;
	if( !mm_uuid_t::FromString( idString, &tmp ) ) {
		PrintError( tag, "Can't parse UUID string `%s`", idString );
		return;
	}

	sv.configStrings.setMatchUuid( wsw::StringView( idString ) );
}

void SVFetchMatchUuidTask::OnQueryFailure() {
	PrintError( "OnQueryFailure", "Failed to fetch a match UUID" );
	// Resetting this flag means automatic retry next frame.
	// We still are stick to this task design instead of using plain queries
	// due to convenient response parsing facilities and overall better code structure.
	parent->continueFetchUuidTask = false;
}

void SVStatsowFacade::OnClientDisconnected( client_t *client ) {
	if( !IsValid() ) {
		return;
	}

	if( !client->mm_session.IsValidSessionId() ) {
		return;
	}

	if( TryStartingTask( NewClientDisconnectTask( client->mm_session ) ) ) {
		return;
	}

	Com_Printf( S_COLOR_RED "SVStatsowFacade::OnClientDisconnected(): Can't launch a task\n" );
}

void SVClientConnectTask::OnQuerySuccess() {
	if( !CheckClientStillValid( "OnQuerySuccess" ) ) {
		return;
	}

	ScopeGuard scopeGuard( [=]() { OnAnyOutcome(); } );
	ScopeGuard failureGuard( [=]() { OnAnyFailure(); } );

	const char *tag = "OnQuerySuccess";

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	if( query->GetRootDouble( "banned", 0 ) != 0 ) {
		const char *reason = query->GetRootString( "reason", "Your account at " APP_URL " has been banned." );
		SV_DropClient( client, DROP_TYPE_GENERAL, "Error: %s", reason );
		return;
	}

	if( !TryReadingCredentials() ) {
		return;
	}

	userInfoChanged = true;
	// Don't call the code for the "failure" path at scope exit
	failureGuard.Suppress();

	ReadRatings();
}

void SVClientConnectTask::OnQueryFailure() {
	if( !CheckClientStillValid( "OnQueryFailure") ) {
		return;
	}

	Com_Printf( S_COLOR_YELLOW "%s: Remote or network failure\n", name );

	OnAnyFailure();
	OnAnyOutcome();
}

bool SVClientConnectTask::CheckClientStillValid( const char *tag ) {
	if( client->edict ) {
		return true;
	}

	// Happens if the game module rejects connection
	Com_Printf( "%s::%s(): The client is no longer valid\n", name, tag );
	return false;
}

void SVClientConnectTask::OnAnyOutcome() {
	assert( client->edict );

	if( userInfoChanged ) {
		SV_UserinfoChanged( client );
	}

	char buffer[UUID_BUFFER_SIZE];
	Com_Printf( "SV_MM_ClientConnect: %s with session id %s\n", client->name, client->mm_session.ToString( buffer ) );
}

void SVClientConnectTask::OnAnyFailure() {
	const char *tag = "SVClientConnectTask::OnAnyFailure()";
	// Make sure we have changed game module decision
	assert( client->edict );

	// unable to validate client, either kick him out or force local session
	if( parent->sv_mm_loginonly->integer ) {
		SV_DropClient( client, DROP_TYPE_GENERAL, "%s", "Failed to validate connection ticket" );
		return;
	}

	char buffer[UUID_BUFFER_SIZE];
	mm_uuid_t session = Uuid_FFFsUuid();
	Com_Printf( "%s: Using session %s for client %s\n", tag, session.ToString( buffer ), client->name );
	client->mm_session = session;
	userInfoChanged = true;
}

bool SVClientConnectTask::TryReadingCredentials() {
	const char *tag = "OnQuerySuccess()";

	ObjectReader rootReader( query->ResponseJsonRoot() );
	// TODO: This would have been better if we could rely on std::optional support and just return optional<ObjectReader>
	cJSON *infoSection = rootReader.GetObject( "player_info" );
	if( !infoSection ) {
		PrintError( tag, "Missing `player_info` section" );
		return false;
	}

	ObjectReader infoReader( infoSection );
	const char *login = infoReader.GetString( "login", "" );
	if( !*login ) {
		PrintError( tag, "Missing `login` field" );
		return false;
	}

	Q_strncpyz( client->mm_login, login, sizeof( client->mm_login ) );
	if( !Info_SetValueForKey( client->userinfo, "cl_mm_login", login ) ) {
		// TODO: What to do in this case? Just print an error?
		PrintError( tag, "Failed to set infokey `cl_mm_login` for player `%s`", login );
	}

	const char *mmflags = query->GetRootString( "mmflags", "" );
	if( *mmflags ) {
		// TODO: What to do in this case? Just print an error?
		if( !Info_SetValueForKey( client->userinfo, "mmflags", mmflags ) ) {
			PrintError( tag, "Failed to set infokey `mmflags` for player `%s`", login );
		}
	}

	userInfoChanged = true;
	return true;
}

void SVClientConnectTask::ReadRatings() {
	// Again this cries for optionals
	cJSON *section = ObjectReader( query->ResponseJsonRoot() ).GetArray( "ratings" );
	if( !section || !ge ) {
		return;
	}

	edict_t *const ent = EDICT_NUM( ( client - svs.clients ) + 1 );
	const auto consumer = [=]( const char *gametype, float rating, float deviation ) {
		ge->AddRating( ent, gametype, rating, deviation );
	};

	StatsowFacadeTask::ParseRatingsSection( section, consumer );
}

mm_uuid_t SVStatsowFacade::TryHandlingAnonymousConnection( char *userInfo, const char *message ) {
	if( !sv_mm_loginonly->integer ) {
		char buffer[UUID_BUFFER_SIZE];
		Info_SetValueForKey( userInfo, "cl_mm_session", Uuid_FFFsUuid().ToString( buffer ) );
		return Uuid_FFFsUuid();
	}

	Info_SetValueForKey( userInfo, "rejflag", "0" );
	Info_SetValueForKey( userInfo, "rejtype", va( "%d", DROP_TYPE_GENERAL ) );
	Info_SetValueForKey( userInfo, "rejmsg", message );
	return Uuid_ZeroUuid();
}

mm_uuid_t SVStatsowFacade::OnClientConnected( client_t *client,
	                                          const netadr_t *address,
	                                          char *userInfo,
	                                          const mm_uuid_t &ticket,
	                                          const mm_uuid_t &session ) {
	char buffer[UUID_BUFFER_SIZE];

	if( !IsValid() ) {
		// Return a local session id that is not valid but is not a zero.
		// That's what the game module expects in this case.
		Info_SetValueForKey( userInfo, "cl_mm_session", Uuid_FFFsUuid().ToString( buffer ) );
		return Uuid_FFFsUuid();
	}

	// Allow bots to connect without authentication.
	// Checking this is a bit tricky as an entity is not assigned yet.
	// This also allows the supplied user info parameter to serve some utility.
	if( const char *keyValue = Info_ValueForKey( userInfo, "socket" ) ) {
		if( !Q_stricmp( keyValue, "loopback" ) ) {
			Info_SetValueForKey( userInfo, "cl_mm_session", Uuid_FFFsUuid().ToString( buffer ) );
			return Uuid_FFFsUuid();
		}
	}

	char buffer1[UUID_BUFFER_SIZE], buffer2[UUID_BUFFER_SIZE];
	const char *const tag = "SVStatsowFacade::OnClientConnected()";

	if( !session.IsValidSessionId() ) {
		Com_Printf( S_COLOR_YELLOW "%s: The client session id `%s` is not valid\n", tag, session.ToString( buffer1 ) );
		return TryHandlingAnonymousConnection( userInfo, "This server requires logging in" );
	}

	if( !ticket.IsValidSessionId() ) {
		const char *format = S_COLOR_YELLOW "%s: The ticket `%s` of client `%s` is not valid\n";
		Com_Printf( format, tag, ticket.ToString( buffer1 ), session.ToString( buffer2 ) );
		return TryHandlingAnonymousConnection( userInfo, "Illegal connection ticket" );
	}

	if( TryStartingTask( NewClientConnectTask( client, session, ticket, NET_AddressToString( address ) ) ) ) {
		return session;
	}

	Com_Printf( S_COLOR_RED "%s: Can't launch a ClientConnect task\n", tag );
	return TryHandlingAnonymousConnection( userInfo, "Can't validate the connection ticket" );
}

void SVStatsowFacade::Frame() {
	tasksRunner.CheckStatus();

	// Call this regardless of the logged-in status
	// as the game module expects a non-empty UUID to start a match
	CheckMatchUuid();

	if( ourSession.IsValidSessionId() ) {
		heartbeatRunner.CheckStatus();
		return;
	}

	// Check whether we have a valid session or using a fake "FFFs" session
	if( !ourSession.IsZero() ) {
		return;
	}

	// Skip if we're changing "logged-in" status
	if( isLoggingIn || isLoggingOut ) {
		return;
	}

	// Disallow logging in for listen servers. Use a dummy server session.
	if( Cvar_Value( "dedicated" ) == 0 ) {
		ourSession = Uuid_FFFsUuid();
		return;
	}

	// Disallow logging in if MM services are not explicitly enabled. Use a dummy server session.
	if( !sv_mm_enable->integer ) {
		ourSession = Uuid_FFFsUuid();
		return;
	}

	if( !StartLoggingIn() ) {
		OnLoginFailure();
		return;
	}

	assert( isLoggingIn );
	// Wait for logging in
	while( isLoggingIn ) {
		Sys_Sleep( 16 );
		tasksRunner.CheckStatus();
	}
}

void SVStatsowFacade::LogoutBlocking() {
	if( isLoggingIn ) {
		// Just quit in this case
		return;
	}

	if( !IsValid() ) {
		return;
	}

	if( !TryStartingTask( NewLogoutTask() ) ) {
		Com_Printf( S_COLOR_RED "SVStatsowFacade::LogoutBlocking(): Can't launch a LogoutTask\n" );
		return;
	}

	isLoggingOut = true;
	// TODO: Should this be atomic? Was not in the code before.
	while( isLoggingOut ) {
		tasksRunner.CheckStatus();
		Sys_Sleep( 10 );
	}
}

void SVLogoutTask::OnQueryFailure() {
	PrintError( "OnQueryFailure", "Logout query has failed" );
	parent->OnLoggedOut();
}

void SVLogoutTask::OnQuerySuccess() {
	PrintMessage( "OnQuerySuccess", "Logout query has succeeded" );
	parent->OnLoggedOut();
}

void SVStatsowFacade::OnLoginFailure() {
	if( sv_mm_loginonly->integer ) {
		Com_Error( ERR_FATAL, "Statsow authentication has failed. `sv_mm_loginonly` value forbids running the server.\n" );
	}

	Com_Printf( S_COLOR_YELLOW "Statsow login has failed. Disabling server Statsow services.\n" );
	Cvar_ForceSet( sv_mm_enable->name, "0" );
	ourSession = Uuid_FFFsUuid();
	isLoggingIn = false;
}

void SVStatsowFacade::OnLoginSuccess() {
	Com_Printf( "Starting server Statsow services\n" );
	assert( !reliablePipe );
	reliablePipe = new( ::malloc( sizeof( ReliablePipe ) ) )ReliablePipe;
	isLoggingIn = false;
}

void SVLoginTask::OnAnyFailure() {
	parent->OnLoginFailure();
}

void SVLoginTask::OnQueryFailure() {
	OnAnyFailure();
}

void SVLoginTask::OnQuerySuccess() {
	ScopeGuard failureGuard([=]() { OnAnyFailure(); });

	constexpr const char *const tag = "OnQuerySuccess";
	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const char *sessionString = query->GetRootString( "session_id", "" );
	if( !*sessionString ) {
		PrintError( tag, "The `session_id` response field is not specified" );
		return;
	}

	if( !Uuid_FromString( sessionString, &parent->ourSession ) ) {
		PrintError( tag, "Failed to parse session string `%s`", sessionString );
		return;
	}

	failureGuard.Suppress();
	PrintMessage( tag, "Session id is %s", sessionString );
	parent->OnLoginSuccess();
}

bool SVStatsowFacade::StartLoggingIn() {
	assert( !isLoggingIn && !isLoggingOut );

	constexpr const char *tag = "SVStatsowFacade::StartLoggingIn()";

	if( sv_mm_authkey->string[0] == '\0' ) {
		Com_Printf( S_COLOR_RED "%s: The auth key is not specified\n", tag );
		return false;
	}

	if( TryStartingTask( NewLoginTask() ) ) {
		isLoggingIn = true;
		return true;
	}

	Com_Printf( S_COLOR_RED "%s: Can't launch a LoginTask\n", tag );
	return false;
}

SVStatsowFacade::SVStatsowFacade()
	: tasksRunner( this ), heartbeatRunner( this, "server", sv_ip->string ) {

	/*
	* create cvars
	* ch : had to make sv_mm_enable to cmdline only, because of possible errors
	* if enabled while players on server
	*/
	sv_mm_enable = Cvar_Get( "sv_mm_enable", "0", CVAR_ARCHIVE | CVAR_NOSET | CVAR_SERVERINFO );
	sv_mm_loginonly = Cvar_Get( "sv_mm_loginonly", "0", CVAR_ARCHIVE | CVAR_SERVERINFO );
	sv_mm_authkey = Cvar_Get( "sv_mm_authkey", "", CVAR_ARCHIVE );

	// this is used by game, but to pass it to client, we'll initialize it in sv
	Cvar_Get( "sv_skillRating", va( "%.0f", MM_RATING_DEFAULT ), CVAR_READONLY | CVAR_SERVERINFO );

	Com_Printf( "SVStatsowFacade: Initialized\n" );
}

SVStatsowFacade::~SVStatsowFacade() {
	Com_Printf( "SVStatsowFacade: Shutting down...\n" );

	if( reliablePipe ) {
		reliablePipe->~ReliablePipe();
		::free( reliablePipe );
	}

	LogoutBlocking();
}