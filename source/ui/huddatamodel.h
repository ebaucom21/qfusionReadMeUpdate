#ifndef WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H
#define WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H

#include <QAbstractListModel>
#include <QColor>

#include <array>

#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstaticstring.h"

struct ReplicatedScoreboardData;

namespace wsw::ui {

class HudDataModel;

class InventoryModel : public QAbstractListModel {
	friend class HudDataModel;

	enum Role {
		HasWeapon = Qt::UserRole + 1,
		Active,
		IconPath,
		Color,
		WeakAmmoCount,
		StrongAmmoCount
	};

	struct Entry {
		int weaponNum, weakCount, strongCount;
		bool hasWeapon;
	};

	wsw::StaticVector<Entry, 10> m_entries;
	int m_activeWeaponNum { 0 };

	void checkPropertyChanges();

	static inline const QVector<int> kWeakAmmoRoleAsVector { WeakAmmoCount };
	static inline const QVector<int> kStrongAmmoRoleAsVector { StrongAmmoCount };
	static inline const QVector<int> kActiveAsRole { Active };
	static inline const QVector<int> kAllMutableRolesAsVector { HasWeapon, Active, WeakAmmoCount, StrongAmmoCount };

	void resetWithEntries( const wsw::StaticVector<Entry, 10> &entries );

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;
};

class TeamListModel : public QAbstractListModel {
	friend class HudDataModel;

	enum Role {
		Health,
		Armor,
		WeaponIconPath,
		Nickname,
		Location,
		Powerups
	};

	struct Entry {
		unsigned playerNum;
		unsigned health { 0 }, armor { 0 };
		unsigned weapon { 0 };
		unsigned location { 0 };
		unsigned powerups { 0 };
	};

	using EntriesVector = wsw::StaticVector<Entry, 32>;

	EntriesVector m_oldEntries;
	EntriesVector m_entries;

	static inline const QVector<int> kHealthAsRole { Health };
	static inline const QVector<int> kArmorAsRole { Armor };
	static inline const QVector<int> kHealthAndArmorAsRole { Armor };
	static inline const QVector<int> kWeaponIconPathAsRole { WeaponIconPath };
	static inline const QVector<int> kLocationAsRole { Location };

	unsigned m_povPlayerNum { ~0u };
	int m_team { -1 };

	std::array<unsigned, MAX_CLIENTS> m_nicknameUpdateCounters;

	TeamListModel() {
		m_nicknameUpdateCounters.fill( 0 );
	}

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;

	void fillEntries( const ReplicatedScoreboardData &scoreboardData, EntriesVector &entries );
	void resetWithScoreboardData( const ReplicatedScoreboardData &scoreboardData );

	void update( const ReplicatedScoreboardData &scoreboardData, unsigned povPlayerNum );
};

class HudDataModel : public QObject {
	Q_OBJECT

	InventoryModel m_inventoryModel;
	TeamListModel m_teamListModel;

	wsw::StaticString<32> m_alphaName;
	wsw::StaticString<32> m_betaName;
	QByteArray m_styledAlphaName;
	QByteArray m_styledBetaName;

	QByteArray m_alphaPlayersStatus, m_betaPlayersStatus;
	int m_numAliveAlphaPlayers { 0 }, m_numAliveBetaPlayers { 0 };
	int m_pendingNumAliveAlphaPlayers { 0 }, m_pendingNumAliveBetaPlayers { 0 };
	wsw::StaticString<32> m_alphaTeamStatus, m_betaTeamStatus;
	QByteArray m_styledAlphaTeamStatus, m_styledBetaTeamStatus;

	int m_alphaColor { 0 }, m_betaColor { 0 };
	int m_alphaScore { 0 }, m_pendingAlphaScore { 0 };
	int m_betaScore { 0 }, m_pendingBetaScore { 0 };
	bool m_hasTwoTeams { false };
	bool m_isSpectator { true };

	QByteArray m_formattedSeconds;
	QByteArray m_formattedMinutes;
	QByteArray m_formattedMatchState;
	int m_matchTimeSeconds { 0 };
	int m_matchTimeMinutes { 0 };

