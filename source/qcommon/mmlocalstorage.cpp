#include "qcommon.h"
#include "mmlocalstorage.h"
#include "singletonholder.h"

#include "../../third-party/sqlite-amalgamation/sqlite3.h"

#include <initializer_list>
#include <functional>

/**
 * Allows to tie a connection lifecycle to a lexical scope.
 */
struct ScopedConnectionGuard {
	LocalReliableStorage *const parent;
	DBConnection connection;

	/**
	 * Accepts already (maybe) existing connection.
	 */
	ScopedConnectionGuard( LocalReliableStorage *parent_, DBConnection connection_ )
		: parent( parent_ ), connection( connection_ ) {}

	/**
	 * Requests the {@code LocalReliableStorage} parent to create a connection.
	 */
	explicit ScopedConnectionGuard( LocalReliableStorage *parent_ )
		: parent( parent_ ), connection( parent_->NewConnection() ) {}

	/**
	 * Allows passing this object as a {@code DBConnection} without explicit casts.
	 */
	operator DBConnection() {
		return connection;
	}

	/**
	 * Call this if the held connection should be closed forcefully right now for some reasons.
	 */
	void ForceClosing() {
		if( connection ) {
			(void)::sqlite3_close( connection );
			connection = nullptr;
		}
	}

	~ScopedConnectionGuard() {
		if( connection ) {
			parent->DeleteConnection( connection );
		}
	}
};

/**
 * A base class for wrappers over raw connections.
 */
class SQLiteAdapter {
protected:
	DBConnection const connection;

	explicit SQLiteAdapter( DBConnection connection_ ): connection( connection_ ) {}
public:
	/**
	 * Checks whether the active connection has entered a transaction
	 */
	bool IsInTransaction() const {
		return ::sqlite3_get_autocommit( connection ) == 0;
	}
};

/**
 * Provides an interface for execution of SQL statements in immediate mode.
 */
class SQLiteExecAdapter: public SQLiteAdapter {
	bool ExecImpl( const char *sql );
	void ExecOrFailImpl( const char *sql );
public:
	explicit SQLiteExecAdapter( DBConnection connection ) : SQLiteAdapter( connection ) {}

	bool Exec( const char *sql ) { return ExecImpl( sql ); }
	bool ExecV( const char *sql, ... );
	void ExecOrFail( const char *sql ) { ExecOrFailImpl( sql ); }
	void ExecOrFailV( const char *format, ... );

	bool Begin() {
		// Some notes...
		// Override the default "deferred" isolation level that is very weak.
		// Use the most strict mode possible.
		return Exec( "begin exclusive" );
	}

	bool Commit() { return Exec( "commit" ); }
	bool Rollback() { return Exec( "rollback" ); }
};

bool SQLiteExecAdapter::ExecImpl( const char *sql ) {
	const char *tag = "SQLiteExecAdapter::ExecImpl";
	Com_DPrintf( "%s: About to execute `%s`\n", tag, sql );

	char *err = nullptr;
	const int code = ::sqlite3_exec( connection, sql, nullptr, nullptr, &err );
	if( code == SQLITE_OK || code == SQLITE_DONE ) {
		return true;
	}

	if( !err ) {
		return false;
	}

	Com_Printf( S_COLOR_RED "%s: An error `%s` occurred while executing `%s`\n", tag, err, sql );
	::sqlite3_free( err );
	return false;
}

void SQLiteExecAdapter::ExecOrFailImpl( const char *sql ) {
	const char *tag = "SQLiteExecAdapter::ExecOrFailImpl()";
	Com_DPrintf( "%s: About to execute `%s`\n", tag, sql );

	char *err = nullptr;
	if( ::sqlite3_exec( connection, sql, nullptr, nullptr, &err ) == SQLITE_OK ) {
		return;
	}

	if( !err ) {
		Com_Error( ERR_FATAL, "%s: An error occurred while executing `%s`\n", tag, sql );
	}

	char buffer[2048];
	// Assemble a message in a local buffer and call Com_Error() only after ::sqlite3_free() call is performed
	Q_snprintfz( buffer, sizeof( buffer ), "%s: An error `%s` occurred while executing `%s`", tag, err, sql );
	::sqlite3_free( err );

	Com_Error( ERR_FATAL, "%s\n", buffer );
}

