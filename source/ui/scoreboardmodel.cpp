#include "scoreboardmodel.h"
#include "local.h"

namespace wsw::ui {

[[nodiscard]]
static inline auto formatGlyph( int codePoint ) -> QChar {
	// Only the Unicode BMP is supported as we limit transmitted values to short
	assert( (unsigned)codePoint < (unsigned)std::numeric_limits<uint16_t>::max() );
	QChar ch( (uint16_t)codePoint );
	return ch.isPrint() ? ch : QChar();
}

[[nodiscard]]
static inline auto formatStatus( int value ) -> QVariant {
	if( value < 32 ) {
		return value;
	}
	return formatGlyph( value );
}

auto ScoreboardTeamModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_playerIndicesForList[m_teamListIndex].size();
}

auto ScoreboardTeamModel::columnCount( const QModelIndex & ) const -> int {
	return (int) m_proxy->m_scoreboard.getColumnsCount();
}

auto ScoreboardTeamModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Kind, "kind" }, { Value, "value" }, { IsGhosting, "isGhosting" } };
}

auto ScoreboardTeamModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( !modelIndex.isValid() ) {
		return QVariant();
	}
	const auto &scb = m_proxy->m_scoreboard;
	const auto &indices = m_proxy->m_playerIndicesForList;
	const auto column = (unsigned)modelIndex.column();
	if( column >= scb.getColumnsCount() ) {
		return QVariant();
	}
	const auto row = (unsigned)modelIndex.row();
	if( row >= indices[m_teamListIndex].size() ) {
		return QVariant();
	}
	if( role == Kind ) {
		return scb.getColumnKind( column );
	}
	const auto playerIndex = indices[m_teamListIndex][row];
	if( role == IsGhosting ) {
		return scb.isPlayerGhosting( playerIndex );
	}
	if( role != Value ) {
		return QVariant();
	}
	// TODO: This is awkward a bit
	switch( scb.getColumnKind( column ) ) {
		case Nickname: return toStyledText( scb.getPlayerNameForColumn( playerIndex, column ) );
		case Clan: return toStyledText( scb.getPlayerClanForColumn( playerIndex, column ) );
		case Score: return scb.getPlayerScoreForColumn( playerIndex, column );
		case Status: return formatStatus( scb.getPlayerStatusForColumn( playerIndex, column ) );
		case Ping: return formatPing( scb.getPlayerPingForColumn( playerIndex, column ) );
		case Number: return scb.getPlayerNumberForColumn( playerIndex, column );
		case Glyph: return formatGlyph( scb.getPlayerGlyphForColumn( playerIndex, column ) );
		case Icon: return scb.getPlayerIconForColumn( playerIndex, column );
	}
	throw std::logic_error( "Unreachable" );
}

auto ScoreboardSpecsModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_playerIndicesForList[TEAM_SPECTATOR].size();
}

auto ScoreboardSpecsModel::columnCount( const QModelIndex & ) const -> int {
	return (int) m_proxy->m_scoreboard.getColumnsCount();
}

auto ScoreboardSpecsModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Nickname, "name" }, { Ping, "ping" } };
}

auto ScoreboardSpecsModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( !modelIndex.isValid() ) {
		return QVariant();
	}
	const auto &indices = m_proxy->m_playerIndicesForList[TEAM_SPECTATOR];
	const auto row = (unsigned)modelIndex.row();
	if( row >= indices.size() ) {
		return QVariant();
	}
	const auto &scb = m_proxy->m_scoreboard;
	const auto playerIndex = indices[row];
	if( role == Ping ) {
		return formatPing( scb.hasPing() ? scb.getPlayerPing( playerIndex ) : 0 );
	}
	if( role == Nickname ) {
		return toStyledText( scb.getPlayerName( playerIndex ) );
	}
	return QVariant();
}

void ScoreboardModelProxy::reload() {
	m_scoreboard.reload();
}

void ScoreboardModelProxy::handleConfigString( unsigned configStringIndex, const wsw::StringView &string ) {
	m_scoreboard.handleConfigString( configStringIndex, string );
}

auto ScoreboardModelProxy::getColumnKind( int column ) const -> int {
	return (int)m_scoreboard.getColumnKind( (unsigned)column );
}

