/*
Copyright (C) 2011 Christian Holmberg

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

#ifndef __MM_QUERY_H__
#define __MM_QUERY_H__

#include <functional>

#include "mmrating.h"

#include "../qcommon/wswcurl.h"
#include "../qcommon/cjson.h"

/**
 * A proxy that wraps an underlying {@code cJSON} value and provides convenient accessor methods.
 */
class NodeReader {
protected:
	const cJSON *const underlying;

	explicit NodeReader( const cJSON *underlying_ ): underlying( underlying_ ) {}

	static const char *AsString( const cJSON *node, const char *nameAsField = nullptr );
	static double AsDouble( const cJSON *node, const char *nameAsField = nullptr );
};

/**
 * A wrapper over a raw {@code cJSON} object value that allows retrieval of fields by name.
 */
class ObjectReader: public NodeReader {
	friend class ArrayReader;
public:
	explicit ObjectReader( const cJSON *underlying_ ): NodeReader( underlying_ ) {
		assert( underlying_->type == cJSON_Object );
	}

	const char *GetString( const char *field, const char *defaultValue = nullptr ) const;
	double GetDouble( const char *field, double defaultValue = std::numeric_limits<double>::quiet_NaN() ) const;

	cJSON *GetObject( const char *field ) const;
	cJSON *GetArray( const char *field ) const;
};

/**
 * A wrapper over a raw {@code cJSON} object value that allows sequential iteration and retrieval of elements.
 */
class ArrayReader: public NodeReader {
	friend class ObjectReader;

	const cJSON *child;
public:
	explicit ArrayReader( const cJSON *underlying_ ): NodeReader( underlying_ ) {
		assert( underlying_->type == cJSON_Array );
		child = underlying_->child;
	}

	bool IsDone() const { return !child; }

	void Next() {
		assert( !IsDone() );
		child = child->next;
	}

	bool IsAtArray() const { return child && child->type == cJSON_Array; }
	ArrayReader GetChildArray() const { return ArrayReader( child ); }
	bool IsAtObject() const { return child && child->type == cJSON_Object; }
	ObjectReader GetChildObject() const { return ObjectReader( child ); }
};

/**
 * An object that helps managing a query lifecycle including
 * setting predefined form parameters and arbitrary JSON attachments,
 * starting execution of a query, checking a query current status and result retrieval.
 * @note most of methods are defined inline and hence available at every inclusion site.
 * Some methods are way too tied with {@code qcommon} stuff and have to be exported in modules (namely the game module).
 * The {@code FailWith} call should be defined in modules appropriately as well.
 */
