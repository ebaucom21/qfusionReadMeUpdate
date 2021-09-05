#include "huddatamodel.h"
#include "local.h"
#include "../qcommon/qcommon.h"
#include "../gameshared/gs_public.h"
#include "../client/client.h"
#include "../cgame/crosshairstate.h"

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
int CG_TeamAlphaProgress();
int CG_TeamBetaProgress();
auto CG_GetMatchClockTime() -> std::pair<int, int>;
auto CG_WeaponAmmo( int weapon ) -> std::pair<int, int>;
std::optional<unsigned> CG_ActiveChasePov();
bool CG_IsPovAlive();
wsw::StringView CG_PlayerName( unsigned playerNum );
wsw::StringView CG_LocationName( unsigned location );

namespace wsw::ui {

class WeaponPropsCache {
	struct Entry {
		QByteArray fullName;
		QByteArray shortName;
		QByteArray iconPath;
		QColor color;
	};

	wsw::StaticVector<Entry, WEAP_TOTAL - 1> m_entries;
	QStringList m_availableStrongCrosshairs;
	QStringList m_availableCrosshairs;
public:
	WeaponPropsCache() {
		QByteArray prefix( "image://wsw/gfx/hud/icons/weapon/" );

		m_entries.emplace_back( { "Gunblade", "gb", prefix + "gunblade", QColor::fromRgbF( 1.0, 1.0, 0.5 ) } );
		m_entries.emplace_back( { "Machinegun", "mg", prefix + "machinegun", QColor::fromRgbF( 0.5, 0.5, 0.5 ) } );
		m_entries.emplace_back( { "Riotgun", "rg", prefix + "riot", QColor::fromRgbF( 1.0, 0.5, 0.0 ) } );
		m_entries.emplace_back( { "Grenade Launcher", "gl", prefix + "grenade", QColor::fromRgbF( 0.0, 0.0, 1.0 ) } );
		m_entries.emplace_back( { "Rocket Launcher", "rl", prefix + "rocket", QColor::fromRgbF( 0.7, 0.0, 0.0 ) } );
		m_entries.emplace_back( { "Plasmagun", "pg", prefix + "plasma", QColor::fromRgbF( 0.0, 0.7, 0.0 ) } );
		m_entries.emplace_back( { "Lasergun", "lg", prefix + "laser", QColor::fromRgbF( 0.9, 0.9, 0.0 ) } );
		m_entries.emplace_back( { "Electrobolt", "eb", prefix + "electro", QColor::fromRgbF( 0.0, 0.5, 1.0 ) } );
		m_entries.emplace_back( { "Shockwave", "sw", prefix + "shockwave", QColor::fromRgbF( 0.3, 0.7, 1.0 ) } );
		m_entries.emplace_back( { "Instagun", "ig", prefix + "instagun", QColor::fromRgbF( 0.0, 1.0, 1.0 ) } );

		QString buffer;
		m_availableCrosshairs.reserve( kNumRegularCrosshairs );
		for( unsigned i = 1; i < kNumRegularCrosshairs + 1; ++i ) {
			CrosshairState::makePath<QString, QLatin1String>( &buffer, CrosshairState::Weak, i );
			m_availableCrosshairs.append( buffer );
		}
		m_availableStrongCrosshairs.reserve( kNumStrongCrosshairs );
		for( unsigned i = 1; i < kNumStrongCrosshairs + 1; ++i ) {
			CrosshairState::makePath<QString, QLatin1String>( &buffer, CrosshairState::Strong, i );
			m_availableStrongCrosshairs.append( buffer );
		}
	}