auto ScoreboardModelProxy::getTitleColumnSpan( int column ) const -> int {
	return (int)m_scoreboard.getTitleColumnSpan( (unsigned)column );
}

auto ScoreboardModelProxy::getColumnTitle( int column ) const -> QByteArray {
	const wsw::StringView title( m_scoreboard.getColumnTitle( (unsigned)column ) );
	return !title.empty() ? QByteArray( title.data(), title.size() ) : QByteArray();
}

auto ScoreboardModelProxy::getImageAssetPath( int asset ) const -> QByteArray {
	if( auto maybePath = m_scoreboard.getImageAssetPath( (unsigned)asset ) ) {
		return QByteArray( "image://wsw/" ) + QByteArray( maybePath->data(), maybePath->size() );
	}
	return QByteArray();
}

bool ScoreboardModelProxy::isMixedListRowAlpha( int row ) const {
	const auto &nums = std::end( m_playerIndicesForList )[-1];
	assert( (unsigned)row < nums.size() );
	return m_scoreboard.getPlayerTeam( nums[row] ) == TEAM_ALPHA;
}

ScoreboardModelProxy::ScoreboardModelProxy() {
	new( m_specsModelHolder.unsafe_grow_back() )ScoreboardSpecsModel( this );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_PLAYERS );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_ALPHA );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_BETA );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_BETA + 1 );

	m_displayVar = Cvar_Get( "ui_scoreboardDisplay", "0", CVAR_ARCHIVE );
	checkDisplayVar();
}

void ScoreboardModelProxy::dispatchPlayerRowUpdates( const PlayerUpdates &updates, int team,
													 int rowInTeam, int rowInMixedList ) {
	assert( team >= TEAM_PLAYERS && team <= TEAM_BETA );
	QAbstractTableModel *const teamModel = &m_teamModelsHolder[team - 1];
	QAbstractTableModel *const mixedModel = ( team != TEAM_PLAYERS ) ? m_teamModelsHolder.end() - 1 : nullptr;
	if( updates.ghosting ) {
		const QVector<int> *changedRoles = &ScoreboardTeamModel::kGhostingRoleAsVector;
		if( updates.nickname | updates.clan | updates.score | updates.shortSlotsMask ) {
			changedRoles = &ScoreboardTeamModel::kValueAndGhostingRolesAsVector;
		}

		// We have to force redrawing of each cell upon ghosting status change
		for( unsigned i = 0; i < m_scoreboard.getColumnsCount(); ++i ) {
			QModelIndex teamModelIndex( teamModel->index( rowInTeam, (int)i ) );
			teamModel->dataChanged( teamModelIndex, teamModelIndex, *changedRoles );
			if( mixedModel ) {
				QModelIndex mixedModelIndex( mixedModel->index( rowInMixedList, (int)i ) );
				mixedModel->dataChanged( mixedModelIndex, mixedModelIndex, *changedRoles );
			}
		}
		return;
	}

	assert( !updates.ghosting );
	const QVector<int> &changedRoles = ScoreboardTeamModel::kValueRoleAsVector;
	for( unsigned i = 0; i < m_scoreboard.getColumnsCount(); ++i ) {
		// Check whether the table cell really needs updating
		const auto kind = m_scoreboard.getColumnKind( i );
		if( kind >= Status ) {
			assert( kind == Status || kind == Ping || kind == Number || kind == Glyph || kind == Icon );
			const unsigned slotBit = 1u << m_scoreboard.getColumnSlot( i );
			if( !( slotBit & (unsigned)updates.shortSlotsMask ) ) {
				continue;
			}
		} else {
			if( kind == Nickname ) {
				if( !updates.nickname ) {
					continue;
				}
			} else if( kind == Clan ) {
				if( !updates.clan ) {
					continue;
				}
			} else if( kind == Score ) {
				if( !updates.score ) {
					continue;
				}
			} else {
				assert( 0 && "Unreachable" );
			}
		}

		QModelIndex teamModelIndex( teamModel->index( rowInTeam, (int)i ) );
		teamModel->dataChanged( teamModelIndex, teamModelIndex, changedRoles );
		if( mixedModel ) {
			QModelIndex mixedModelIndex( mixedModel->index( rowInMixedList, (int)i ) );
			mixedModel->dataChanged( mixedModelIndex, mixedModelIndex, changedRoles );
		}
	}
}