class QueryObject final {
	friend class JsonWriter;
	friend class NodeReader;
	friend class ObjectReader;
	friend class ArrayReader;
	friend class LocalReliableStorage;
	friend class ReliablePipe;
	friend class StatsowNetworkTask;
	template <typename> friend class StatsowTasksRunner;
	template <typename> friend class StatsowHeartbeatRunner;
public:
	using CompletionCallback = std::function<void( QueryObject * )>;
private:
	/**
	 * An implementation of this is left for every binary it gets included.
	 */
#ifndef _MSC_VER
	[[noreturn]]
	static void FailWith( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
#else
	[[noreturn]]
	static void FailWith( _Printf_format_string_ const char *format, ... );
#endif
	/**
	 * Gets invoked by CURL completion callbacks.
	 * Useful if the query gets begun by {@code QueryObject::
	 */
	CompletionCallback completionCallback = []( QueryObject * ) {};

	/**
	 * A JSON root of (an outgoing) request.
	 * Gets created on demand.
	 * The request must not be in "started" state for a valid access to this.
	 * @note this root gets converted to a regular form parameter
	 * for making query restart functionality code uniform.
	 * @note should be released via {@code cJSON_Delete(cJSON *)} call.
	 */
	cJSON *requestRoot { nullptr };

	/**
	 * A root of a response JSON (if any).
	 * The request must be in "ready" state and must succeed for a valid access to this.
	 * @note should be released via {@code cJSON_Delete(cJSON *)} call.
	 */
	cJSON *responseRoot { nullptr };

	/**
	 * A current (active) request.
	 * A successful {@code QueryObject::Prepare()} invocation creates this request.
	 * @note should be released via {@code ::wswcurl_delete(wswcurl_req_s *)} call.
	 */
	wswcurl_req_s *req { nullptr };

	/**
	 * Just copy a request to this var and it is going to be deleted
	 * (we do not want to link against {@code wswcurl} in the game module.
	 * This is for supporting query restart functionality.
	 * @note should be released via {@code ::wswcurl_delete(wswcurl_req_s *)} call.
	 */
	wswcurl_req_s *oldReq { nullptr };

	/**
	 * A full request URL.
	 * @note should be released via {@code ::free(void *)} call.
	 */
	char *url { nullptr };

	/**
	 * An interface (outgoing IP to be clear) of this host that should be used for making a request.
	 * Statsow expect server requests to come from IP's/interfaces specified in server credentials.
	 * This field is not mandatory for clients.
	 * @note should be released via {@code ::free(void *)} call.
	 * @note {@code interface} could look better but it's a MSVC reserved word.
	 */
	char *outgoingIp { nullptr };

	/**
	 * A raw response string.
	 * The request must be in "ready" state for a valid access to this.
	 * @note should be released via {@code ::free(void *)} call.
	 */
	char *rawResponse { nullptr };

	/**
	 * Stores copies of form parameters for query restarts.
	 * (they are not attached directly to a CURL object).
	 * This is also very useful for requests serialization/deserialization.
	 */
	struct FormParam {
		FormParam *next { nullptr };
		const char *name { nullptr };
		const char *value { nullptr };
		// TODO: Once again, we're looking forward to ability to use C++17 string_view for our builds
		uint32_t nameLen { 0 };
		uint32_t valueLen { 0 };
	};

	FormParam *formParamsHead { nullptr };
	/**
	 * Indicates whether we have already converted JSON request root to a form parameter
	 * (so there is a present JSON root even if the corresponding class field is null).
	 * Useful for requests serialization to / deserialization from a local storage.
	 */
	bool hasConveredJsonToFormParam { false };

public:
	/**
	 * Defines kinds of errors that may occur.
	 */
	enum class ErrorFlags: uint32_t {
		OtherFailure = ( 1 << 4 ),
		NetworkFailure = ( 1 << 5 ),
		ServerFailure = ( 1 << 6 ),
		MalformedRequest = ( 1 << 7 ),
		MalformedResponse = ( 1 << 8 ),
	};

	/**
	 * Clears a mask that defines kinds of errors that allow a query retry.
	 */
	void ClearRetryErrorFlags() {
		retryErrorFlags = (ErrorFlags)0;
	}

	/**
	 * Adds a kind of errors that allows a query retry to a recoverable errors mask.
	 */
	void AddRetryErrorFlags( ErrorFlags flagsToAdd ) {
		retryErrorFlags = (ErrorFlags)( (uint32_t)retryErrorFlags | (uint32_t)flagsToAdd );
	}
private:
	ErrorFlags retryErrorFlags {
		(ErrorFlags)( (uint32_t)ErrorFlags::NetworkFailure | (uint32_t)ErrorFlags::ServerFailure )
	};

	enum class Status: uint32_t {
		Created = ( 1 << 0 ),
		Started = ( 1 << 1 ),
		Succeeded = ( 1 << 2 ),
		ExplicitRetry = ( 1 << 3 ),
		OtherFailure = ( 1 << 4 ),
		NetworkFailure = ( 1 << 5 ),
		ServerFailure = ( 1 << 6 ),
		MalformedRequest = ( 1 << 7 ),
		MalformedResponse = ( 1 << 8 ),
	};

	static_assert( (int)ErrorFlags::OtherFailure == (int)Status::OtherFailure, "" );
	static_assert( (int)ErrorFlags::NetworkFailure == (int)Status::NetworkFailure, "" );
	static_assert( (int)ErrorFlags::ServerFailure == (int)Status::ServerFailure, "" );
	static_assert( (int)ErrorFlags::MalformedRequest == (int)Status::MalformedRequest, "" );
	static_assert( (int)ErrorFlags::MalformedResponse == (int)Status::MalformedResponse, "" );

	/**
	 * An atomic request status indicator used for status polling.
	 */
	std::atomic<Status> status { Status::Created };

	/**
	 * Indicates whether this is a POST query (GET otherwise).
	 * Using two disjoint types would look better but unforturnately
	 * operating on polymorphic types is complicated across module bounds.
	 */
	bool isPostQuery { false };

	/**
	 * A request should delete self once completion callback returns if this flag is set.
	 * Otherwise {@code QueryObject::DeleteQuery(QueryObject *)} should be invoked manually.
	 */
	bool deleteOnCompletion { false };

	/**
	 * A helper for using instead of calling a constructor.
	 * We still try avoiding using exceptions whether possible.
	 */
	static QueryObject *NewQuery( const char *outgoingIp_ );

	/**
	 * Another helper for multiphase construction
	 */
	static QueryObject *NewQuery( const char *outgoingIp, char *combinedUrl );

	/**
	 * A helper for query deserialization by the {@code ReliablePipe}.
	 * @param url_ an URL string that must be a valid url
	 * @return a new {@code QueryObject} for the url, null on failure.
	 */
	static QueryObject *PostQueryForUrl( const char *url_, const char *outgoingIp_ = nullptr );

	~QueryObject();

	const char *FindFormParamByName( const char *name ) const;

	void ClearFormData();

	static char *CombineHostAndResource( const char *host, const char *resource, const char *postfix = "" );

	/**
	 * Creates a CURL request and fills pending/postponed query parameters.
	 */
	bool Prepare();

	/**
	 * Converts the request JSON object to a regular form parameter
	 * (a serialization to string, a zlib compression and a base-64 encoding are performed).
	 */
	bool ConvertJsonToEncodedForm();

	/**
	 * A raw (plain C) completion callback for CURL system.
	 * Invokes a proper {@code Handle*} method, the {@code completionCallback}
	 * and deletes the object if {@code deleteOnCompletion} flag is set.
	 * @param req the executed request
	 * @param wswStatus the status code. An HTTP status if positive.
	 * Corresponds to a negated CURL error code if an error occurred.
	 * @param customp a type-erased this object passed for the callback.
	 */
	static void RawCallback( wswcurl_req *req, int wswStatus, void *customp );

	/**
	 * Handles situations when {@code wswStatus} parameter supplied to
	 * {@code RawCallback()} corresponds to some other (non-HTTP) CURL error.
	 */
	void HandleOtherFailure( wswcurl_req *req, int wswStatus );

	/**
	 * Handles situations when {@code wswStatus} parameter supplied to
	 * {@code RawCallback()} corresponds to a CURL-detected HTTP error.
	 */
	void HandleHttpFailure( wswcurl_req *req, int wswStatus );

	/**
	 * Handles situations when a non-negative {@code wswStatus} parameter
	 * has been supplied to {@code RawCallback()}.
	 * An attempt to get and parse response body should be made.
	 * {@code this->status} should be set appropriately.
	 */
	void HandleHttpSuccess( wswcurl_req *req );

	/**
	 * Sets the object completion status. There is no strict memory ordering implied.
	 * In worst case the updated status is going to be read next polling frame.
	 */
	void SetStatus( Status status_ ) {
		this->status.store( status_, std::memory_order_relaxed );
	}

	/**
	 * A helper method that checks whether a response field access is valid.
	 * @param itemToAccess a name of an item (a field) to access.
	 */
	void CheckReadyStatusForAccess( const char *itemToAccess ) const {
		Status actualStatus = this->status.load( std::memory_order_seq_cst );
		if( actualStatus < Status::Succeeded ) {
			FailWith( "Attempt to get %s while the request is not ready yet", itemToAccess );
		} else if( actualStatus > Status::Succeeded ) {
			FailWith( "Attempt to get %s while the request has not succeeded", itemToAccess );
		}
	}

	/**
	 * Launches a CURL request setting callbacks. Sets a "started" status of the object.
	 */
	void Fire();

	/**
	 * A helper for the object status retrieval, checks the actual status against the supplied one.
	 * @warning the object must be in "ready" state for calling this.
	 */
	bool TestCompletionStatus( Status testedStatus ) const {
		return GetCompletionStatus() == testedStatus;
	}

	QueryObject &SetField( const char *name, const mm_uuid_t &value ) {
		char buffer[UUID_BUFFER_SIZE];
		return SetField( name, value.ToString( buffer ) );
	}

	static inline char *CopyString( char *p, const char *s, size_t len ) {
		memcpy( p, s, len );
		p[len] = '\0';
		return p + len;
	}

	void SetGetQueryField( const char *name, size_t nameLen, const char *value, size_t valueLen ) {
		assert( url );

		// GET request, store parameters
		// add in '=', '&' and '\0' = 3

		size_t currLen = strlen( url );
		// FIXME: add proper URL encode
		size_t len = currLen + 3 + nameLen + valueLen;
		url = (char *)realloc( url, len );

		char *p = url + currLen;
		p = CopyString( p, name, nameLen );
		*p++ = '=';
		p = CopyString( p, value, valueLen );
		*p++ = '&';
		*p = '\0';
	}

	void SetPostQueryField( const char *name, size_t nameLen, const char *value, size_t valueLen ) {
		auto *p = (char *)::malloc( sizeof( FormParam ) + nameLen + 1 + valueLen + 1 );
		auto *param = new( p )FormParam;
		p += sizeof( FormParam );

		param->name = p;
		param->nameLen = (uint32_t)nameLen;
		p = CopyString( p, name, nameLen );

		// Skip zero byte after name
		p++;

		param->value = p;
		param->valueLen = (uint32_t)valueLen;
		CopyString( p, value, valueLen );

		param->next = formParamsHead;
		formParamsHead = param;
	}

	Status GetCompletionStatus() const {
		Status actualStatus = status.load( std::memory_order_seq_cst );
		if( actualStatus < Status::Succeeded ) {
			FailWith( "Attempt to test status of request that is not ready yet" );
		}
		return actualStatus;
	}

	/**
	 * Tries to start a network request without a necessity to check
	 * an individual query status every frame
	 * (but calling {@code QueryObject::Poll()} is still required).
	 * This is a specialization for {@code StatsowNetworkTask} use-case.
	 * @param callback a block of code to call once the object becomes ready.
	 * @note contrary to {@code SendDeletingOnCompletion()} the object
	 * does not get deleted automatically on completion.
	 */
	bool SendWithOnCompletion( CompletionCallback &&callback_ ) {
		return SendWithCallback( std::move( callback_ ), false );
	}

	/**
	 * A shared implementation for {@code SendDeletingOnCompletion()} and {@code SendWithOnCompletion()}
	 */
	bool SendWithCallback( CompletionCallback &&callback_, bool deleteOnCompletion_ );
public:
	/**
	 * Set a key/value request parameter.
	 * GET parameters are added to an URL immediately.
	 * POST parameters are saved and will be added as form parameters.
	 * @param name a field name
	 * @param value a field value
	 * @return this object for conformance to fluent API style.
	 * @note this method should be defined in this header within the class definition
	 * so its binary code is available for all usage sites (executables and game module).
	 */
	QueryObject &SetField( const char *name, const char *value ) {
		assert( name && value );
		size_t nameLen = ::strlen( name );
		assert( nameLen <= std::numeric_limits<uint32_t>::max() );
		size_t valueLen = ::strlen( value );
		assert( valueLen <= std::numeric_limits<uint32_t>::max() );

		return SetField( name, nameLen, value, valueLen );
	}

	QueryObject &SetField( const char *name, size_t nameLen, const char *value, size_t valueLen ) {
		// CAUTION!
		// Do not rely on input strings having zero terminators.
		// Copy the given bytes count exactly and put zero bytes after manually.

		if( isPostQuery ) {
			SetPostQueryField( name, nameLen, value, valueLen );
		} else {
			SetGetQueryField( name, nameLen, value, valueLen );
		}

		return *this;
	}

	/**
	 * Reads a root response string field. Provided for convenience.
	 * @param field the field name
	 * @param defaultValue a default value for an absent field (should be non-null)
	 * @return a string value of the read field or the supplied default value
	 * @note the field must have a string type if present.
	 */
	const char *GetRootString( const char *field, const char *defaultValue ) const {
		return ObjectReader( ResponseJsonRoot() ).GetString( field, defaultValue );
	}

	/**
	 * Reads a root response numeric field. Provided for convenience.
	 * @param field the field name
	 * @param defaultValue a default value for an absent field (should not be a NAN).
	 * @return a numeric value of the read field or the supplied default value.
	 * @note the field must have a numeric type if present.
	 */
	double GetRootDouble( const char *field, double defaultValue ) {
		return ObjectReader( ResponseJsonRoot() ).GetDouble( field, defaultValue );
	}

	/**
	 * Sets a well-known predefined "server session" form parameter.
	 */
	QueryObject &SetServerSession( const mm_uuid_t &value ) {
		return SetField( "server_session", value );
	}

	/**
	 * Sets a well-known predefined "client session" form parameter.
	 */
	QueryObject &SetClientSession( const mm_uuid_t &value ) {
		return SetField( "client_session", value );
	}

	/**
	 * Sets a well-known predefined "client-to-server connection ticket" form parameter.
	 */
	QueryObject &SetTicket( const mm_uuid_t &value ) {
		return SetField( "ticket", value );
	}

	/**
	 * Sets a well-known predefined "client login handle" (login request id) form parameter.
	 */
	QueryObject &SetHandle( const mm_uuid_t &value ) {
		return SetField( "handle", value );
	}

	/**
	 * Sets a well-known predefined "login" (username) form parameter.
	 */
	QueryObject &SetLogin( const char *value ) {
		return SetField( "login", value );
	}

	/**
	 * Sets a well-known predefined "password" form parameter.
	 */
	QueryObject &SetPassword( const char *value ) {
		return SetField( "password", value );
	}

	/**
	 * Sets a well-known predefined "server port" form parameter
	 */
	QueryObject &SetPort( int value ) {
		char buffer[64];
		std::sprintf( buffer, "%d", value );
		buffer[sizeof( buffer ) - 1] = '\0';
		return SetField( "port", buffer );
	}

	/**
	 * Sets a well-known predefined "server auth key" form parameter.
	 */
	QueryObject &SetAuthKey( const char *value ) {
		return SetField( "auth_key", value );
	}

	/**
	 * Sets a well-known predefined "server name" form parameter.
	 */
	QueryObject &SetServerName( const char *value ) {
		return SetField( "server_name", value );
	}

	/**
	 * Sets a well-known predefined "server demos base URL" form parameter.
	 */
	QueryObject &SetDemosBaseUrl( const char *value ) {
		return SetField( "demos_baseurl", value );
	}

	/**
	 * Sets a well-known predefined "server IP address" form parameter.
	 */
	QueryObject &SetServerAddress( const char *value ) {
		return SetField( "server_address", value );
	}

	/**
	 * Sets a well-known predefined "client IP address" form parameter.
	 */
	QueryObject &SetClientAddress( const char *value ) {
		return SetField( "client_address", value );
	}

	/**
	 * Sets a well-known predefined "matchmaker match id" form parameter
	 */
	QueryObject &SetMatchId( const char *value ) {
		// Validate...
		mm_uuid_t tmp;
		mm_uuid_t *id = mm_uuid_t::FromString( value, &tmp );
		assert( id );
		return SetMatchId( *id );
	}

	/**
	 * Sets a well-known predefined "matchmaker match id" form parameter.
	 */
	QueryObject &SetMatchId( const mm_uuid_t &value ) {
		return SetField( "match_id", value );
	}

	/**
	 * Sets a well known predefined "race run id" form parameter
	 */
	QueryObject &SetRaceRunId( const mm_uuid_t &value ) {
		return SetField( "race_run_id", value );
	}

	/**
	 * Sets a well-known predefined "gametype" form parameter.
	 */
	QueryObject &SetGametype( const char *value ) {
		return SetField( "gametype", value );
	}

	/**
	 * Sets a well-known predefined "participants" form parameter
	 */
	template <typename Iter>
	QueryObject &SetParticipants( Iter begin_, Iter end_ ) {
		const auto count = (int)std::distance( begin_, end_ );

#ifdef MAX_CLIENTS
		assert( count >= 0 && count < MAX_CLIENTS );
#endif

		if( !count ) {
			return SetField( "participants", "{}" );
		}

		char localBuffer[UUID_DATA_LENGTH * 2 + 4u];
		char *buffer = localBuffer;
		if( count > 2 ) {
			// Account for "{}", '\0' and (',' * count)
			buffer = (char *)::malloc( count * UUID_DATA_LENGTH + count + 4u );
		}

		char *p = buffer;
		*p++ = '{';
		for( Iter iter = begin_; iter != end_; ++iter ) {
			( *iter ).ToString( p );
			p += UUID_DATA_LENGTH;
			*p++ = ',';
		}

		p[-1] = '}';
		p[+0] = '\0';

		(void)SetField( "participants", buffer );

		if( buffer != localBuffer ) {
			::free( buffer );
		}

		return *this;
	}

	/**
	 * Sets a well-known predefined "has accepted a match" form parameter.
	 */
	QueryObject &SetAccepted( bool value ) {
		return SetField( "accepted", value ? "true" : "false" );
	}

	/**
	 * Gets a JSON response root to attach fields to.
	 * Creates it if it is necessary. The query must be a POST query.
	 * The query object should not be in "started" state.
	 */
	cJSON *RequestJsonRoot() {
		if( !isPostQuery ) {
			FailWith( "Attempt to add a JSON root to a GET request" );
		}
		if( status.load( std::memory_order_seq_cst ) >= Status::Started ) {
			FailWith( "Attempt to add a JSON root to an already started request" );
		}
		if( hasConveredJsonToFormParam ) {
			FailWith( "A JSON root has already been converted to a form param" );
		}
		if( !requestRoot ) {
			requestRoot = cJSON_CreateObject();
		}
		return requestRoot;
	}

	/**
	 * Tries to create a new query object for GET request.
	 * @param resource a resource part of a resulting URL.
	 * @param outgoingIp an interface to use for making a network request (matters for a server).
	 * @return a non-null object if the query creation has succeeded, null otherwise.
	 */
	static QueryObject *NewGetQuery( const char *resource, const char *outgoingIp = nullptr );

	/**
	 * Tries to create a new query object for POST request.
	 * @param resource a resource part of a resulting URL.
	 * @param outgoingIp an interface to use for making a network request (matters for a server).
	 * @return a non-null object if the query creating has succeeded, null otherwise.
	 */
	static QueryObject *NewPostQuery( const char *resource, const char *outgoingIp = nullptr );

	/**
	 * Deletes a query previously created by
	 * {@code QueryObject::NewGetQuery(const char *, const char *)}
	 * or {@code QueryObject::NewPostQuery(const char *, const char *).
	 * @warning the network request must not be in active state.
	 * Either do not launch a query or wait for completion.
	 */
	static void DeleteQuery( QueryObject *query );

	/**
	 * Tries to start a network request for further status polling of it.
	 * The query object should be deleted manually using
	 * {@code QueryObject::DeleteQuery(QueryObject *)} after becoming ready.
	 */
	bool SendForStatusPolling();

	/**
	 * Tries to start a network request in "fire-and-forget" fashion.
	 * @param callback a block of code to call once the object becomes ready.
	 * @note the query object would be deleted automatically on completion.
	 */
	bool SendDeletingOnCompletion( CompletionCallback &&callback_ ) {
		return SendWithCallback( std::move( callback_ ), true );
	}

	/**
	 * Should be called (every) frame if there are active queries.
	 * @fixme eliminate the necessity of doing this.
	 */
	static void Poll();

	/**
	 * Checks whether the network request has been executed (successfully or not).
	 * Its safe to call this in any state.
	 */
	bool IsReady() const {
		return status.load( std::memory_order_seq_cst ) >= Status::Succeeded;
	}

	/**
	 * Checks whether the query execution has not been fired yet.
	 * This is safe to call in any state.
	 */
	bool HasStarted() const {
		return status.load( std::memory_order_seq_cst ) >= Status::Started;
	}

	/**
	 * Indicates that the query has been executed successfully
	 * and response retrieval (if any) is now can be performed.
	 * @warning an object must be in "ready" state to call this.
	 */
	bool HasSucceeded() const { return TestCompletionStatus( Status::Succeeded ); }

	/**
	 * Checks whether the failure is not a logical error and is due to
	 * some 3-rd party troubles (e.g. network ones) so the query can (and should) be retried.
	 * @warning an object must be in "ready" state to call this.
	 */
	bool ShouldRetry() {
		const Status status_ = GetCompletionStatus();
		// Allow retry on success as well (this is for an explicitly requested task retries on success).
		if( status == Status::Succeeded || status_ == Status::ExplicitRetry ) {
			return true;
		}
		static_assert( sizeof( Status ) == sizeof( uint32_t ) && sizeof( ErrorFlags ) == sizeof( uint32_t ), "" );
		return (bool)( ( (uint32_t)status_ ) & ( (uint32_t)retryErrorFlags ) );
	}

	/**
	 * Resets the query state (if present) for a retry.
	 */
	void ResetForRetry() {
		if( req ) {
			assert( !oldReq );
			std::swap( req, oldReq );
		}
		if( rawResponse ) {
			::free( rawResponse );
			rawResponse = nullptr;
		}
		if( responseRoot ) {
			::cJSON_Delete( responseRoot );
			responseRoot = nullptr;
		}
		SetStatus( Status::Created );
	}

	/**
	 * Gets a root of a JSON response (if any).
	 * The object must be in "ready" state for the access being valid.
	 */
	const cJSON *ResponseJsonRoot() const {
		CheckReadyStatusForAccess( "a JSON response root" );
		return responseRoot;
	}

	/**
	 * Gets a root of a JSON response (if any).
	 * The object must be in "ready" state for the access being valid.
	 */
	cJSON *ResponseJsonRoot() {
		CheckReadyStatusForAccess( "a JSON response root" );
		return responseRoot;
	}

	/**
	 * Gets a raw response string.
	 * The object must be in "ready" state for the access being valid.
	 */
	const char *RawResponse() {
		CheckReadyStatusForAccess( "a raw response" );
		return rawResponse;
	}
};

inline const char* NodeReader::AsString( const cJSON *node, const char *nameAsField ) {
	if( node && node->type == cJSON_String && node->valuestring ) {
		return node->valuestring;
	}
	if( nameAsField ) {
		QueryObject::FailWith( "Can't get a string value of `%s`", nameAsField );
	}
	QueryObject::FailWith( "Can't get a string value of an array element" );
}

inline double NodeReader::AsDouble( const cJSON *node, const char *nameAsField ) {
	if( node && node->type == cJSON_Number ) {
		return node->valuedouble;
	}
	if( nameAsField ) {
		QueryObject::FailWith( "Can't get a double value of `%s`", nameAsField );
	}
	QueryObject::FailWith( "Can't get a double value of an array element" );
}

inline const char *ObjectReader::GetString( const char *field, const char *defaultValue ) const {
	cJSON *f = cJSON_GetObjectItem( const_cast<cJSON *>( underlying ), field );
	if( !f ) {
		if( defaultValue ) {
			return defaultValue;
		}
		QueryObject::FailWith( "Can't get `%s` field\n", field );
	}
	return AsString( f, field );
}

inline cJSON *ObjectReader::GetObject( const char *field ) const {
	cJSON *f = cJSON_GetObjectItem( const_cast<cJSON *>( underlying ), field );
	return ( f && f->type == cJSON_Object ) ? f : nullptr;
}

inline cJSON *ObjectReader::GetArray( const char *field ) const {
	cJSON *f = cJSON_GetObjectItem( const_cast<cJSON *>( underlying ), field );
	return ( f && f->type == cJSON_Array ) ? f : nullptr;
}

inline double ObjectReader::GetDouble( const char *field, double defaultValue ) const {
	cJSON *f = cJSON_GetObjectItem( const_cast<cJSON *>( underlying ), field );
	if( !f ) {
		if( !std::isnan( defaultValue ) ) {
			return defaultValue;
		}
		QueryObject::FailWith( "Can't get `%s` field\n", field );
	}
	return AsDouble( f, field );
}

class alignas( 8 )JsonWriter {
	friend class CompoundWriter;
	friend class ObjectWriter;
	friend class ArrayWriter;
	friend struct WritersAllocator;

