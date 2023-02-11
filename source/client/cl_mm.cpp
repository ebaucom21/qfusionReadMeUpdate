/*
Copyright (C) 2007 Will Franklin.

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

#include "cl_mm.h"

#include "../qcommon/mmcommon.h"
#include "../qcommon/mmquery.h"
#include "../qcommon/mmnetworktask.h"

#include "../qcommon/base64.h"
#include "../qcommon/singletonholder.h"

#include <errno.h>

static void CL_MM_Logout_f() {
	CLStatsowFacade::Instance()->Logout( false );
}

static void CL_MM_Login_f() {
	const char *user = nullptr, *password = nullptr;

	// first figure out the user
	if( Cmd_Argc() > 1 ) {
		user = Cmd_Argv( 1 );
	}
	if( Cmd_Argc() > 2 ) {
		password = Cmd_Argv( 2 );
	}

	CLStatsowFacade::Instance()->Login( user, password );
}

/**
 * A base class for all client-side descendants of {@code StatsowFacadeTask}
 * that provides some shared client-side-specific utilities.
 */
class CLStatsowTask : public StatsowFacadeTask<CLStatsowFacade> {
protected:
	CLStatsowTask( CLStatsowFacade *parent_, const char *name_, const char *resource_, unsigned retryDelay_ = 0 )
		: StatsowFacadeTask( parent_, name_, va( "client/%s", resource_ ) ) {
		this->retryDelay = retryDelay_;
		if( !query ) {
			return;
		}

		// QueryObject treats network errors as allowing retry by default.
		// This is fine for the server side but not for clients.

		query->ClearRetryErrorFlags();
		query->AddRetryErrorFlags( QueryObject::ErrorFlags::ServerFailure );
	}

	bool CheckResponseStatus( const char *methodTag, bool displayInUi = false ) const {
		return CheckParsedResponse( methodTag, displayInUi ) && CheckStatusField( methodTag, displayInUi );
	}

	bool CheckParsedResponse( const char *methodTag, bool displayInUi = false ) const;
	bool CheckStatusField( const char *methodTag, bool displayInUi = false ) const;

	bool GetResponseUuid( const char *methodTag, const char *field, mm_uuid_t *result, const char **stringToShow = 0 );
};

bool CLStatsowTask::CheckParsedResponse( const char *methodTag, bool displayInUi ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	if( query->ResponseJsonRoot() ) {
		return true;
	}
	const char *desc = "Failed to parse a JSON response";
	if( displayInUi ) {
		parent->ErrorMessage( name, methodTag, "%s", desc );
	} else {
		Com_Printf( "%s::%s(): %s\n", name, methodTag, desc );
	}
	return false;
}

bool CLStatsowTask::CheckStatusField( const char *methodTag, bool displayInUi ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	double status = query->GetRootDouble( "status", std::numeric_limits<double>::infinity() );
	if( !std::isfinite( status ) ) {
		const char *desc = "Can't find a numeric `status` field in the response";
		if( displayInUi ) {
			parent->ErrorMessage( name, methodTag, "%s", desc );
		} else {
			Com_Printf( S_COLOR_YELLOW "%s::%s: %s\n", name, methodTag, desc );
		}
		return false;
	}

	if( status != 0 ) {
		return true;
	}

	const char *error = query->GetRootString( "error", "" );
	const char *desc;
	char buffer[MAX_STRING_CHARS];
	if( *error ) {
		desc = va_r( buffer, sizeof( buffer ), "Request error at remote host: `%s`", error );
	} else {
		desc = "Unspecified error at remote host";
	}
	if( displayInUi ) {
		parent->ErrorMessage( name, methodTag, "%s", desc );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s::%s(): %s\n", name, methodTag, desc );
	}

	return false;
}

bool CLStatsowTask::GetResponseUuid( const char *methodTag, const char *field,
	                                 mm_uuid_t *result, const char **stringToShow ) {
	const char *uuidString = query->GetRootString( field, "" );
	if( !*uuidString ) {
		PrintError( methodTag, "Can't find the `%s` string field in the response", field );
		return false;
	}

	if( !mm_uuid_t::FromString( uuidString, result ) ) {
		PrintError( methodTag, "Can't parse the `%s` string `%s` as an UUID", field, uuidString );
		return false;
	}

	if( stringToShow ) {
		*stringToShow = uuidString;
	}

	return true;
}

