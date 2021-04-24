#include "huddatamodel.h"
#include "local.h"
#include "../qcommon/qcommon.h"
#include "../gameshared/gs_public.h"
#include "../client/client.h"

#include <QColor>
#include <QQmlEngine>

// Hacks
bool CG_IsSpectator();
bool CG_HasTwoTeams();
int CG_ActiveWeapon();
bool CG_HasWeapon( int weapon );
int CG_Health();
int CG_Armor();
int CG_TeamAlphaColor();
int CG_TeamBetaColor();
auto CG_GetMatchClockTime() -> std::pair<int, int>;
auto CG_WeaponAmmo( int weapon ) -> std::pair<int, int>;
std::optional<unsigned> CG_ActiveChasePov();
wsw::StringView CG_PlayerName( unsigned playerNum );
wsw::StringView CG_LocationName( unsigned location );

namespace wsw::ui {


class WeaponPropsCache {
	mutable QByteArray m_weaponNames[WEAP_TOTAL - 1];
	QByteArray m_weaponIconPaths[WEAP_TOTAL - 1];
	QColor m_weaponColors[WEAP_TOTAL - 1];
public:
	WeaponPropsCache() {
		QByteArray prefix( "image://wsw/gfx/hud/icons/weapon/" );

		m_weaponIconPaths[WEAP_GUNBLADE - 1] = prefix + "gunblade";
		m_weaponColors[WEAP_GUNBLADE - 1] = QColor::fromRgbF( 1.0, 1.0, 0.5 );

		m_weaponIconPaths[WEAP_MACHINEGUN - 1] = prefix + "machinegun";
		m_weaponColors[WEAP_MACHINEGUN - 1] = QColor::fromRgbF( 0.5, 0.5, 0.5 );

		m_weaponIconPaths[WEAP_RIOTGUN - 1] = prefix + "riot";
		m_weaponColors[WEAP_RIOTGUN - 1] = QColor::fromRgbF( 1.0, 0.5, 0.0 );

		m_weaponIconPaths[WEAP_GRENADELAUNCHER - 1] = prefix + "grenade";
		m_weaponColors[WEAP_GRENADELAUNCHER - 1] = QColor::fromRgbF( 0.0, 0.0, 1.0 );

		m_weaponIconPaths[WEAP_ROCKETLAUNCHER - 1] = prefix + "rocket";
		m_weaponColors[WEAP_ROCKETLAUNCHER - 1] = QColor::fromRgbF( 0.7, 0.0, 0.0 );

		m_weaponIconPaths[WEAP_PLASMAGUN - 1] = prefix + "plasma";
		m_weaponColors[WEAP_PLASMAGUN - 1] = QColor::fromRgbF( 0.0, 0.7, 0.0 );

		m_weaponIconPaths[WEAP_LASERGUN - 1] = prefix + "laser";
		m_weaponColors[WEAP_LASERGUN - 1] = QColor::fromRgbF( 0.9, 0.9, 0.0 );

		m_weaponIconPaths[WEAP_ELECTROBOLT - 1] = prefix + "electro";
		m_weaponColors[WEAP_ELECTROBOLT - 1] = QColor::fromRgbF( 0.0, 0.5, 1.0 );

		m_weaponIconPaths[WEAP_SHOCKWAVE - 1] = prefix + "shockwave";
		m_weaponColors[WEAP_SHOCKWAVE - 1] = QColor::fromRgbF( 0.3, 0.7, 1.0 );

		m_weaponIconPaths[WEAP_INSTAGUN - 1] = prefix + "instagun";
		m_weaponColors[WEAP_INSTAGUN - 1] = QColor::fromRgbF( 0.0, 1.0, 1.0 );

		assert( std::end( m_weaponIconPaths )[-1].length() );
	}

	[[nodiscard]]
	auto getWeaponIconPath( int weapon ) const -> QByteArray {
		assert( (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return weapon ? m_weaponIconPaths[weapon - 1] : QByteArray();
	}

	[[nodiscard]]
	auto getWeaponName( int weapon ) const -> QByteArray {
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		QByteArray &name = m_weaponNames[weapon - 1];
		if( !name.length() ) {
			// Capitalize on data level to reduce possible QML overhead
			const wsw::StringView rawName( GS_GetWeaponDef( weapon )->name );
			name.reserve( (QByteArray::size_type)rawName.length() );
			for( char ch: rawName ) {
				name.append( (char)std::toupper( ch ) );
			}
		}
		return name;
	}

	[[nodiscard]]
	auto getWeaponColor( int weapon ) const -> QColor {
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return m_weaponColors[weapon - 1];
	}
};

static WeaponPropsCache weaponPropsCache;

auto InventoryModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ HasWeapon, "hasWeapon" },
		{ Active, "active" },
		{ IconPath, "iconPath" },
		{ Color, "color" },
		{ WeakAmmoCount, "weakAmmoCount" },
		{ StrongAmmoCount, "strongAmmoCount" }
	};
}

