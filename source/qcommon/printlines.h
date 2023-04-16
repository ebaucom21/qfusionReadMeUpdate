#ifndef WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H
#define WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H

#include "textstreamwriter.h"

class PrintLineStreamsAllocator;

namespace wsw {

enum class PrintLineCategory : uint8_t { Common, Server, Client, Sound, Renderer, UI, GameShared, CGame, Game, AI };
enum class PrintLineSeverity : uint8_t { Debug, Info, Warning, Error };

class PrintLineStream {
	friend auto createPrintLineStream( PrintLineCategory, PrintLineSeverity ) -> PrintLineStream *;
	friend void submitPrintLineStream( PrintLineStream * );
	friend class ::PrintLineStreamsAllocator;

	char *const m_data;
	const unsigned m_limit { 0 };
	unsigned m_offset { 0 };
	const PrintLineCategory m_category;
	const PrintLineSeverity m_severity;
public:
	PrintLineStream( char *data, unsigned limit, PrintLineCategory category, PrintLineSeverity severity ) noexcept
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
auto createPrintLineStream( PrintLineCategory, PrintLineSeverity ) -> PrintLineStream *;

void submitPrintLineStream( PrintLineStream * );

class PendingPrintLine {
	wsw::PrintLineStream *m_stream;
	wsw::TextStreamWriter<PrintLineStream> m_writer;
public:
	explicit PendingPrintLine( wsw::PrintLineStream *stream ) : m_stream( stream ), m_writer( stream ) {}

	[[nodiscard]]
	auto getWriter() -> TextStreamWriter<PrintLineStream> & { return m_writer; }

	~PendingPrintLine() { submitPrintLineStream( m_stream ); }
};

}

#endif