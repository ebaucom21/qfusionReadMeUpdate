#include "scoreboardmodel.h"
#include "local.h"

#include <QJsonObject>

#include <array>

extern cvar_t *cg_showChasers;
std::optional<unsigned> CG_ActiveChasePov();

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
	return (int)m_proxy->m_playerIndicesForLists[m_teamListIndex].size();
}

auto ScoreboardTeamModel::columnCount( const QModelIndex & ) const -> int {
	return (int)m_proxy->m_scoreboard.getColumnsCount();
}

auto ScoreboardTeamModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Kind, "kind" }, { Value, "value" }, { IsGhosting, "isGhosting" } };
}

auto ScoreboardTeamModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( !modelIndex.isValid() ) {
		return QVariant();
	}
	const auto &scb = m_proxy->m_scoreboard;
	const auto &indices = m_proxy->m_playerIndicesForLists;
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

auto ScoreboardSpecsModelData::asQmlArray() const -> QJsonArray {
	if( m_isMarkedAsUpdated ) {
		m_isMarkedAsUpdated = false;
		m_cachedArrayData = QJsonArray();
		const auto &scoreboard = m_proxy->m_scoreboard;
		if( scoreboard.hasPing() ) {
			// TODO: allow specifying formatPing() result type
			for( unsigned playerIndex: *m_indices ) {
				m_cachedArrayData.append( QJsonObject {
					{ "name", toStyledText( scoreboard.getPlayerName( playerIndex ) ) },
					{ "ping", QString::fromLocal8Bit( formatPing( scoreboard.getPlayerPing( playerIndex ) ) ) }
				});
			}
		} else {
			for( unsigned playerIndex: *m_indices ) {
				const QString zeroPingString( QString::fromLocal8Bit( formatPing( 0 ) ) );
				m_cachedArrayData.append( QJsonObject {
					{ "name", toStyledText( scoreboard.getPlayerName( playerIndex ) ) },
					{ "ping", zeroPingString }
				});
			}
		}
	}
	return m_cachedArrayData;
}

void ScoreboardModelProxy::reload() {
	m_scoreboard.reload();
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
	const auto &nums = std::end( m_playerIndicesForLists )[-1];
	assert( (unsigned)row < nums.size() );
	return m_scoreboard.getPlayerTeam( nums[row] ) == TEAM_ALPHA;
}

ScoreboardModelProxy::ScoreboardModelProxy() {
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_PLAYERS );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_ALPHA );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_BETA );
	new( m_teamModelsHolder.unsafe_grow_back() )ScoreboardTeamModel( this, TEAM_BETA + 1 );

	m_displayVar = Cvar_Get( "ui_scoreboardDisplay", "0", CVAR_ARCHIVE );
	checkVars();
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

void ScoreboardModelProxy::checkVars() {
	const auto oldDisplay = m_display;
	m_display = (Display)m_displayVar->value;
	if( m_display != SideBySide && m_display != ColumnWise && m_display != Mixed ) {
		m_display = SideBySide;
		Cvar_ForceSet( m_displayVar->name, va( "%d", (int)m_display ) );
	}
	if( m_display != oldDisplay ) {
		Q_EMIT displayChanged( m_display );
	}

	const auto oldHasChasers = m_hasChasers;
	m_hasChasers = cg_showChasers->integer && CG_ActiveChasePov() != std::nullopt;
	if( m_hasChasers != oldHasChasers ) {
		Q_EMIT hasChasersChanged( m_hasChasers );
	}
}