class CLStartLoggingInTask : public CLStatsowTask {
	void OnQueryResult( bool succeeded ) override {
		// Check whether logging in has not timed out during frames
		if( parent->isLoggingIn ) {
			// Call the superclass method
			CLStatsowTask::OnQueryResult( succeeded );
		}
	}
public:
	CLStartLoggingInTask( CLStatsowFacade *parent_, const char *user_, const char *password_ )
		: CLStatsowTask( parent_, "CLStartLoggingInTask", "login", 750 ) {
		assert( user_ && *user_ );
		assert( password_ && *password_ );
		if( !query ) {
			return;
		}
		query->SetLogin( user_ );
		query->SetPassword( password_ );
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLContinueLoggingInTask : public CLStatsowTask {
	void ParseAdditionalInfo( const cJSON *root );
	void ParseRatingsSection( const cJSON *section );
	void ParsePlayerInfoSection( const cJSON *section );

	void OnQueryResult( bool succeeded ) override {
		// Check whether logging in has not timed out during frames
		if( parent->isLoggingIn ) {
			// Call the superclass method
			CLStatsowTask::OnQueryResult( succeeded );
		}
	}

	bool QueryResultForbidsRetry() override {
		// See comments to this overridden method in SVFetchMatchUuidTask
		return false;
	}

	bool IsRetryPermittedNow() override {
		return parent->continueLogin2ndStageTask;
	}
public:
	CLContinueLoggingInTask( CLStatsowFacade *parent_, const mm_uuid_t &handle_ )
		: CLStatsowTask( parent_, "CLContinueLoggingInTask", "login", 1000 ) {
		assert( handle_.IsValidSessionId() );
		if( query ) {
			query->SetHandle( handle_ );
		}
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLLogoutTask : public CLStatsowTask {
public:
	explicit CLLogoutTask( CLStatsowFacade *parent_ )
		: CLStatsowTask( parent_, "CLLogoutTask", "logout", 1000 ) {
		assert( parent->ourSession.IsValidSessionId() );
		if( query ) {
			query->SetClientSession( parent->ourSession );
		}
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLConnectTask : public CLStatsowTask {
	void OnQuerySuccess() override;
	void OnQueryFailure() override;

	void OnAnyOutcome() {
		// Proceed regardless of getting ticket status (success or failure).
		// The client connection is likely to be rejected
		// (unless the server allows non-authorized players)
		// but we should stop holding the client in "getting ticket" state.
		Com_Printf( "CLConnectTask::OnAnyOutcome(): Using ticket %s\n", parent->GetTicketString().data() );
		CL_SetClientState( CA_CONNECTING );
	}
public:
	CLConnectTask( CLStatsowFacade *parent_, const char *address_ )
		: CLStatsowTask( parent_, "CLConnectTask", "connect", 750 ) {
		if( !query ) {
			return;
		}
		assert( parent->ourSession.IsValidSessionId() );
		query->SetClientSession( parent->ourSession );
		query->SetServerAddress( address_ );
	}
};

class CLMatchMakerTask : public CLStatsowTask {
	bool *const waitForTaskCompletion;

	void OnQueryFailure() override;
protected:
	CLMatchMakerTask( CLStatsowFacade *parent_, bool *waitForTaskCompletion_, const char *name_, const char *resource_ )
		: CLStatsowTask( parent_, name_, resource_ ), waitForTaskCompletion( waitForTaskCompletion_ ) {
		assert( waitForTaskCompletion_ );
	}

	void OnAnyFailure() {
		parent->matchHandle = Uuid_ZeroUuid();
		parent->matchAddress.clear();
		InterruptWaitingForTaskCompletion();
	}

	void InterruptWaitingForTaskCompletion() {
		*waitForTaskCompletion = false;
	}

	bool GetMatchPresentStatus( const char *tag, int *presentStatus ) {
		double value = query->GetRootDouble( "present", std::numeric_limits<double>::infinity() );
		if( !std::isfinite( value ) ) {
			PrintError( tag, "A `present` field of the response is missing" );
			return false;
		}
		*presentStatus = Q_sign( value );
		return true;
	}
};

class CLCheckMatchTask : public CLMatchMakerTask {
	void OnQuerySuccess() override;
public:
	CLCheckMatchTask( CLStatsowFacade *parent_, bool *waitForTaskCompletion_ )
		: CLMatchMakerTask( parent_, waitForTaskCompletion_, "CLCheckMatchTask", "mm/checkMatch" ) {
		if( query ) {
			query->SetClientSession( parent->ourSession );
		}
	}
};

class CLCheckServerTask : public CLMatchMakerTask {
	void OnQuerySuccess() override;
public:
	CLCheckServerTask( CLStatsowFacade *parent_, bool *waitForTaskCompletion_ )
		: CLMatchMakerTask( parent_, waitForTaskCompletion_, "CLCheckServerTask", "mm/checkServer" ) {
		if( !query ) {
			return;
		}
		query->SetClientSession( parent->ourSession );
		query->SetMatchId( parent->matchHandle );
	}
};

class CLAcceptMatchTask : public CLMatchMakerTask {
	void OnQuerySuccess() override;
public:
	CLAcceptMatchTask( CLStatsowFacade *parent_, bool *waitForTaskCompletion_ )
		: CLMatchMakerTask( parent_, waitForTaskCompletion_, "CLAcceptMatchTask", "mm/acceptOrReject" ) {
		if( !query ) {
			return;
		}
		query->SetClientSession( parent->ourSession );
		query->SetMatchId( parent->matchHandle );
		// Currently just set "accepted" status by default
		query->SetAccepted( true );
	}
};

static SingletonHolder<CLStatsowFacade> instanceHolder;

void CLStatsowFacade::Init() {
	::instanceHolder.init();
}

void CLStatsowFacade::Shutdown() {
	::instanceHolder.shutdown();
}

CLStatsowFacade *CLStatsowFacade::Instance() {
	return ::instanceHolder.instance();
}

CLStartLoggingInTask *CLStatsowFacade::NewStartLoggingInTask( const char *user, const char *password ) {
	return NewTaskStub<CLStartLoggingInTask>( this, user, password );
}

CLContinueLoggingInTask *CLStatsowFacade::NewContinueLoggingInTask( const mm_uuid_t &handle ) {
	return NewTaskStub<CLContinueLoggingInTask>( this, handle );
}

CLLogoutTask *CLStatsowFacade::NewLogoutTask() {
	return NewTaskStub<CLLogoutTask>( this );
}

CLConnectTask *CLStatsowFacade::NewConnectTask( const char *address ) {
	return NewTaskStub<CLConnectTask>( this, address );
}

CLCheckMatchTask *CLStatsowFacade::NewCheckMatchTask() {
	return NewTaskStub<CLCheckMatchTask>( this, &isCheckingPendingMatch );
}

CLCheckServerTask *CLStatsowFacade::NewCheckServerTask() {
	return NewTaskStub<CLCheckServerTask>( this, &isCheckingPendingMatch );
}

CLAcceptMatchTask *CLStatsowFacade::NewAcceptTask() {
	return NewTaskStub<CLAcceptMatchTask>( this, &isCheckingPendingMatch );
}

void CLConnectTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "Request error" );
	OnAnyOutcome();
}

void CLConnectTask::OnQuerySuccess() {
	ScopeGuard scopeGuard([this]() { OnAnyOutcome(); });

	constexpr const char *const tag = "OnQuerySuccess";

	auto *const jsonRoot = query->ResponseJsonRoot();
	if( !jsonRoot ) {
		parent->ErrorMessage( name, tag, "Failed to parse response data" );
		return;
	}

	if( !CheckStatusField( tag ) ) {
		return;
	}

	const char *ticketString = query->GetRootString( "ticket", "" );
	if( !*ticketString ) {
		parent->ErrorMessage( name, tag, "The server have not supplied a ticket" );
		return;
	}

	if( !mm_uuid_t::FromString( ticketString, &parent->ticket ) ) {
		parent->ErrorMessage( name, tag, "The ticket `%s` is malformed", ticketString );
		return;
	}
}

bool CLStatsowFacade::StartConnecting( const netadr_t *address ) {
	this->ticket = Uuid_ZeroUuid();

	if( !IsValid() ) {
		return false;
	}

	if( TryStartingTask( NewConnectTask( NET_AddressToString( address ) ) ) ) {
		return true;
	}

	// TODO: Is UI going to show this message? Seemingly no.
	ErrorMessage( "CLStatsowFacade", "StartConnecting", "Can't launch a connect task" );
	return false;
}

bool CLStatsowFacade::WaitUntilConnectionAllowed() {
	while( isLoggingIn || isLoggingOut ) {
		DoWaitingStep();
	}
	return ticket.IsValidSessionId();
}

void CLStatsowFacade::CheckOrWaitForAutoLogin() {
	if( hasTriedLoggingIn ) {
		return;
	}

	hasTriedLoggingIn = true;

	const int autoLogin = cl_mm_autologin->integer;
	if( !autoLogin ) {
		return;
	}

	Login( nullptr, nullptr );

	while( isLoggingIn ) {
		DoWaitingStep();
		PollLoginStatus();
	}

	// Interrupt if we have not logged in successfully or checking for pending match is not specified
	if( !ourSession.IsValidSessionId() || autoLogin > 0 ) {
		return;
	}

	CheckOrWaitForPendingMatch();
}

void CLStatsowFacade::DoWaitingStep() {
	Sys_Sleep( 16 );
	tasksRunner.CheckStatus();
}

void CLStatsowFacade::WaitBlockingWhile( const volatile bool *condition ) {
	assert( condition );
	while( *condition ) {
		DoWaitingStep();
	}
}

template <typename Task>
bool CLStatsowFacade::StartAndWaitForCompletion( Task *task, bool *waitForCompletion, const char *methodTag, const char *taskTag ) {
	*waitForCompletion = true;

	if( !TryStartingTask( task ) ) {
		Com_Printf( S_COLOR_RED "%s: Can't spawn a `%s` task\n", methodTag, taskTag );
		*waitForCompletion = false;
		return false;
	}

	WaitBlockingWhile( waitForCompletion );
	return true;
}

void CLStatsowFacade::CheckOrWaitForPendingMatch() {
	assert( !isLoggingIn && !isLoggingOut );
	assert( ourSession.IsValidSessionId() );

	constexpr const char *const methodTag = "CLStatsowFacade::CheckOrWaitForPendingMatch";
	ScopeGuard failureGuard( [&]() {
		this->matchHandle = Uuid_ZeroUuid();
		this->matchAddress.clear();
	});

	matchHandle = Uuid_ZeroUuid();

	if( !StartAndWaitForCompletion( NewCheckMatchTask(), &isCheckingPendingMatch, methodTag, "check match" ) ) {
		return;
	}

	if( !matchHandle.IsValidSessionId() ) {
		return;
	}

	if( !StartAndWaitForCompletion( NewCheckServerTask(), &isCheckingPendingMatch, methodTag, "check server" ) ) {
		return;
	}

	if( matchAddress.empty() ) {
		return;
	}

	if( !StartAndWaitForCompletion( NewAcceptTask(), &isCheckingPendingMatch, methodTag, "accept" ) ) {
		return;
	}

	// Might have been reset on the last task failure
	if( matchAddress.empty() ) {
		return;
	}

	Com_Printf( "About to append a connect command using address `%s`\n", matchAddress.c_str() );
	Cbuf_ExecuteText( EXEC_APPEND, va( "connect \"%s\"", matchAddress.c_str() ) );
}

void CLStatsowFacade::PollLoginStatus() {
	if( !isLoggingIn ) {
		return;
	}

	// Wait for obtaining a valid handle
	if( !loginHandle.IsValidSessionId() ) {
		return;
	}

	// Check whether this continuation flag for the task is not set yet.
	// This means the task has not been launched yet.
	if( !continueLogin2ndStageTask ) {
		if( ContinueLoggingIn() ) {
			assert( continueLogin2ndStageTask );
		}
		return;
	}

	// The task is still running.
	assert( continueLogin2ndStageTask );
	// Check whether logging in process has timed out.
	if( Sys_Milliseconds() + 10 * 1000 > loginStartedAt ) {
		return;
	}

	ErrorMessage( "CLStatsowFacade", "Frame", "Login timed out" );
	OnLoginFailure();
}

void CLStatsowFacade::Frame() {
	tasksRunner.CheckStatus();

	if( ourSession.IsValidSessionId() ) {
		if( !isLoggingOut ) {
			heartbeatRunner.CheckStatus();
		}
		return;
	}

	if( !isLoggingIn ) {
		CheckOrWaitForAutoLogin();
		return;
	}

	PollLoginStatus();
}

void CLLogoutTask::OnQueryFailure() {
	PrintError( "OnQueryFailure", "Logout request failed" );
	// The Statsow server is going to close the session anyway.
	// We stop sending heartbeats at this moment.
	parent->OnLogoutCompleted();
}

void CLLogoutTask::OnQuerySuccess() {
	PrintMessage( "OnQuerySuccess", "Logout request succeeded" );
	parent->OnLogoutCompleted();
}

void CLStatsowFacade::OnLogoutCompleted() {
	profileWebUrl.clear();
	profileRmlUrl.clear();
	lastErrorMessage.clear();
	isLoggingIn = false;
	isLoggingOut = false;
	ourSession = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();
	loginHandle = Uuid_ZeroUuid();
}

bool CLStatsowFacade::Logout( bool waitForCompletion ) {
	constexpr const char *const name = "CLStatsowFacade";
	constexpr const char *const tag = "Logout";

	if( !IsValid() ) {
		ErrorMessage( name, tag, "Is not logged in" );
		return false;
	}

	if( isLoggingOut ) {
		ErrorMessage( name, tag, "Is already logging out" );
		return false;
	}

	// TODO: check clientstate, has to be unconnected
	if( CL_GetClientState() > CA_DISCONNECTED ) {
		ErrorMessage( name, tag, "Can't logout from MM while connected to server" );
		return false;
	}

	// Set this before starting the task
	isLoggingOut = true;
	if( !TryStartingTask( NewLogoutTask() ) ) {
		ErrorMessage( name, tag, "Failed to start a logout task" );
		isLoggingOut = false;
		return false;
	}

	if( !waitForCompletion ) {
		Com_DPrintf( "CLStatsowFacade::Logout(): Returning early without waiting for completion\n" );
		return true;
	}

	const auto timeoutAt = Sys_Milliseconds();
	while( isLoggingOut && Sys_Milliseconds() < ( timeoutAt + MM_LOGOUT_TIMEOUT ) ) {
		DoWaitingStep();
	}

	// Consider the call result successful anyway
	// (the Statsow server is going to close the session if we are not going to send heartbeats)
	return true;
}

void CLStatsowFacade::OnLoginSuccess() {
	if( !isLoggingIn ) {
		return;
	}

	assert( lastErrorMessage.empty() );
	assert( ourSession.IsValidSessionId() );

	loginHandle = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();

	char buffer[UUID_BUFFER_SIZE];
	ourSession.ToString( buffer );
	Com_Printf( "CLStatsowFacade::OnLoginSuccess(): The session id is %s\n", buffer );
	Cvar_ForceSet( cl_mm_session->name, buffer );

	hasTriedLoggingIn = true;
	isLoggingIn = false;
	continueLogin2ndStageTask = false;
}

void CLStatsowFacade::OnLoginFailure() {
	if( !isLoggingIn ) {
		return;
	}

	Cvar_ForceSet( cl_mm_session->name, "" );
	ourSession = Uuid_FFFsUuid();
	loginHandle = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();

	hasTriedLoggingIn = true;
	isLoggingIn = false;
	continueLogin2ndStageTask = false;
}

void CLStartLoggingInTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "First-stage login query failure" );
	parent->OnLoginFailure();
}

void CLStartLoggingInTask::OnQuerySuccess() {
	constexpr const char *const tag = "OnQuerySuccess";
	
	ScopeGuard failureGuard( [this]() {
		parent->ErrorMessage( name, tag, "Login failure" );
		parent->OnLoginFailure();
	});

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const char *handleString = "";
	if( !GetResponseUuid( tag, "handle", &parent->loginHandle, &handleString ) ) {
		return;
	}

	PrintMessage( tag, "Got login process handle %s", handleString );
	// Allow the second state of the login process
	// (this field is set to a huge value by default)
	failureGuard.Suppress();
}

void CLContinueLoggingInTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "Second-stage login query failure" );
	parent->OnLoginFailure();
}

