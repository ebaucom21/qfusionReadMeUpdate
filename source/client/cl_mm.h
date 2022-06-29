#ifndef QFUSION_CL_MM_H
#define QFUSION_CL_MM_H

#include "../qcommon/mmfacade.h"

/**
 * Provides a Statsow services facade for a game client.
 */
class CLStatsowFacade {
	friend class CLStatsowTask;
	friend class CLStartLoggingInTask;
	friend class CLContinueLoggingInTask;
	friend class CLLogoutTask;
	friend class CLConnectTask;
	friend class CLMatchMakerTask;
	friend class CLCheckMatchTask;
	friend class CLCheckServerTask;
	friend class CLAcceptMatchTask;

	template <typename> friend class SingletonHolder;
	template <typename> friend class StatsowFacadeTask;
	template <typename> friend class StatsowTasksRunner;
	template <typename> friend class StatsowHeartbeatRunner;

	StatsowTasksRunner<CLStatsowFacade> tasksRunner;
	StatsowHeartbeatRunner<CLStatsowFacade> heartbeatRunner;

	mm_uuid_t ourSession { Uuid_ZeroUuid() };
	mm_uuid_t ticket { Uuid_ZeroUuid() };
	mm_uuid_t loginHandle { Uuid_ZeroUuid() };
	mm_uuid_t matchHandle { Uuid_ZeroUuid() };

	wsw::String matchAddress;

	wsw::String lastErrorMessage;
	mutable wsw::StringView lastErrorMessageView;

	wsw::String profileWebUrl;
	mutable wsw::StringView profileWebUrlView;

	wsw::String profileRmlUrl;
	mutable wsw::StringView profileRmlUrlView;

	mutable wsw::StringView baseUrlView;

	mutable char ticketStringBuffer[UUID_BUFFER_SIZE];
	mutable wsw::StringView ticketStringView;

	struct cvar_s *cl_mm_user;
	struct cvar_s *cl_mm_password;
	struct cvar_s *cl_mm_session;
	struct cvar_s *cl_mm_autologin;

	int64_t loginStartedAt { 0 };

	bool isLoggingIn { false };
	bool isLoggingOut { false };
	bool continueLogin2ndStageTask { false };
	bool hasTriedLoggingIn { false };
	bool isCheckingPendingMatch { false };

	CLStatsowFacade();
	~CLStatsowFacade();

	class CLStartLoggingInTask *NewStartLoggingInTask( const char *user, const char *password );
	class CLContinueLoggingInTask *NewContinueLoggingInTask( const mm_uuid_t &handle );
	class CLLogoutTask *NewLogoutTask();
	class CLConnectTask *NewConnectTask( const char *address );
	class CLCheckMatchTask *NewCheckMatchTask();
	class CLCheckServerTask *NewCheckServerTask();
	class CLAcceptMatchTask *NewAcceptTask();

	// Just to get the code working ... use caching for a final implementation
	template <typename Task, typename... Args>
	Task *NewTaskStub( Args... args ) {
		if( void *mem = ::malloc( sizeof( Task ) ) ) {
			return new( mem )Task( args... );
		}
		return nullptr;
	}

	// Just to get the code working... use caching for a final implementation
	template <typename Task>
	void DeleteTaskStub( Task *task ) {
		if( task ) {
			tasksRunner.UnlinkTask( task );
			task->~Task();
		}
		::free( task );
	}

	template <typename Task>
	void DeleteTask( Task *task ) {
		DeleteTaskStub( task );
	}

	/**
	 * Saves an error message to be displayed in the UI.
	 * @param format a printf format string
	 * @param ... format arguments
	 */
#ifndef _MSC_VER
	void ErrorMessage( const char *format, ... )
		__attribute__( ( format( printf, 2, 3 ) ) );
#else
	void ErrorMessage( _Printf_format_string_ const char *format, ... );
#endif

	/**
	 * Saves an error message to be displayed in the UI.
	 * Prints an error to console as well. Two additional arguments are supplied for this purpose
	 * (these arguments do not get shown in UI to avoid user confusion).
	 * @param classTag a name of enclosing class for printing in console
	 * @param methodTag a name of enclosing method for printing in console
	 * @param format a printf format string
	 * @param ... format arguments
	 */
#ifndef _MSC_VER
	void ErrorMessage( const char *classTag, const char *methodTag, const char *format, ... )
		__attribute__( ( format( printf, 4, 5 ) ) );
#else
	void ErrorMessage( const char *classTag, const char *methodTag, _Printf_format_string_ const char *format, ... );
#endif

	void SaveErrorString( const char *format, va_list args );

	bool ContinueLoggingIn();
	bool StartLoggingIn( const char *user, const char *password );

	void OnLoginSuccess();
	void OnLoginFailure();
	void OnLogoutCompleted();

	template <typename Task>
	bool TryStartingTask( Task *task ) {
		return tasksRunner.TryStartingTask( task );
	}

	const wsw::StringView &GetStringAsView( const wsw::String &s, wsw::StringView *view ) const {
		*view = wsw::StringView( s.data(), s.length() );
		return *view;
	}
public:
	static void Init();
	static void Shutdown();
	static CLStatsowFacade *Instance();

	/**
	 * The client has successfully connected to the Statsow backend and performed authentication if true.
	 */
	bool IsValid() { return ourSession.IsValidSessionId(); }

	void Frame();
	bool WaitUntilConnectionAllowed();
	void CheckOrWaitForAutoLogin();
	void CheckOrWaitForPendingMatch();
	void CheckOrWaitForLoggingOutAtExit();
	void PollLoginStatus();
	bool StartConnecting( const struct netadr_s *address );

	/**
	 * The volatile specifier is just to avoid an IDE inspection warning.
	 * All operations tied to polling of {@code QueryObject} status
	 * are performed in the main client thread.
	 */
	void WaitBlockingWhile( const volatile bool *waitForCompletion );
	void DoWaitingStep();

	template <typename Task>
	bool StartAndWaitForCompletion( Task *task, bool *waitForCompletion, const char *methodTag, const char *taskTag );

	bool Login( const char *user, const char *password );
	bool Logout( bool waitForCompletion );

	int GetLoginState() const;

	const wsw::StringView &GetLastErrorMessage() const {
		return GetStringAsView( lastErrorMessage, &lastErrorMessageView );
	}

	const wsw::StringView &GetProfileWebUrl() const {
		return GetStringAsView( profileWebUrl, &profileWebUrlView );
	}

	const wsw::StringView &GetProfileRmlUrl() const {
		return GetStringAsView( profileRmlUrl, &profileRmlUrlView );
	}

	const wsw::StringView &GetBaseWebUrl() const;

	const wsw::StringView &GetTicketString() const {
		ticket.ToString( ticketStringBuffer );
		ticketStringView = wsw::StringView( ticketStringBuffer, UUID_DATA_LENGTH );
		return ticketStringView;
	}
};

#endif
