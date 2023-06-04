#include "outputmessages.h"
#include "freelistallocator.h"
#include "wswstaticvector.h"
#include "qcommon.h"

#include <mutex>

extern cvar_t *com_outputCategoryMask;
extern cvar_t *com_outputSeverityMask;
extern cvar_t *com_enableOutputCategoryPrefix;

extern qmutex_t *com_print_mutex;
extern cvar_t *logconsole;
extern cvar_t *logconsole_append;
extern cvar_t *logconsole_flush;
extern cvar_t *logconsole_timestamp;
extern int log_file;

static const char *kPrintedMessageColorForSeverity[4] {
	S_COLOR_GREY, S_COLOR_WHITE, S_COLOR_YELLOW, S_COLOR_RED
};

static const char *kPrintedMessagePrefixForCategory[9] {
	"COM", " SV", " CL", "  S", "  R", " UI", " CG", "  G", " AI"
};

class alignas( 16 ) MessageStreamsAllocator {
	std::recursive_mutex m_mutex;
	wsw::HeapBasedFreelistAllocator m_allocator;

	static constexpr size_t kSize = MAX_PRINTMSG + sizeof( wsw::OutputMessageStream );
	static constexpr size_t kCapacity = 1024;

	static constexpr size_t kSeveritiesCount = std::size( kPrintedMessageColorForSeverity );
	static constexpr size_t kCategoriesCount = std::size( kPrintedMessagePrefixForCategory );

	wsw::StaticVector<wsw::OutputMessageStream, kSeveritiesCount * kCategoriesCount> m_nullStreams;
public:
	MessageStreamsAllocator() : m_allocator( kSize, kCapacity ) {
		// TODO: This is very flaky, but alternatives aren't perfect either...
		for( unsigned i = 0; i < kCategoriesCount; ++i ) {
			assert( std::strlen( kPrintedMessagePrefixForCategory[i] ) == 3 );
			const auto category( ( wsw::MessageCategory( i ) ) );
			for( unsigned j = 0; j < kSeveritiesCount; ++j ) {
				const auto severity( ( wsw::MessageSeverity )j );
				new( m_nullStreams.unsafe_grow_back() )wsw::OutputMessageStream( nullptr, 0, category, severity );
			}
		}
	}

	[[nodiscard]]
	auto nullStreamFor( wsw::MessageCategory category, wsw::MessageSeverity severity ) -> wsw::OutputMessageStream * {
		return std::addressof( m_nullStreams[(unsigned)category * kSeveritiesCount + (unsigned)severity] );
	}

	[[nodiscard]]
	bool isANullStream( wsw::OutputMessageStream *stream ) {
		return (size_t)( stream - m_nullStreams.data() ) < std::size( m_nullStreams );
	}

	[[nodiscard]]
	auto alloc( wsw::MessageCategory category, wsw::MessageSeverity severity ) -> wsw::OutputMessageStream * {
		[[maybe_unused]] volatile std::lock_guard guard( m_mutex );
		if( !m_allocator.isFull() ) [[likely]] {
			uint8_t *mem = m_allocator.allocOrNull();
			auto *buffer = (char *)( mem + sizeof( wsw::OutputMessageStream ) );
			return new( mem )wsw::OutputMessageStream( buffer, MAX_PRINTMSG, category, severity );
		} else if( auto *mem = (uint8_t *)::malloc( kSize ) ) {
			auto *buffer = (char *)( mem + sizeof( wsw::OutputMessageStream ) );
			return new( mem )wsw::OutputMessageStream( buffer, MAX_PRINTMSG, category, severity );
		} else {
			return nullStreamFor( category, severity );
		}
	}

	[[nodiscard]]
	auto free( wsw::OutputMessageStream *stream ) {
		if( !isANullStream( stream ) ) [[likely]] {
			[[maybe_unused]] volatile std::lock_guard guard( m_mutex );
			stream->~OutputMessageStream();
			if( m_allocator.mayOwn( stream ) ) [[likely]] {
				m_allocator.free( stream );
			} else {
				::free( stream );
			}
		}
	}
};

static MessageStreamsAllocator g_logLineStreamsAllocator;

auto wsw::createMessageStream( wsw::MessageCategory category, wsw::MessageSeverity severity ) -> wsw::OutputMessageStream * {
	if( com_outputCategoryMask ) [[likely]] {
		if( !( com_outputCategoryMask->integer & ( 1 << (unsigned)category ) ) ) {
			return ::g_logLineStreamsAllocator.nullStreamFor( category, severity );
		}
	}
	if( com_outputSeverityMask ) [[likely]] {
		if( !( com_outputSeverityMask->integer & ( 1 << (unsigned)severity ) ) ) {
			return ::g_logLineStreamsAllocator.nullStreamFor( category, severity );
		}
	}
	return ::g_logLineStreamsAllocator.alloc( category, severity );
}