void CLContinueLoggingInTask::OnQuerySuccess() {
	constexpr const char *tag = "OnQuerySuccess";

	Com_DPrintf( "%s::%s(): The raw response is `%s`\n", name, tag, query->RawResponse() ? query->RawResponse() : "" );

	ScopeGuard failureGuard( [this]() {
		parent->ErrorMessage( name, tag, "Login failure" );
		parent->OnLoginFailure();
	});

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const auto ready = (int)query->GetRootDouble( "ready", 0 );
	if( ready != 1 && ready != 2 ) {
		PrintError( tag, "Bad response `ready` value %d", ready );
		return;
	}

	if( ready == 1 ) {
		// Request an explicit task retry on success
		hasRequestedRetry = true;
		failureGuard.Suppress();
		return;
	}

	if( !GetResponseUuid( tag, "session_id", &parent->ourSession ) ) {
		return;
	}

	failureGuard.Suppress();

	ParseAdditionalInfo( query->ResponseJsonRoot() );
	parent->OnLoginSuccess();
}

void CLContinueLoggingInTask::ParseAdditionalInfo( const cJSON *root ) {
	assert( parent->profileWebUrl.empty() );
	assert( parent->profileRmlUrl.empty() );

	constexpr const char *const tag = "CLContinueLoggingInTask::ParseAdditionalInfo()";

	ObjectReader rootReader( root );
	if( const cJSON *section = rootReader.GetArray( "ratings" ) ) {
		ParseRatingsSection( section );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s: A `ratings` field of the response is missing\n", tag );
	}

	if( const cJSON *section = rootReader.GetObject( "player_info" ) ) {
		ParsePlayerInfoSection( section );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s: A `player_info` field of the response is missing\n", tag );
	}
}