bool SQLiteExecAdapter::ExecV( const char *format, ... ) {
	char buffer[2048];
	va_list va;
	va_start( va, format );
	// TODO: Check retval
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	va_end( va );
	return ExecImpl( buffer );
}

void SQLiteExecAdapter::ExecOrFailV( const char *format, ... ) {
	char buffer[2048];
	va_list va;
	va_start( va, format );
	// TODO: Check retval
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	va_end( va );
	ExecOrFailImpl( buffer );
}

template <typename T> bool SQLiteBindArg( sqlite3_stmt *stmt, int index, const T &value ) {
	return T::implement_specialization_for_this_type();
}

template <> bool SQLiteBindArg( sqlite3_stmt *stmt, int index, const wsw::StringView &value ) {
	const int code = ::sqlite3_bind_text( stmt, index, value.data(), (int)value.size(), SQLITE_STATIC );
	if( code == SQLITE_OK ) {
		return true;
	}

	const char *format = S_COLOR_RED "A binding of arg #%d `%s`(@%d #chars) failed with `%s`\n";
	Com_Printf( format, index, value, (int)value.size(), ::sqlite3_errstr( code ) );
	return false;
}

static inline bool SQLiteBindCStringArg( sqlite3_stmt *stmt, int index, const char *value ) {
	const int code = ::sqlite3_bind_text( stmt, index, value, -1, SQLITE_STATIC );
	if( code == SQLITE_OK ) {
		return true;
	}

	const char *format = S_COLOR_RED "A binding of arg #%d `%s` failed with `%s`\n";
	Com_Printf( format, index, value, ::sqlite3_errstr( code ) );
	return false;
}

using ConstCharPtr = const char *;
using CharPtr = char *;

template <> bool SQLiteBindArg( sqlite3_stmt *stmt, int index, const ConstCharPtr &value ) {
	return SQLiteBindCStringArg( stmt, index, value );
}

template <> bool SQLiteBindArg( sqlite3_stmt *stmt, int index, const CharPtr &value ) {
	return SQLiteBindCStringArg( stmt, index, value );
}

template <> bool SQLiteBindArg( sqlite3_stmt *stmt, int index, const int &value ) {
	const int code = ::sqlite3_bind_int( stmt, index, value );
	if( code == SQLITE_OK ) {
		return true;
	}

	const char *format = S_COLOR_RED "A binding of arg #%d = %d failed with `%s`\n";
	Com_Printf( format, index, value, ::sqlite3_errstr( code ) );
	return false;
}

/**
 * Defines a helper for insertion of multiple rows sequentially
 * given a statement for instertion of a single row.
 */
class SQLiteInsertAdapter : public SQLiteAdapter {
	sqlite3_stmt *stmt { nullptr };
public:
	SQLiteInsertAdapter( DBConnection connection_, const char *sql_ )
		: SQLiteAdapter( connection_ ) {
		const int code = ::sqlite3_prepare_v2( connection_, sql_, -1, &stmt, nullptr );
		if( code == SQLITE_OK ) {
			return;
		}

		const char *tag = "SQLiteInsertAdapter::SQLiteInsertAdapter()";
		const char *format = S_COLOR_RED "%s: An error `%s` occurred while trying to prepare `%s`\n";
		Com_Printf( format, tag, ::sqlite3_errstr( code ), sql_ );
	}

	~SQLiteInsertAdapter() {
		if( !stmt ) {
			return;
		}

		const int code = ::sqlite3_finalize( stmt );
		if( code == SQLITE_OK ) {
			return;
		}

		const char *tag = "SQLiteInsertAdapter::~SQLiteInsertAdapter()";
		const char *format = S_COLOR_RED "%s: An error `%s` occurred while trying to finalize a statement\n";
		Com_Printf( format, tag, ::sqlite3_errstr( code ) );
	}

