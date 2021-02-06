#include "chatmodel.h"
#include "local.h"

namespace wsw::ui {

[[nodiscard]]
auto ChatModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Timestamp, "timestamp" },
		{ Name, "name" },
		{ Message, "message" }
	};
}

[[nodiscard]]
auto ChatModel::rowCount( const QModelIndex & ) const -> int {
	return m_data.count();
}

[[nodiscard]]
auto ChatModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( !index.isValid() ) {
		return QVariant();
	}
	const int row = index.row();
	if( (unsigned)row >= (unsigned)m_data.count() ) {
		return QVariant();
	}
	switch( role ) {
		case Timestamp: return m_data[row].timestamp;
		case Name: return m_data[row].name;
		case Message: return m_data[row].message;
		default: return QVariant();
	}
}

void ChatModel::addNewEntry( QString timestamp, const wsw::StringView &name, const wsw::StringView &message ) {
	m_data.append({
		std::move( timestamp ),
		m_proxy->acquireCachedName( name ),
		toStyledText( message )
	});
}

void ChatModel::clear() {
	beginResetModel();
	m_data.clear();
	endResetModel();
}

void CompactChatModel::addMessage( const wsw::StringView &name, const wsw::StringView &message ) {
	if( m_data.size() >= 24 ) {
		beginRemoveRows( QModelIndex(), 0, 0 );
		m_proxy->releaseCachedName( m_data.first().name );
		endRemoveRows();
		m_data.removeFirst();
	}

	beginInsertRows( QModelIndex(), m_data.size(), m_data.size() );
	addNewEntry( m_proxy->formatTime( m_proxy->getLastMessageQtTime() ), name, message );
	endInsertRows();

	endResetModel();
}

/**
 * We pass the allowed difference as a template parameter to avoid confusion with absolute minute argument values
 */
template <int N>
[[nodiscard]]
static bool isWithinNMinutes( int minute1, int minute2 ) {
	// TODO: Account for leap seconds (lol)?
	assert( (unsigned)minute1 < 60u );
	assert( (unsigned)minute2 < 60u );
	if( minute1 == minute2 ) {
		return true;
	}
	const int minMinute = std::min( minute1, minute2 );
	const int maxMinute = std::max( minute1, minute2 );
	if( maxMinute - minMinute < N + 1 ) {
		return true;
	}
	return ( minMinute + 60 ) - maxMinute < N + 1;
}

void RichChatModel::addMessage( const wsw::StringView &name, const wsw::StringView &message ) {
	if( !m_lastMessageName.equalsIgnoreCase( name ) ) {
		m_lastMessageName.assign( name );
		addNewGroup( name, message );
		return;
	}

	if( m_proxy->wasThisMessageInTheSameFrame() ) {
		addToCurrGroup( message );
		return;
	}

	if( !tryAddingToCurrGroup( message ) ) {
		addNewGroup( name, message );
	}
}

bool RichChatModel::tryAddingToCurrGroup( const wsw::StringView &message ) {
	const QDateTime currDateTime = m_proxy->getLastMessageQtTime();
	const QDate currDate = currDateTime.date();
	if( currDate != m_currHeadingDate ) {
		return false;
	}

	const QTime currTime = currDateTime.time();
	if( currTime.hour() != m_currHeadingHour ) {
		return false;
	}

	const int currMinute = currTime.minute();
	if( !isWithinNMinutes<4>( currMinute, m_currHeadingMinute ) ) {
		return false;
	}
	if( !isWithinNMinutes<2>( currMinute, m_lastMessageMinute ) ) {
		return false;
	}

	assert( !m_data.empty() );
	// Disallow huge walls of text in a single list entry mostly for performance reasons
	if( m_data.last().message.length() + message.length() > 256 ) {
		return false;
	}

	addToCurrGroup( message, currMinute );
	return true;
}

void RichChatModel::addNewGroup( const wsw::StringView &name, const wsw::StringView &message ) {
	const QDateTime dateTime = m_proxy->getLastMessageQtTime();
	m_currHeadingDate = dateTime.date();
	const QTime time = dateTime.time();
	m_currHeadingHour = time.hour();
	m_currHeadingMinute = m_lastMessageMinute = time.minute();

	if( m_data.size() >= 64 ) {
		beginRemoveRows( QModelIndex(), 0, 0 );
		m_proxy->releaseCachedName( m_data.first().name );
		m_data.removeFirst();
		endRemoveRows();
	}

	beginInsertRows( QModelIndex(), m_data.size(), m_data.size() );
	addNewEntry( m_proxy->formatTime( m_currHeadingHour, m_currHeadingMinute ), name, message );
	endInsertRows();
}

void RichChatModel::addToCurrGroup( const wsw::StringView &message ) {
	addToCurrGroup( message, m_proxy->getLastMessageQtTime().time().minute() );
}

void RichChatModel::addToCurrGroup( const wsw::StringView &message, int lastMessageMinute ) {
	m_lastMessageMinute = lastMessageMinute;

	beginResetModel();
	assert( !m_data.empty() );
	QString &content = m_data.last().message;
	content.append( QLatin1String( "<br>", 4 ) );
	content.append( toStyledText( message ) );
	endResetModel();
}

auto ChatModelProxy::formatTime( const QDateTime &dateTime ) -> QString {
	QTime time = dateTime.time();
	return formatTime( time.hour(), time.minute() );
}

auto ChatModelProxy::formatTime( int hour, int minute ) -> QString {
	// TODO: Cache this?
	// TODO: Use a localized format?
	return QString::asprintf( "%d:%02d", hour, minute );
}

auto ChatModelProxy::acquireCachedName( const wsw::StringView &name ) -> QString {
	// TODO: Implement names cache!
	return toStyledText( name );
}

void ChatModelProxy::clear() {
	m_compactModel.clear();
	m_richModel.clear();
}

void ChatModelProxy::addMessage( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) {
	m_wasInTheSameFrame = ( m_lastMessageFrameTimestamp == frameTimestamp );
	if( !m_wasInTheSameFrame ) {
		m_lastMessageFrameTimestamp = frameTimestamp;
		m_lastMessageQtTime = QDateTime::currentDateTime();
	}

	m_compactModel.addMessage( name, message );
	m_richModel.addMessage( name, message );
}

}