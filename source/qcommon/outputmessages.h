#ifndef WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H
#define WSW_8d319689_66d9_4c8c_bd74_ca751aedeea3_H

#include "textstreamwriter.h"

class MessageStreamsAllocator;

namespace wsw {

enum class MessageCategory : uint8_t { Common, Server, Client, Sound, Renderer, UI, CGame, Game, AI };
enum class MessageSeverity : uint8_t { Debug, Info, Warning, Error };

class OutputMessageStream {
	friend auto createMessageStream( MessageCategory, MessageSeverity ) -> OutputMessageStream *;
	friend void submitMessageStream( OutputMessageStream * );

	friend class ::MessageStreamsAllocator;
public:
	OutputMessageStream( char *data, unsigned limit, MessageCategory category, MessageSeverity severity ) noexcept
		: m_data( data ), m_limit( limit ), m_category( category ), m_severity( severity ) {}

	[[nodiscard]]
	auto reserve( size_t size ) noexcept -> char * {
		return ( m_offset + size < m_limit ) ? m_data + m_offset : nullptr;
	}

	void advance( size_t size ) noexcept {
		m_offset += (unsigned)size;
		assert( m_offset <= m_limit );
	}
private:
	char *const m_data;
	const unsigned m_limit { 0 };
	unsigned m_offset { 0 };
	const MessageCategory m_category;
	const MessageSeverity m_severity;
};

[[nodiscard]]
auto createMessageStream( MessageCategory, MessageSeverity ) -> OutputMessageStream *;

void submitMessageStream( OutputMessageStream * );

class PendingOutputMessage {
public:
	explicit PendingOutputMessage( wsw::OutputMessageStream *stream ) : m_stream( stream ), m_writer( stream ) {}
	~PendingOutputMessage() { submitMessageStream( m_stream ); }

	[[nodiscard]]
	auto getWriter() -> TextStreamWriter<OutputMessageStream> & { return m_writer; }
private:
	wsw::OutputMessageStream *m_stream;
	wsw::TextStreamWriter<OutputMessageStream> m_writer;
};

}

#endif