	// OK lets just add a specialization for the actually used call singature...
	// Variadic templates are still horrible for real use...
	template <typename Arg0, typename Arg1, typename Arg2>
	bool InsertNextRow( Arg0 arg0, Arg1 arg1, Arg2 arg2 ) {
		if( !stmt ) {
			return false;
		}

		const char *tag = "SQLiteInsertHelper::InsertNextRow()";

		const int numBoundParams = ::sqlite3_bind_parameter_count( stmt );
		if( numBoundParams != 3 ) {
			Com_Printf( S_COLOR_RED "%s: Illegal number of bound parameters: %d (3 is expected)\n", tag, numBoundParams );
			return false;
		}

		// Query parameter indices are 1-based
		if( !SQLiteBindArg( stmt, 1, arg0 ) ) return false;
		if( !SQLiteBindArg( stmt, 2, arg1 ) ) return false;
		if( !SQLiteBindArg( stmt, 3, arg2 ) ) return false;

		int code;
		if( ( code = ::sqlite3_step( stmt ) ) != SQLITE_DONE ) {
			Com_Printf( S_COLOR_RED "%s: An error `%s` occurred while performing a step\n", tag, ::sqlite3_errstr( code ) );
			return false;
		}

		if( ( code = ::sqlite3_reset( stmt ) ) != SQLITE_OK ) {
			Com_Printf( S_COLOR_YELLOW "%s: An error `%s` occurred while resetting a stmt\n", tag, ::sqlite3_errstr( code ) );
		}
		if( ( code = ::sqlite3_clear_bindings( stmt ) ) != SQLITE_OK ) {
			Com_Printf( S_COLOR_YELLOW "%s: An error `%s` occurred while clearing bindings\n", tag, ::sqlite3_errstr( code ) );
		}

		return true;
	}
};

/**
 * A helper for data retrieval from a raw SQLite data rows.
 * Only minimal methods set for our needs is provided.
 */
class SQLiteRowReader {
	friend class SQLiteSelectAdapter;

	sqlite3_stmt *stmt;
	explicit SQLiteRowReader( sqlite3_stmt *stmt_ ): stmt( stmt_ ) {}
public:
	int NumColumns() const {
		return ::sqlite3_data_count( stmt );
	}

	const wsw::StringView GetString( int num ) const {
		assert( (unsigned)num < (unsigned)NumColumns() );
		auto *data = (const char *)::sqlite3_column_text( stmt, num );
		assert( data && "Nullable columns are not supported\n" );
		int numBytes = ::sqlite3_column_bytes( stmt, num );
		return wsw::StringView( data, (size_t)numBytes );
	}
};

/**
 * A helper for retrieval of rows produced by a SELECT query.
 */
