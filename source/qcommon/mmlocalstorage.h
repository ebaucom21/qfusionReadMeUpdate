#ifndef QFUSION_MM_STORAGE_H
#define QFUSION_MM_STORAGE_H

#include "mmquery.h"

#include <utility>
#include <functional>
#include <random>

using DBConnection = struct sqlite3 *;

/**
 * Represents a reliable ACID storage for serialized queries.
 * This allows dumping queries to the storage and send them sequentially later.
 * (A network call has to wait for a query reply/confirmation from a remote host
 * and throughput of network pipeline is severely limited).
 */
class LocalReliableStorage {
	friend class ReliablePipe;
	friend struct ScopedConnectionGuard;

	char *databasePath { nullptr };

	explicit LocalReliableStorage( const char *databasePath_ );

	~LocalReliableStorage() {
		::free( databasePath );
	}

	bool CreateTablesIfNeeded( DBConnection connection );

	/**
	 * Creates a new database connection.
	 * Connections allows to perform operations on database and act as a transaction context.
	 * @return a non-zero (non-null) value if a new connection has been created successfully.
	 */
	DBConnection NewConnection();

	/**
	 * Deletes a database connection.
	 * All resources tied to it (like statements, bound parameters, etc.)
	 * must have been released to the moment of this call.
	 */
	void DeleteConnection( DBConnection connection );

	/**
	 * An utility method to get "query_id" field from a query.
	 * It must be already present for all queries kept in this storage.
	 */
	static const char *GetQueryId( const QueryObject *query );

	/**
	 * A {@code Push(DBConnection, QueryObject *, int)} implementation helper
	 */
	bool InsertPendingQuery( DBConnection connection, const QueryObject *query, int priority );
	/**
	 * A {@code Push(DBConnection, QueryObject *, int)} implementation helper
	 */
	bool InsertQueryFields( DBConnection connection, const QueryObject *query );
public:
	/**
	 * Tries to store a query in a database.
	 * @param connection a connection that acts as a transaction context.
	 * @param query a {@code QueryObject} that could be sent later via network.
	 * @param priority a numeric priority of a query, zero by default.
	 * Queries that have larger priorities get sent first.
	 * @return true if the query has been successfully added to database.
	 * @note this method is assumed to be called within transaction.
	 * @note the query object lifecycle should be managed entirely by caller.
	 */
	bool Push( DBConnection connection, QueryObject *query, int priority = 0 );

	/**
	 * Tries to fetch a random not-sent query.
	 * @param connection a connection that acts as a transaction context. Must be in transaction.
	 * @param queryOutgoingIp an outgoing IP to use for the query.
	 * @return a query to send, null if nothing is retrieved.
	 * The query should be released by a caller by using {@code QueryObject::DeleteQuery()}.
	 */
	QueryObject *FetchNext( DBConnection connection, const char *queryOutgoingIp = nullptr );

	/**
	 * Marks a query as sent (actually deletes it from pending queries).
	 * @param connection a connection that acts as a transaction context. Must be in transaction.
	 * @param query a query to mark as sent.
	 * @note call it if the network service confirmed successful query delivery and processing of query at remote host.
	 */
	bool MarkAsSent( DBConnection connection, const QueryObject *query );
	/**
	 * Marks a query as failed (moves it from pending queries to failed queries table)
	 * @param connection a connection that acts as a transaction context. Must be in transaction.
	 * @param query a query to mark as failed.
	 * @note do not call it on network failure or temporary remote host failure.
	 * Call this only if there is a certain schema or logic error tied to the query has been detected.
	 */
	bool MarkAsFailed( DBConnection connection, const QueryObject *queryToFill );

	/**
	 * Executes a block of a code within transaction.
	 * @param block a block of a code that should return true if a transaction should be committed.
	 * @return true if a transaction lifecycle has been completed successfully (begin/commit/rollback calls succeeded).
	 */
	bool WithinTransaction( std::function<bool( DBConnection )> &&block );
};

#endif