auto InventoryModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto InventoryModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( const int row = index.row(); (unsigned)row < m_entries.size() ) {
			switch( role ) {
				case HasWeapon: return m_entries[row].hasWeapon;
				case Active: return m_entries[row].weaponNum == m_activeWeaponNum;
				case IconPath: return weaponPropsCache.getWeaponIconPath( m_entries[row].weaponNum );
				case Color: return weaponPropsCache.getWeaponColor( m_entries[row].weaponNum );
				case WeakAmmoCount: return m_entries[row].weakCount;
				case StrongAmmoCount: return m_entries[row].strongCount;
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

void InventoryModel::resetWithEntries( const wsw::StaticVector<Entry, 10> &entries ) {
	assert( entries.begin() != m_entries.begin() );
	beginResetModel();
	m_entries.clear();
	for( const Entry &e: entries ) {
		m_entries.push_back( e );
	}
	endResetModel();
}

void InventoryModel::checkPropertyChanges() {
	wsw::StaticVector<Entry, 10> entries;
	for( int i = WEAP_GUNBLADE; i < WEAP_TOTAL; ++i ) {
		const auto [weakCount, strongCount] = CG_WeaponAmmo( i );
		const bool hasWeapon = CG_HasWeapon( i );
		if( weakCount || strongCount || hasWeapon ) {
			Entry entry;
			// TODO: Detect infinite IG ammo
			if( i == WEAP_GUNBLADE ) {
				// TODO: Is it always available?
				entry.weakCount = -1;
				entry.strongCount = -strongCount;
			} else {
				entry.weakCount = weakCount;
				entry.strongCount = strongCount;
			}
			entry.weaponNum = i;
			entry.hasWeapon = hasWeapon;
			entries.push_back( entry );
		}
	}

	// Too much additions, don't complicate things.
	// Removals are unlikely as well.
	if( entries.size() > m_entries.size() + 1 || entries.size() < m_entries.size() ) {
		resetWithEntries( entries );
		return;
	}

	// Try detecting insertion of items into inventory
	if( entries.size() == m_entries.size() + 1 ) {
		unsigned mismatchIndex = 0;
		for(; mismatchIndex < m_entries.size(); ++mismatchIndex ) {
			if( entries[mismatchIndex].weaponNum != m_entries[mismatchIndex].weaponNum ) {
				break;
			}
		}
		for( unsigned i = mismatchIndex; i < m_entries.size(); ++i ) {
			// Another mismatch detected
			if( entries[i + 1].weaponNum != m_entries[i].weaponNum ) {
				resetWithEntries( entries );
				return;
			}
		}
		// This triggers an insertion animation
		beginInsertRows( QModelIndex(), (int)mismatchIndex, (int)mismatchIndex );
		m_entries.insert( m_entries.begin() + mismatchIndex, entries[mismatchIndex] );
		endInsertRows();
	} else {
		assert( entries.size() == m_entries.size() );
		// Simultaneous removals and additions of weapons are rare but possible.
		// We have to detect that.
		for( unsigned i = 0; i < entries.size(); ++i ) {
			if( entries[i].weaponNum != m_entries[i].weaponNum ) {
				resetWithEntries( entries );
				return;
			}
		}
	}

	const auto oldActiveWeapon = m_activeWeaponNum;
	m_activeWeaponNum = CG_ActiveWeapon();

	// Weapon numbers of respective entries are the same at this moment.
	// Check updates of other fields.
	assert( entries.size() == m_entries.size() );
	for( unsigned i = 0; i < entries.size(); ++i ) {
		const auto &oldEntry = m_entries[i];
		const auto &newEntry = entries[i];
		assert( oldEntry.weaponNum == newEntry.weaponNum );

		int numMismatchingRoles = 0;
		const bool hasWeaponMismatch = oldEntry.hasWeapon != newEntry.hasWeapon;
		numMismatchingRoles += hasWeaponMismatch;
		const bool weakCountMismatch = oldEntry.weakCount != newEntry.weakCount;
		numMismatchingRoles += weakCountMismatch;
		const bool strongCountMismatch = oldEntry.strongCount != newEntry.strongCount;
		numMismatchingRoles += strongCountMismatch;
		const bool wasActive = oldEntry.weaponNum == oldActiveWeapon;
		const bool isActive = newEntry.weaponNum == m_activeWeaponNum;
		numMismatchingRoles += wasActive != isActive;

		if( numMismatchingRoles ) {
			m_entries[i] = entries[i];
			const QModelIndex modelIndex( createIndex( ( int )i, 0 ) );
			if( numMismatchingRoles > 1 ) {
				Q_EMIT dataChanged( modelIndex, modelIndex, kAllMutableRolesAsVector );
			} else if( weakCountMismatch ) {
				Q_EMIT dataChanged( modelIndex, modelIndex, kWeakAmmoRoleAsVector );
			} else if( strongCountMismatch ) {
				Q_EMIT dataChanged( modelIndex, modelIndex, kStrongAmmoRoleAsVector );
			} else {
				Q_EMIT dataChanged( modelIndex, modelIndex, kActiveAsRole );
			}
		}
	}
}

auto TeamListModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Health, "health" }, { Armor, "armor" }, { WeaponIconPath, "weaponIconPath" },
		{ Nickname, "nickname" }, { Location, "location" }, { Powerups, "powerups" }
	};
}