class SQLiteSelectAdapter : public SQLiteAdapter {
	sqlite3_stmt *stmt { nullptr };
public:
	using RowConsumer = std::function<bool(const SQLiteRowReader &)>;

#ifndef _MSC_VER
	SQLiteSelectAdapter( DBConnection connection, const char *format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
#else
	SQLiteSelectAdapter( DBConnection connection, _Printf_format_string_ const char *format, ... );
#endif

	~SQLiteSelectAdapter() {
		if( stmt ) {
			::sqlite3_finalize( stmt );
		}
	}

	/**
	 * Executes a query step and applies the {@code rowConsumer} if needed.
	 * @param rowConsumer a {@code RowConsumer} that may process a supplied row.
	 * @return a positive value if there was a row fetched and the {@code rowConsumer} has been invokeked successfully.
	 * @return zero if a query is completed.
	 * @return a negative value if an error has occurred.
	 */
	int Next( const RowConsumer &rowConsumer ) {
		if( !stmt ) {
			return -1;
		}

		const int code = ::sqlite3_step( stmt );
		if( code == SQLITE_ROW ) {
			return rowConsumer( SQLiteRowReader( stmt ) ) ? +1 : -1;
		}
		if( code == SQLITE_DONE ) {
			return 0;
		}

		const char *format = S_COLOR_RED "SQLiteSelectAdapter::Next(): An error `%s` occurred while performing a step\n";
		Com_Printf( format, ::sqlite3_errstr( code ) );
		(void)::sqlite3_finalize( stmt );
		stmt = nullptr;
		return -1;
	}

	/**
	 * Tries to read rows sequentially until a query is completed.
	 * Applies a {@code RowConsumer} for every read row.
	 * @param rowConsumer a {@code RowConsumer} that could process a row.
	 * @return a number of read rows on success, a negative value on failure
	 */
	int TryReadingAll( const RowConsumer &rowConsumer ) {
		int numRows = 0;
		for(;; ) {
			int stepResult = Next( rowConsumer );
			if( stepResult > 0 ) {
				numRows++;
				continue;
			}
			// A negative value indicates a failure
			return stepResult == 0 ? numRows : -1;
		}
	}
};

SQLiteSelectAdapter::SQLiteSelectAdapter( DBConnection connection_, const char *format, ... )
	: SQLiteAdapter( connection_ ) {
	char sql[2048];
	va_list va;
	va_start( va, format );
	Q_vsnprintfz( sql, sizeof( sql ), format, va );
	va_end( va );

	const char *tag = "SQLiteSelectAdapter::SQLiteSelectAdapter()";
	Com_DPrintf( "%s: About to prepare `%s`\n", tag, sql );

	const int code = sqlite3_prepare_v2( connection, sql, -1, &stmt, nullptr );
	if( code == SQLITE_OK ) {
		return;
	}

	const char *messageFormat = S_COLOR_RED "%s: An error `%s` occurred while trying to prepare `%s`\n";
	Com_Printf( messageFormat, tag, sql, ::sqlite3_errstr( code ) );
}

LocalReliableStorage::LocalReliableStorage( const char *databasePath_ ) {
	// Actually never fails... or a failure is discovered immediately
	this->databasePath = ::strdup( databasePath_ );

	const char *tag = "LocalReliableStorage::LocalReliableStorage()";

	ScopedConnectionGuard connection( this );
	if( !connection ) {
		const char *format = "%s: Can't open an initial connection. Is the path `%s` valid and accessible?\n";
		Com_Error( ERR_FATAL, format, tag, databasePath_ );
	}

	if( !CreateTablesIfNeeded( connection ) ) {
		// Close the connection before triggering a failure.
		// We do not know what kind of locks SQLite uses.
		connection.ForceClosing();
		Com_Error( ERR_FATAL, "%s: Can't create or check existence of tables\n", tag );
	}

	Com_Printf( "A local reliable storage has been successfully initialized at `%s`\n", databasePath_ );
}

DBConnection LocalReliableStorage::NewConnection() {
	DBConnection connection = nullptr;
	const char *const tag = "LocalReliableStorage::NewConnection()";
	const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
	if( ::sqlite3_open_v2( databasePath, &connection, flags, nullptr ) == SQLITE_OK ) {
		Com_DPrintf( "%s: An SQLite connection %p has been successfully opened\n", tag, (const void *)connection );
		return connection;
	}

	const char *format = S_COLOR_RED "%s: Can't open an SQLite connection to `%s`: `%s`\n";
	Com_Printf( format, tag, databasePath, sqlite3_errmsg( connection ) );
	(void )::sqlite3_close( connection );
	return nullptr;
}

void LocalReliableStorage::DeleteConnection( DBConnection connection ) {
	const int code = ::sqlite3_close( connection );
	if( code == SQLITE_OK ) {
		return;
	}

	constexpr const char *tag = "LocalReliableStorage::DeleteConnection";

	if( code == SQLITE_BUSY ) {
		const char *format =
			"%s: An SQLiteConnection %p has some non-finalized "
			"child resources and is going to remain open. Aborting...\n";
		Com_Error( ERR_FATAL, format, tag, (const void *)connection );
	}

	Com_Error( ERR_FATAL, "%s: Unknown ::sqlite3_close() error for %p. Aborting...\n", tag, (const void *)connection );
}

bool LocalReliableStorage::CreateTablesIfNeeded( DBConnection connection ) {
	constexpr const char *const queriesFormat =
		"create table if not exists %s ("
		"	query_priority integer not null,"
		"	query_id text not null primary key,"
		"	query_url text not null);";

	constexpr const char *const fieldsTable =
		"create table if not exists query_fields ("
		"	query_id text not null,"
		"	field_name text not null,"
		"	field_value text not null);";

	SQLiteExecAdapter adapter( connection );
	// We can execute this first (no foreign constraints are defined) actually to fit line limit
	if( !adapter.ExecV( fieldsTable ) ) {
		return false;
	}

	return adapter.ExecV( queriesFormat, "pending_queries" ) && adapter.ExecV( queriesFormat, "failed_queries" );
}

bool LocalReliableStorage::WithinTransaction( std::function<bool( DBConnection )> &&block ) {
	ScopedConnectionGuard connection( this );
	if( !connection ) {
		return false;
	}

	SQLiteExecAdapter execAdapter( connection );
	if( !execAdapter.Begin() ) {
		return false;
	}

	if( block( connection ) ) {
		if( !execAdapter.Commit() ) {
			return false;
		}
	} else {
		if( !execAdapter.Rollback() ) {
			return false;
		}
	}

	return true;
}

bool LocalReliableStorage::Push( DBConnection connection, QueryObject *query, int priority ) {
	// We add a "synthetic" query_id field to queries.
	// They are purely for this query serialization system
	// to mark entries/fields of different queries.
	// Even if they may be transmitted along with queries occasionally
	// they are meaningless for the Statsow server and get omitted.
	if( query->FindFormParamByName( "query_id" ) ) {
		Com_Error( ERR_FATAL, "A `query_id` field is already present in the query object\n" );
	}

	// We may use an arbitrary random character sequence
	// but let's use UUID's for consistency with the rest of the codebase.
	char queryIdAsString[UUID_BUFFER_SIZE];
	mm_uuid_t::Random().ToString( queryIdAsString );

	// We must set this to be able to recover query_id from query results
	query->SetField( "query_id", queryIdAsString );

	return InsertPendingQuery( connection, query, priority ) && InsertQueryFields( connection, query );
}

bool LocalReliableStorage::InsertPendingQuery( DBConnection connection, const QueryObject *query, int priority ) {
	const char *sql = "insert into pending_queries (query_priority, query_id, query_url) values (?, ?, ?);";
	SQLiteInsertAdapter adapter( connection, sql );
	assert( adapter.IsInTransaction() );
	return adapter.InsertNextRow( priority, query->FindFormParamByName( "query_id" ), query->url );
}

bool LocalReliableStorage::InsertQueryFields( DBConnection connection, const QueryObject *query ) {
	const char *sql = "insert into query_fields(query_id, field_name, field_value) values (?, ?, ?);";
	SQLiteInsertAdapter adapter( connection, sql );
	assert( adapter.IsInTransaction() );

	const char *tag = "LocalReliableStorage::InsertQueryFields()";

	const char *queryId = GetQueryId( query );
	for( auto formParam = query->formParamsHead; formParam; formParam = formParam->next ) {
		const wsw::StringView id( queryId, UUID_DATA_LENGTH );
		Com_DPrintf( "%s: writing (`query id`, `%s`)\n", tag, queryId );
		const wsw::StringView name( formParam->name, formParam->nameLen );
		Com_DPrintf( "%s: writing (`name`, `%s`)\n", tag, formParam->name );
		const wsw::StringView value( formParam->value, formParam->valueLen );
		Com_DPrintf( "%s: writing (`value`, `%s`)\n", tag, formParam->value );
		if( !adapter.InsertNextRow( id, name, value ) ) {
			return false;
		}
	}

	return true;
}

QueryObject *LocalReliableStorage::FetchNext( DBConnection connection, const char *queryOutgoingIp ) {
	// This CTE selects a best numeric query priority value in the table
	const char *priorityCte =
		"select distinct query_priority as priority from pending_queries order by query_priority desc limit 1";

	// This CTE selects id and url of a random query that has the previously selected priority
	const char *chosenCte =
		"select query_id, query_url from pending_queries "
		"where exists (select 1 from top_priority where top_priority.priority = pending_queries.query_priority) "
		"order by random() limit 1";

	// Select all fields that belong to the chosen query.
	// Return the url in the first row.
	const char *sqlFormat =
		"with top_priority as (%s), chosen as (%s) "
		"select chosen.query_url, query_fields.field_name, query_fields.field_value "
		"from query_fields join chosen "
		"on query_fields.query_id = chosen.query_id";

	QueryObject *query = nullptr;

	auto printReadRow = [&]( const char *name, const wsw::StringView &value ) {
		constexpr const char *tag = "LocalReliableStorage::FetchNext()";
		// TODO: This is quite error-prone (string views currently point to zero-terminated data
		// but there's no strict guarantee). Introduce and use streams instead.

		// Hack to always print the read URL regardless of "developer" cvar value
		if( !Q_stricmp( name, "url" ) ) {
			Com_Printf( "%s: the URL is `%s`\n", tag, value.data() );
		} else {
			Com_DPrintf( "%s: the `%s` is `%s`\n", tag, name, value.data());
		}
	};

	const auto rowConsumer = [&]( const SQLiteRowReader &reader ) -> bool {
		assert( reader.NumColumns() == 3 );
		if( !query ) {
			auto url( reader.GetString( 0 ) );
			printReadRow( "url", url );
			query = QueryObject::PostQueryForUrl( url.data(), queryOutgoingIp );
			if( !query ) {
				return false;
			}
		}
		const auto name( reader.GetString( 1 ) );
		printReadRow( "field name", name );
		const auto value( reader.GetString( 2 ) );
		printReadRow( "field value", value );
		query->SetField( name.data(), name.size(), value.data(), value.size() );
		return true;
	};

	SQLiteSelectAdapter adapter( connection, sqlFormat, priorityCte, chosenCte );
	assert( adapter.IsInTransaction() );
	if( adapter.TryReadingAll( rowConsumer ) > 0 ) {
		// At least a single row has been read so the query must have been created
		assert( query );
		// This won't harm ... even if there was no an actual attachment
		query->hasConveredJsonToFormParam = true;
		return query;
	}

	// The query does not get created if no rows are retrieved
	if( query ) {
		QueryObject::DeleteQuery( query );
	}

	return nullptr;
}

const char *LocalReliableStorage::GetQueryId( const QueryObject *query ) {
	const char *queryIdAsString = query->FindFormParamByName( "query_id" );
	if( !queryIdAsString ) {
		Com_Error( ERR_FATAL, "The query object is missing `query_id` field\n" );
	}

	// Check whether it is a valid UUID for consistency
	// but do not actually convert result to UUID
	// (we would have to convert this back to string in that case)

	mm_uuid_t tmp;
	if( !Uuid_FromString( queryIdAsString, &tmp ) ) {
		Com_Error( ERR_FATAL, "The query id string `%s` is not a valid UUID\n", queryIdAsString );
	}

	return queryIdAsString;
}

bool LocalReliableStorage::MarkAsSent( DBConnection connection, const QueryObject *query ) {
	SQLiteExecAdapter adapter( connection );
	assert( adapter.IsInTransaction() );

	const char *queryId = GetQueryId( query );
	Com_Printf( "LocalReliableStorage::MarkAsSent(): query id is %s\n", queryId );

	// No foreign constraints are defined for various reasons
	// (namely query id can refer to 2 different tables)
	// so we have to execute two delete queries...
	// Custom triggers are not really maintainable in this environment.
	constexpr const char *format1 = "delete from pending_queries where query_id = '%s'";
	constexpr const char *format2 = "delete from query_fields where query_id = '%s'";
	return adapter.ExecV( format1, queryId ) && adapter.ExecV( format2, queryId );
}

bool LocalReliableStorage::MarkAsFailed( DBConnection connection, const QueryObject *query ) {
	// We could try using a CTE but that would look horrible.
	// Just execute 2 queries given we're in a transaction context.
	SQLiteExecAdapter adapter( connection );
	assert( adapter.IsInTransaction() );

	const char *queryId = GetQueryId( query );
	Com_Printf( "LocalReliableStorage::MarkAsFailed(): query id is %s\n", queryId );

	if( !adapter.ExecV( "insert into failed_queries select * from pending_queries where query_id = '%s'", queryId ) ) {
		return false;
	}

	return adapter.ExecV( "delete from pending_queries where query_id = '%s'", queryId );
}