void CLContinueLoggingInTask::ParseRatingsSection( const cJSON *section ) {
	auto consumer = [=]( const char *gametype, float rating, float ) {
		// Just print to console right now
		Com_DPrintf( "Gametype: `%s`, rating: %d\n", gametype, (int)rating );
	};
	StatsowFacadeTask::ParseRatingsSection( section, consumer );
}

void CLContinueLoggingInTask::ParsePlayerInfoSection( const cJSON *section ) {
	assert( section && section->type == cJSON_Object );

	ObjectReader infoReader( section );
	const char *profileWebUrl = infoReader.GetString( "profile_web_url", "" );
	const char *profileRmlUrl = infoReader.GetString( "profile_rml_url", profileWebUrl );

	parent->profileRmlUrl.assign( profileRmlUrl );
	parent->profileWebUrl.assign( profileWebUrl );

	const char *lastLoginAddress = infoReader.GetString( "last_login_ip", "N/A" );
	const char *lastLoginTimestamp = infoReader.GetString( "last_login_timestamp", "N/A" );
	Com_Printf( "Last logged in from `%s` at `%s`\n", lastLoginAddress, lastLoginTimestamp );
}

bool CLStatsowFacade::ContinueLoggingIn() {
	assert( isLoggingIn );
	assert( loginHandle.IsValidSessionId() );

	// Set this prior to launching the task
	continueLogin2ndStageTask = true;
	if( TryStartingTask( NewContinueLoggingInTask( loginHandle ) ) ) {
		return true;
	}

	continueLogin2ndStageTask = false;
	Com_Printf( S_COLOR_RED "CLStatsowFacade::ContinueLoggingIn(): Can't launch a task\n" );
	OnLoginFailure();
	return false;
}

