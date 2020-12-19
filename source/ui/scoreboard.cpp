#include "scoreboard.h"

#include "../gameshared/q_shared.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswtonum.h"
#include "../client/client.h"

using wsw::operator""_asView;

// TODO: This should be shared...
static const wsw::StringView kPlaceholder( "-"_asView );

wsw::StringView CG_PlayerName( unsigned );
wsw::StringView CG_PlayerClan( unsigned );

namespace wsw::ui {

void Scoreboard::clearSchema() {
	m_columnKinds.clear();
	m_pingSlot = std::nullopt;
	m_nameColumn = std::nullopt;
	m_columnTitlesStorage.clear();
	m_columnAssetsStorage.clear();
}

void Scoreboard::reload() {
	clearSchema();

	if( const auto maybeError = doReload() ) {
		assert( maybeError->isZeroTerminated() );
		Com_Printf( S_COLOR_RED "ScoreboardModelProxy: %s\n", maybeError->data() );
		clearSchema();
	}

	static_assert( std::is_pod_v<decltype( m_oldRawData )> );
	std::memset( &m_oldRawData, 0, sizeof( ReplicatedScoreboardData ) );
}

auto Scoreboard::doReload() -> std::optional<wsw::StringView> {
	assert( m_columnKinds.empty() && m_columnTitlesStorage.empty() && m_columnAssetsStorage.empty() );

	if( const auto maybeAssetsString = ::cl.configStrings.get( CS_SCOREBOARD_ASSETS ) ) {
		if( !parseAssets( *maybeAssetsString ) ) {
			return "Failed to parse the assets config string"_asView;
		}
	}

	// Protect against bogus server configstring data
	if( const auto maybeLayoutString = ::cl.configStrings.get( CS_SCOREBOARD_SCHEMA ) ) {
		if( !parseLayout( *maybeLayoutString ) ) {
			return "Failed to parse the layout config string"_asView;
		}
	} else {
		return "The layout config string is missing"_asView;
	}

	assert( !m_columnTitlesStorage.empty() && m_columnTitlesStorage.size() == m_columnKinds.size() );
	return std::nullopt;
}

bool Scoreboard::parseLayoutTitle( const wsw::StringView &token ) {
	if( token.length() > 16 ) {
		return false;
	}
	wsw::StringView title( token );
	if( token.equalsIgnoreCase( kPlaceholder ) ) {
		title = wsw::StringView();
	}
	if( !m_columnTitlesStorage.canAdd( title ) ) {
		return false;
	}
	wsw::StaticString<16> tmp;
	if( title.contains( '_' ) ) {
		for( char ch : title ) {
			tmp.push_back( ( ch != '_' ) ? ch : ' ' );
		}
		title = tmp.asView();
	}
	m_columnTitlesStorage.add( title );
	return true;
}

bool Scoreboard::parseLayoutKind( const wsw::StringView &token ) {
	const std::array<ColumnKind, 5> uniqueFields { Nickname, Clan, Ping, Score, Status };

	const auto maybeValue = wsw::toNum<unsigned>( token );
	// TODO: Use magic_enum for obtaining the max value
	if( !maybeValue || *maybeValue > Icon ) {
		return false;
	}
	const auto kind = (ColumnKind)*maybeValue;
	if( std::find( uniqueFields.begin(), uniqueFields.end(), kind ) != uniqueFields.end() ) {
		// Disallow multiple columns of these kinds
		if( std::find( m_columnKinds.begin(), m_columnKinds.end(), kind ) != m_columnKinds.end() ) {
			return false;
		}
		// Disallow empty titles for these kinds
		if( m_columnTitlesStorage.back().empty() ) {
			if( kind != Status ) {
				return false;
			}
		}
	} else {
		if( m_columnTitlesStorage.back().empty() ) {
			if( kind != Icon ) {
				return false;
			}
		}
	}
	m_columnKinds.push_back( kind );
	return true;
}

bool Scoreboard::parseLayoutSlot( const wsw::StringView &token ) {
	assert( m_columnKinds.size() == m_columnSlots.size() + 1 );

	const ColumnKind kind = m_columnKinds.back();
	if( kind == Nickname || kind == Clan || kind == Score ) {
		if( !token.equalsIgnoreCase( kPlaceholder ) ) {
			return false;
		}
		// Push an illegal number so we crash on attempt of using it.
		// TODO: Use std::optional instead?
		m_columnSlots.push_back( ~0u );
		return true;
	}

	if( token.equalsIgnoreCase( kPlaceholder ) ) {
		return false;
	}

	const auto maybeSlot = wsw::toNum<unsigned>( token );
	if( !maybeSlot ) {
		return false;
	}

	const unsigned slot = *maybeSlot;
	if( slot >= kMaxShortSlots ) {
		return false;
	}

	for( unsigned existingSlot: m_columnSlots ) {
		if( existingSlot == slot ) {
			return false;
		}
	}

	m_columnSlots.push_back( slot );
	assert( m_columnSlots.size() == m_columnKinds.size() );
	return true;
}

bool Scoreboard::parseLayout( const wsw::StringView &string ) {
	assert( m_columnKinds.empty() );

	wsw::StringSplitter splitter( string );
	while( const auto maybeTokenAndNum = splitter.getNextWithNum() ) {
		if( m_columnKinds.size() == m_columnKinds.capacity() ) {
			return false;
		}
		bool res;
		const auto &[token, num] = *maybeTokenAndNum;
		switch( num % 3 ) {
			case 0: res = parseLayoutTitle( token ); break;
			case 1: res = parseLayoutKind( token ); break;
			case 2: res = parseLayoutSlot( token ); break;
		}
		if( !res ) {
			return false;
		}
	}

	if( m_columnKinds.size() < 2 || m_columnTitlesStorage.size() != m_columnKinds.size() ) {
		return false;
	}

	assert( m_pingSlot == std::nullopt );
	assert( m_nameColumn == std::nullopt );
	// TODO: Share with the game module implementation? Supply the slot within the replicated data?
	unsigned slotCounter = 0;
	for( unsigned column = 0; column < m_columnKinds.size(); ++column ) {
		const auto kind = m_columnKinds[column];
		if( kind == Nickname ) {
			m_nameColumn = column;
		} else if( kind != Clan && kind != Score ) {
			if( kind == Ping ) {
				m_pingSlot = slotCounter;
			}
			slotCounter++;
		}
	}

	// The ping column could be omitted but the name is always present
	assert( m_nameColumn.has_value() );
	return true;
}

bool Scoreboard::parseAssets( const wsw::StringView &string ) {
	assert( m_columnAssetsStorage.empty() );

	wsw::StringSplitter splitter( string );
	while( const auto maybeToken = splitter.getNext() ) {
		const auto token( *maybeToken );
		if( !m_columnAssetsStorage.canAdd( token ) ) {
			return false;
		}
		for( const wsw::StringView &existing: m_columnAssetsStorage ) {
			if( token.equalsIgnoreCase( existing ) ) {
				return false;
			}
		}
		// TODO: Check whether it's a valid asset?
		m_columnAssetsStorage.add( token );
	}

	return true;
}

bool Scoreboard::checkUpdates( const RawData &currData, PlayerUpdatesList &playerUpdates, TeamUpdatesList &teamUpdates ) {
	playerUpdates.clear();
	teamUpdates.clear();

	// TODO: Check team score updates
	// TODO: Check team name updates (do we really need to add this test as we already track changes for join buttons?)

	bool isTeamUpdateNeeded[4] { false, false, false, false };

	for( unsigned playerIndex = 0; playerIndex < kMaxPlayers; ++playerIndex ) {
		const auto oldTeam = m_oldRawData.getPlayerTeam( playerIndex );
		const auto newTeam = currData.getPlayerTeam( playerIndex );
		if( oldTeam != newTeam ) {
			isTeamUpdateNeeded[oldTeam] = isTeamUpdateNeeded[newTeam] = true;
		}
		addPlayerUpdates( m_oldRawData, currData, playerIndex, playerUpdates );
	}

	for( unsigned i = 0; i < 4; ++i ) {
		if( !isTeamUpdateNeeded[i] ) {
			continue;
		}
		TeamUpdates updates {};
		updates.team = i;
		updates.players = true;
		teamUpdates.push_back( updates );
	}

	const bool result = !( teamUpdates.empty() && playerUpdates.empty() );
	if( result ) {
		m_oldRawData = currData;
	}
	return result;
}

auto Scoreboard::getImageAssetPath( unsigned asset ) const -> std::optional<wsw::StringView> {
	if( asset && asset < m_columnAssetsStorage.size() + 1 ) {
		return m_columnAssetsStorage[asset - 1];
	}
	return std::nullopt;
}

void Scoreboard::handleConfigString( unsigned configStringIndex, const wsw::StringView &string ) {
	const auto playerNum = (unsigned)( configStringIndex - CS_PLAYERINFOS );
	assert( playerNum < (unsigned)MAX_CLIENTS );
	// Consider this as a full update currently
	m_pendingPlayerUpdates[playerNum] = (PendingPlayerUpdates)( PendingClanUpdate | PendingNameUpdate );
}

auto Scoreboard::getPlayerPing( unsigned playerIndex ) const -> int {
	assert( m_pingSlot.has_value() );
	assert( playerIndex < (unsigned)kMaxPlayers );
	return m_oldRawData.getPlayerShort( playerIndex, *m_pingSlot );
}

auto Scoreboard::getPlayerName( unsigned playerIndex ) const -> wsw::StringView {
	assert( m_nameColumn.has_value() );
	assert( playerIndex < (unsigned)kMaxPlayers );
	return CG_PlayerName( m_oldRawData.getPlayerNum( playerIndex ) );
}

auto Scoreboard::getPlayerNameForColumn( unsigned playerIndex, unsigned column ) const -> wsw::StringView {
	assert( m_columnKinds[column] == Nickname );
	assert( playerIndex < (unsigned)kMaxPlayers );
	return CG_PlayerName( m_oldRawData.getPlayerNum( playerIndex ) );
}

auto Scoreboard::getPlayerClanForColumn( unsigned playerIndex, unsigned column ) const -> wsw::StringView {
	assert( m_columnKinds[column] == Clan );
	assert( playerIndex < (unsigned)kMaxPlayers );
	return CG_PlayerClan( m_oldRawData.getPlayerNum( playerIndex ) );
}

void Scoreboard::addPlayerUpdates( const RawData &oldOne, const RawData &newOne,
								   unsigned playerIndex, PlayerUpdatesList &dest ) {
	const auto oldPlayerNum = oldOne.getPlayerNum( playerIndex );
	const auto newPlayerNum = newOne.getPlayerNum( playerIndex );

	bool nickname, clan;
	if( oldPlayerNum != newPlayerNum ) {
		// Consider doing a full update in this case
		nickname = clan = true;
		// Keep m_pendingPlayerUpdates as-is.
		// What to do in this case is non-obvious and having a small pending extra update is harmless.
	} else {
		const auto playerNum = newPlayerNum;
		nickname = m_pendingPlayerUpdates[playerNum] & PendingNameUpdate;
		clan = m_pendingPlayerUpdates[playerNum] & PendingClanUpdate;
		m_pendingPlayerUpdates[playerNum] = NoPendingUpdates;
	}

	const bool score = newOne.getPlayerScore( playerIndex ) != oldOne.getPlayerScore( playerIndex );
	const bool ghosting = newOne.isPlayerGhosting( playerIndex ) != oldOne.isPlayerGhosting( playerIndex );

	uint8_t mask = 0;
	static_assert( 1u << kMaxShortSlots <= std::numeric_limits<uint8_t>::max() );
	for( unsigned slot = 0; slot < kMaxShortSlots; ++slot ) {
		if( newOne.getPlayerShort( playerIndex, slot ) != oldOne.getPlayerShort( playerIndex, slot ) ) {
			mask |= ( 1u << slot );
		}
	}

	if( (unsigned)nickname | (unsigned)clan | (unsigned)score | (unsigned)mask | (unsigned)ghosting ) {
		new( dest.unsafe_grow_back() )PlayerUpdates { (uint8_t)playerIndex, mask, nickname, clan, score, ghosting };
	}
}

}