	static constexpr int STACK_SIZE = 32;

	static bool CheckTopOfStack( const char *tag, int topOfStack_ ) {
		if( topOfStack_ < 0 || topOfStack_ >= STACK_SIZE ) {
			const char *kind = topOfStack_ < 0 ? "underflow" : "overflow";
			QueryObject::FailWith( "%s: Objects stack %s, top of stack index is %d\n", tag, kind, topOfStack_ );
		}
		return true;
	}

	void AddSection( const char *name, cJSON *section ) {
		cJSON *attachTo = TopOfStack().section;
		assert( attachTo->type == cJSON_Object || attachTo->type == cJSON_Array );
		if( attachTo->type == cJSON_Object ) {
			cJSON_AddItemToObject( attachTo, name, section );
		} else {
			cJSON_AddItemToArray( attachTo, section );
		}
	}

	void NotifyOfNewArray( const char *name ) {
		cJSON *section = cJSON_CreateArray();
		AddSection( name, section );
		topOfStackIndex++;
		stack[topOfStackIndex] = writersAllocator.NewArrayWriter( section );
	}

	void NotifyOfNewObject( const char *name ) {
		cJSON *section = cJSON_CreateObject();
		AddSection( name, section );
		topOfStackIndex++;
		stack[topOfStackIndex] = writersAllocator.NewObjectWriter( section );
	}