void ScoreboardModelProxy::dispatchSpecRowUpdates( const PlayerUpdates &updates, int rowInTeam ) {
	ScoreboardSpecsModel &model = m_specsModelHolder.front();
	// TODO: Check roles that got changed, build and supply updated roles vector
	const QModelIndex modelIndex( model.index( rowInTeam ) );
	model.dataChanged( modelIndex, modelIndex );
}

void ScoreboardModelProxy::checkDisplayVar() {
	if( m_displayVar->modified ) {
		const auto oldDisplay = m_display;
		m_display = (Display)m_displayVar->value;
		if( m_display != SideBySide && m_display != ColumnWise && m_display != Mixed ) {
			m_display = SideBySide;
			Cvar_ForceSet( m_displayVar->name, va( "%d", (int)m_display ) );
		}
		if( m_display != oldDisplay ) {
			Q_EMIT displayChanged( m_display );
		}
		m_displayVar->modified = false;
	}
}

void ScoreboardModelProxy::update( const ReplicatedScoreboardData &currData ) {
	checkDisplayVar();

	Scoreboard::PlayerUpdatesList playerUpdates;
	Scoreboard::TeamUpdatesList teamUpdates;
	if( !m_scoreboard.checkUpdates( currData, playerUpdates, teamUpdates ) ) {
		return;
	}

	for( auto &indices: m_playerIndicesForList ) {
		indices.clear();
	}

	// We should update player nums first so fully reset models get a correct data from the very beginning

	using PlayerIndicesTable = std::array<uint8_t, kMaxPlayers>;
	PlayerIndicesTable listPlayerTables[5];

	for( unsigned playerIndex = 0; playerIndex < kMaxPlayers; ++playerIndex ) {
		if( !m_scoreboard.isPlayerConnected( playerIndex ) ) {
			continue;
		}
		const auto teamNum = m_scoreboard.getPlayerTeam( playerIndex );
		// TODO: How do we separate specs from empty client slots?
		auto &teamNums = m_playerIndicesForList[teamNum];
		auto &teamTable = listPlayerTables[teamNum];
		teamTable[playerIndex] = (uint8_t)teamNums.size();
		teamNums.push_back( playerIndex );
		if( teamNum == TEAM_ALPHA || teamNum == TEAM_BETA ) {
			auto &mixedIndices = m_playerIndicesForList[TEAM_BETA + 1];
			auto &mixedTable = listPlayerTables[TEAM_BETA + 1];
			mixedTable[playerIndex] = (uint8_t)mixedIndices.size();
			mixedIndices.push_back( playerIndex );
		}
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

	const bool wasMixedListReset = wasTeamReset[TEAM_ALPHA] | wasTeamReset[TEAM_BETA];
	if( wasMixedListReset ) {
		auto &model = std::end( m_teamModelsHolder )[-1];
		model.beginResetModel();
		model.endResetModel();
	}

	for( const auto &playerUpdate: playerUpdates ) {
		const auto playerIndex = playerUpdate.playerIndex;
		if( !m_scoreboard.isPlayerConnected( playerIndex ) ) {
			continue;
		}
		const auto teamNum = m_scoreboard.getPlayerTeam( playerIndex );
		if( wasTeamReset[teamNum] ) {
			continue;
		}
		const auto &teamIndicesTable = listPlayerTables[teamNum];
		const auto rowInTeam = (unsigned)teamIndicesTable[playerIndex];
		assert( rowInTeam < m_playerIndicesForList[teamNum].size() );
		if( teamNum == TEAM_SPECTATOR ) {
			dispatchSpecRowUpdates( playerUpdate, rowInTeam );
			continue;
		}
		const auto &mixedIndicesTable = listPlayerTables[teamNum];
		const auto rowInMixedList = (unsigned)mixedIndicesTable[playerIndex];
		dispatchPlayerRowUpdates( playerUpdate, teamNum, rowInTeam, rowInMixedList );
	}

	for( int i = 0; i < 4; ++i ) {
		if( wasTeamReset[i] ) {
			Q_EMIT teamReset( i );
		}
	}
	if( wasMixedListReset ) {
		Q_EMIT teamReset( 4 );
	}
}

}
