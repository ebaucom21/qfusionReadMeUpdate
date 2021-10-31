#ifndef WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H
#define WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H

#include "textstreamwriter.h"

class ConsoleLineStreamsAllocator;

namespace wsw {

struct ConsoleLineStream {
	friend auto createRegularLineStream() -> ConsoleLineStream *;
	friend auto createDeveloperLineStream() -> ConsoleLineStream *;
	friend void submitLineStream( ConsoleLineStream * );
	friend class ::ConsoleLineStreamsAllocator;

	char *const m_data;
	const unsigned m_limit { 0 };
	unsigned m_offset { 0 };
	unsigned m_tag { 0 };
public:
	ConsoleLineStream( char *data, unsigned limit, unsigned tag ) noexcept
		: m_data( data ), m_limit( limit ), m_tag( tag ) {}

	[[nodiscard]]
	auto reserve( size_t size ) -> char * {
		return ( m_offset + size < m_limit ) ? m_data + m_offset : nullptr;
	}

	void advance( size_t size ) noexcept {
		m_offset += size;
	}
};

[[nodiscard]]
auto createRegularLineStream() -> ConsoleLineStream *;
[[nodiscard]]
auto createDeveloperLineStream() -> ConsoleLineStream *;

void submitLineStream( ConsoleLineStream * );

class PendingConsoleLine {
	wsw::ConsoleLineStream *m_stream;
	wsw::TextStreamWriter<ConsoleLineStream> m_writer;
public:
	PendingConsoleLine( wsw::ConsoleLineStream *stream, const char *prefix, size_t prefixLen )
		: m_stream( stream ), m_writer( stream ) {
		m_writer.writeChars( prefix, prefixLen );
	}

	[[nodiscard]]
	auto operator()() -> TextStreamWriter<ConsoleLineStream> & { return m_writer; }

	~PendingConsoleLine() {
		submitLineStream( m_stream );
	}
};

class PendingRegularConsoleLine final : public PendingConsoleLine {
public:
	PendingRegularConsoleLine( const char *prefix, size_t prefixLen )
		: PendingConsoleLine( createRegularLineStream(), prefix, prefixLen ) {}
};

class PendingDeveloperConsoleLine final : public PendingConsoleLine {
public:
	PendingDeveloperConsoleLine( const char *prefix, size_t prefixLen )
		: PendingConsoleLine( createDeveloperLineStream(), prefix, prefixLen ) {}
};

}

#endif