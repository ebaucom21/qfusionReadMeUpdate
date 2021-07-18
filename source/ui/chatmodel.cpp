#include "chatmodel.h"
#include "local.h"

namespace wsw::ui {

[[nodiscard]]
static inline auto asQByteArray( const wsw::StringView &view ) -> QByteArray {
	return QByteArray::fromRawData( view.data(), (int)view.size() );
}

auto CompactChatModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Timestamp, "timestamp" }, { Name, "name" }, { Message, "message" } };
}

auto CompactChatModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_lineRefs.size();
}

auto CompactChatModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( const int row = index.row(); (unsigned)row < (unsigned)m_proxy->m_lineRefs.size() ) {
			const Line *const line = m_proxy->m_lineRefs[row];
			switch( role ) {
				case Timestamp: return asQByteArray( line->getTimestamp() );
				case Name: return toStyledText( line->getName() );
				case Message: return toStyledText( line->getMessage() );
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

void CompactChatModel::beginRemoveOldestLine( Line *line ) {
	const int lastRow = m_proxy->m_lineRefs.size() - 1;
	assert( m_proxy->m_lineRefs[lastRow] == line );
	beginRemoveRows( QModelIndex(), lastRow, lastRow );
}

auto RichChatModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ RegularMessage, "regularMessage" },
		{ SectionName, "sectionName" },
		{ SectionTimestamp, "sectionTimestamp" }
	};
}

auto RichChatModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto RichChatModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( const int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			const auto [regularLine, sectionLine] = m_entries[row];
			switch( role ) {
				case RegularMessage: return regularLine ? toStyledText( regularLine->getMessage() ) : QVariant();
				case SectionName: return sectionLine ? toStyledText( sectionLine->getName( ) ) : QVariant();
				case SectionTimestamp: return sectionLine ? asQByteArray( sectionLine->getTimestamp() ) : QVariant();
				default: return QVariant();
			}
		}
	}
	return QVariant();
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

void RichChatModel::beginClear() {
	beginResetModel();
}

void RichChatModel::endClear() {
	m_entries.clear();
	endResetModel();
}

void RichChatModel::beginRemoveOldestLine( Line *line ) {
	assert( line );
	// For every shared line 1 or 2 entries are created.
	// We choose removal of these entries as the simplest approach.
	// This leaves next group messages (if any) without their corresponding section but we can accept that.
	const auto numEntries = (int)m_entries.size();
	if( m_entries.back().sectionLine == line ) {
		assert( m_entries.size() >= 2 );
		assert( m_entries[numEntries - 2].regularLine == line );
		beginRemoveRows( QModelIndex(), numEntries - 2, numEntries - 1 );
		m_entries.pop_back();
		m_entries.pop_back();
	} else if( m_entries.back().regularLine == line ) {
		beginRemoveRows( QModelIndex(), numEntries - 1, numEntries - 1 );
		m_entries.pop_back();
	} else {
		throw std::logic_error( "unreachable: the shared line must have its RichChatModel entry counterparts" );
	}
}

void RichChatModel::endRemoveOldestLine() {
	endRemoveRows();
}

void RichChatModel::beginAddingLine( Line *line, const QDate &date, int timeHours, int timeMinutes ) {
	if( canAddToCurrGroup( line, date, timeHours, timeMinutes ) ) {
		beginInsertRows( QModelIndex(), 0, 0 );
		m_lastMessageMinute = timeMinutes;
		m_totalGroupLengthSoFar += line->getMessage().length();
		m_entries.push_front( { line, nullptr } );
	} else {
		beginInsertRows( QModelIndex(), 0, 1 );
		m_currHeadingDate = date;
		m_currHeadingHour = timeHours;
		m_currHeadingMinute = m_lastMessageMinute = timeMinutes;
		m_totalGroupLengthSoFar = line->getMessage().length();
		m_entries.push_front( { nullptr, line } );
		m_entries.push_front( { line, nullptr } );
	}
	m_lastMessageName.assign( line->getName() );
}

void RichChatModel::endAddingLine() {
	endInsertRows();
}

bool RichChatModel::canAddToCurrGroup( const Line *line, const QDate &date, int timeHours, int timeMinutes ) {
	if( line->getMessage().length() + m_totalGroupLengthSoFar < 1000 ) {
		if( line->getName().equalsIgnoreCase( m_lastMessageName.asView() ) ) {
			if( date == m_currHeadingDate && timeHours == m_currHeadingHour ) {
				if( isWithinNMinutes<4>( timeMinutes, m_currHeadingMinute ) ) {
					if( isWithinNMinutes<2>( timeMinutes, m_lastMessageMinute ) ) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

void ChatModelProxy::clear() {
	for( ChatModel *model: m_childModels ) {
		model->beginClear();
	}

	// This call performs a shallow cleanup but the following one actually does it
	m_lineRefs.clear();
	m_linesAllocator.clear();

	for( ChatModel *model: m_childModels ) {
		model->endClear();
	}
}

void ChatModelProxy::addMessage( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) {
	m_wasInTheSameFrame = ( m_lastMessageFrameTimestamp == frameTimestamp );
	if( !m_wasInTheSameFrame ) {
		m_lastMessageFrameTimestamp = frameTimestamp;
		const QDateTime dateTime( QDateTime::currentDateTime() );
		m_lastMessageQtDate = dateTime.date();
		const QTime time( dateTime.time() );
		m_lastMessageTimeHours = time.hour();
		m_lastMessageTimeMinutes = time.minute();
		(void)m_lastMessageFormattedTime.assignf( "%d:%02d", m_lastMessageTimeHours, m_lastMessageTimeMinutes );
	}

	if( m_lineRefs.size() == kMaxLines ) {
		Line *const oldestLine = m_lineRefs.back();
		for( ChatModel *model: m_childModels ) {
			model->beginRemoveOldestLine( oldestLine );
		}

		m_lineRefs.pop_back();
		m_linesAllocator.free( oldestLine );

		for( ChatModel *model: m_childModels ) {
			model->endRemoveOldestLine();
		}
	}

	assert( !m_linesAllocator.isFull() );
	uint8_t *const mem = m_linesAllocator.allocOrNull();
	auto *const line = new( mem )Line;

	line->basePtr = (char *)( line + 1 );

	char *writePtr = line->basePtr;
	std::memcpy( writePtr, name.data(), name.size() );
	writePtr[name.size()] = '\0';
	line->nameLen = (uint8_t)name.length();

	writePtr += name.length() + 1;
	std::memcpy( writePtr, m_lastMessageFormattedTime.data(), m_lastMessageFormattedTime.size() );
	writePtr[m_lastMessageFormattedTime.size()] = '\0';
	line->timestampLen = m_lastMessageFormattedTime.length();

	writePtr += m_lastMessageFormattedTime.length() + 1;
	std::memcpy( writePtr, message.data(), message.size() );
	line->messageLen = message.length();
	writePtr[message.length()] = '\0';

	for( ChatModel *model: m_childModels ) {
		model->beginAddingLine( line, m_lastMessageQtDate, m_lastMessageTimeHours, m_lastMessageTimeMinutes );
	}

	m_lineRefs.push_front( line );

	for( ChatModel *model: m_childModels ) {
		model->endAddingLine();
	}
}

}