	[[nodiscard]]
	auto getWeaponIconPath( int weapon ) const -> QByteArray {
		assert( (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return weapon ? m_entries[weapon - 1].iconPath : QByteArray();
	}

	[[nodiscard]]
	auto getWeaponFullName( int weapon ) const -> QByteArray {
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return m_entries[weapon - 1].fullName;
	}

	[[nodiscard]]
	auto getWeaponShortName( int weapon ) const -> QByteArray {
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return m_entries[weapon - 1].shortName;
	}

	[[nodiscard]]
	auto getWeaponColor( int weapon ) const -> QColor {
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return m_entries[weapon - 1].color;
	}

	[[nodiscard]]
	auto getAvailableCrosshairs() const -> QStringList { return m_availableCrosshairs; }
	[[nodiscard]]
	auto getAvailableStrongCrosshairs() const -> QStringList { return m_availableStrongCrosshairs; }

	[[nodiscard]]
	auto getFragsFeedIconPath( unsigned meansOfDeath ) const -> QByteArray {
		assert( meansOfDeath >= MOD_GUNBLADE_W && meansOfDeath < (unsigned)MOD_COUNT );
		// No static guarantees here but we can spot bugs easily in this case
		if( meansOfDeath >= MOD_GUNBLADE_W && meansOfDeath <= MOD_INSTAGUN_S ) {
			return getWeaponIconPath( (int)( WEAP_GUNBLADE + ( meansOfDeath - MOD_GUNBLADE_W ) / 2 ) );
		}
		// TODO: What's the point of having these separate values?
		if( meansOfDeath == MOD_GRENADE_SPLASH_S || meansOfDeath == MOD_GRENADE_SPLASH_W ) {
			return getWeaponIconPath( WEAP_GRENADELAUNCHER );
		}
		if( meansOfDeath == MOD_ROCKET_SPLASH_S || meansOfDeath == MOD_ROCKET_SPLASH_W ) {
			return getWeaponIconPath( WEAP_ROCKETLAUNCHER );
		}
		if( meansOfDeath == MOD_PLASMA_SPLASH_S || meansOfDeath == MOD_PLASMA_SPLASH_W ) {
			return getWeaponIconPath( WEAP_PLASMAGUN );
		}
		if( meansOfDeath >= MOD_SHOCKWAVE_SPLASH_W && meansOfDeath <= MOD_SHOCKWAVE_CORONA_S ) {
			return getWeaponIconPath( WEAP_SHOCKWAVE );
		}
		return getWeaponIconPath( WEAP_GUNBLADE );
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

auto FragsFeedModel::roleNames() const -> QHash<int, QByteArray> {
	return { { { Victim, "victim" }, { Attacker, "attacker" }, { IconPath, "iconPath" } } };
}

auto FragsFeedModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto FragsFeedModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( const auto row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			const auto &entry = m_entries[row];
			switch( role ) {
				case Victim: return toDisplayedName( entry.victimName.asView(), entry.victimTeamColor );
				case Attacker: return toDisplayedName( entry.attackerName.asView(), entry.attackerTeamColor );
				case IconPath: return weaponPropsCache.getFragsFeedIconPath( entry.meansOfDeath );
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

auto FragsFeedModel::toDisplayedName( const wsw::StringView &rawName, const std::optional<int> &teamColor ) -> QString {
	// Optimize for retrieval of an empty attacker
	if( !rawName.empty() ) {
		if( !teamColor ) {
			return toStyledText( rawName );
		} else {
			return wrapInColorTags( rawName, *teamColor );
		}
	}
	return QString();
}

void FragsFeedModel::addFrag( const std::pair<wsw::StringView, int> &victimAndTeam,
							  int64_t timestamp, unsigned meansOfDeath,
							  const std::optional<std::pair<wsw::StringView, int>> &attackerAndTeam ) {
	// TODO: Add to pending entries in case of a huge feed size?
	if( m_entries.full() ) {
		beginRemoveRows( QModelIndex(), 0, 0 );
		m_entries.erase( m_entries.begin() );
		endRemoveRows();
	}

	beginInsertRows( QModelIndex(), (int)m_entries.size(), (int)m_entries.size() );

	m_entries.emplace_back( Entry {} );
	Entry &entry = m_entries.back();
	entry.meansOfDeath = meansOfDeath;
	entry.timestamp = timestamp;

	if( m_hudDataModel->m_hasTwoTeams ) {
		entry.victimName.assign( victimAndTeam.first );
		removeColorTokens( &entry.victimName );
		entry.victimTeamColor = victimAndTeam.second == TEAM_ALPHA ?
			m_hudDataModel->m_rawAlphaColor : m_hudDataModel->m_rawBetaColor;
		if( attackerAndTeam ) {
			entry.attackerName.assign( attackerAndTeam->first );
			removeColorTokens( &entry.attackerName );
			entry.attackerTeamColor = attackerAndTeam->second == TEAM_ALPHA ?
				m_hudDataModel->m_rawAlphaColor : m_hudDataModel->m_rawBetaColor;
		}
	} else {
		entry.victimName.assign( victimAndTeam.first );
		if( attackerAndTeam ) {
			entry.attackerName.assign( attackerAndTeam->first );
		}
	}

	endInsertRows();
}

void FragsFeedModel::reset() {
	beginResetModel();
	m_entries.clear();
	endResetModel();
}

template <typename Entries>
[[nodiscard]]
static auto getNumTimedOutEntries( const Entries &entries, int64_t currTime, unsigned timeout ) -> unsigned {
	unsigned i = 0;
	for(; i < entries.size(); ++i ) {
		if( entries[i].timestamp + timeout > currTime ) {
			break;
		}
	}
	return i;
}

void FragsFeedModel::update( int64_t currTime ) {
	if( const unsigned numTimedOutEntries = getNumTimedOutEntries( m_entries, currTime, 5000u ) ) {
		beginRemoveRows( QModelIndex(), 0, (int)numTimedOutEntries - 1 );
		m_entries.erase( m_entries.begin(), m_entries.begin() + numTimedOutEntries );
		endRemoveRows();
	}
}

auto MessageFeedModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Message, "message" } };
}

auto MessageFeedModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto MessageFeedModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() && role == Message ) {
		if( const auto row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			return toStyledText( m_entries[row].message.asView() );
		}
	}
	return QVariant();
}

void MessageFeedModel::addMessage( const wsw::StringView &message, int64_t timestamp ) {
	// We add to an intermediate circular buffer so the primary model is not flooded by additions/removals
	m_pendingEntries[m_pendingEntriesTail].timestamp = timestamp;
	m_pendingEntries[m_pendingEntriesTail].message.assign( message );
	m_pendingEntriesTail = ( m_pendingEntriesTail + 1 ) % kMaxEntries;
	if( m_numPendingEntries == kMaxEntries ) {
		m_pendingEntriesHead++;
	} else {
		m_numPendingEntries++;
	}
}

void MessageFeedModel::update( int64_t currTime ) {
	if( const unsigned numTimedOutEntries = getNumTimedOutEntries( m_entries, currTime, 7500u ) ) {
		beginRemoveRows( QModelIndex(), 0, (int)numTimedOutEntries - 1 );
		m_entries.erase( m_entries.begin(), m_entries.begin() + numTimedOutEntries );
		endRemoveRows();
	}

	if( m_numPendingEntries ) {
		const auto totalNumEntries = m_numPendingEntries + m_entries.size();
		if( totalNumEntries > kMaxEntries ) {
			const auto numEntriesToRemove = totalNumEntries - kMaxEntries;
			assert( numEntriesToRemove > 0 && numEntriesToRemove <= kMaxEntries );
			beginRemoveRows( QModelIndex(), 0, (int)( numEntriesToRemove - 1 ) );
			m_entries.erase( m_entries.begin(), m_entries.begin() + numEntriesToRemove );
			endRemoveRows();
		}

		assert( m_entries.size() + m_numPendingEntries <= kMaxEntries );
		beginInsertRows( QModelIndex(), (int)m_entries.size(), (int)m_entries.size() + (int)m_numPendingEntries - 1 );
		unsigned cursor = m_pendingEntriesHead;
		for( unsigned i = 0; i < m_numPendingEntries; ++i ) {
			assert( !m_entries.full() );
			m_entries.push_back( m_pendingEntries[cursor] );
			cursor = ( cursor + 1 ) % kMaxEntries;
		}
		endInsertRows();

		m_numPendingEntries = m_pendingEntriesHead = m_pendingEntriesTail = 0;
	}

	if( m_entries.empty() ) {
		m_isFadingOut = false;
	} else {
		m_isFadingOut = true;
		for( const Entry &entry: m_entries ) {
			if( entry.timestamp + 5000 > currTime ) {
				m_isFadingOut = false;
				break;
			}
		}
	}
}

auto AwardsModel::roleNames() const -> QHash<int, QByteArray> {
	return { { Message, "message" } };
}

auto AwardsModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto AwardsModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( modelIndex.isValid() ) {
		if( const int row = modelIndex.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			return toStyledText( m_entries[row].message.asView() );
		}
	}
	return QVariant();
}

void AwardsModel::addAward( const wsw::StringView &award, int64_t timestamp ) {
	m_pendingEntries.emplace_back( Entry { timestamp, award } );
}

void AwardsModel::update( int64_t currTime ) {
	if( const auto numTimedOutEntries = getNumTimedOutEntries( m_entries, currTime, 3000 ) ) {
		beginRemoveRows( QModelIndex(), 0, (int)numTimedOutEntries - 1 );
		m_entries.erase( m_entries.begin(), m_entries.begin() + numTimedOutEntries );
		endRemoveRows();
	}

	if( !m_pendingEntries.empty() ) {
		if( m_entries.full() ) {
			beginRemoveRows( QModelIndex(), 0, 0 );
			m_entries.erase( m_entries.begin() );
			endRemoveRows();
		}

		beginInsertRows( QModelIndex(), (int) m_entries.size(), (int) m_entries.size() );
		// TODO: Move from the font element
		auto *const entry = new( m_entries.unsafe_grow_back() )Entry( m_pendingEntries.front() );
		// Compensate the queue delay
		entry->timestamp += ( currTime - m_pendingEntries.front().timestamp );
		// This is fine for this small data set (todo: use a deque?)
		m_pendingEntries.erase( m_pendingEntries.begin() );
		endInsertRows();
	}
}

auto HudDataModel::getActiveWeaponIcon() const -> QByteArray {
	return m_activeWeapon ? weaponPropsCache.getWeaponIconPath( m_activeWeapon ) : QByteArray();
}

auto HudDataModel::getActiveWeaponName() const -> QByteArray {
	return m_activeWeapon ? weaponPropsCache.getWeaponFullName( m_activeWeapon ) : QByteArray();
}

auto HudDataModel::getWeaponFullName( int weapon ) const -> QByteArray {
	return weaponPropsCache.getWeaponFullName( weapon );
}

auto HudDataModel::getWeaponShortName( int weapon ) const -> QByteArray {
	return weaponPropsCache.getWeaponShortName( weapon );
}

auto HudDataModel::getWeaponIconPath( int weapon ) const -> QByteArray {
	return weaponPropsCache.getWeaponIconPath( weapon );
}

auto HudDataModel::getAvailableCrosshairs() const -> QStringList {
	return weaponPropsCache.getAvailableCrosshairs();
}

auto HudDataModel::getAvailableStrongCrosshairs() const -> QStringList {
	return weaponPropsCache.getAvailableStrongCrosshairs();
}

static const QByteArray kWarmup( "WARMUP" );
static const QByteArray kCountdown( "COUNTDOWN" );
static const QByteArray kOvertime( "OVERTIME" );
static const QByteArray kNoMatchState;

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