void wsw::submitMessageStream( wsw::OutputMessageStream *stream ) {
	if( com_outputCategoryMask ) [[likely]] {
		if( !( (unsigned)com_outputCategoryMask->integer & ( 1u << (unsigned)stream->m_category ) ) ) {
			return;
		}
	}
	if( com_outputSeverityMask ) [[likely]] {
		if( !( (unsigned)com_outputSeverityMask->integer & ( 1u << (unsigned) stream->m_severity ) ) ) {
			return;
		}
	}
	// TODO: Eliminate Com_Printf()
	if( !::g_logLineStreamsAllocator.isANullStream( stream ) ) {
		stream->m_data[wsw::min( stream->m_limit, stream->m_offset )] = '\0';
		const char *color = kPrintedMessageColorForSeverity[(unsigned)stream->m_severity];
		if( com_enableOutputCategoryPrefix && com_enableOutputCategoryPrefix->integer ) {
			const char *prefix = kPrintedMessagePrefixForCategory[(unsigned)stream->m_category];
			Com_Printf( S_COLOR_GREY "[%s] %s%s\n", prefix, color, stream->m_data );
		} else {
			Com_Printf( "%s%s\n", color, stream->m_data );
		}
	} else {
		Com_Printf( S_COLOR_RED "A null line stream was used. The line content was discarded\n" );
	}
	::g_logLineStreamsAllocator.free( stream );
}

/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int rd_target;
static char *rd_buffer;
static int rd_buffersize;
static void ( *rd_flush )( int target, const char *buffer, const void *extra );
static const void *rd_extra;

void Com_BeginRedirect( int target, char *buffer, int buffersize,
						void ( *flush )( int, const char*, const void* ), const void *extra ) {
	if( !target || !buffer || !buffersize || !flush ) {
		return;
	}

	QMutex_Lock( com_print_mutex );

	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;
	rd_extra = extra;

	*rd_buffer = 0;
}

void Com_EndRedirect( void ) {
	rd_flush( rd_target, rd_buffer, rd_extra );

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
	rd_extra = NULL;

	QMutex_Unlock( com_print_mutex );
}

void Com_DeferConsoleLogReopen( void ) {
	if( logconsole != NULL ) {
		logconsole->modified = true;
	}
}

void Com_CloseConsoleLog( bool lock, bool shutdown ) {
	if( shutdown ) {
		lock = true;
	}

	if( lock ) {
		QMutex_Lock( com_print_mutex );
	}

	if( log_file ) {
		FS_FCloseFile( log_file );
		log_file = 0;
	}

	if( shutdown ) {
		logconsole = NULL;
	}

	if( lock ) {
		QMutex_Unlock( com_print_mutex );
	}
}

void Com_ReopenConsoleLog( void ) {
	char errmsg[MAX_PRINTMSG] = { 0 };

	QMutex_Lock( com_print_mutex );

	Com_CloseConsoleLog( false, false );

	if( logconsole && logconsole->string && logconsole->string[0] ) {
		size_t name_size;
		char *name;

		name_size = strlen( logconsole->string ) + strlen( ".log" ) + 1;
		name = ( char* )Q_malloc( name_size );
		Q_strncpyz( name, logconsole->string, name_size );
		COM_DefaultExtension( name, ".log", name_size );

		if( FS_FOpenFile( name, &log_file, ( logconsole_append && logconsole_append->integer ? FS_APPEND : FS_WRITE ) ) == -1 ) {
			log_file = 0;
			Q_snprintfz( errmsg, MAX_PRINTMSG, "Couldn't open: %s\n", name );
		}

		Q_free( name );
	}

	QMutex_Unlock( com_print_mutex );

	if( errmsg[0] ) {
		Com_Printf( "%s", errmsg );
	}
}

/*
* Com_Printf
*
* Both client and server can use this, and it will output
* to the apropriate place.
*/
void Com_Printf( const char *format, ... ) {
	va_list argptr;
	char msg[MAX_PRINTMSG];

	/*
	time_t timestamp;
	char timestamp_str[MAX_PRINTMSG];
	struct tm *timestampptr;
	timestamp = time( NULL );
	timestampptr = gmtime( &timestamp );
	strftime( timestamp_str, MAX_PRINTMSG, "%Y-%m-%dT%H:%M:%SZ ", timestampptr );
	*/

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	QMutex_Lock( com_print_mutex );

	/*
	if( rd_target ) {
		if( (int)( strlen( msg ) + strlen( rd_buffer ) ) > ( rd_buffersize - 1 ) ) {
			rd_flush( rd_target, rd_buffer, rd_extra );
			*rd_buffer = 0;
		}
		strcat( rd_buffer, msg );

		QMutex_Unlock( com_print_mutex );
		return;
	}*/

	// also echo to debugging console
	Sys_ConsoleOutput( msg );

#ifndef DEDICATED_ONLY
	Con_Print( msg );
#endif

	if( log_file ) {
		/*
		if( logconsole_timestamp && logconsole_timestamp->integer ) {
			FS_Printf( log_file, "%s", timestamp_str );
		}*/
		/*
		FS_Printf( log_file, "%s", msg );
		*/
		/*
		if( logconsole_flush && logconsole_flush->integer ) {
			FS_Flush( log_file ); // force it to save every time
		}*/
	}

	QMutex_Unlock( com_print_mutex );
}


/*
* Com_DPrintf
*
* A Com_Printf that only shows up if the "developer" cvar is set
*/
void Com_DPrintf( const char *format, ... ) {
	va_list argptr;
	char msg[MAX_PRINTMSG];

	if( !developer || !developer->integer ) {
		return; // don't confuse non-developers with techie stuff...

	}
	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}