	int m_activeWeapon { 0 };
	int m_activeWeaponWeakAmmo { 0 };
	int m_activeWeaponStrongAmmo { 0 };

	int m_health { 0 }, m_armor { 0 };

	bool m_hasLocations { false };

	bool m_hasSetInventoryModelOwnership { false };
	bool m_hasSetTeamListModelOwnership { false };

	[[nodiscard]]
	auto getAlphaName() const -> const QByteArray & { return m_styledAlphaName; }
	[[nodiscard]]
	auto getBetaName() const -> const QByteArray & { return m_styledBetaName; }
	[[nodiscard]]
	auto getAlphaColor() const -> QColor { return toQColor( m_alphaColor ); }
	[[nodiscard]]
	auto getBetaColor() const -> QColor { return toQColor( m_betaColor ); }
	[[nodiscard]]
	auto getAlphaScore() const -> int { return m_alphaScore; }
	[[nodiscard]]
	auto getBetaScore() const -> int { return m_betaScore; }
	[[nodiscard]]
	auto getAlphaPlayersStatus() const -> const QByteArray & { return m_alphaPlayersStatus; }
	[[nodiscard]]
	auto getBetaPlayersStatus() const -> const QByteArray & { return m_betaPlayersStatus; }
	[[nodiscard]]
	auto getAlphaTeamStatus() const -> const QByteArray & { return m_styledAlphaTeamStatus; }
	[[nodiscard]]
	auto getBetaTeamStatus() const -> const QByteArray & { return m_styledBetaTeamStatus; }
	[[nodiscard]]
	bool getHasTwoTeams() const { return m_hasTwoTeams; }
	[[nodiscard]]
	bool getIsSpectator() const { return m_isSpectator; }

	[[nodiscard]]
	static auto toQColor( int color ) -> QColor {
		return QColor::fromRgb( COLOR_R( color ), COLOR_G( color ), COLOR_B( color ) );
	}

	[[nodiscard]]
	auto getMatchTimeSeconds() const -> QByteArray { return m_formattedSeconds; }
	[[nodiscard]]
	auto getMatchTimeMinutes() const -> QByteArray { return m_formattedMinutes; }
	[[nodiscard]]
	auto getMatchState() const -> QByteArray { return m_formattedMatchState; }

	static void setFormattedTime( QByteArray *dest, int value );
	static void setStyledTeamName( QByteArray *dest, const wsw::StringView &name );

	[[nodiscard]]
	auto getActiveWeaponIcon() const -> QByteArray;
	[[nodiscard]]
	auto getActiveWeaponName() const -> QByteArray;

	[[nodiscard]]
	auto getActiveWeaponStrongAmmo() const -> int { return m_activeWeaponStrongAmmo; }
	[[nodiscard]]
	auto getActiveWeaponWeakAmmo() const -> int { return m_activeWeaponWeakAmmo; }

	[[nodiscard]]
	auto getHealth() const -> int { return m_health; }
	[[nodiscard]]
	auto getArmor() const -> int { return m_armor; }

	[[nodiscard]]
	bool getHasLocations() const { return m_hasLocations; }

