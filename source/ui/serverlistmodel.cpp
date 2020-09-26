#include "serverlistmodel.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>

namespace wsw::ui {

auto ServerListModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ ServerName, "serverName" },
		{ MapName, "mapName" },
		{ Gametype, "gametype" },
		{ Address, "address" },
		{ NumPlayers, "numPlayers" },
		{ MaxPlayers, "maxPlayers" },
		{ TimeMinutes, "timeMinutes" },
		{ TimeSeconds, "timeSeconds" },
		{ TimeFlags, "timeFlags" },
		{ AlphaTeamName, "alphaTeamName" },
		{ BetaTeamName, "betaTeamName" },
		{ AlphaTeamScore, "alphaTeamScore" },
		{ BetaTeamScore, "betaTeamScore" },
		{ AlphaTeamList, "alphaTeamList" },
		{ BetaTeamList, "betaTeamList" },
		{ PlayersTeamList, "playersTeamList" },
		{ SpectatorsList, "spectatorsList" }
	};
}

auto ServerListModel::rowCount( const QModelIndex & ) const -> int {
	int size = m_servers.size();
	if( size % 2 ) {
		size += 1;
	}
	return size / 2;
}

auto ServerListModel::columnCount( const QModelIndex & ) const -> int {
	return 2;
}

auto ServerListModel::getServerAtIndex( int index ) const -> const PolledGameServer * {
	if( (unsigned)index < m_servers.size() ) {
		return m_servers[index];
	}
	return nullptr;
}

auto ServerListModel::getIndexOfServer( const PolledGameServer *server ) const -> std::optional<unsigned> {
	for( unsigned i = 0; i < m_servers.size(); ++i ) {
		if( m_servers[i] == server ) {
			return i;
		}
	}
	return std::nullopt;
}

auto ServerListModel::data( const QModelIndex &index, int role ) const -> QVariant {
	const auto *server = getServerAtIndex( index.row() * 2 + index.column() );
	if( !server ) {
		return QVariant();
	}

	// TODO: Should these conversions be cached?

	switch( role ) {
		case ServerName:
			return toStyledText( server->getServerName() );
		case MapName:
			return toStyledText( server->getMapName() );
		case Gametype:
			return toStyledText( server->getGametype() );
		case Address:
			return QVariant( QString::fromLatin1( NET_AddressToString( &server->getAddress() ) ) );
		case Ping:
			// TODO: No ping yet?
			return QVariant();
		case NumPlayers:
			return server->getNumClients();
		case MaxPlayers:
			return server->getMaxClients();
		case TimeMinutes:
			return server->getTime().timeMinutes;
		case TimeSeconds:
			return server->getTime().timeSeconds;
		case TimeFlags:
			return toMatchTimeFlags( server->getTime() );
		case AlphaTeamName:
			return toStyledText( server->getAlphaName() );
		case BetaTeamName:
			return toStyledText( server->getBetaName() );
		case AlphaTeamScore:
			return server->getAlphaScore();
		case BetaTeamScore:
			return server->getBetaScore();
		case AlphaTeamList:
			return toQmlTeamList( server->getAlphaTeam().first );
		case BetaTeamList:
			return toQmlTeamList( server->getBetaTeam().first );
		case PlayersTeamList:
			return toQmlTeamList( server->getPlayersTeam().first );
		case SpectatorsList:
			return toQmlTeamList( server->getSpectators().first );
		default:
			return QVariant();
	}
}

void ServerListModel::onServerAdded( const PolledGameServer *server ) {
	if( getIndexOfServer( server ) ) {
		return;
	}

	auto serversCount = (int)m_servers.size();
	if( serversCount % 2 ) {
		m_servers.push_back( server );
		QModelIndex modelIndex( QAbstractTableModel::index( rowCount( QModelIndex() ), 1 ) );
		Q_EMIT dataChanged( modelIndex, modelIndex );
		return;
	}

	const auto newRowCount = ( serversCount + 1 ) / 2;
	beginInsertRows( QModelIndex(), newRowCount, newRowCount );
	m_servers.push_back( server );
	endInsertRows();
}

