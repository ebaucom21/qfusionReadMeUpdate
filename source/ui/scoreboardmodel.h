#ifndef WSW_95e07a0d_69fa_4239_9546_1fa5a5580f55_H
#define WSW_95e07a0d_69fa_4239_9546_1fa5a5580f55_H

#include "scoreboard.h"
#include "../gameshared/gs_public.h"

#include <QAbstractListModel>

struct ReplicatedScoreboardData;

namespace wsw::ui {

class ScoreboardModelProxy;

class ScoreboardTeamModel : public QAbstractTableModel, ScoreboardShared {
	friend class ScoreboardModelProxy;

	ScoreboardModelProxy *const m_proxy;
	const int m_teamNum;

	enum Role {
		Kind = Qt::UserRole + 1,
		Value
	};

	ScoreboardTeamModel( ScoreboardModelProxy *proxy, int teamNum ) : m_proxy( proxy ), m_teamNum( teamNum ) {}

	static inline QVector<int> kValueRoleAsVector { Value };

	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto columnCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto data( const QModelIndex &modelIndex, int role ) const -> QVariant override;
};

class ScoreboardSpecsModel : public QAbstractListModel {
	friend class ScoreboardModelProxy;

	ScoreboardModelProxy *const m_proxy;

	enum Role {
		Nickname = Qt::UserRole + 1,
		Ping,
	};

	explicit ScoreboardSpecsModel( ScoreboardModelProxy *proxy ) : m_proxy( proxy ) {}

	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto columnCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto data( const QModelIndex &modelIndex, int role ) const -> QVariant override;
};

class ScoreboardModelProxy : public ScoreboardShared {
	friend class ScoreboardTeamModel;
	friend class ScoreboardSpecsModel;

	Scoreboard m_scoreboard;

	// Can't declare members of QAbstractTableModel descendants and we don't want to use a dynamic allocation
	StaticVector<ScoreboardTeamModel, 3> m_teamModelsHolder;
	StaticVector<ScoreboardSpecsModel, 1> m_specsModelHolder;

	wsw::StaticVector<unsigned, MAX_CLIENTS> m_playerNumsForTeam[4];

	using PlayerUpdates = Scoreboard::PlayerUpdates;

	void dispatchPlayerRowUpdates( const PlayerUpdates &updates, int team, int rowInTeam );
	void dispatchSpecRowUpdates( const PlayerUpdates &updates, int rowInTeam );
public:
	ScoreboardModelProxy();

	[[nodiscard]]
	auto getPlayersModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[0]; }
	[[nodiscard]]
	auto getAlphaModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[1]; }
	[[nodiscard]]
	auto getBetaModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[2]; }

	void handleConfigString( unsigned configStringIndex, const wsw::StringView &string );

	void reload();
	void update( const ReplicatedScoreboardData &currData );
};

}

#endif
