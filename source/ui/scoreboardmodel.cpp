#include "scoreboardmodel.h"

namespace wsw::ui {

auto ScoreboardTeamModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_playerNumsForTeam[m_teamNum].size();
}

auto ScoreboardTeamModel::columnCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_scoreboard.getColumnCount();
}

auto ScoreboardTeamModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Kind, "kind" }, { Value, "value" } };
}

// TODO: Share this across the codebase
[[nodiscard]]
static inline auto asQVariant( const wsw::StringView &view ) -> QVariant {
	return QString::fromLatin1( view.data(), view.size() );
}

auto ScoreboardTeamModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( !modelIndex.isValid() ) {
		return QVariant();
	}
	const auto &scb = m_proxy->m_scoreboard;
	const auto &nums = m_proxy->m_playerNumsForTeam;
	const auto column = (unsigned)modelIndex.column();
	if( column >= scb.getColumnCount() ) {
		return QVariant();
	}
	const auto row = (unsigned)modelIndex.row();
	if( row >= nums[m_teamNum].size() ) {
		return QVariant();
	}
	if( role == Kind ) {
		return scb.getColumnKind( column );
	}
	if( role != Value ) {
		return QVariant();
	}
	const auto playerNum = nums[m_teamNum][row];
	// TODO: This is awkward a bit
	switch( scb.getColumnKind( column ) ) {
		case Nickname: return asQVariant( scb.getPlayerNameForColumn( playerNum, column ) );
		case Clan: return asQVariant( scb.getPlayerClanForColumn( playerNum, column ) );
		case Score: return scb.getPlayerScoreForColumn( playerNum, column );
		case Ping: return scb.getPlayerPingForColumn( playerNum, column );
		case Number: return scb.getPlayerNumberForColumn( playerNum, column );
		case Icon: return scb.getPlayerIconForColumn( playerNum, column );
	}
	throw std::logic_error( "Unreachable" );
}

auto ScoreboardSpecsModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_playerNumsForTeam[TEAM_SPECTATOR].size();
}

auto ScoreboardSpecsModel::columnCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_scoreboard.getColumnCount();
}

auto ScoreboardSpecsModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Nickname, "name" }, { Ping, "Ping" } };
}

auto ScoreboardSpecsModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( !modelIndex.isValid() ) {
		return QVariant();
	}
	const auto &nums = m_proxy->m_playerNumsForTeam[TEAM_SPECTATOR];
	const auto row = (unsigned)modelIndex.row();
	if( row >= nums.size() ) {
		return QVariant();
	}
	const auto &scb = m_proxy->m_scoreboard;
	const auto playerNum = nums[row];
	// TODO: These assumptions about column numbers are risky. This is just to get the stuff working.
	if( role == Ping ) {
		const auto pingColumn = m_proxy->m_scoreboard.getColumnCount() - 1;
		return scb.getPlayerPingForColumn( playerNum, pingColumn );
	}
	if( role == Nickname ) {
		const auto nameColumn = 0;
		return asQVariant( scb.getPlayerNameForColumn( playerNum, nameColumn ) );
	}
	return QVariant();
}

void ScoreboardModelProxy::reload() {
	m_scoreboard.reload();
}

void ScoreboardModelProxy::handleConfigString( unsigned configStringIndex, const wsw::StringView &string ) {
	m_scoreboard.handleConfigString( configStringIndex, string );
}

ScoreboardModelProxy::ScoreboardModelProxy() {
	new( m_specsModelHolder.unsafe_grow_back() )ScoreboardSpecsModel( this );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_PLAYERS );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_ALPHA );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_BETA );
}

void ScoreboardModelProxy::dispatchPlayerRowUpdates( const PlayerUpdates &updates, int team, int rowInTeam ) {
	const QVector<int> &changedRoles = ScoreboardTeamModel::kValueRoleAsVector;
	QAbstractTableModel &model = m_teamModelsHolder[team - 1];

	for( unsigned i = 0; i < m_scoreboard.getColumnCount(); ++i ) {
		// TODO: Check updates for whether the column has been really changed
		QModelIndex modelIndex( model.index( rowInTeam, (int)i ) );
		model.dataChanged( modelIndex, modelIndex, changedRoles );
	}
}

void ScoreboardModelProxy::dispatchSpecRowUpdates( const PlayerUpdates &updates, int rowInTeam ) {
	ScoreboardSpecsModel &model = m_specsModelHolder.front();
	// TODO: Check roles that got changed, build and supply updated roles vector
	const QModelIndex modelIndex( model.index( rowInTeam ) );
	model.dataChanged( modelIndex, modelIndex );
}

void ScoreboardModelProxy::update( const ReplicatedScoreboardData &currData ) {
	Scoreboard::PlayerUpdatesList playerUpdates;
	Scoreboard::TeamUpdatesList teamUpdates;
	if( !m_scoreboard.checkUpdates( currData, playerUpdates, teamUpdates ) ) {
		return;
	}

	for( auto &nums : m_playerNumsForTeam ) {
		nums.clear();
	}

	// We should update player nums first so fully reset models get a correct data from the very beginning

	uint8_t playerIndexInTeam[4][MAX_CLIENTS];

	// TODO: Limit by gs.maxclients
	for( unsigned i = 0; i < MAX_CLIENTS; ++i ) {
		const auto teamNum = m_scoreboard.getPlayerTeam( i );
		// TODO: How do we separate specs from empty client slots?
		auto &nums = m_playerNumsForTeam[teamNum];
		playerIndexInTeam[teamNum][i] = (uint8_t)nums.size();
		nums.push_back( i );
	}

	bool wasTeamReset[4] { false, false, false, false };

	// TODO: Use destructuring for fields
	for( const auto &teamUpdate: teamUpdates ) {
		// TODO: Handle other team updates (not only player changes) as well
		if( !teamUpdate.players ) {
			continue;
		}
		wasTeamReset[teamUpdate.team] = true;
		// Forcing a full reset is the easiest approach.
		if( teamUpdate.team == TEAM_SPECTATOR ) {
			auto &model = m_specsModelHolder.front();
			model.beginResetModel();
			model.endResetModel();
		} else {
			auto &model = m_teamModelsHolder[teamUpdate.team - 1];
			model.beginResetModel();
			model.endResetModel();
		}
	}

	// TODO: Use destructuring for fields
	for( const auto &playerUpdate: playerUpdates ) {
		const auto playerNum = playerUpdate.playerNum;
		const auto teamNum = m_scoreboard.getPlayerTeam( playerNum );
		if( wasTeamReset[teamNum] ) {
			continue;
		}
		const auto row = (unsigned)playerIndexInTeam[teamNum][playerNum];
		assert( row < m_playerNumsForTeam[teamNum].size() );
		if( teamNum != TEAM_SPECTATOR ) {
			dispatchPlayerRowUpdates( playerUpdate, teamNum, row );
		} else {
			dispatchSpecRowUpdates( playerUpdate, row );
		}
	}
}

}
