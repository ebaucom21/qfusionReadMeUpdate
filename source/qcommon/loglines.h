#ifndef WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H
#define WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H

#include "textstreamwriter.h"

class LogLineStreamsAllocator;

namespace wsw {

enum class LogLineCategory : uint8_t { Common, Server, Client, Sound, Renderer, UI, GameShared, CGame, Game, AI };
enum class LogLineSeverity : uint8_t { Debug, Info, Warning, Error };

class LogLineStream {
	friend auto createLogLineStream( LogLineCategory, LogLineSeverity ) -> LogLineStream *;
	friend void submitLogLineStream( LogLineStream * );
	friend class ::LogLineStreamsAllocator;

	char *const m_data;
	const unsigned m_limit { 0 };
	unsigned m_offset { 0 };
	const LogLineCategory m_category;
	const LogLineSeverity m_severity;
public:
	LogLineStream( char *data, unsigned limit, LogLineCategory category, LogLineSeverity severity ) noexcept
		: m_data( data ), m_limit( limit ), m_category( category ), m_severity( severity ) {}

	[[nodiscard]]
	auto reserve( size_t size ) -> char * {
		return ( m_offset + size < m_limit ) ? m_data + m_offset : nullptr;
	}

	void advance( size_t size ) noexcept {
		m_offset += size;
	}
};

[[nodiscard]]
auto createLogLineStream( LogLineCategory, LogLineSeverity ) -> LogLineStream *;

void submitLogLineStream( LogLineStream * );

class PendingLogLine {
	wsw::LogLineStream *m_stream;
	wsw::TextStreamWriter<LogLineStream> m_writer;
public:
	explicit PendingLogLine( wsw::LogLineStream *stream ) : m_stream( stream ), m_writer( stream ) {}

	[[nodiscard]]
	auto operator()() -> TextStreamWriter<LogLineStream> & { return m_writer; }

	~PendingLogLine() { submitLogLineStream( m_stream ); }
};

}

#endif