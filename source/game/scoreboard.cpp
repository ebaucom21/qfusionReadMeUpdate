#include "scoreboard.h"
#include "../qcommon/singletonholder.h"
#include "g_local.h"

using wsw::operator""_asView;

const ReplicatedScoreboardData *G_GetScoreboardData() {
	return wsw::g::Scoreboard::instance()->getRawReplicatedData();
}

namespace wsw::g {

static const wsw::StringView kNameTitle( "Name"_asView );
static const wsw::StringView kClanTitle( "Clan"_asView );
static const wsw::StringView kPingTitle( "Ping"_asView );
static const wsw::StringView kScoreTitle( "Score"_asView );

static const wsw::StringView kPlaceholder( "-" );

static const wsw::StringView kBuiltinColumnTitles[] { kNameTitle, kClanTitle, kPingTitle, kScoreTitle };

static SingletonHolder<wsw::g::Scoreboard> instanceHolder;

void Scoreboard::init() {
	instanceHolder.Init();
}

void Scoreboard::shutdown() {
	instanceHolder.Shutdown();
}

auto Scoreboard::instance() -> Scoreboard * {
	return instanceHolder.Instance();
}

void Scoreboard::expectState( State expectedState ) {
	if( m_state != expectedState ) {
		throw Error( "Unexpected state" );
	}
}

void Scoreboard::checkPlayerNum( unsigned playerNum ) const {
	if( playerNum >= (unsigned)gs.maxclients ) {
		throw Error( "Illegal player num" );
	}
}

void Scoreboard::checkSlot( unsigned slot, ColumnKind expectedKind ) const {
	if( !slot || slot >= m_columnKinds.size() + 1 ) {
		throw Error( "Illegal column slot" );
	}
	if( m_columnKinds[slot - 1] != expectedKind ) {
		throw Error( "Illegal column kind" );
	}
}

auto Scoreboard::registerUserColumn( const wsw::StringView &title, ColumnKind kind,
									 unsigned titleColumnSpan ) -> unsigned {
	assert( kind == Number || kind == Icon || kind == Glyph );
	assert( m_columnKinds.size() == m_titleColumnSpans.size() );
	expectState( Schema );
	// Reserve space for ping, score and status
	if( m_columnKinds.size() + 3 == m_columnKinds.capacity() ) {
		throw Error( "Too many columns" );
	}
	if( !titleColumnSpan ) {
		throw Error( "titleColumnSpan must be non-zero" );
	}
	if( titleColumnSpan > 3 ) {
		throw Error( "titleColumnSpan is limited by 3" );
	}
	// Check whether a previous span still has an effect
	if( m_titleSpanColumnsLeft ) {
		if( kind != Icon ) {
			throw Error( "Columns must be of Icon kind if there's a title spanning over multiple columns" );
		}
		if( titleColumnSpan > 1 ) {
			throw Error( "Can't start a new title column span while a previous one is incomplete" );
		}
		titleColumnSpan = 0;
	} else {
		if( titleColumnSpan > 1 ) {
			if( kind != Icon ) {
				throw Error( "Columns must be of Icon kind if there's a title spanning over multiple columns" );
			}
		}
	}
	wsw::StringView titleToUse( title );
	if( title.empty() ) {
		if( titleColumnSpan > 1 ) {
			throw Error( "A title can't be empty for titleColumnSpan > 1" );
		}
		titleToUse = kPlaceholder;
	} else {
		if( title.length() > kMaxTitleLen ) {
			throw Error( "The title is too long" );
		}
		for( const wsw::StringView &builtin : kBuiltinColumnTitles ) {
			if( builtin.equalsIgnoreCase( title ) ) {
				throw Error( "Can't use a title of a builtin column" );
			}
		}
		for( const wsw::StringView &existing: m_columnTitlesStorage ) {
			if( existing.equalsIgnoreCase( title ) ) {
				throw Error( "Duplicated column title" );
			}
		}
	}
	m_titleColumnSpans.push_back( titleColumnSpan );
	if( m_titleSpanColumnsLeft ) {
		m_titleSpanColumnsLeft--;
	} else {
		m_titleSpanColumnsLeft = titleColumnSpan - 1;
	}
	m_columnTitlesStorage.add( titleToUse );
	m_columnKinds.push_back( kind );
	return m_columnKinds.size() + 1;
}

void Scoreboard::beginDefiningSchema() {
	expectState( NoState );
	m_state = Schema;

	m_pingSlot = ~0u;
	m_statusSlot = ~0u;

	m_titleSpanColumnsLeft = 0u;

	m_columnKinds.push_back( Nickname );
	m_columnTitlesStorage.add( kNameTitle );
	m_titleColumnSpans.push_back( 1 );
	m_columnKinds.push_back( Clan );
	m_columnTitlesStorage.add( kClanTitle );
	m_titleColumnSpans.push_back( 1 );
}

auto Scoreboard::registerAsset( const wsw::StringView &path ) -> unsigned {
	expectState( Schema );
	if( m_columnAssetsStorage.size() == kMaxAssets ) {
		throw Error( "Too many assets" );
	}
	// TODO: Check whether it contains whitespaces?
	// TODO: We need globally defined CharLookup instances
	for( const wsw::StringView &existing: m_columnAssetsStorage ) {
		if( existing.equalsIgnoreCase( path ) ) {
			throw Error( "The asset is already registered" );
		}
	}
	// A zero asset could be used to (temporarily) hide an icon
	return m_columnAssetsStorage.add( path ) + 1u;
}

void Scoreboard::endDefiningSchema() {
	expectState( Schema );
	m_state = NoState;

	if( m_titleSpanColumnsLeft ) {
		throw Error( "Can't end defining schema while a current title column span is incomplete" );
	}

	m_columnKinds.push_back( Score );
	m_columnTitlesStorage.add( kScoreTitle );
	m_titleColumnSpans.push_back( 1 );
	m_columnKinds.push_back( Ping );
	m_columnTitlesStorage.add( kPingTitle );
	m_titleColumnSpans.push_back( 1 );
	m_columnKinds.push_back( Status );
	m_columnTitlesStorage.add( kPlaceholder );
	m_titleColumnSpans.push_back( 1 );

	assert( m_columnKinds.size() == m_titleColumnSpans.size() );
	assert( m_columnKinds.size() == m_columnTitlesStorage.size() );
	wsw::StaticString<1024> schemaBuffer;
	wsw::StaticString<1024> assetsBuffer;

	unsigned slotCounter = 0;
	for( unsigned i = 0; i < m_columnKinds.size(); ++i ) {
		for( char ch: m_columnTitlesStorage[i] ) {
			if( ::isspace( ch ) ) {
				schemaBuffer << '_';
			} else {
				schemaBuffer << ch;
			}
		}
		const auto kind = m_columnKinds[i];
		schemaBuffer << ' ' << (unsigned)kind << ' ';
		if( kind == Nickname || kind == Clan || kind == Score ) {
			schemaBuffer << '-';
		} else {
			if( kind == Ping ) {
				m_pingSlot = slotCounter;
			} else if( kind == Status ) {
				m_statusSlot = slotCounter;
			}
			schemaBuffer << slotCounter++;
		}
		schemaBuffer << ' ' << m_titleColumnSpans[i] << ' ';
	}

	for( const wsw::StringView &asset: m_columnAssetsStorage ) {
		assetsBuffer << asset << ' ';
	}

	trap_ConfigString( CS_SCOREBOARD_SCHEMA, schemaBuffer.data() );
	trap_ConfigString( CS_SCOREBOARD_ASSETS, assetsBuffer.data() );

	m_columnTitlesStorage.clear();
	m_columnTitlesStorage.shrink_to_fit();
	m_columnAssetsStorage.clear();
	m_columnAssetsStorage.shrink_to_fit();
}

void Scoreboard::beginUpdating() {
	expectState( NoState );
	m_state = Update;
	std::memset( &m_replicatedData, 0, sizeof( m_replicatedData ) );
}

void Scoreboard::setPlayerIcon( const gclient_s *client, unsigned slot, unsigned icon ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	checkSlot( slot, Icon );
	slot = slot - 1;
	if( icon && icon > m_columnAssetsStorage.size() ) {
		throw Error( "Icon index is out of bounds" );
	}
	m_replicatedData.setPlayerShort( playerNum, slot, (int16_t)icon );
}

void Scoreboard::setPlayerNumber( const gclient_s *client, unsigned slot, int number ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	checkSlot( slot, Number );
	slot = slot - 1;
	// Saturate values out-of-range
	using Limits = std::numeric_limits<int16_t>;
	const auto value = (int16_t)std::clamp( number, (int)Limits::min(), (int)Limits::max() );
	m_replicatedData.setPlayerShort( playerNum, slot, value );
}

void Scoreboard::setPlayerGlyph( const gclient_s *client, unsigned slot, unsigned codePoint ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	checkSlot( slot, Glyph );
	slot = slot - 1;
	if( codePoint > (unsigned)std::numeric_limits<uint16_t>::max() ) {
		throw Error( "Illegal code point (malformed or out of the Unicode BMP)" );
	}
	m_replicatedData.setPlayerShort( playerNum, slot, (int16_t)codePoint );
}

void Scoreboard::setPlayerStatusIcon( const gclient_s *client, unsigned icon ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	if( icon && icon > m_columnAssetsStorage.size() ) {
		throw Error( "Icon index is out of bounds" );
	}
	static_assert( kMaxAssets < 32 );
	m_replicatedData.setPlayerShort( playerNum, m_statusSlot, (int16_t)icon );
}

void Scoreboard::setPlayerStatusGlyph( const gclient_s *client, unsigned codePoint ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	static_assert( kMaxAssets < 32 );
	if( codePoint && codePoint < 32 ) {
		throw Error( "Illegal code point (a dozen of first numeric values is reserved for icons)" );
	}
	if( codePoint > (unsigned)std::numeric_limits<uint16_t>::max() ) {
		throw Error( "Illegal code point (malformed or out of the Unicode BMP)" );
	}
	m_replicatedData.setPlayerShort( playerNum, m_statusSlot, (int16_t)codePoint );
}

void Scoreboard::endUpdating() {
	expectState( Update );

	static_assert( TEAM_SPECTATOR >= 0 && TEAM_SPECTATOR < 4 );
	static_assert( TEAM_PLAYERS >= 0 && TEAM_PLAYERS < 4 );
	static_assert( TEAM_ALPHA >= 0 && TEAM_ALPHA < 4 );
	static_assert( TEAM_BETA >= 0 && TEAM_BETA < 4 );

	struct NumAndScore { unsigned num; int score; };
	alignas( 16 ) NumAndScore sortHandles[kMaxPlayers];
	std::memset( sortHandles, 0, sizeof( sortHandles ) );

	// Set ready status automatically for now
	const bool shouldSetReadyStatusIcon = ( !GS_RaceGametype() && GS_MatchState() < MATCH_STATE_COUNTDOWN );
	const bool *const readyStates = level.ready;

	const auto *const playerEnts = game.edicts + 1;
	static_assert( kMaxPlayers == (unsigned)MAX_CLIENTS );
	for( unsigned playerNum = 0; playerNum < kMaxPlayers; ++playerNum ) {
		const auto *const ent = playerEnts + playerNum;
		// Indices and client numbers match 1-1 at this stage.
		const auto playerIndex = playerNum;
		m_replicatedData.playerNumsAndFlagBits[playerIndex] = playerNum;
		m_replicatedData.playerNumsAndFlagBits[playerIndex] |= kFlagBitGhosting;
		sortHandles[playerIndex].num = playerNum;
		int16_t ping = 999;
		int score = std::numeric_limits<int32_t>::min();
		if( ent->r.inuse ) {
			const auto *const client = ent->r.client;
			const auto clientState = trap_GetClientState( (int)playerNum );
			if( clientState >= CS_CONNECTING ) {
				m_replicatedData.playerNumsAndFlagBits[playerIndex] |= kFlagBitConnected;
				ping = client->r.ping;
				if( clientState == CS_SPAWNED && ent->s.team > TEAM_SPECTATOR ) {
					score = client->level.stats.score;
					if( !G_ISGHOSTING( ent ) ) {
						m_replicatedData.playerNumsAndFlagBits[playerIndex] &= ~kFlagBitGhosting;
					}
				}
			}
		}
		sortHandles[playerIndex].score = score;
		m_replicatedData.setPlayerShort( playerIndex, m_pingSlot, ping );
		m_replicatedData.setPlayerScore( playerIndex, score );
		assert( playerIndex == playerNum );
		if( shouldSetReadyStatusIcon && readyStates[playerNum] ) {
			// “✔” (U+2714)
			m_replicatedData.setPlayerShort( playerIndex, m_statusSlot, 0x2714 );
		}
	}

	// We have been addressing the data by client numbers.
	// The update is finished.
	// A strict match of indices and client numbers is no longer needed.
	// We can sort now.
	// Clients could sort on their own but this would complicate client code that is already much more complex.
	// A server is able to change the sorting of scoreboard dynamically, this opens modding opportunities.
	// Also this saves redundant operations on every client.

	// TODO: std::stable_sort allocates, try avoiding that (note that using std::sort is wrong)
	if( level.gametype.inverseScore ) {
		// Sort in an ascending order
		auto cmp = []( const NumAndScore &lhs, const NumAndScore &rhs ) { return lhs.score < rhs.score; };
		std::stable_sort( std::begin( sortHandles ), std::end( sortHandles ), cmp );
	} else {
		// Sort in a descending order
		auto cmp = []( const NumAndScore &lhs, const NumAndScore &rhs ) { return lhs.score > rhs.score; };
		std::stable_sort( std::begin( sortHandles ), std::end( sortHandles ), cmp );
	}

	const ReplicatedScoreboardData unsortedData( m_replicatedData );
	m_replicatedData.playersTeamMask = 0;

	static_assert( kMaxPlayers == (unsigned)MAX_CLIENTS );
	for( unsigned i = 0; i < kMaxPlayers; ++i ) {
		[[maybe_unused]] const auto [playerNum, score] = sortHandles[i];
		assert( score == unsortedData.scores[playerNum] );
		if( i != playerNum ) {
			// Copy the old row for the player to its place in the sorting order
			m_replicatedData.copyThatRow( i, unsortedData, playerNum );
		}
		// Set team bits for actual indices
		m_replicatedData.setPlayerTeam( i, playerEnts[playerNum].s.team );
	}

	// Now all player rows are sorted by scores in a desired order.
	// All players are in the same list of rows.
	// This means that any list made of elements picked from this list
	// is going to be sorted as well if element indices of picked elements are unique and growing monotonically.
	// This makes players lists / team lists / alpha-beta mixed lists sorted naturally.

	m_state = NoState;
}

void Scoreboard::update() {
	beginUpdating();

	GT_asCallUpdateScoreboard();

	endUpdating();
}

}