bool CLStatsowFacade::StartLoggingIn( const char *user, const char *password ) {
	constexpr const char *const classTag = "CLStatsowFacade";
	constexpr const char *const methodTag = "StartLoggingIn";

	assert( !isLoggingIn && !isLoggingOut );
	assert( user && *user );
	assert( password && *password );

	if( CL_GetClientState() > CA_DISCONNECTED ) {
		ErrorMessage( classTag, methodTag, "Can't login while connecting to a server" );
		return false;
	}

	Com_DPrintf( "CLStatsowFacade::StartLoggingIn(): Using `%s`, `%s`\n", user, password );

	if( TryStartingTask( NewStartLoggingInTask( user, password ) ) ) {
		loginStartedAt = Sys_Milliseconds();
		hasTriedLoggingIn = true;
		isLoggingIn = true;
		return true;
	}

	ErrorMessage( classTag, methodTag, "Can't launch a login task" );
	return false;
}

void CLMatchMakerTask::OnQueryFailure() {
	PrintError( "OnQueryFailure", "The query has failed" );
	OnAnyFailure();
}

void CLCheckMatchTask::OnQuerySuccess() {
	ScopeGuard failureGuard( [&]() { OnAnyFailure(); } );

	constexpr const char *const tag = "OnQuerySuccess";
	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	int presentStatus = 0;
	if( !GetMatchPresentStatus( tag, &presentStatus ) ) {
		return;
	}

	if( presentStatus <= 0 ) {
		PrintMessage( tag, "There's no pending match\n" );
		failureGuard.Suppress();
		InterruptWaitingForTaskCompletion();
		return;
	}

	const char *stringToShow = nullptr;
	if( !this->GetResponseUuid( tag, "match_id", &parent->matchHandle, &stringToShow ) ) {
		return;
	}

	PrintMessage( tag, "Got pending match handle %s\n", stringToShow );
	failureGuard.Suppress();
	InterruptWaitingForTaskCompletion();
}

