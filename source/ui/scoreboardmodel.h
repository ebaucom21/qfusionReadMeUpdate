#ifndef WSW_95e07a0d_69fa_4239_9546_1fa5a5580f55_H
#define WSW_95e07a0d_69fa_4239_9546_1fa5a5580f55_H

#include "scoreboard.h"
#include "../gameshared/gs_public.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QObject>

struct ReplicatedScoreboardData;

namespace wsw::ui {

class ScoreboardModelProxy;

class ScoreboardTeamModel : public QAbstractTableModel, ScoreboardShared {
	Q_OBJECT

	friend class ScoreboardModelProxy;

	ScoreboardModelProxy *const m_proxy;
	const int m_teamListIndex;

	enum Role {
		Kind = Qt::UserRole + 1,
		Value,
		IsGhosting
	};

	ScoreboardTeamModel( ScoreboardModelProxy *proxy, int teamListIndex )
		: m_proxy( proxy ), m_teamListIndex( teamListIndex ) {}

	static inline QVector<int> kValueRoleAsVector { Value };
	static inline QVector<int> kGhostingRoleAsVector { IsGhosting };
	static inline QVector<int> kValueAndGhostingRolesAsVector { Value, IsGhosting };

	Q_PROPERTY( int teamTag MEMBER m_teamListIndex CONSTANT )

	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto columnCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto data( const QModelIndex &modelIndex, int role ) const -> QVariant override;
};

class ScoreboardSpecsModelData {
	mutable QJsonArray m_cachedArrayData;
	ScoreboardModelProxy *const m_proxy;
	StaticVector<unsigned, MAX_CLIENTS> *m_indices;
	mutable bool m_isMarkedAsUpdated { false };
public:
	ScoreboardSpecsModelData( ScoreboardModelProxy *proxy, StaticVector<unsigned, MAX_CLIENTS> *indices )
		: m_proxy( proxy ), m_indices( indices ) {}

	void markAsUpdated() { m_isMarkedAsUpdated = true; }
	[[nodiscard]]
	bool isMarkedAsUpdated() const { return m_isMarkedAsUpdated; }
	[[nodiscard]]
	auto asQmlArray() const -> QJsonArray;
};

class ScoreboardModelProxy : public QObject, ScoreboardShared {
	Q_OBJECT

	friend class ScoreboardTeamModel;
	friend class ScoreboardSpecsModelData;
public:
	enum Display {
		SideBySide,
		ColumnWise,
		Mixed
	};
	Q_ENUM( Display );

	Scoreboard m_scoreboard;

	// Can't declare a plain array due to the type being noncopyable and we don't want to use a dynamic allocation.
	StaticVector<ScoreboardTeamModel, 4> m_teamModelsHolder;
	ScoreboardSpecsModelData m_specsModel { this, &m_playerIndicesForList[TEAM_SPECTATOR] };
	ScoreboardSpecsModelData m_chasersModel { this, &m_chasers };
	ScoreboardSpecsModelData m_challengersModel { this, &m_challengers };

	cvar_s *m_displayVar { nullptr };
	Display m_display { SideBySide };

	[[nodiscard]]
	auto getDisplay() const { return m_display; }

	void checkDisplayVar();

	wsw::StaticVector<unsigned, MAX_CLIENTS> m_playerIndicesForList[5];
	wsw::StaticVector<unsigned, MAX_CLIENTS> m_chasers;
	wsw::StaticVector<unsigned, MAX_CLIENTS> m_challengers;

	using PlayerUpdates = Scoreboard::PlayerUpdates;

	void dispatchPlayerRowUpdates( const PlayerUpdates &updates, int team, int rowInTeam, int rowInMixedList );
public:
	enum class QmlColumnKind {
		Nickname,
		Clan,
		Score,
		Status,
		Ping,
		Number,
		Glyph,
		Icon
	};
	Q_ENUM( QmlColumnKind );
	static_assert( (int)QmlColumnKind::Nickname == (int)Nickname );
	static_assert( (int)QmlColumnKind::Clan == (int)Clan );
	static_assert( (int)QmlColumnKind::Score == (int)Score );
	static_assert( (int)QmlColumnKind::Status == (int)Status );
	static_assert( (int)QmlColumnKind::Ping == (int)Ping );
	static_assert( (int)QmlColumnKind::Number == (int)Number );
	static_assert( (int)QmlColumnKind::Glyph == (int)Glyph );
	static_assert( (int)QmlColumnKind::Icon == (int)Icon );

	ScoreboardModelProxy();

	Q_SIGNAL void teamReset( int resetTeamTag );

	[[nodiscard]]
	Q_INVOKABLE int getColumnKind( int column ) const;
	[[nodiscard]]
	Q_INVOKABLE QByteArray getColumnTitle( int column ) const;
	[[nodiscard]]
	Q_INVOKABLE int getTitleColumnSpan( int column ) const;
	[[nodiscard]]
	Q_INVOKABLE QByteArray getImageAssetPath( int asset ) const;
	[[nodiscard]]
	Q_INVOKABLE bool isMixedListRowAlpha( int row ) const;
	[[nodiscard]]
	Q_INVOKABLE int getColumnsCount() const { return m_scoreboard.getColumnsCount(); }

	Q_SIGNAL void displayChanged( Display display );
	Q_PROPERTY( Display display READ getDisplay NOTIFY displayChanged );

	Q_SIGNAL void specsModelChanged();
	Q_PROPERTY( QJsonArray specsModel READ getSpecsModel NOTIFY specsModelChanged );
	Q_SIGNAL void chasersModelChanged();
	Q_PROPERTY( QJsonArray chasersModel READ getChasersModel NOTIFY chasersModelChanged );
	Q_SIGNAL void challengersModelChanged();
	Q_PROPERTY( QJsonArray challengersModel READ getChallengersModel NOTIFY challengersModelChanged );

	[[nodiscard]]
	auto getSpecsModel() -> QJsonArray { return m_specsModel.asQmlArray(); }
	[[nodiscard]]
	auto getChasersModel() -> QJsonArray { return m_chasersModel.asQmlArray(); }
	[[nodiscard]]
	auto getChallengersModel() -> QJsonArray { return m_challengersModel.asQmlArray(); }

	[[nodiscard]]
	auto getPlayersModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[0]; }
	[[nodiscard]]
	auto getAlphaModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[1]; }
	[[nodiscard]]
	auto getBetaModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[2]; }
	[[nodiscard]]
	auto getMixedModel() -> ScoreboardTeamModel * { return &m_teamModelsHolder[3]; }

	void reload();
	void update( const ReplicatedScoreboardData &currData );
};

}

#endif
