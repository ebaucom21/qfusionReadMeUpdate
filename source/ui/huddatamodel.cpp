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
		assert( weapon && (unsigned)weapon < (unsigned)WEAP_TOTAL );
		return m_weaponIconPaths[weapon - 1];
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

	m_inventoryModel.checkPropertyChanges();
}

void HudDataModel::updateScoreboardData( const ReplicatedScoreboardData &scoreboardData ) {
	m_pendingAlphaScore = scoreboardData.alphaScore;
	m_pendingBetaScore = scoreboardData.betaScore;
}

}