	void NotifyOfArrayEnd() {
		writersAllocator.DeleteHelper( &TopOfStack() );
		topOfStackIndex--;
	}

	void NotifyOfObjectEnd() {
		writersAllocator.DeleteHelper( &TopOfStack() );
		topOfStackIndex--;
	}

	/**
	 * An object that can be on top of the stack and that
	 * actually attaches values to the current top JSON node.
	 */
	class CompoundWriter {
		friend class JsonWriter;
	protected:
		JsonWriter *const parent;
		cJSON *const section;

		int64_t CheckPrecisionLoss( int64_t value ) {
			// Try to prevent optimizing out this
			const volatile auto dValue = (double)value;
			if( (int64_t)dValue != value ) {
				QueryObject::FailWith( "Can't store %" PRIi64 " in double without precision loss", value );
			}
			return value;
		}
	public:
		CompoundWriter( JsonWriter *parent_, cJSON *section_ )
			: parent( parent_ ), section( section_ ) {}

		virtual	~CompoundWriter() = default;

		virtual void operator<<( const char *nameOrValue ) = 0;
		virtual void operator<<( int value ) = 0;
		virtual void operator<<( unsigned value ) = 0;
		virtual void operator<<( int64_t value ) = 0;
		virtual void operator<<( double value ) = 0;
		virtual void operator<<( const mm_uuid_t &value ) = 0;
		virtual void operator<<( char ch ) = 0;
	};

