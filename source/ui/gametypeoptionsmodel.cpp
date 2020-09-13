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
	return (int)m_allowedOptionIndices.size();
}

auto GametypeOptionsModel::data( const QModelIndex &index, int role ) const -> QVariant {
 	if( !index.isValid() ) {
 		return QVariant();
 	}
 	const int row = index.row();
 	if( (unsigned)row >= (unsigned)m_allowedOptionIndices.size() ) {
 		return QVariant();
 	}
 	if( (Role)role == Role::Current ) {
 		return m_currentSelectedItemNums[row];
 	}

 	const unsigned num = m_allowedOptionIndices[row];
 	assert( num < (unsigned)m_allowedOptionIndices.size() );
 	const auto &entry = m_optionEntries[num];
 	switch( (Role)role ) {
		case Role::Title: return getString( entry.titleSpanIndex );
		case Role::Kind: return entry.kind;
		case Role::Model: return entry.model;
		default: return QVariant();
	}
}

auto GametypeOptionsModel::addString( const wsw::StringView &string ) -> unsigned {
	return m_stringDataStorage.add( string );
}

auto GametypeOptionsModel::getString( unsigned stringSpanIndex ) const -> QByteArray {
	const wsw::StringView view( m_stringDataStorage[stringSpanIndex] );
	return QByteArray( view.data(), (int)view.size() );
}

auto GametypeOptionsModel::getSelectableEntry( int row, int chosen ) const -> const SelectableItemEntry & {
	assert( (unsigned)row < (unsigned)m_allowedOptionIndices.size() );
	const auto index = m_allowedOptionIndices[row];
	assert( (unsigned)index < (unsigned)m_optionEntries.size() );
	assert( m_optionEntries[index].kind != Boolean );
	[[maybe_unused]] auto [listOff, listLen] = m_optionEntries[index].selectableItemsSpan;
	assert( (unsigned)listOff + (unsigned)listLen <= (unsigned)m_selectableItemEntries.size() );
	assert( (unsigned)chosen < (unsigned)listLen );
	return m_selectableItemEntries[listOff + (unsigned)chosen];
}

auto GametypeOptionsModel::getSelectorItemTitle( int optionIndex, int chosenIndex ) const -> QByteArray {
	return getString( getSelectableEntry( optionIndex, chosenIndex ).titleSpanIndex );
}

auto GametypeOptionsModel::getSelectorItemIcon( int optionIndex, int chosenIndex ) const -> QByteArray {
	return getString( getSelectableEntry( optionIndex, chosenIndex ).iconSpanIndex );
}

void GametypeOptionsModel::select( int row, int chosen ) {
	assert( (unsigned)row < m_currentSelectedItemNums.size() );
	if( m_currentSelectedItemNums[row] == chosen ) {
		return;
	}

	// TODO: Check whether the "chosen" is valid

	m_currentSelectedItemNums[row] = chosen;
	const QModelIndex modelIndex( this->index( row ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, kCurrentRoleAsVector );

	assert( (unsigned)row < (unsigned)m_allowedOptionIndices.size() );
	const auto index = m_allowedOptionIndices[row];
	assert( (unsigned)index < (unsigned)m_optionEntries.size() );

	const wsw::StringView commandName( m_stringDataStorage[m_optionEntries[index].commandSpanIndex] );
	wsw::StaticString<64> commandText;
	commandText << commandName << ' ' << index << ' ' << chosen;
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
	m_optionEntries.clear();
	m_selectableItemEntries.clear();
	m_allowedOptionIndices.clear();
	m_stringDataStorage.clear();
}

void GametypeOptionsModel::reload() {
	const bool wasAvailable = isAvailable();
	beginResetModel();
	clear();

	const char *command = "requestoptions";
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
		const auto maybeString = ::cl.configStrings.get( configStringNum );
		if( !maybeString ) {
			break;
		}

		if( !parseEntryParts( *maybeString, parts ) ) {
			return false;
		}

		const auto &kindToken = parts[1];
		if( kindToken.equalsIgnoreCase( kBoolean ) ) {
			if( !parts[3].equals( wsw::StringView( "-" ) ) ) {
				return false;
			}
			// TODO: Check whether parts[2] could be a valid command
			m_optionEntries.emplace_back( OptionEntry {
				addString( parts[0] ),
				addString( parts[2] ),
				Kind::Boolean,
				0,
				{ 0, 0 }
			});
			continue;
		}

		if( !kindToken.equalsIgnoreCase( kOneOfList ) ) {
			return false;
		}

		const auto maybeListItemsSpan = addListItems( parts[3] );
		if( !maybeListItemsSpan ) {
			return false;
		}

		// TODO: Check whether parts[2] could be a valid command
		m_optionEntries.emplace_back( OptionEntry {
			addString( parts[0] ),
			addString( parts[2] ),
			Kind::OneOfList,
			(int)maybeListItemsSpan->second,
			*maybeListItemsSpan
		});
	}

	if( auto maybeTitle = ::cl.configStrings.get( CS_GAMETYPE_OPTIONS_TITLE ) ) {
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
		// TODO: Sanitize a displayed name...
		// TODO: Check for a valid icon path...
		tmpSpans[spanNum] = addString( token );
		if( spanNum != 1 ) {
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
	wsw::StaticVector<unsigned, kMaxOptions> allowed;
	wsw::StaticVector<int, kMaxOptions> current;

	// TODO: What to do in case of errors?

	wsw::StringSplitter splitter( status );
	while( const auto maybeTokenAndIndex = splitter.getNextWithNum() ) {
		const auto [token, index] = *maybeTokenAndIndex;
		if( index >= m_optionEntries.size() ) {
			return;
		}
		const auto maybeValue = wsw::toNum<int>( token );
		if( !maybeValue ) {
			return;
		}

		const int value = *maybeValue;
		current.push_back( value );
		if( value >= 0 ) {
			allowed.push_back( index );
		}
	}
	if( current.size() != m_optionEntries.size() ) {
		return;
	}

	if( m_allowedOptionIndices.size() == allowed.size() ) {
		if( std::equal( allowed.begin(), allowed.end(), m_allowedOptionIndices.begin() ) ) {
			// Allowed options are the same, only current values are possibly changed
			for( unsigned i = 0; i < m_currentSelectedItemNums.size(); ++i ) {
				if( current[i] != m_currentSelectedItemNums[i] ) {
					m_currentSelectedItemNums[i] = current[i];
					const QModelIndex modelIndex( index( (int)i ) );
					Q_EMIT dataChanged( modelIndex, modelIndex, kCurrentRoleAsVector );
				}
			}
			return;
		}
	}

	// TODO: We can avoid a full reset in some cases
	beginResetModel();

	m_allowedOptionIndices.clear();
	for( unsigned index: allowed ) {
		m_allowedOptionIndices.push_back( index );
	}
	m_currentSelectedItemNums.clear();
	for( int value: current ) {
		m_currentSelectedItemNums.push_back( value );
	}

	endResetModel();
}

}