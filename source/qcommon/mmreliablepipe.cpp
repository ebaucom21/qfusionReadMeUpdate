#include "mmreliablepipe.h"
#include "singletonholder.h"
#include "qthreads.h"
#include "qcommon.h"

const char *ReliablePipe::MakeLocalStoragePath() {
	return va( "%s/sv_port_%d.sqlite3", FS_CacheDirectory(), (unsigned)Cvar_Value( "sv_port" ) );
}

void *ReliablePipe::BackgroundRunner::ThreadFunc( void *param ) {
	assert( Cvar_Value( "dedicated" ) );

	Sys_Sleep( 333 );

	auto *const runner = (BackgroundRunner *)param;
	Com_Printf( "Launching MM/stats ReliablePipe::%s...\n", runner->logTag );
	runner->RunMessageLoop();
	Com_Printf( "Stopping MM/stats ReliablePipe::%s...\n", runner->logTag );
	return nullptr;
}

void ReliablePipe::BackgroundRunner::RunMessageLoop() {
	while( !CanTerminate() ) {
		RunStep();
	}
}

ReliablePipe::ReliablePipe()
	: reliableStorage( MakeLocalStoragePath() ) {
	// Never actually fails?
	this->reportsPipe = QBufPipe_Create( 128, 1 );

	// Never actually fails?
	this->backgroundWriter = new( ::malloc( sizeof( BackgroundWriter ) ) )BackgroundWriter( &reliableStorage, reportsPipe );
	this->backgroundSender = new( ::malloc( sizeof( BackgroundSender ) ) )BackgroundSender( &reliableStorage );

	// Never returns on fail?
	this->writerThread = QThread_Create( &BackgroundWriter::ThreadFunc, backgroundWriter );
	this->senderThread = QThread_Create( &BackgroundRunner::ThreadFunc, backgroundSender );
}

ReliablePipe::~ReliablePipe() {
	backgroundWriter->SignalForTermination();
	backgroundSender->SignalForTermination();

	QThread_Join( writerThread );
	QThread_Join( senderThread );

	backgroundWriter->~BackgroundWriter();
	backgroundSender->~BackgroundSender();

	::free( backgroundWriter );
	::free( backgroundSender );

	QBufPipe_Destroy( &reportsPipe );
}

void ReliablePipe::EnqueueMatchReport( QueryObject *matchReport ) {
	// Make sure we have set the server session having obtained the object ownership over the game module
	assert( matchReport->FindFormParamByName( "server_session" ) );
	// Force conversion of the JSON request root to the form parameter a JSON root is present
	if( matchReport->requestRoot ) {
		assert( !matchReport->hasConveredJsonToFormParam );
		assert( !matchReport->FindFormParamByName( "json_attachment" ) );
		matchReport->ConvertJsonToEncodedForm();
		assert( !matchReport->requestRoot );
		assert( matchReport->hasConveredJsonToFormParam );
		assert( matchReport->FindFormParamByName( "json_attachment" ) );
	}

	BackgroundWriter::AddReportCmd cmd( backgroundWriter, matchReport );
	QBufPipe_WriteCmd( reportsPipe, &cmd, sizeof( cmd ), sizeof( cmd ) );
}

ReliablePipe::BackgroundWriter::Handler ReliablePipe::BackgroundWriter::pipeHandlers[1] = {
	&ReliablePipe::BackgroundWriter::AddReportHandler
};

unsigned ReliablePipe::BackgroundWriter::AddReportHandler( const void *data ) {
	AddReportCmd cmd;
	memcpy( &cmd, data, sizeof( AddReportCmd ) );
	// Can block for a substantial amount of time
	// (for several seconds awaiting for completion of uploader thread transaction)
	cmd.self->AddReport( cmd.report );
	return (unsigned)sizeof( AddReportCmd );
}

void ReliablePipe::BackgroundWriter::RunStep() {
	Sys_Sleep( 32 );

	QBufPipe_ReadCmds( pipe, pipeHandlers );
}

void ReliablePipe::BackgroundWriter::RunMessageLoop() {
	// Run the regular inherited message loop
	ReliablePipe::BackgroundRunner::RunMessageLoop();

	// Make sure all reports get written to the database.
	// They could be still enqueued as the network uploader that could hold transactions
	// has much lower throughput compared to writing to disk locally.
	// At this moment the uploader is either terminated
	// or won't do another read-and-upload attempt, hence the database is not going to be locked.
	// This call blocks until all reports (if any) are written to the database.
	QBufPipe_ReadCmds( pipe, pipeHandlers );
}

