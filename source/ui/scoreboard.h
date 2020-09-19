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

	struct PlayerUpdates {
		uint8_t shortSlotsMask;
		bool nickname: 1;
		bool clan: 1;
		bool score: 1;
	};

	struct TeamUpdates {
		uint8_t team;
		bool score : 1;
		bool name : 1;
	};

	[[nodiscard]]
	auto checkPlayerDataUpdates( const RawData &oldOne, const RawData &newOne, unsigned playerNum )
		-> std::optional<PlayerUpdates>;
public:
	void reload();

	void handleConfigString( unsigned configStringIndex, const wsw::StringView &string );

	using PlayerUpdatesList = wsw::StaticVector<PlayerUpdates, MAX_CLIENTS>;
	using TeamUpdatesList = wsw::StaticVector<TeamUpdates, 2>;

	[[nodiscard]]
	bool checkUpdates( const RawData &currData, PlayerUpdatesList &playerUpdates, TeamUpdatesList &teamUpdates );
};

}

#endif