	[[nodiscard]]
	auto getStatusForNumberOfPlayers( int numPlayers ) const -> QByteArray;
	void updateTeamPlayerStatuses( const ReplicatedScoreboardData &scoreboardData );
public:
	Q_SIGNAL void alphaNameChanged( const QByteArray &alphaName );
	Q_PROPERTY( const QByteArray alphaName READ getAlphaName NOTIFY alphaNameChanged );
	Q_SIGNAL void betaNameChanged( const QByteArray &betaName );
	Q_PROPERTY( const QByteArray betaName READ getBetaName NOTIFY betaNameChanged );
	Q_SIGNAL void alphaColorChanged( const QColor &alphaColor );
	Q_PROPERTY( QColor alphaColor READ getAlphaColor NOTIFY alphaColorChanged );
	Q_SIGNAL void betaColorChanged( const QColor &betaColor );
	Q_PROPERTY( QColor betaColor READ getBetaColor NOTIFY betaColorChanged );
	Q_SIGNAL void alphaScoreChanged( int alphaScore );
	Q_PROPERTY( int alphaScore READ getAlphaScore NOTIFY alphaScoreChanged );
	Q_SIGNAL void betaScoreChanged( int betaScore );
	Q_PROPERTY( int betaScore READ getBetaScore NOTIFY betaScoreChanged );
	Q_SIGNAL void alphaPlayersStatusChanged( const QByteArray &alphaPlayersStatus );
	Q_PROPERTY( const QByteArray alphaPlayersStatus READ getAlphaPlayersStatus NOTIFY alphaPlayersStatusChanged );
	Q_SIGNAL void betaPlayersStatusChanged( const QByteArray &betaPlayersStatus );
	Q_PROPERTY( const QByteArray betaPlayersStatus READ getBetaPlayersStatus NOTIFY betaPlayersStatusChanged );
	Q_SIGNAL void alphaTeamStatusChanged( const QByteArray &alphaTeamStatus );
	Q_PROPERTY( const QByteArray alphaTeamStatus READ getAlphaTeamStatus NOTIFY alphaTeamStatusChanged );
	Q_SIGNAL void betaTeamStatusChanged( const QByteArray &betaTeamStatus );
	Q_PROPERTY( const QByteArray betaTeamStatus READ getBetaTeamStatus NOTIFY betaTeamStatusChanged );

	Q_SIGNAL void hasTwoTeamsChanged( bool hasTwoTeams );
	Q_PROPERTY( bool hasTwoTeams READ getHasTwoTeams NOTIFY hasTwoTeamsChanged );
	Q_SIGNAL void isSpectatorChanged( bool isSpectator );
	Q_PROPERTY( bool isSpectator READ getIsSpectator NOTIFY isSpectatorChanged );

	Q_SIGNAL void matchTimeSecondsChanged( const QByteArray &seconds );
	Q_PROPERTY( QByteArray matchTimeSeconds READ getMatchTimeSeconds NOTIFY matchTimeSecondsChanged );
	Q_SIGNAL void matchTimeMinutesChanged( const QByteArray &minutes );
	Q_PROPERTY( QByteArray matchTimeMinutes READ getMatchTimeMinutes NOTIFY matchTimeMinutesChanged );

	Q_SIGNAL void activeWeaponIconChanged( const QByteArray &activeWeaponIcon );
	Q_PROPERTY( QByteArray activeWeaponIcon READ getActiveWeaponIcon NOTIFY activeWeaponIconChanged );
	Q_SIGNAL void activeWeaponNameChanged( const QByteArray &activeWeaponName );
	Q_PROPERTY( QByteArray activeWeaponName READ getActiveWeaponName NOTIFY activeWeaponNameChanged );
	Q_SIGNAL int activeWeaponWeakAmmoChanged( int activeWeaponWeaponAmmo );
	Q_PROPERTY( int activeWeaponWeakAmmo READ getActiveWeaponWeakAmmo NOTIFY activeWeaponWeakAmmoChanged );
	Q_SIGNAL int activeWeaponStrongAmmoChanged( int activeWeaponStrongAmmo );
	Q_PROPERTY( int activeWeaponStrongAmmo READ getActiveWeaponStrongAmmo NOTIFY activeWeaponStrongAmmoChanged );

	Q_SIGNAL void healthChanged( int health );
	Q_PROPERTY( int health READ getHealth NOTIFY healthChanged );
	Q_SIGNAL void armorChanged( int armor );
	Q_PROPERTY( int armor READ getArmor NOTIFY armorChanged );

	Q_SIGNAL void hasLocationsChanged( bool hasLocations );
	Q_PROPERTY( bool hasLocations READ getHasLocations NOTIFY hasLocationsChanged );

	enum Powerup {
		Quad  = 0x1,
		Shell = 0x2,
		Regen = 0x4
	};
	Q_ENUM( Powerup );

	[[nodiscard]]
	Q_INVOKABLE QAbstractListModel *getInventoryModel();
	[[nodiscard]]
	Q_INVOKABLE QAbstractListModel *getTeamListModel();

	HudDataModel();

	void checkPropertyChanges();
	void updateScoreboardData( const ReplicatedScoreboardData &scoreboardData );
};

}

#endif