#include "gametypeoptionsmodel.h"

#include "../client/client.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswtonum.h"

using wsw::operator""_asView;

namespace wsw::ui {

auto GametypeOptionsModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ (int)Role::Title, "title" },
		{ (int)Role::Kind, "kind" },
		{ (int)Role::Model, "model" },
		{ (int)Role::Current, "current" }
	};
}

auto GametypeOptionsModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_allowedOptions.size();
}

auto GametypeOptionsModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
 	if( modelIndex.isValid() ) {
 		if( const int row = modelIndex.row(); (unsigned)row < (unsigned)m_allowedOptions.size() ) {
 			if( (Role)role == Role::Current ) {
 				return m_allowedOptions[row].currentValue;
 			} else {
 				const unsigned index = m_allowedOptions[row].entryIndex;
 				assert( index < (unsigned)m_allOptionEntries.size() );
 				const auto &entry = m_allOptionEntries[index];
 				switch( (Role)role ) {
 					case Role::Title: return getString( entry.titleSpanIndex );
 					case Role::Kind: return entry.kind;
 					case Role::Model: return entry.model;
 					default: return QVariant();
 				}
 			}
 		}
 	}
 	return QVariant();
}

auto GametypeOptionsModel::addString( const wsw::StringView &string ) -> unsigned {
	return m_stringDataStorage.add( string );
}

auto GametypeOptionsModel::getString( unsigned stringSpanIndex ) const -> QByteArray {
	const wsw::StringView view( m_stringDataStorage[stringSpanIndex] );
	return QByteArray::fromRawData( view.data(), (int)view.size() );
}

auto GametypeOptionsModel::getSelectableEntry( int optionRow, int indexInRow ) const -> const SelectableItemEntry & {
	assert( (unsigned)optionRow < (unsigned)m_allowedOptions.size() );
	const auto index = m_allowedOptions[optionRow].entryIndex;
	assert( (unsigned)index < (unsigned)m_allOptionEntries.size() );
	assert( m_allOptionEntries[index].kind != Boolean );
	[[maybe_unused]] auto [listOff, listLen] = m_allOptionEntries[index].selectableItemsSpan;
	assert( (unsigned)listOff + (unsigned)listLen <= (unsigned)m_selectableItemEntries.size() );
	assert( (unsigned)indexInRow < (unsigned)listLen );
	return m_selectableItemEntries[listOff + (unsigned)indexInRow];
}

auto GametypeOptionsModel::getSelectorItemTitle( int optionRow, int indexInRow ) const -> QByteArray {
	return getString( getSelectableEntry( optionRow, indexInRow ).titleSpanIndex );
}

static const QByteArray kImagePrefix( "image://wsw/" );

auto GametypeOptionsModel::getSelectorItemIcon( int optionIndex, int chosenIndex ) const -> QByteArray {
	return kImagePrefix + getString( getSelectableEntry( optionIndex, chosenIndex ).iconSpanIndex );
}