	/**
	 * A {@code CompoundWriter} that attaches values to the current top JSON object node.
	 */
	class ObjectWriter: public CompoundWriter {
		const char *fieldName;

		const char *CheckFieldName( const char *tag ) {
			if( !fieldName ) {
				QueryObject::FailWith( "JsonWriter::ObjectWriter::operator<<(%s): "
				    "A field name has not been set before supplying a value", tag );
			}
			return fieldName;
		}
	public:
		ObjectWriter( JsonWriter *parent_, cJSON *section_ )
			: CompoundWriter( parent_, section_ ), fieldName( nullptr ) {
			assert( section_->type == cJSON_Object );
		}

		void operator<<( const char *nameOrValue ) override {
			if( !fieldName ) {
				// TODO: Check whether it is a valid identifier?
				fieldName = nameOrValue;
			} else {
				cJSON_AddStringToObject( section, fieldName, nameOrValue );
				fieldName = nullptr;
			}
		}

		void operator<<( int value ) override {
			cJSON_AddNumberToObject( section, CheckFieldName( "int" ), value );
			fieldName = nullptr;
		}

		void operator<<( unsigned value ) override {
			cJSON_AddNumberToObject( section, CheckFieldName( "unsigned" ), value );
			fieldName = nullptr;
		}

		void operator<<( int64_t value ) override {
			cJSON_AddNumberToObject( section, CheckFieldName( "int64_t"), CheckPrecisionLoss( value ) );
			fieldName = nullptr;
		}

