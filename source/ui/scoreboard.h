#ifndef WSW_955f6ab1_1bd9_4796_8984_0f1b743c660c_H
#define WSW_955f6ab1_1bd9_4796_8984_0f1b743c660c_H

#include "../qcommon/qcommon.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/stringspanstorage.h"

namespace wsw::ui {

// TODO: Shouldn't it belong to the CG subsystem?
class Scoreboard : wsw::ScoreboardShared {
	wsw::StaticVector<ColumnKind, kMaxColumns> m_columnKinds;
	wsw::StaticVector<unsigned, kMaxAssets> m_columnSlots;

	wsw::StringSpanStaticStorage<uint8_t, uint8_t, kMaxColumns, kTitleDataLimit> m_columnTitlesStorage;
	wsw::StringSpanStaticStorage<uint8_t, uint8_t, kMaxAssets, kAssetDataLimit> m_columnAssetsStorage;

	using RawData = ReplicatedScoreboardData;

	RawData m_oldRawData {};

	enum PendingPlayerUpdates : uint8_t {
		NoPendingUpdates   = 0x0,
		PendingClanUpdate  = 0x1,
		PendingNameUpdate  = 0x2
	};

	PendingPlayerUpdates m_pendingPlayerUpdates[MAX_CLIENTS] {};

	[[nodiscard]]
	bool parseLayout( const wsw::StringView &string );

	[[nodiscard]]
	bool parseLayoutTitle( const wsw::StringView &token );

	[[nodiscard]]
	bool parseLayoutKind( const wsw::StringView &token );

	[[nodiscard]]
	bool parseLayoutSlot( const wsw::StringView &token );

	[[nodiscard]]
	bool parseAssets( const wsw::StringView &string );

	void clearSchema();

	[[nodiscard]]
	auto doReload() -> std::optional<wsw::StringView>;
public:
	struct PlayerUpdates {
		uint8_t playerIndex;
		uint8_t shortSlotsMask;
		bool nickname: 1;
		bool clan: 1;
		bool score: 1;
	};

	struct TeamUpdates {
		uint8_t team;
		bool score : 1;
		bool name : 1;
		bool players : 1;
	};
private:
	[[nodiscard]]
	auto checkPlayerDataUpdates( const RawData &oldOne, const RawData &newOne, unsigned playerNum )
		-> std::optional<PlayerUpdates>;
public:
	void reload();

	void handleConfigString( unsigned configStringIndex, const wsw::StringView &string );

	[[nodiscard]]
	auto getImageAssetPath( unsigned asset ) const -> std::optional<wsw::StringView>;

	[[nodiscard]]
	auto getColumnCount() const -> unsigned {
		return m_columnKinds.size();
	}
	[[nodiscard]]
	auto getColumnKind( unsigned column ) const -> ColumnKind {
		return m_columnKinds[column];
	}
	[[nodiscard]]
	auto getPlayerTeam( unsigned playerIndex ) const -> unsigned {
		return m_oldRawData.getPlayerTeam( playerIndex );
	}

	[[nodiscard]]
	auto getPlayerIconForColumn( unsigned playerIndex, unsigned column ) const -> unsigned {
		assert( m_columnKinds[column] == Icon );
		return m_oldRawData.getPlayerShort( playerIndex, m_columnSlots[column] );
	}
	[[nodiscard]]
	auto getPlayerNumberForColumn( unsigned playerIndex, unsigned column ) const -> int {
		assert( m_columnKinds[column] == Number );
		return m_oldRawData.getPlayerShort( playerIndex, m_columnSlots[column] );
	}
	[[nodiscard]]
	auto getPlayerPingForColumn( unsigned playerIndex, unsigned column ) const -> int {
		assert( m_columnKinds[column] == Ping );
		return m_oldRawData.getPlayerShort( playerIndex, m_columnSlots[column] );
	}
	[[nodiscard]]
	auto getPlayerScoreForColumn( unsigned playerIndex, unsigned column ) const -> int {
		assert( m_columnKinds[column] == Score );
		return m_oldRawData.getPlayerScore( playerIndex );
	}

	[[nodiscard]]
	auto getPlayerNameForColumn( unsigned playerIndex, unsigned column ) const -> wsw::StringView;
	[[nodiscard]]
	auto getPlayerClanForColumn( unsigned playerIndex, unsigned column ) const -> wsw::StringView;

	using PlayerUpdatesList = wsw::StaticVector<PlayerUpdates, MAX_CLIENTS>;
	using TeamUpdatesList = wsw::StaticVector<TeamUpdates, 3>;

	[[nodiscard]]
	bool checkUpdates( const RawData &currData, PlayerUpdatesList &playerUpdates, TeamUpdatesList &teamUpdates );
};

}

#endif