void CLCheckServerTask::OnQuerySuccess() {
	ScopeGuard failureGuard( [&]() { OnAnyFailure(); } );

	constexpr const char *const tag = "OnQuerySuccess";
	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	int presentStatus;
	if( !GetMatchPresentStatus( tag, &presentStatus ) ) {
		return;
	}

	// If the match has been canceled
	if( presentStatus < 0 ) {
		PrintMessage( tag, "The match has been canceled" );
		failureGuard.Suppress();
		InterruptWaitingForTaskCompletion();
		return;
	}

	// Keep waiting for server address assignation
	if( presentStatus == 0 ) {
		hasRequestedRetry = true;
		failureGuard.Suppress();
		return;
	}

	const char *const addressString = query->GetRootString( "server_address", "" );
	if( !*addressString ) {
		PrintError( tag, "A `server_address` field of the response is missing" );
		return;
	}

	netadr_t tmp;
	if( !NET_StringToAddress( addressString, &tmp ) ) {
		PrintError( tag, "The `server_address` value `%s` is not a valid address", addressString );
		return;
	}

	PrintMessage( tag, "Got a server address %s", addressString );
	parent->matchAddress.assign( addressString );
	failureGuard.Suppress();
	InterruptWaitingForTaskCompletion();
}

void CLAcceptMatchTask::OnQuerySuccess() {
	constexpr const char *const tag = "OnQuerySuccess";
	if( !CheckResponseStatus( tag ) ) {
		OnAnyFailure();
		return;
	}

	PrintMessage( tag, "An offer reply has been successfully confirmed" );
	InterruptWaitingForTaskCompletion();
}

