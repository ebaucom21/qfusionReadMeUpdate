#ifndef WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H
#define WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H

#include <QString>
#include <QImage>
#include <array>

#include "../qcommon/wswexceptions.h"
#include "../qcommon/outputmessages.h"

namespace wsw { class StringView; }

struct ImageOptions;

namespace wsw::ui {

[[nodiscard]]
auto toStyledText( const wsw::StringView &text ) -> QString;

[[nodiscard]]
auto wrapInColorTags( const wsw::StringView &text, int rgb ) -> QString;

[[nodiscard]]
auto formatPing( int ping ) -> QByteArray;

class NameChangesTracker {
	std::array<unsigned, 32> m_nameCounters;
	std::array<unsigned, 32> m_clanCounters;

	static NameChangesTracker s_instance;
public:
	NameChangesTracker() noexcept {
		// Fill by ones so an initial check for default-zero counter fields
		// in various classes gets triggered upon first access.
		m_nameCounters.fill( 1u );
		m_clanCounters.fill( 1u );
	}

	[[nodiscard]]
	static auto instance() -> NameChangesTracker * { return &s_instance; }

	[[nodiscard]]
	auto getLastNicknameUpdateCounter( unsigned playerNum ) const -> unsigned { return m_nameCounters[playerNum]; }
	[[nodiscard]]
	auto getLastClanUpdateCounter( unsigned playerNum ) const -> unsigned { return m_clanCounters[playerNum]; }

	void registerNicknameUpdate( unsigned playerNum ) { m_nameCounters[playerNum]++; }
	void registerClanUpdate( unsigned playerNum ) { m_clanCounters[playerNum]++; }
};

inline NameChangesTracker NameChangesTracker::s_instance;

}

// The contained stuff is defined in the client code
namespace wsw {

[[nodiscard]]
auto rasterizeSvg( const QByteArray &data, const ImageOptions &options ) -> QImage;

}

#define uiDebug()   wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::UI, wsw::MessageSeverity::Debug ) ).getWriter()
#define uiNotice()  wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::UI, wsw::MessageSeverity::Info ) ).getWriter()
#define uiWarning() wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::UI, wsw::MessageSeverity::Warning ) ).getWriter()
#define uiError()   wsw::PendingOutputMessage( wsw::createMessageStream( wsw::MessageCategory::UI, wsw::MessageSeverity::Error ) ).getWriter()

template <typename Stream>
[[maybe_unused]]
inline auto operator<<( wsw::TextStreamWriter<Stream> &writer, const QString &string ) -> wsw::TextStreamWriter<Stream> & {
	writer << string.toUtf8();
	return writer;
}

template <typename Stream>
[[maybe_unused]]
inline auto operator<<( wsw::TextStreamWriter<Stream> &writer, const QSize &size ) -> wsw::TextStreamWriter<Stream> & {
	writer << size.width();
	writer.hasPendingSeparator = false;
	writer << 'x';
	writer.hasPendingSeparator = false;
	writer << size.height();
	return writer;
}


template <typename Stream>
[[maybe_unused]]
inline auto operator<<( wsw::TextStreamWriter<Stream> &streamWriter, const QObject *object ) -> wsw::TextStreamWriter<Stream> & {
	const char *objectName    = "QObject";
	const void *objectAddress = object;
	if( object ) {
		objectName = object->metaObject()->className();
	}
	streamWriter.writeChars( objectName, std::strlen( objectName ) );
	streamWriter.hasPendingSeparator = false;
	streamWriter << '(';
	streamWriter.hasPendingSeparator = false;
	streamWriter << objectAddress;
	streamWriter.hasPendingSeparator = false;
	streamWriter << ')';
	return streamWriter;
}

#endif