void ScoreboardModelProxy::update( const ReplicatedScoreboardData &currData ) {
	checkVars();

	Scoreboard::PlayerUpdatesList playerUpdates;
	Scoreboard::TeamUpdatesList teamUpdates;
	const auto maybeUpdateFlags = m_scoreboard.checkAndGetUpdates( currData, playerUpdates, teamUpdates );
	if( !maybeUpdateFlags ) {
		return;
	}

	for( auto &indices: m_playerIndicesForLists ) {
		indices.clear();
	}

	// We should update player nums first so fully reset models get a correct data from the very beginning

	static_assert( TEAM_SPECTATOR == 0 && TEAM_PLAYERS == 1 && TEAM_ALPHA == 2 && TEAM_BETA == 3 );
	using TableOfRowsInTeam = std::array<uint8_t, kMaxPlayers>;
	alignas( 16 ) TableOfRowsInTeam tablesOfRowsInTeams[5];

	for( unsigned playerIndex = 0; playerIndex < kMaxPlayers; ++playerIndex ) {
		if( m_scoreboard.isPlayerConnected( playerIndex ) ) {
			const auto teamNum                         = m_scoreboard.getPlayerTeam( playerIndex );
			auto &indicesForTeam                       = m_playerIndicesForLists[teamNum];
			auto &playerIndicesToRowsInTeamList        = tablesOfRowsInTeams[teamNum];
			playerIndicesToRowsInTeamList[playerIndex] = (uint8_t)indicesForTeam.size();
			indicesForTeam.push_back( playerIndex );
			if( teamNum == TEAM_ALPHA || teamNum == TEAM_BETA ) {
				auto &indicesForMixedList                   = m_playerIndicesForLists[TEAM_BETA + 1];
				auto &playerIndicesToRowsInMixedList        = tablesOfRowsInTeams[TEAM_BETA + 1];
				playerIndicesToRowsInMixedList[playerIndex] = (uint8_t)indicesForMixedList.size();
				indicesForMixedList.push_back( playerIndex );
			}
		}
	}

	const bool mustResetChasers = (unsigned)*maybeUpdateFlags & (unsigned)Scoreboard::UpdateFlags::Chasers;
	const bool mustResetChallengers = (unsigned)*maybeUpdateFlags & (unsigned)Scoreboard::UpdateFlags::Challengers;
	if( mustResetChasers | mustResetChallengers ) {
		alignas( 16 ) unsigned clientIndices[kMaxPlayers];
		std::fill( std::begin( clientIndices ), std::end( clientIndices ), ~0u );
		for( unsigned playerIndex = 0; playerIndex < kMaxPlayers; ++playerIndex ) {
			if( m_scoreboard.isPlayerConnected( playerIndex ) ) {
				const unsigned clientNum = m_scoreboard.getPlayerNum( playerIndex );
				clientIndices[clientNum] = playerIndex;
			}
		}
		if( mustResetChasers ) {
			m_chasers.clear();
			for( unsigned clientNum = 0; clientNum < MAX_CLIENTS; ++clientNum ) {
				if( m_scoreboard.isClientMyChaser( clientNum ) ) {
					m_chasers.push_back( clientIndices[clientNum] );
				}
			}
			m_chasersModel.markAsUpdated();
		}
		if( mustResetChallengers ) {
			m_challengers.clear();
			for( unsigned i = 0; i < (unsigned)MAX_CLIENTS; ++i ) {
				if( const auto maybeClientNum = m_scoreboard.getClientNumOfChallenger( i ) ) {
					m_challengers.push_back( clientIndices[*maybeClientNum] );
				} else {
					break;
				}
			}
			m_challengersModel.markAsUpdated();
		}
	}

	bool wasTeamReset[4] { false, false, false, false };

	for( const Scoreboard::TeamUpdates &teamUpdate: teamUpdates ) {
		// TODO: Handle other team updates (not only player changes) as well
		if( teamUpdate.players ) {
			assert( teamUpdate.team >= TEAM_SPECTATOR && teamUpdate.team <= TEAM_BETA );
			wasTeamReset[teamUpdate.team] = true;
			// Forcing a full reset is the easiest approach.
			if( teamUpdate.team == TEAM_SPECTATOR ) {
				m_specsModel.markAsUpdated();
			} else {
				auto &model = m_teamModelsHolder[teamUpdate.team - 1];
				model.beginResetModel();
				model.endResetModel();
			}
		}
	}

	const bool wasMixedListReset = wasTeamReset[TEAM_ALPHA] | wasTeamReset[TEAM_BETA];
	if( wasMixedListReset ) {
		auto &model = std::end( m_teamModelsHolder )[-1];
		model.beginResetModel();
		model.endResetModel();
	}

	// Build index translation tables prior to dispatching updates, if needed
	static_assert( kMaxPlayers <= 32 );
	unsigned chasersPlayerIndicesMask = 0, challengersPlayerIndicesMask = 0;
	if( !playerUpdates.empty() ) {
		if( !mustResetChasers ) {
			for( const unsigned playerIndex: m_chasers ) {
				chasersPlayerIndicesMask |= ( 1u << playerIndex );
			}
		}
		if( !mustResetChallengers ) {
			for( const unsigned playerIndex: m_challengers ) {
				challengersPlayerIndicesMask |= ( 1u << playerIndex );
			}
		}
	}

	for( const Scoreboard::PlayerUpdates &playerUpdate: playerUpdates ) {
		const unsigned playerIndex = playerUpdate.playerIndex;
		if( m_scoreboard.isPlayerConnected( playerIndex ) ) {
			const unsigned playerBit = ( 1u << playerIndex );
			if( chasersPlayerIndicesMask & playerBit ) {
				m_chasersModel.markAsUpdated();
			}
			if( challengersPlayerIndicesMask & playerBit ) {
				m_challengersModel.markAsUpdated();
			}
			const auto teamNum = (int)m_scoreboard.getPlayerTeam( playerIndex );
			if( !wasTeamReset[teamNum] ) {
				const auto &playerIndexToRowInTeamList = tablesOfRowsInTeams[teamNum];
				const auto rowInTeamList               = (int)playerIndexToRowInTeamList[playerIndex];
				assert( (unsigned)rowInTeamList < (unsigned)m_playerIndicesForLists[teamNum].size() );
				if( teamNum == TEAM_SPECTATOR ) {
					m_specsModel.markAsUpdated();
				} else {
					const auto &playerIndexToRowInMixedList = tablesOfRowsInTeams[TEAM_BETA + 1];
					const auto rowInMixedList               = (int)playerIndexToRowInMixedList[playerIndex];
					dispatchPlayerRowUpdates( playerUpdate, teamNum, rowInTeamList, rowInMixedList );
				}
			}
		}
	}

	static_assert( std::size( wasTeamReset ) == 4 );
	for( int i = 0; i < 4; ++i ) {
		if( wasTeamReset[i] ) {
			if( i != TEAM_SPECTATOR ) {
				Q_EMIT teamReset( i );
			}
		}
	}
	if( wasMixedListReset ) {
		Q_EMIT teamReset( 4 );
	}

	if( m_specsModel.isMarkedAsUpdated() ) {
		Q_EMIT specsModelChanged();
	}
	if( m_chasersModel.isMarkedAsUpdated() ) {
		Q_EMIT chasersModelChanged();
	}
	if( m_challengersModel.isMarkedAsUpdated() ) {
		Q_EMIT challengersModelChanged();
	}
}

}