	m_clientHudVar = Cvar_Get( "cg_clientHUD", "default", CVAR_ARCHIVE );
	m_clientHudVar->modified = true;
	m_specHudVar = Cvar_Get( "cg_specHUD", "default", CVAR_ARCHIVE );
	m_specHudVar->modified = true;
}

auto HudDataModel::getInventoryModel() -> QObject * {
	if( !m_hasSetInventoryModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_inventoryModel, QQmlEngine::CppOwnership );
		m_hasSetInventoryModelOwnership = true;
	}
	return &m_inventoryModel;
}

auto HudDataModel::getTeamListModel() -> QObject * {
	if( !m_hasSetTeamListModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_teamListModel, QQmlEngine::CppOwnership );
		m_hasSetTeamListModelOwnership = true;
	}
	return &m_teamListModel;
}

auto HudDataModel::getFragsFeedModel() -> QObject * {
	if( !m_hasSetFragsFeedModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_fragsFeedModel, QQmlEngine::CppOwnership );
		m_hasSetFragsFeedModelOwnership = true;
	}
	return &m_fragsFeedModel;
}

auto HudDataModel::getMessageFeedModel() -> QObject * {
	if( !m_hasSetMessageFeedModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_messageFeedModel, QQmlEngine::CppOwnership );
		m_hasSetMessageFeedModelOwnership = true;
	}
	return &m_messageFeedModel;
}

