#ifndef WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H
#define WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H

#include <QAbstractListModel>
#include <QColor>

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

class HudDataModel : public QObject {
	Q_OBJECT

	InventoryModel m_inventoryModel;

	wsw::StaticString<32> m_alphaName;
	wsw::StaticString<32> m_betaName;
	QByteArray m_styledAlphaName;
	QByteArray m_styledBetaName;

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

	bool m_hasSetInventoryModelOwnership { false };

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
public:
	Q_SIGNAL void alphaNameChanged( const QByteArray &alphaName );
	Q_PROPERTY( QByteArray alphaName READ getAlphaName NOTIFY alphaNameChanged );
	Q_SIGNAL void betaNameChanged( const QByteArray &betaName );
	Q_PROPERTY( QByteArray betaName READ getBetaName NOTIFY betaNameChanged );
	Q_SIGNAL void alphaColorChanged( const QColor &alphaColor );
	Q_PROPERTY( QColor alphaColor READ getAlphaColor NOTIFY alphaColorChanged );
	Q_SIGNAL void betaColorChanged( const QColor &betaColor );
	Q_PROPERTY( QColor betaColor READ getBetaColor NOTIFY betaColorChanged );
	Q_SIGNAL void alphaScoreChanged( int alphaScore );
	Q_PROPERTY( int alphaScore READ getAlphaScore NOTIFY alphaScoreChanged );
	Q_SIGNAL void betaScoreChanged( int betaScore );
	Q_PROPERTY( int betaScore READ getBetaScore NOTIFY betaScoreChanged );
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

	[[nodiscard]]
	Q_INVOKABLE QAbstractListModel *getInventoryModel();

	HudDataModel();

	void checkPropertyChanges();
	void updateScoreboardData( const ReplicatedScoreboardData &scoreboardData );
};

}

#endif