auto TeamListModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto TeamListModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( const auto row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case Health: return m_entries[row].health;
				case Armor: return m_entries[row].armor;
				case WeaponIconPath: return weaponPropsCache.getWeaponIconPath( (int)m_entries[row].weapon );
				case Nickname: return toStyledText( CG_PlayerName( m_entries[row].playerNum ) );
				case Location: return toStyledText( CG_LocationName( m_entries[row].location ) );
				case Powerups: return m_entries[row].powerups;
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

void TeamListModel::resetWithScoreboardData( const ReplicatedScoreboardData &scoreboardData ) {
	beginResetModel();
	fillEntries( scoreboardData, m_entries );
	endResetModel();
}

void TeamListModel::fillEntries( const ReplicatedScoreboardData &scoreboardData, EntriesVector &entries ) {
	entries.clear();
	for( unsigned i = 0; i < MAX_CLIENTS; ++i ) {
		if( !scoreboardData.isPlayerGhosting( i ) && scoreboardData.getPlayerTeam( i ) == m_team ) {
			const unsigned playerNum = scoreboardData.getPlayerNum( i );
			if( playerNum != m_povPlayerNum ) {
				// TODO: Looking forward to being able to use designated initializers
				Entry entry;
				entry.playerNum = playerNum;
				entry.health    = scoreboardData.getPlayerHealth( i );
				entry.armor     = scoreboardData.getPlayerArmor( i );
				entry.weapon    = scoreboardData.getPlayerWeapon( i );
				entry.location  = scoreboardData.getPlayerLocation( i );
				entry.powerups  = scoreboardData.getPlayerPowerupBits( i );
				entries.push_back( entry );
			}
		}
	}

	// Sort by player num for now
	const auto cmp = []( const Entry &lhs, const Entry &rhs ) {
		return lhs.playerNum < rhs.playerNum;
	};
	std::sort( entries.begin(), entries.end(), cmp );
}

void TeamListModel::update( const ReplicatedScoreboardData &scoreboardData, unsigned povPlayerNum ) {
	int povTeam = -1;
	for( unsigned i = 0; i < MAX_CLIENTS; ++i ) {
		if( scoreboardData.getPlayerNum( i ) == povPlayerNum ) {
			povTeam = scoreboardData.getPlayerTeam( i );
			break;
		}
	}

	if( povTeam < 0 ) {
		throw std::logic_error( "Failed to find the current POV in the scoreboard" );
	}

	// The POV player is in another team.
	// This also naturally covers many POV changes.
	if( povTeam != m_team ) {
		m_team = povTeam;
		assert( m_team == TEAM_ALPHA || m_team == TEAM_BETA );
		m_povPlayerNum = povPlayerNum;
		resetWithScoreboardData( scoreboardData );
		return;
	}

	// Handle remaining POV change cases
	if( m_povPlayerNum != povPlayerNum ) {
		m_povPlayerNum = povPlayerNum;
		resetWithScoreboardData( scoreboardData );
		return;
	}

	// Backup entries
	m_oldEntries.clear();
	std::copy( std::begin( m_entries ), std::end( m_entries ), std::back_inserter( m_oldEntries ) );

	fillEntries( scoreboardData, m_entries );

	// TODO: This can be optimized in case of having a single-row difference
	if( m_entries.size() != m_oldEntries.size() ) {
		beginResetModel();
		endResetModel();
		return;
	}

	const auto *const tracker = wsw::ui::NameChangesTracker::instance();

	// Just dispatch updates
	for( unsigned i = 0; i < m_entries.size(); ++i ) {
		const auto &oldEntry = m_oldEntries[i];
		const auto &entry = m_entries[i];
		int numUpdates = 0;
		if( entry.playerNum != oldEntry.playerNum ) {
			// Do a full reset
			numUpdates = 999;
		} else {
			const unsigned counter = tracker->getLastNicknameUpdateCounter( entry.playerNum );
			if( counter != m_nicknameUpdateCounters[entry.playerNum] ) {
				m_nicknameUpdateCounters[entry.playerNum] = counter;
				numUpdates = 999;
			}
		}

		const bool hasHealthUpdates = entry.health != oldEntry.health;
		numUpdates += hasHealthUpdates;
		const bool hasArmorUpdates = entry.armor != oldEntry.armor;
		numUpdates += hasArmorUpdates;
		const bool hasWeaponUpdates = entry.weapon != oldEntry.weapon;
		numUpdates += hasWeaponUpdates;
		const bool hasLocationUpdates = entry.location != oldEntry.location;

		if( numUpdates ) {
			// Specify a full row update by default
			const QVector<int> *changedRoles = nullptr;
			// Optimize for most frequent updates
			if( numUpdates == 1 ) {
				if( hasHealthUpdates ) {
					changedRoles = &kHealthAsRole;
				} else if( hasArmorUpdates ) {
					changedRoles = &kArmorAsRole;
				} else if( hasWeaponUpdates ) {
					changedRoles = &kWeaponIconPathAsRole;
				} else if( hasLocationUpdates ) {
					changedRoles = &kLocationAsRole;
				}
			} else if( numUpdates == 2 ) {
				if( hasHealthUpdates && hasArmorUpdates ) {
					changedRoles = &kHealthAndArmorAsRole;
				}
			}

			const QModelIndex modelIndex( createIndex( (int)i, 0 ) );
			if( changedRoles ) {
				Q_EMIT dataChanged( modelIndex, modelIndex, *changedRoles );
			} else {
				Q_EMIT dataChanged( modelIndex, modelIndex );
			}
		}
	}
}

auto HudDataModel::getActiveWeaponIcon() const -> QByteArray {
	return m_activeWeapon ? weaponPropsCache.getWeaponIconPath( m_activeWeapon ) : QByteArray();
}

auto HudDataModel::getActiveWeaponName() const -> QByteArray {
	return m_activeWeapon ? weaponPropsCache.getWeaponName( m_activeWeapon ) : QByteArray();
}

void HudDataModel::setFormattedTime( QByteArray *dest, int value ) {
	assert( value >= 0 );
	dest->clear();
	if( value >= 100 ) {
		dest->setNum( value );
	} else {
		dest->clear();
		const int q = value / 10;
		const int r = value - ( q * 10 );
		dest->append( (char)( '0' + q ) );
		dest->append( (char)( '0' + r ) );
	}
}

void HudDataModel::setStyledTeamName( QByteArray *dest, const wsw::StringView &name ) {
	// TODO: toStyledText() should allow accepting an external buffer of an arbitrary structurally compatible type
	*dest = toStyledText( name ).toLatin1();
}

HudDataModel::HudDataModel() {
	assert( !m_matchTimeMinutes && !m_matchTimeSeconds );
	setFormattedTime( &m_formattedMinutes, m_matchTimeMinutes );
	setFormattedTime( &m_formattedSeconds, m_matchTimeSeconds );
}

auto HudDataModel::getInventoryModel() -> QAbstractListModel * {
	if( !m_hasSetInventoryModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_inventoryModel, QQmlEngine::CppOwnership );
		m_hasSetInventoryModelOwnership = true;
	}
	return &m_inventoryModel;
}