int CLStatsowFacade::GetLoginState() const {
	if( ourSession.IsValidSessionId() ) {
		return MM_LOGIN_STATE_LOGGED_IN;
	}
	return isLoggingIn ? MM_LOGIN_STATE_IN_PROGRESS : MM_LOGIN_STATE_LOGGED_OUT;
}

const wsw::StringView &CLStatsowFacade::GetBaseWebUrl() const {
	baseUrlView = wsw::StringView( APP_MATCHMAKER_WEB_URL );
	return baseUrlView;
}

static inline bool IsNullOrEmpty( const char *s ) {
	if ( !s ) {
		return true;
	}
	return !*s;
}

bool CLStatsowFacade::Login( const char *user, const char *password ) {
	lastErrorMessage.clear();

	if( isLoggingIn || isLoggingOut ) {
		return false;
	}

	if( IsNullOrEmpty( user ) ) {
		user = cl_mm_user->string;
		if( IsNullOrEmpty( user ) ) {
			return false;
		}
		// Save the user if the auto-login is not in "secret" mode
		if( cl_mm_autologin->integer > 0 ) {
			Cvar_ForceSet( cl_mm_user->name, user );
		}
	}

	if( !IsNullOrEmpty( password ) ) {
		// Save the password is the auto-login is not in "secret" mode
		if( cl_mm_autologin->integer > 0 ) {
			MM_PasswordWrite( user, password );
		}
		return StartLoggingIn( user, password );
	}

	// Let the command line have a priority over the local password storage.
	// Credentials supplied via a command line should not be saved.
	password = cl_mm_password->string;
	if( IsNullOrEmpty( password ) ) {
		password = MM_PasswordRead( user );
	}

	if( !IsNullOrEmpty( password ) ) {
		return StartLoggingIn( user, password );
	}

	ErrorMessage( "CLStatsowFacade", "Login", "Can't retrieve a password from a local storage or command line" );
	return false;
}