		void operator<<( double value ) override {
			cJSON_AddNumberToObject( section, CheckFieldName( "double" ), value );
			fieldName = nullptr;
		}

		void operator<<( const mm_uuid_t &value ) override {
			char buffer[UUID_BUFFER_SIZE];
			value.ToString( buffer );
			cJSON_AddStringToObject( section, CheckFieldName( "const mm_uuid_t &" ), buffer );
			fieldName = nullptr;
		}

		/**
		 * Starts a new array/object or ends a current one if valid characters are supplied
		 */
		void operator<<( char ch ) override {
			if( ch == '{' ) {
				parent->NotifyOfNewObject( CheckFieldName( "{..." ) );
				fieldName = nullptr;
			} else if( ch == '[' ) {
				parent->NotifyOfNewArray( CheckFieldName( "[..." ) );
				fieldName = nullptr;
			} else if( ch == '}' ) {
				parent->NotifyOfObjectEnd();
			} else if( ch == ']' ) {
				QueryObject::FailWith( "ArrayWriter::operator<<('...]'): Unexpected token (an array end token)" );
			} else {
				QueryObject::FailWith( "ArrayWriter::operator<<(char): Illegal character (%d as an integer)", (int)ch );
			}
		}
	};

	/**
	 * A {@code CompoundWriter} that attaches values to the current top JSON array node.
	 */
	class ArrayWriter: public CompoundWriter {
	public:
		ArrayWriter( JsonWriter *parent_, cJSON *section_ )
			: CompoundWriter( parent_, section_ ) {
			assert( section_->type == cJSON_Array );
		}