void GametypeOptionsModel::select( int optionRow, int indexInRow ) {
	assert( (unsigned)optionRow < m_allowedOptions.size() );
	if( m_allowedOptions[optionRow].currentValue == indexInRow ) {
		return;
	}

	// TODO: Check whether the "indexInRow" is valid

	m_allowedOptions[optionRow].currentValue = indexInRow;
	const QModelIndex modelIndex( this->index( optionRow ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, kCurrentRoleAsVector );

	assert( (unsigned)optionRow < (unsigned)m_allowedOptions.size() );
	const auto index = m_allowedOptions[optionRow].entryIndex;
	assert( (unsigned)index < (unsigned)m_allOptionEntries.size() );

	const wsw::StringView commandName( m_stringDataStorage[m_allOptionEntries[index].commandSpanIndex] );
	wsw::StaticString<64> commandText;
	static_assert( commandText.capacity() > kMaxCommandLen + 10 );
	commandText << commandName << ' ' << indexInRow;
	Cbuf_ExecuteText( EXEC_APPEND, commandText.data() );
}

bool GametypeOptionsModel::parseEntryParts( const wsw::StringView &string,
											wsw::StaticVector<wsw::StringView, 4> &parts ) {
	parts.clear();
	wsw::StringSplitter splitter( string );
	while( const auto maybeToken = splitter.getNext( '|' ) ) {
		if( parts.size() == 4 ) {
			return false;
		}
		const auto token( maybeToken->trim() );
		if( token.empty() ) {
			return false;
		}
		parts.push_back( token );
	}
	return parts.size() == 4;
}

void GametypeOptionsModel::clear() {
	m_allOptionEntries.clear();
	m_selectableItemEntries.clear();
	m_allowedOptions.clear();
	m_stringDataStorage.clear();
}

void GametypeOptionsModel::reload() {
	const bool wasAvailable = isAvailable();
	beginResetModel();
	clear();

	const char *command = "requestoptionsstatus";
	if( Cmd_Exists( command ) ) {
		if( !doReload() ) {
			clear();
		} else {
			Cbuf_ExecuteText( EXEC_APPEND, command );
		}
	}

	endResetModel();
	if( const bool available = isAvailable(); available != wasAvailable ) {
		Q_EMIT availableChanged( available );
	}
}

static const wsw::StringView kBoolean( "Boolean"_asView );
static const wsw::StringView kOneOfList( "OneOfList"_asView );

static const QString kLoadouts( "Loadouts" );

bool GametypeOptionsModel::doReload() {
	wsw::StaticVector<wsw::StringView, 4> parts;

	static_assert( MAX_GAMETYPE_OPTIONS == kMaxOptions );
	unsigned configStringNum = CS_GAMETYPE_OPTIONS;
	for(; configStringNum < CS_GAMETYPE_OPTIONS + MAX_GAMETYPE_OPTIONS; ++configStringNum ) {
		// Interrupt at an empty config string (getting this is perfectly valid)
		const auto maybeString = ::cl.configStrings.get( configStringNum );
		if( !maybeString ) {
			break;
		}

		// Expect 4 parts separated by the "|" character
		if( !parseEntryParts( *maybeString, parts ) ) {
			return false;
		}

		// The first part is the option name.
		// The second part describes the kind of the option.
		// The third part contains the command.
		// The fourth part contains an encoded list for the OneOfList option kind
		const wsw::StringView &title = parts[0], &kind = parts[1], &command = parts[2];
		if( command.length() > kMaxCommandLen ) {
			return false;
		}

		if( kind.equalsIgnoreCase( kBoolean ) ) {
			// The fourth part must be a placeholder in this case
			if( parts[3].equals( wsw::StringView( "-" ) ) ) {
				// TODO: Check whether parts[2] could be a valid command
				m_allOptionEntries.emplace_back( OptionEntry {
					.titleSpanIndex   = addString( title ),
					.commandSpanIndex = addString( command ),
					.kind             = Kind::Boolean,
				});
			} else {
				return false;
			}
		} else if( kind.equalsIgnoreCase( kOneOfList ) ) {
			if( const auto maybeListItemsSpan = addListItems( parts[3] ) ) {
				// TODO: Check whether parts[2] could be a valid command
				[[maybe_unused]] const auto [_, numItems] = *maybeListItemsSpan;
				m_allOptionEntries.emplace_back( OptionEntry {
					.titleSpanIndex      = addString( title ),
					.commandSpanIndex    = addString( command ),
					.kind                = Kind::OneOfList,
					.model               = numItems,
					.selectableItemsSpan = *maybeListItemsSpan,
				});
			} else {
				return false;
			}
		} else {
			return false;
		}
	}

	if( const auto maybeTitle = ::cl.configStrings.get( CS_GAMETYPE_OPTIONS_TITLE ) ) {
		QString title( QString::fromLatin1( maybeTitle->data(), maybeTitle->size() ) );
		if( m_tabTitle != title ) {
			m_tabTitle = title;
			Q_EMIT tabTitleChanged( m_tabTitle );
		}
	} else {
		if( m_tabTitle != kLoadouts ) {
			m_tabTitle = kLoadouts;
			Q_EMIT tabTitleChanged( m_tabTitle );
		}
	}

	return true;
}

auto GametypeOptionsModel::addListItems( const wsw::StringView &string )
	-> std::optional<std::pair<unsigned, unsigned>> {
	wsw::StringSplitter splitter( string );

	const auto oldListItemsSize = m_selectableItemEntries.size();

	// The items list consists of pairs (title, icon path).
	// The transmitted list is flattened and elements are separated by ","

	unsigned tmpSpans[2];
	unsigned lastTokenNum = 0;
	while( const auto maybeTokenAndNum = splitter.getNextWithNum( ',' ) ) {
		auto [token, tokenNum] = *maybeTokenAndNum;
		token = token.trim();
		if( token.empty() ) {
			return std::nullopt;
		}

		const auto spanNum = tokenNum % 2;
		lastTokenNum = tokenNum;
		// TODO: Sanitize the displayed name...

		const bool isOptionName = spanNum == 0;
		if( isOptionName ) {
			if( token.length() > kMaxOptionLen ) {
				return std::nullopt;
			}
		} else {
			if( token.length() >= MAX_QPATH ) {
				return std::nullopt;
			}
		}

		tmpSpans[spanNum] = addString( token );
		if( isOptionName ) {
			continue;
		}

		// More than 6 list options are disallowed
		if( m_selectableItemEntries.size() - oldListItemsSize > 6 ) {
			return std::nullopt;
		}

		m_selectableItemEntries.push_back( SelectableItemEntry { tmpSpans[0], tmpSpans[1] } );
	}

	// Check for empty or incomplete
	if( !lastTokenNum || !( lastTokenNum % 2 ) ) {
		return std::nullopt;
	}

	return std::make_pair( oldListItemsSize, m_selectableItemEntries.size() - oldListItemsSize );
}

void GametypeOptionsModel::handleOptionsStatusCommand( const wsw::StringView &status ) {
	wsw::StaticVector<OptionRow, kMaxOptions> allowedOptions;

	// Options status is transmitted as a list of current option values.
	// Negative current values indicate disallowed options.

	// TODO: What to do in case of errors?

	wsw::StringSplitter splitter( status );
	while( const auto maybeTokenAndIndex = splitter.getNextWithNum() ) {
		const auto [token, index] = *maybeTokenAndIndex;
		assert( m_allOptionEntries.size() <= kMaxOptions );
		if( index >= m_allOptionEntries.size() ) {
			return;
		}
		const auto maybeValue = wsw::toNum<int>( token );
		if( !maybeValue ) {
			return;
		}
		if( const int value = *maybeValue; value >= 0 ) {
			const OptionEntry &entry = m_allOptionEntries[index];
			if( entry.kind == Boolean ) {
				if( value != 0 && value != 1 ) {
					return;
				}
			} else if( entry.kind == OneOfList ) {
				[[maybe_unused]] const auto [_, listLen] = entry.selectableItemsSpan;
				if( (unsigned)value >= (unsigned)listLen ) {
					return;
				}
			}
			allowedOptions.push_back( { (unsigned)index, value } );
		}
	}

	// TODO: We can avoid doing a full reset in some other cases as well
	if( m_allowedOptions.size() == allowedOptions.size() ) {
		const auto compareIndices = []( const auto &a, const auto &b ) { return a.entryIndex == b.entryIndex; };
		// If indices are the same, only current values are changed
		if( std::equal( allowedOptions.begin(), allowedOptions.end(), m_allowedOptions.begin(), compareIndices ) ) {
			for( unsigned i = 0; i < m_allowedOptions.size(); ++i ) {
				if( m_allowedOptions[i].currentValue != allowedOptions[i].currentValue ) {
					m_allowedOptions[i].currentValue = allowedOptions[i].currentValue;
					const QModelIndex modelIndex( index( (int)i ) );
					Q_EMIT dataChanged( modelIndex, modelIndex, kCurrentRoleAsVector );
				}
			}
			return;
		}
	}

	beginResetModel();

	m_allowedOptions.clear();
	m_allowedOptions.insert( m_allowedOptions.end(), allowedOptions.begin(), allowedOptions.end() );

	endResetModel();
}

}