void CLStatsowFacade::ErrorMessage( const char *format, ... ) {
	va_list va;
	va_start( va, format );
	SaveErrorString( format, va );
	va_end( va );
}

void CLStatsowFacade::ErrorMessage( const char *classTag, const char *methodTag, const char *format, ... ) {
	va_list va;
	va_start( va, format );
	SaveErrorString( format, va );
	va_end( va );

	Com_Printf( S_COLOR_RED "%s::%s(): %s\n", classTag, methodTag, lastErrorMessage.c_str() );
}

void CLStatsowFacade::SaveErrorString( const char *format, va_list args ) {
	lastErrorMessage.clear();

	char localBuffer[1024];

	va_list va;
	va_copy( va, args );
	int result = Q_vsnprintfz( localBuffer, sizeof( localBuffer ), format, va );
	va_end( va );

	if( result >= 0 ) {
		lastErrorMessage.assign( localBuffer );
		return;
	}

	char *p = nullptr;
#ifndef _WIN32
	result = ::vasprintf( &p, format, va );
	va_end( va );

#if !defined( __linux__ ) || !defined( __GLIBC__ )
	#error vasprintf error reporting behaviour on the platform should be checked
#endif

	// This check applies to GLIBC only
	if( result >= 0 ) {
		lastErrorMessage.assign( p );
	}

	if( p ) {
		::free( p );
	}
#else
	size_t size = 4000u;
	for(;; ) {
		p = (char *)::malloc( size );
		if( !p ) {
			break;
		}

		va_copy( va, args );
		result = Q_vsnprintfz( p, size, format, va );
		va_end( va );

		if( result >= 0 ) {
			lastErrorMessage.assign( p );
			::free( p );
		}

		::free( p );
		size = size < ( 1u << 16 ) ? 2 * size : ( 3 * size ) / 2;
	}
#endif
}

CLStatsowFacade::CLStatsowFacade()
	: tasksRunner( this ), heartbeatRunner( this, "client" ) {
	/*
	* create cvars
	*/
	cl_mm_session = Cvar_Get( "cl_mm_session", "", CVAR_READONLY | CVAR_USERINFO );
	cl_mm_autologin = Cvar_Get( "cl_mm_autologin", "1", CVAR_ARCHIVE );

	cl_mm_user = Cvar_Get( "cl_mm_user", "", CVAR_ARCHIVE );
	cl_mm_password = Cvar_Get( "cl_mm_password", "", CVAR_NOSET );

	/*
	* add commands
	*/
	Cmd_AddCommand( "mm_login", CL_MM_Login_f );
	Cmd_AddCommand( "mm_logout", CL_MM_Logout_f );

	Cvar_ForceSet( cl_mm_session->name, "" );
}

void CLStatsowFacade::CheckOrWaitForLoggingOutAtExit() {
	if( !ourSession.IsValidSessionId() ) {
		return;
	}

	// Check whether we have already initiated background logout.
	// Try starting non-blocking logging out otherwise so we can use the same code path
	if( !isLoggingOut ) {
		Logout( false );
	}

	// If we continue logging out or we have just initiated logging out successfully wait for it
	WaitBlockingWhile( &isLoggingOut );
}

CLStatsowFacade::~CLStatsowFacade() {
	CheckOrWaitForLoggingOutAtExit();

	Cvar_ForceSet( cl_mm_session->name, "0" );

	Cmd_RemoveCommand( "mm_login" );
	Cmd_RemoveCommand( "mm_logout" );
}