void ReliablePipe::BackgroundWriter::AddReport( QueryObject *report ) {
	bool hasInsertionSucceeded = false;
	auto block = [&]( DBConnection connection ) {
		hasInsertionSucceeded = reliableStorage->Push( connection, report );
		// Returning true means the transaction should be committed
		return true;
	};

	constexpr const char *tag = "ReliablePipe::BackgroundWriter::AddReport()";

	for(;; ) {
		bool hasTransactionSucceeded = reliableStorage->WithinTransaction( block );
		// TODO: investigate SQLite behaviour... this code is based purely on MVCC RDBMS habits...
		if( hasTransactionSucceeded ) {
			// TODO: can insertion really fail?
			if( !hasInsertionSucceeded ) {
				Com_Printf( S_COLOR_RED "%s: Dropping a report\n", tag );
			}
			QueryObject::DeleteQuery( report );
			return;
		}

		// Wait for an opportunity for writing given us by the uploader thread time-to-time.
		// There should not be a substantial database lock contention.
		Com_Printf( "%s: Awaiting for a database write access\n", tag );
		Sys_Sleep( 72 );
	}
}

void ReliablePipe::BackgroundSender::RunStep() {
	Sys_Sleep( 16 );

	constexpr const char *tag = "ReliablePipe::BackgroundSender::RunStep()";

	unsigned sleepInterval = 667;
	reliableStorage->WithinTransaction( [&]( DBConnection connection ) {
		if( !( activeQuery = reliableStorage->FetchNext( connection ) ) ) {
			// No active report is present in the database yet.
			// Writer threads have plenty of time for performing their transactions in this case
			sleepInterval = 1500;
			// Returning true means the transaction should be committed
			return true;
		}

		Com_Printf( "%s: About to send query and start polling status\n", tag );

		activeQuery->SendForStatusPolling();
		while( !activeQuery->IsReady() ) {
			Sys_Sleep( 16 );
			QueryObject::Poll();
		}

		if( activeQuery->HasSucceeded() ) {
			bool transactionResult;
			if( CheckQueryResponse() ) {
				transactionResult = reliableStorage->MarkAsSent( connection, activeQuery );
			} else {
				transactionResult = reliableStorage->MarkAsFailed( connection, activeQuery );
			}
			sleepInterval = transactionResult ? 1500 : 750;
			// Request committing or rolling back depending of result of this call
			DeleteActiveQuery();
			return transactionResult;
		}

		// This is more useful for non-persistent/reliable queries like client login.
		// In this scenario let's retry implicitly on next RunStep() call.
		// Fetching the same report (with the same id) is not guaranteed
		// but we should not rely on reports ordering.
		// This is to give a breathe for writer thread that is possibly tries
		// to open a transaction while we're still holding a database exclusive lock.
		if( activeQuery->ShouldRetry() ) {
			Com_Printf( "%s: A query retry is scheduled\n", tag );
			DeleteActiveQuery();
			return true;
		}

		assert( activeQuery->IsReady() && !activeQuery->HasSucceeded() && !activeQuery->ShouldRetry() );

		// Request committing or rolling back depending of result of this call
		Com_Printf( "%s: A query execution has failed\n", tag );
		bool result = reliableStorage->MarkAsFailed( connection, activeQuery );
		DeleteActiveQuery();
		return result;
	});

	Sys_Sleep( sleepInterval );
}

bool ReliablePipe::BackgroundSender::CheckQueryResponse() {
	assert( activeQuery && activeQuery->HasSucceeded() );

	constexpr const char *tag = "ReliablePipe::BackgroundSender::CheckQueryResponse()";

	if( !activeQuery->RawResponse() ) {
		Com_Printf( S_COLOR_RED "%s: The query response is empty\n", tag );
		return false;
	}

	const double status = activeQuery->GetRootDouble( "status", std::numeric_limits<double>::infinity() );
	if( !std::isfinite( status ) ) {
		Com_Printf( S_COLOR_RED "%s: The query field `status` is missing or has an invalid format\n", tag );
		return false;
	}

	if( status != 0 ) {
		Com_Printf( "%s: A query remote execution has succeeded\n", tag );
		return true;
	}

	const char *errorString = activeQuery->GetRootString( "error", "" );
	if( !*errorString ) {
		Com_Printf( S_COLOR_RED "%s: A query remote execution has failed\n", tag );
		return false;
	}

	Com_Printf( S_COLOR_RED "%s: A query remote execution has failed with `%s`\n", tag, errorString );
	return false;
}