auto HudDataModel::getTeamListModel() -> QAbstractListModel * {
	if( !m_hasSetTeamListModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_teamListModel, QQmlEngine::CppOwnership );
		m_hasSetTeamListModelOwnership = true;
	}
	return &m_teamListModel;
}

void HudDataModel::checkPropertyChanges() {
	const bool hadTwoTeams = getHasTwoTeams();
	m_hasTwoTeams = CG_HasTwoTeams();
	if( const bool hasTwoTeams = getHasTwoTeams(); hasTwoTeams != hadTwoTeams ) {
		Q_EMIT hasTwoTeamsChanged( hasTwoTeams );
	}

	const bool wasSpectator = getIsSpectator();
	m_isSpectator = CG_IsSpectator();
	if( const bool isSpectator = getIsSpectator(); isSpectator != wasSpectator ) {
		Q_EMIT isSpectatorChanged( isSpectator );
	}

	if( m_pendingAlphaScore != m_alphaScore ) {
		m_alphaScore = m_pendingAlphaScore;
		Q_EMIT alphaScoreChanged( m_alphaScore );
	}
	if( m_pendingBetaScore != m_betaScore ) {
		m_betaScore = m_pendingBetaScore;
		Q_EMIT betaScoreChanged( m_betaScore );
	}

	const wsw::StringView alphaName( ::cl.configStrings.getTeamAlphaName().value_or( wsw::StringView() ) );
	if( !m_alphaName.equals( alphaName ) ) {
		m_alphaName.assign( alphaName );
		setStyledTeamName( &m_styledAlphaName, alphaName );
		Q_EMIT alphaNameChanged( getAlphaName() );
	}

	const wsw::StringView betaName( ::cl.configStrings.getTeamBetaName().value_or( wsw::StringView() ) );
	if( !m_betaName.equals( betaName ) ) {
		m_betaName.assign( betaName );
		setStyledTeamName( &m_styledBetaName, betaName );
		Q_EMIT betaNameChanged( getBetaName() );
	}

	if( m_numAliveAlphaPlayers != m_pendingNumAliveAlphaPlayers ) {
		m_numAliveAlphaPlayers = m_pendingNumAliveAlphaPlayers;
		m_alphaPlayersStatus = getStatusForNumberOfPlayers( m_numAliveAlphaPlayers );
		Q_EMIT alphaPlayersStatusChanged( m_alphaPlayersStatus );
	}

	if( m_numAliveBetaPlayers != m_pendingNumAliveBetaPlayers ) {
		m_numAliveBetaPlayers = m_pendingNumAliveBetaPlayers;
		m_betaPlayersStatus = getStatusForNumberOfPlayers( m_numAliveBetaPlayers );
		Q_EMIT betaPlayersStatusChanged( m_betaPlayersStatus );
	}

	const wsw::StringView alphaStatus( ::cl.configStrings.getTeamAlphaStatus().value_or( wsw::StringView() ) );
	if( !m_alphaTeamStatus.equals( alphaStatus ) ) {
		m_alphaTeamStatus.assign( alphaStatus );
		m_styledAlphaTeamStatus = toStyledText( alphaStatus ).toUtf8();
		Q_EMIT alphaTeamStatusChanged( m_styledAlphaTeamStatus );
	}

	const wsw::StringView betaStatus( ::cl.configStrings.getTeamBetaStatus().value_or( wsw::StringView() ) );
	if( !m_betaTeamStatus.equals( betaStatus ) ) {
		m_betaTeamStatus.assign( betaStatus );
		m_styledBetaTeamStatus = toStyledText( betaStatus ).toUtf8();
		Q_EMIT betaTeamStatusChanged( m_styledBetaTeamStatus );
	}

	const auto oldAlphaColor = m_alphaColor;
	if( oldAlphaColor != ( m_alphaColor = CG_TeamAlphaColor() ) ) {
		Q_EMIT alphaColorChanged( getAlphaColor() );
	}

	const auto oldBetaColor = m_betaColor;
	if( oldBetaColor != ( m_betaColor = CG_TeamBetaColor() ) ) {
		Q_EMIT betaColorChanged( getBetaColor() );
	}

	const auto [minutes, seconds] = CG_GetMatchClockTime();
	if( minutes != m_matchTimeMinutes ) {
		assert( minutes >= 0 );
		m_matchTimeMinutes = minutes;
		setFormattedTime( &m_formattedMinutes, minutes );
		Q_EMIT matchTimeMinutesChanged( getMatchTimeMinutes() );
	}
	if( seconds != m_matchTimeSeconds ) {
		assert( (unsigned)seconds < 60u );
		m_matchTimeSeconds = seconds;
		setFormattedTime( &m_formattedSeconds, seconds );
		Q_EMIT matchTimeSecondsChanged( getMatchTimeSeconds() );
	}

	const auto oldActiveWeapon = m_activeWeapon;
	if( oldActiveWeapon != ( m_activeWeapon = CG_ActiveWeapon() ) ) {
		Q_EMIT activeWeaponIconChanged( getActiveWeaponIcon() );
		Q_EMIT activeWeaponNameChanged( getActiveWeaponName() );
	}

	const auto oldActiveWeaponWeakAmmo = m_activeWeaponWeakAmmo;
	const auto oldActiveWeaponStrongAmmo = m_activeWeaponStrongAmmo;
	std::tie( m_activeWeaponWeakAmmo, m_activeWeaponStrongAmmo ) = CG_WeaponAmmo( m_activeWeapon );
	if( m_activeWeapon == WEAP_GUNBLADE ) {
		// TODO: Is it always available?
		m_activeWeaponWeakAmmo = -1;
		m_activeWeaponStrongAmmo = -m_activeWeaponStrongAmmo;
	}
	if( oldActiveWeaponWeakAmmo != m_activeWeaponWeakAmmo ) {
		Q_EMIT activeWeaponWeakAmmoChanged( m_activeWeaponWeakAmmo );
	}
	if( oldActiveWeaponStrongAmmo != m_activeWeaponStrongAmmo ) {
		Q_EMIT activeWeaponStrongAmmoChanged( m_activeWeaponStrongAmmo );
	}

	const auto oldHealth = m_health;
	m_health = CG_Health();
	if( oldHealth != m_health ) {
		Q_EMIT healthChanged( m_health );
	}

	const auto oldArmor = m_armor;
	m_armor = CG_Armor();
	if( oldArmor != m_armor ) {
		Q_EMIT armorChanged( m_armor );
	}

	const auto hadLocations = m_hasLocations;
	assert( MAX_LOCATIONS > 1 );
	// Require having at least a couple of locations defined
	m_hasLocations = ::cl.configStrings.get( CS_LOCATIONS + 0 ) && ::cl.configStrings.get( CS_LOCATIONS + 1 );
	if( hadLocations != m_hasLocations ) {
		Q_EMIT hasLocationsChanged( m_hasLocations );
	}

	m_inventoryModel.checkPropertyChanges();
}