		void operator<<( const char *nameOrValue ) override {
			cJSON_AddItemToArray( section, cJSON_CreateString( nameOrValue ) );
		}

		void operator<<( int value ) override {
			cJSON_AddItemToArray( section, cJSON_CreateNumber( value ) );
		}

		void operator<<( unsigned value ) override {
			cJSON_AddItemToArray( section, cJSON_CreateNumber( value ) );
		}

		void operator<<( int64_t value ) override {
			cJSON_AddItemToArray( section, cJSON_CreateNumber( CheckPrecisionLoss( value ) ) );
		}

		void operator<<( double value ) override {
			cJSON_AddItemToArray( section, cJSON_CreateNumber( value ) );
		}

		void operator<<( const mm_uuid_t &value ) override {
			char buffer[UUID_BUFFER_SIZE];
			cJSON_AddItemToArray( section, cJSON_CreateString( value.ToString( buffer ) ) );
		}

		/**
		 * Starts a new array/object or ends a current one if valid characters are supplied.
		 */
		void operator<<( char ch ) override {
			if( ch == '[' ) {
				parent->NotifyOfNewArray( nullptr );
			} else if( ch == '{' ) {
				parent->NotifyOfNewObject( nullptr );
			} else if( ch == ']' ) {
				parent->NotifyOfArrayEnd();
			} else if( ch == '}' ) {
				QueryObject::FailWith( "ArrayWriter::operator<<('...}'): Unexpected token (an object end token)" );
			} else {
				QueryObject::FailWith( "ArrayWriter::operator<<(char): Illegal character (%d as an integer)", (int)ch );
			}
		}
	};

