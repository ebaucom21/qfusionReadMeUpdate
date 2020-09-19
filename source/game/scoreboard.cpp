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

auto Scoreboard::registerUserColumn( const wsw::StringView &title, ColumnKind kind ) -> unsigned {
	assert( kind == Number || kind == Icon );
	expectState( Schema );
	// Reserve space for ping and score
	if( m_columnKinds.size() + 2 == m_columnKinds.capacity() ) {
		throw Error( "Too many columns" );
	}
	if( title.empty() ) {
		m_columnTitlesStorage.add( kPlaceholder );
		m_columnKinds.push_back( kind );
		return m_columnKinds.size() + 1;
	}
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
	m_columnTitlesStorage.add( title );
	m_columnKinds.push_back( kind );
	return m_columnKinds.size() + 1;
}

void Scoreboard::beginDefiningSchema() {
	expectState( NoState );
	m_state = Schema;

	m_pingSlot = ~0u;

	m_columnKinds.push_back( Nickname );
	m_columnTitlesStorage.add( kNameTitle );
	m_columnKinds.push_back( Clan );
	m_columnTitlesStorage.add( kClanTitle );
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

	m_columnKinds.push_back( Score );
	m_columnTitlesStorage.add( kScoreTitle );
	m_columnKinds.push_back( Ping );
	m_columnTitlesStorage.add( kPingTitle );

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
		if( isSeparateSlotSpaceKind( kind ) ) {
			assert( kind != Ping );
			schemaBuffer << '-';
		} else {
			if( kind == Ping ) {
				m_pingSlot = slotCounter;
			}
			schemaBuffer << slotCounter++;
		}
		schemaBuffer << ' ';
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
}

void Scoreboard::setPlayerIcon( const gclient_s *client, unsigned slot, unsigned icon ) {
	const auto playerNum = PLAYERNUM( client );
	expectState( Update );
	checkPlayerNum( playerNum );
	checkSlot( slot, Icon );
	slot = slot - 1;
	if( icon ) {
		if( icon + 1 > m_columnAssetsStorage.size() ) {
			throw Error( "Icon index is out of bounds" );
		}
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

void Scoreboard::endUpdating() {
	expectState( Update );

	const auto *const ents = game.edicts;
	const auto maxPlayerNum = (unsigned)gs.maxclients;
	for( unsigned playerNum = 0; playerNum < maxPlayerNum; ++playerNum ) {
		const auto *const ent = ents + playerNum + 1;
		if( !ent->r.inuse ) {
			continue;
		}
		int16_t ping = 999;
		int score = 0;
		if( trap_GetClientState( playerNum ) == CS_SPAWNED ) {
			const auto *const client = ent->r.client;
			ping = client->r.ping;
			score = client->level.stats.score;
		}
		m_replicatedData.setPlayerScore( playerNum, score );
		m_replicatedData.setPlayerShort( playerNum, m_pingSlot, ping );
	}

	m_state = NoState;
}

void Scoreboard::update() {
	beginUpdating();

	GT_asCallUpdateScoreboard();

	endUpdating();
}

}