void HudDataModel::updateScoreboardData( const ReplicatedScoreboardData &scoreboardData ) {
	m_pendingAlphaScore = scoreboardData.alphaScore;
	m_pendingBetaScore = scoreboardData.betaScore;
	if( CG_HasTwoTeams() ) {
		if( const auto maybeActiveChasePov = CG_ActiveChasePov() ) {
			m_teamListModel.update( scoreboardData, *maybeActiveChasePov );
			updateTeamPlayerStatuses( scoreboardData );
		}
	}
}

static const QByteArray kStatusesForNumberOfPlayers[] {
	"",
	"\u066D",
	"\u066D \u066D",
	"\u066D \u066D \u066D",
	"\u066D \u066D \u066D \u066D",
	"\u066D \u066D \u066D \u066D \u066D",
	"\u066D \u066D \u066D \u066D \u066D \u066D"
};

auto HudDataModel::getStatusForNumberOfPlayers( int numPlayers ) const -> QByteArray {
	return kStatusesForNumberOfPlayers[std::min( numPlayers, (int)std::size( kStatusesForNumberOfPlayers ) - 1 )];
}

void HudDataModel::updateTeamPlayerStatuses( const ReplicatedScoreboardData &scoreboardData ) {
	m_pendingNumAliveAlphaPlayers = 0;
	m_pendingNumAliveBetaPlayers = 0;
	for( unsigned i = 0; i < MAX_CLIENTS; ++i ) {
		if( scoreboardData.isPlayerConnected( i ) && !scoreboardData.isPlayerGhosting( i ) ) {
			const int team = scoreboardData.getPlayerTeam( i );
			if( team == TEAM_ALPHA ) {
				m_pendingNumAliveAlphaPlayers++;
			} else if( team == TEAM_BETA ) {
				m_pendingNumAliveBetaPlayers++;
			}
		}
	}
}

}