	class alignas( 8 )StackedWritersAllocator {
	protected:
		static_assert( sizeof( ObjectWriter ) >= sizeof( ArrayWriter ), "Redefine LargestEntry" );
		using LargestEntry = ObjectWriter;

		static constexpr auto ENTRY_SIZE = ( sizeof( LargestEntry ) % 8 ) ?
			( sizeof( LargestEntry ) + 8 - sizeof( LargestEntry ) % 8 ) : sizeof( LargestEntry );

		alignas( 8 ) uint8_t storage[STACK_SIZE * ENTRY_SIZE];

		JsonWriter *parent;
		int topOfStack;

		void *AllocEntry( const char *tag ) {
			if( CheckTopOfStack( tag, topOfStack ) ) {
				return storage + ( topOfStack++ ) * ENTRY_SIZE;
			}
			return nullptr;
		}
	public:
		explicit StackedWritersAllocator( JsonWriter *parent_ )
			: parent( parent_ ), topOfStack( 0 ) {
			if( ( (uintptr_t)this ) % 8 ) {
				QueryObject::FailWith( "StackedHelpersAllocator(): the object is misaligned!\n" );
			}
		}

		ArrayWriter *NewArrayWriter( cJSON *section ) {
			return new( AllocEntry( "array" ) )ArrayWriter( parent, section );
		}

		ObjectWriter *NewObjectWriter( cJSON *section ) {
			return new( AllocEntry( "object" ) )ObjectWriter( parent, section );
		}

		void DeleteHelper( CompoundWriter *writer ) {
			writer->~CompoundWriter();
			if( (uint8_t *)writer != storage + ( topOfStack - 1 ) * ENTRY_SIZE ) {
				QueryObject::FailWith( "WritersAllocator::DeleteWriter(): "
									   "Attempt to delete an entry that is not on top of stack\n" );
			}
			topOfStack--;
		}
	};

	cJSON *root;

	StackedWritersAllocator writersAllocator;

	// Put the root object onto the top of stack
	// Do not require closing it explicitly
	CompoundWriter *stack[32 + 1];
	int topOfStackIndex { 0 };

	CompoundWriter &TopOfStack() {
		CheckTopOfStack( "JsonWriter::TopOfStack()", topOfStackIndex );
		return *stack[topOfStackIndex];
	}
public:
	explicit JsonWriter( cJSON *root_ )
		: root( root_ ), writersAllocator( this ) {
		stack[topOfStackIndex] = writersAllocator.NewObjectWriter( root );
	}

	/**
	 * Submits a string to the query writer.
	 * If the currently written child is an object and no field name set yet the string is treated as a new field name.
	 * In all other cases the string is treated as a value of an object field or an array element.
	 * @param nameOrValue a name of a field or a string value.
	 */
	JsonWriter &operator<<( const char *nameOrValue ) {
		TopOfStack() << nameOrValue;
		return *this;
	}

	JsonWriter &operator<<( int value ) {
		TopOfStack() << value;
		return *this;
	}

	JsonWriter &operator<<( unsigned value ) {
		TopOfStack() << value;
		return *this;
	}

	JsonWriter &operator<<( int64_t value ) {
		TopOfStack() << value;
		return *this;
	}

	JsonWriter &operator<<( double value ) {
		TopOfStack() << value;
		return *this;
	}

	JsonWriter &operator<<( const mm_uuid_t &value ) {
		TopOfStack() << value;
		return *this;
	}

	JsonWriter &operator<<( char ch ) {
		TopOfStack() << ch;
		return *this;
	}
};

#endif