void ServerListModel::onServerRemoved( const PolledGameServer *server ) {
	const auto maybeServerIndex = getIndexOfServer( server );
	if( !maybeServerIndex ) {
		return;
	}

	// TODO!!!!!!!!
}

void ServerListModel::onServerUpdated( const PolledGameServer *server ) {
	// TODO: Update sort model prior to that
	const auto maybeServerIndex = getIndexOfServer( server );
	if( !maybeServerIndex ) {
		return;
	}

	const auto row = (int)*maybeServerIndex / 2;
	const auto column = (int)*maybeServerIndex % 2;

	QModelIndex modelIndex( QAbstractTableModel::index( row, column ) );
	Q_EMIT dataChanged( modelIndex, modelIndex );
}

auto ServerListModel::toQmlTeamList( const PlayerInfo *playerInfoHead ) -> QVariant {
	if( !playerInfoHead ) {
		return QVariant();
	}

	QJsonArray result;
	for( const auto *info = playerInfoHead; info; info = info->next ) {
		QJsonObject obj {
			{ "name", toStyledText( info->getName() ) },
			{ "ping", info->getPing() },
			{ "score", info->getScore() }
		};
		result.append( obj );
	}

	return result;
}

auto ServerListModel::toMatchTimeFlags( const MatchTime &time ) -> int {
	int flags = 0;
	// TODO: Parse match time flags as an enum?
	flags |= time.isWarmup ? Warmup : 0;
	flags |= time.isCountdown ? Countdown : 0;
	flags |= time.isFinished ? Finished : 0;
	flags |= time.isOvertime ? Overtime: 0;
	flags |= time.isSuddenDeath ? SuddenDeath : 0;
	flags |= time.isTimeout ? Timeout : 0;
	return flags;
}

class HtmlColorNamesCache {
	QString names[10];
public:
	auto getColorName( int colorNum ) -> const QString & {
		assert( (unsigned)colorNum < 10u );
		if( !names[colorNum].isEmpty() ) {
			return names[colorNum];
		}
		const float *rawColor = color_table[colorNum];
		names[colorNum] = QColor::fromRgbF( rawColor[0], rawColor[1], rawColor[2] ).name( QColor::HexRgb );
		return names[colorNum];
	}
};

static HtmlColorNamesCache htmlColorNamesCache;

static const QLatin1String kFontOpeningTagPrefix( "<font color=\"" );
static const QLatin1String kFontOpeningTagSuffix( "\">" );
static const QLatin1String kFontClosingTag( "</font>" );

auto ServerListModel::toStyledText( const wsw::StringView &text ) -> QString {
	QString result;
	result.reserve( (int)text.size() + 1 );

	bool hadColorToken = false;
	wsw::StringView sv( text );
	for(;;) {
		std::optional<unsigned> index = sv.indexOf( '^' );
		if( !index ) {
			result.append( QLatin1String( sv.data(), sv.size() ) );
			if( hadColorToken ) {
				result.append( kFontClosingTag );
			}
			return result;
		}

		result.append( QLatin1String( sv.data(), (int)*index ) );
		sv = sv.drop( *index + 1 );
		if( sv.empty() ) {
			if( hadColorToken ) {
				result.append( kFontClosingTag );
			}
			return result;
		}

		if( sv.front() < '0' || sv.front() > '9' ) {
			if( sv.front() == '^' ) {
				result.append( '^' );
				sv = sv.drop( 1 );
			}
			continue;
		}

		if( hadColorToken ) {
			result.append( kFontClosingTag );
		}

		result.append( kFontOpeningTagPrefix );
		result.append( htmlColorNamesCache.getColorName( sv.front() - '0' ) );
		result.append( kFontOpeningTagSuffix );

		sv = sv.drop( 1 );
		hadColorToken = true;
	}
}

}