auto HudDataModel::getAwardsModel() -> QObject * {
	if( !m_hasSetAwardsModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_awardsModel, QQmlEngine::CppOwnership );
		m_hasSetAwardsModelOwnership = true;
	}
	return &m_awardsModel;
}

void HudDataModel::addStatusMessage( const wsw::StringView &message, int64_t timestamp ) {
	const wsw::StringView truncatedMessage( message.take( m_originalStatusMessage.capacity() ) );
	// Almost always perform updates, except the single case: a rapid stream of the same textual message.
	// In this case, let initial transitions to complete, otherwise the message won't be noticeable at all.
	if( m_originalStatusMessage.equals( truncatedMessage ) ) {
		if( m_lastStatusMessageTimestamp + 500 > timestamp ) {
			return;
		}
	}

	m_lastStatusMessageTimestamp = timestamp;
	m_originalStatusMessage.assign( truncatedMessage );
	m_formattedStatusMessage = toStyledText( truncatedMessage );
	Q_EMIT statusMessageChanged( m_formattedStatusMessage );
}

void HudDataModel::addFragEvent( const std::pair<wsw::StringView, int> &victimAndTeam,
								 int64_t timestamp, unsigned int meansOfDeath,
								 const std::optional<std::pair<wsw::StringView, int>> &attackerAndTeam ) {
	m_fragsFeedModel.addFrag( victimAndTeam, timestamp, meansOfDeath, attackerAndTeam );
}

static const wsw::StringView kDefaultHudName( "default"_asView );

void HudDataModel::checkHudVarChanges( cvar_t *var, InGameHudLayoutModel *model, HudNameString *currName ) {
	// TODO: We try avoiding using this flag since it assumes a control from a single place in code
	if( var->modified ) {
		wsw::StringView name( var->string );
		// Protect from redundant load() calls
		if( !name.equalsIgnoreCase( currName->asView() ) ) {
			if( name.length() > HudLayoutModel::kMaxHudNameLength ) {
				Cvar_ForceSet( var->name, kDefaultHudName.data() );
				name = kDefaultHudName;
			}
			if( !model->load( name ) ) {
				if( !name.equalsIgnoreCase( kDefaultHudName ) ) {
					Cvar_ForceSet( var->name, kDefaultHudName.data() );
					name = kDefaultHudName;
					// This could fail as well but we assume that the data of the default HUD is not corrupt.
					// A HUD won't be displayed in case of a failure.
					(void)model->load( kDefaultHudName );
				}
			}
			currName->assign( name );
		}
		var->modified = false;
	}
}

void HudDataModel::checkPropertyChanges( int64_t currTime ) {
	checkHudVarChanges( m_clientHudVar, &m_clientLayoutModel, &m_clientHudName );
	checkHudVarChanges( m_specHudVar, &m_specLayoutModel, &m_specHudName );
	const auto *const oldLayoutModel = m_activeLayoutModel;
	m_activeLayoutModel = ( CG_ActiveChasePov() != std::nullopt ) ? &m_clientLayoutModel : &m_specLayoutModel;
	if( oldLayoutModel != m_activeLayoutModel ) {
		Q_EMIT activeLayoutModelChanged( m_activeLayoutModel );
	}

	if( const bool hadTwoTeams = m_hasTwoTeams; hadTwoTeams != ( m_hasTwoTeams = CG_HasTwoTeams() ) ) {
		Q_EMIT hasTwoTeamsChanged( m_hasTwoTeams );
	}

	if( const bool wasSpectator = m_isSpectator; wasSpectator != ( m_isSpectator = CG_IsSpectator() ) ) {
		Q_EMIT isSpectatorChanged( m_isSpectator );
	}

	const bool hadActivePov = m_hasActivePov, wasPovAlive = m_isPovAlive;
	m_hasActivePov = CG_ActiveChasePov() != std::nullopt;
	m_isPovAlive = m_hasActivePov && CG_IsPovAlive();
	if( wasPovAlive != m_isPovAlive ) {
		Q_EMIT isPovAliveChanged( m_isPovAlive );
	}
	if( hadActivePov != m_hasActivePov ) {
		Q_EMIT hasActivePovChanged( m_hasActivePov );
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
		Q_EMIT alphaNameChanged( m_styledAlphaName );
	}

	const wsw::StringView betaName( ::cl.configStrings.getTeamBetaName().value_or( wsw::StringView() ) );
	if( !m_betaName.equals( betaName ) ) {
		m_betaName.assign( betaName );
		setStyledTeamName( &m_styledBetaName, betaName );
		Q_EMIT betaNameChanged( m_styledBetaName );
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

	if( const auto oldProgress = m_alphaProgress; oldProgress != ( m_alphaProgress = CG_TeamAlphaProgress() ) ) {
		Q_EMIT alphaProgressChanged( m_alphaProgress );
	}

	if( const auto oldProgress = m_betaProgress; oldProgress != ( m_betaProgress = CG_TeamBetaProgress() ) ) {
		Q_EMIT betaProgressChanged( m_betaProgress );
	}

	if( const auto oldColor = m_rawAlphaColor; oldColor != ( m_rawAlphaColor = CG_TeamAlphaColor() ) ) {
		m_alphaColor = toQColor( m_rawAlphaColor );
		m_alphaProgressColor = toProgressColor( m_alphaColor );
		Q_EMIT alphaColorChanged( m_alphaColor );
		Q_EMIT alphaProgressColorChanged( m_alphaProgressColor );
	}

	if( const auto oldColor = m_rawBetaColor; oldColor != ( m_rawBetaColor = CG_TeamBetaColor() ) ) {
		m_betaColor = toQColor( m_rawBetaColor );
		m_betaProgressColor = toProgressColor( m_betaColor );
		Q_EMIT betaColorChanged( m_betaColor );
		Q_EMIT betaProgressColorChanged( m_betaProgressColor );
	}

	const auto [minutes, seconds] = CG_GetMatchClockTime();
	if( minutes != m_matchTimeMinutes ) {
		assert( minutes >= 0 );
		m_matchTimeMinutes = minutes;
		setFormattedTime( &m_formattedMinutes, minutes );
		Q_EMIT matchTimeMinutesChanged( m_formattedMinutes );
	}
	if( seconds != m_matchTimeSeconds ) {
		assert( (unsigned)seconds < 60u );
		m_matchTimeSeconds = seconds;
		setFormattedTime( &m_formattedSeconds, seconds );
		Q_EMIT matchTimeSecondsChanged( m_formattedSeconds );
	}

	const QByteArray *displayedMatchState = &kNoMatchState;
	const int rawMatchState = GS_MatchState();
	if( rawMatchState == MATCH_STATE_WARMUP ) {
		displayedMatchState = &kWarmup;
	} else if( rawMatchState == MATCH_STATE_COUNTDOWN ) {
		displayedMatchState = &kCountdown;
	} else if( GS_MatchExtended() ) {
		displayedMatchState = &kOvertime;
	}
	if( m_displayedMatchState.compare( *displayedMatchState ) != 0 ) {
		m_displayedMatchState = *displayedMatchState;
		Q_EMIT matchStateChanged( m_displayedMatchState );
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
	m_fragsFeedModel.update( currTime );
	m_awardsModel.update( currTime );

	const bool wasMessageFeedFadingOut = m_messageFeedModel.isFadingOut();
	m_messageFeedModel.update( currTime );
	const bool isMessageFeedFadingOut = m_messageFeedModel.isFadingOut();
	if( wasMessageFeedFadingOut != isMessageFeedFadingOut ) {
		Q_EMIT isMessageFeedFadingOutChanged( isMessageFeedFadingOut );
	}
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