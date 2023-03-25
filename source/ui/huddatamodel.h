#ifndef WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H
#define WSW_113c126b_3afd_470e_8b61_492a157d3d3d_H

#include <QAbstractListModel>
#include <QColor>

#include <array>

#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstaticstring.h"
#include "hudlayoutmodel.h"

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

class FragsFeedModel : public QAbstractListModel {
	friend class HudDataModel;

	enum Role { Victim = Qt::UserRole + 1, Attacker, IconPath };

	struct Entry {
		int64_t timestamp;
		wsw::StaticString<32> victimName, attackerName;
		unsigned meansOfDeath { 0 };
		std::optional<int> victimTeamColor, attackerTeamColor;
	};

	HudDataModel *const m_hudDataModel;

	explicit FragsFeedModel( HudDataModel *hudDataModel ) : m_hudDataModel( hudDataModel ) {}

	// TODO: Use a circular buffer / StaticDeque (check whether it's really functional)?
	wsw::StaticVector<Entry, 4> m_entries;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;

	[[nodiscard]]
	static auto toDisplayedName( const wsw::StringView &rawName, const std::optional<int> &teamColor ) -> QString;

	void addFrag( const std::pair<wsw::StringView, int> &victimAndTeam,
				  int64_t timestamp, unsigned meansOfDeath,
				  const std::optional<std::pair<wsw::StringView, int>> &attackerAndTeam );

	void reset();
	void update( int64_t currTime );
};

class MessageFeedModel : public QAbstractListModel {
	friend class HudDataModel;

	enum Role { Message = Qt::UserRole + 1 };

	struct Entry {
		int64_t timestamp;
		wsw::StaticString<1024> message;
	};

	static constexpr unsigned kMaxEntries = 4;

	Entry m_pendingEntries[kMaxEntries];
	unsigned m_numPendingEntries { 0 };
	unsigned m_pendingEntriesHead { 0 };
	unsigned m_pendingEntriesTail { 0 };

	bool m_isFadingOut { false };

	wsw::StaticVector<Entry, kMaxEntries> m_entries;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;

	void addMessage( const wsw::StringView &message, int64_t timestamp );

	void update( int64_t currTime );

	[[nodiscard]]
	bool isFadingOut() const { return m_isFadingOut; }
};

class AwardsModel : public QAbstractListModel {
	friend class HudDataModel;

	// TODO: Add icons?
	enum Role { Message = Qt::UserRole + 1 };

	struct Entry {
		int64_t timestamp;
		wsw::StaticString<64> message;
	};

	wsw::StaticVector<Entry, 4> m_entries;
	wsw::Vector<Entry> m_pendingEntries;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &modelIndex, int role ) const -> QVariant override;

	void addAward( const wsw::StringView &award, int64_t timestamp );

	void update( int64_t currTime );
};

struct ObjectiveIndicatorState {
	Q_GADGET

public:
	QColor color;
	int anim { 0 };
	int progress { 0 };
	int iconNum { 0 };
	bool enabled { false };

	Q_PROPERTY( QColor color MEMBER color );
	Q_PROPERTY( int anim MEMBER anim );
	Q_PROPERTY( int progress MEMBER progress );
	Q_PROPERTY( int iconNum MEMBER iconNum );
	Q_PROPERTY( bool enabled MEMBER enabled );

	[[nodiscard]]
	bool operator!=( const ObjectiveIndicatorState &that ) const {
		return color == that.color && anim == that.anim && progress == that.progress &&
			iconNum == that.iconNum && enabled == that.enabled;
	}
};

class HudDataModel : public QObject {
	Q_OBJECT

	friend class FragsFeedModel;

	InventoryModel m_inventoryModel;
	TeamListModel m_teamListModel;
	FragsFeedModel m_fragsFeedModel { this };
	MessageFeedModel m_messageFeedModel;
	AwardsModel m_awardsModel;

	InGameHudLayoutModel m_clientLayoutModel;
	InGameHudLayoutModel m_specLayoutModel;
	QAbstractItemModel *m_activeLayoutModel { nullptr };
	cvar_t *m_clientHudVar { nullptr };
	cvar_t *m_specHudVar { nullptr };

	using HudNameString = wsw::StaticString<HudLayoutModel::kMaxHudNameLength>;
	HudNameString m_clientHudName, m_specHudName;

	int64_t m_lastStatusMessageTimestamp { 0 };
	wsw::StaticString<96> m_originalStatusMessage;
	// TODO make toStyledText() work with arbitrary types
	QString m_formattedStatusMessage;

	wsw::StaticString<32> m_alphaName;
	wsw::StaticString<32> m_betaName;
	QByteArray m_styledAlphaName;
	QByteArray m_styledBetaName;

	QByteArray m_styledAlphaClan;
	QByteArray m_styledBetaClan;

	QByteArray m_alphaPlayersStatus, m_betaPlayersStatus;
	int m_numAliveAlphaPlayers { 0 }, m_numAliveBetaPlayers { 0 };
	int m_pendingNumAliveAlphaPlayers { 0 }, m_pendingNumAliveBetaPlayers { 0 };
	unsigned m_lastIndividualAlphaClanCounter { 0 }, m_lastIndividualBetaClanCounter { 0 };
	std::optional<int> m_pendingIndividualAlphaPlayerNum, m_pendingIndividualBetaPlayerNum;

	ObjectiveIndicatorState m_indicatorStates[3];

	int m_rawAlphaColor { 0 }, m_rawBetaColor { 0 };
	QColor m_alphaColor { toQColor( m_rawAlphaColor ) };
	QColor m_betaColor { toQColor( m_rawBetaColor ) };
	int m_alphaScore { 0 }, m_pendingAlphaScore { 0 };
	int m_betaScore { 0 }, m_pendingBetaScore { 0 };
	bool m_hasTwoTeams { false };
	bool m_isSpectator { true };
	bool m_hasActivePov { false };
	bool m_isPovAlive { false };

	QByteArray m_formattedSeconds;
	QByteArray m_formattedMinutes;
	QByteArray m_matchStateString;
	int m_matchTimeSeconds { 0 };
	int m_matchTimeMinutes { 0 };

	int m_activeWeapon { 0 };
	int m_activeWeaponWeakAmmo { 0 };
	int m_activeWeaponStrongAmmo { 0 };

	int m_health { 0 }, m_armor { 0 };

	bool m_isInPostmatchState { false };

	bool m_hasLocations { false };

	bool m_hasSetInventoryModelOwnership { false };
	bool m_hasSetTeamListModelOwnership { false };
	bool m_hasSetFragsFeedModelOwnership {false };
	bool m_hasSetMessageFeedModelOwnership { false };
	bool m_hasSetAwardsModelOwnership { false };

	[[nodiscard]]
	auto getActiveLayoutModel() -> QObject * { return m_activeLayoutModel; }

	[[nodiscard]]
	static auto toQColor( int color ) -> QColor {
		return QColor::fromRgb( COLOR_R( color ), COLOR_G( color ), COLOR_B( color ) );
	}

	static void setFormattedTime( QByteArray *dest, int value );
	static void setStyledName( QByteArray *dest, const wsw::StringView &name );

	[[nodiscard]]
	auto getActiveWeaponIcon() const -> QByteArray;
	[[nodiscard]]
	auto getActiveWeaponName() const -> QByteArray;

	[[nodiscard]]
	bool getIsMessageFeedFadingOut() const { return m_messageFeedModel.isFadingOut(); }

	[[nodiscard]]
	auto getStatusForNumberOfPlayers( int numPlayers ) const -> QByteArray;
	void updateTeamPlayerStatuses( const ReplicatedScoreboardData &scoreboardData );

	void checkHudVarChanges( cvar_t *var, InGameHudLayoutModel *model, HudNameString *currName );
public:
	Q_SIGNAL void activeLayoutModelChanged( QObject *activeLayoutModel );
	Q_PROPERTY( QObject *activeLayoutModel READ getActiveLayoutModel NOTIFY activeLayoutModelChanged );

	Q_SIGNAL void alphaNameChanged( const QByteArray &alphaName );
	Q_PROPERTY( const QByteArray alphaName MEMBER m_styledAlphaName NOTIFY alphaNameChanged );
	Q_SIGNAL void betaNameChanged( const QByteArray &betaName );
	Q_PROPERTY( const QByteArray betaName MEMBER m_styledBetaName NOTIFY betaNameChanged );
	Q_SIGNAL void alphaClanChanged( const QByteArray &alphaClan );
	Q_PROPERTY( const QByteArray alphaClan MEMBER m_styledAlphaClan NOTIFY alphaClanChanged );
	Q_SIGNAL void betaClanChanged( const QByteArray &betaClan );
	Q_PROPERTY( const QByteArray betaClan MEMBER m_styledBetaClan NOTIFY betaClanChanged );
	Q_SIGNAL void alphaColorChanged( const QColor &alphaColor );
	Q_PROPERTY( QColor alphaColor MEMBER m_alphaColor NOTIFY alphaColorChanged );
	Q_SIGNAL void betaColorChanged( const QColor &betaColor );
	Q_PROPERTY( QColor betaColor MEMBER m_betaColor NOTIFY betaColorChanged );
	Q_SIGNAL void alphaScoreChanged( int alphaScore );
	Q_PROPERTY( int alphaScore MEMBER m_alphaScore NOTIFY alphaScoreChanged );
	Q_SIGNAL void betaScoreChanged( int betaScore );
	Q_PROPERTY( int betaScore MEMBER m_betaScore NOTIFY betaScoreChanged );
	Q_SIGNAL void alphaPlayersStatusChanged( const QByteArray &alphaPlayersStatus );
	Q_PROPERTY( const QByteArray alphaPlayersStatus MEMBER m_alphaPlayersStatus NOTIFY alphaPlayersStatusChanged );
	Q_SIGNAL void betaPlayersStatusChanged( const QByteArray &betaPlayersStatus );
	Q_PROPERTY( const QByteArray betaPlayersStatus MEMBER m_betaPlayersStatus NOTIFY betaPlayersStatusChanged );

	Q_SIGNAL void indicator1StateChanged( const QVariant &indicator1State );
	Q_PROPERTY( QVariant indicator1State READ getIndicator1State NOTIFY indicator1StateChanged );
	Q_SIGNAL void indicator2StateChanged( const QVariant &indicator2State );
	Q_PROPERTY( QVariant indicator2State READ getIndicator2State NOTIFY indicator2StateChanged );
	Q_SIGNAL void indicator3StateChanged( const QVariant &indicator3State );
	Q_PROPERTY( QVariant indicator3State READ getIndicator3State NOTIFY indicator3StateChanged );

	Q_SIGNAL void hasTwoTeamsChanged( bool hasTwoTeams );
	Q_PROPERTY( bool hasTwoTeams MEMBER m_hasTwoTeams NOTIFY hasTwoTeamsChanged );
	Q_SIGNAL void isSpectatorChanged( bool isSpectator );
	Q_PROPERTY( bool isSpectator MEMBER m_isSpectator NOTIFY isSpectatorChanged );

	Q_SIGNAL void hasActivePovChanged( bool hasActivePov );
	Q_PROPERTY( bool hasActivePov MEMBER m_hasActivePov NOTIFY hasActivePovChanged );
	Q_SIGNAL void isPovAliveChanged( bool isPovAlive );
	Q_PROPERTY( bool isPovAlive MEMBER m_isPovAlive NOTIFY isPovAliveChanged );

	Q_SIGNAL void matchTimeSecondsChanged( const QByteArray &seconds );
	Q_PROPERTY( const QByteArray matchTimeSeconds MEMBER m_formattedSeconds NOTIFY matchTimeSecondsChanged );
	Q_SIGNAL void matchTimeMinutesChanged( const QByteArray &minutes );
	Q_PROPERTY( const QByteArray matchTimeMinutes MEMBER m_formattedMinutes NOTIFY matchTimeMinutesChanged );
	Q_SIGNAL void matchStateStringChanged( const QByteArray &matchStateString );
	Q_PROPERTY( const QByteArray matchStateString MEMBER m_matchStateString NOTIFY matchStateStringChanged );

	Q_SIGNAL void isInPostmatchStateChanged( bool isInPostmatchState );
	Q_PROPERTY( bool isInPostmatchState MEMBER m_isInPostmatchState NOTIFY isInPostmatchStateChanged );

	Q_SIGNAL void activeWeaponIconChanged( const QByteArray &activeWeaponIcon );
	Q_PROPERTY( QByteArray activeWeaponIcon READ getActiveWeaponIcon NOTIFY activeWeaponIconChanged );
	Q_SIGNAL void activeWeaponNameChanged( const QByteArray &activeWeaponName );
	Q_PROPERTY( QByteArray activeWeaponName READ getActiveWeaponName NOTIFY activeWeaponNameChanged );
	Q_SIGNAL int activeWeaponWeakAmmoChanged( int activeWeaponWeaponAmmo );
	Q_PROPERTY( int activeWeaponWeakAmmo MEMBER m_activeWeaponWeakAmmo NOTIFY activeWeaponWeakAmmoChanged );
	Q_SIGNAL int activeWeaponStrongAmmoChanged( int activeWeaponStrongAmmo );
	Q_PROPERTY( int activeWeaponStrongAmmo MEMBER m_activeWeaponStrongAmmo NOTIFY activeWeaponStrongAmmoChanged );

	Q_SIGNAL void healthChanged( int health );
	Q_PROPERTY( int health MEMBER m_health NOTIFY healthChanged );
	Q_SIGNAL void armorChanged( int armor );
	Q_PROPERTY( int armor MEMBER m_armor NOTIFY armorChanged );

	Q_SIGNAL void hasLocationsChanged( bool hasLocations );
	Q_PROPERTY( bool hasLocations MEMBER m_hasLocations NOTIFY hasLocationsChanged );

	Q_SIGNAL void isMessageFeedFadingOutChanged( bool isMessageFeedFadingOut );
	Q_PROPERTY( bool isMessageFeedFadingOut READ getIsMessageFeedFadingOut NOTIFY isMessageFeedFadingOutChanged );

	Q_SIGNAL void statusMessageChanged( const QString &statusMessage );
	Q_PROPERTY( QString statusMessage MEMBER m_formattedStatusMessage NOTIFY statusMessageChanged );

	enum Powerup { Quad  = 0x1, Shell = 0x2, Regen = 0x4 };
	Q_ENUM( Powerup );

	[[nodiscard]]
	Q_INVOKABLE QObject *getInventoryModel();
	[[nodiscard]]
	Q_INVOKABLE QObject *getTeamListModel();
	[[nodiscard]]
	Q_INVOKABLE QObject *getFragsFeedModel();
	[[nodiscard]]
	Q_INVOKABLE QObject *getMessageFeedModel();
	[[nodiscard]]
	Q_INVOKABLE QObject *getAwardsModel();

	[[nodiscard]]
	Q_INVOKABLE QByteArray getWeaponFullName( int weapon ) const;
	[[nodiscard]]
	Q_INVOKABLE QByteArray getWeaponShortName( int weapon ) const;
	[[nodiscard]]
	Q_INVOKABLE QByteArray getWeaponIconPath( int weapon ) const;
	[[nodiscard]]
	Q_INVOKABLE QByteArray getWeaponModelPath( int weapon ) const;
	[[nodiscard]]
	Q_INVOKABLE QColor getWeaponColor( int weapon ) const;

	[[nodiscard]]
	Q_INVOKABLE QStringList getAvailableCrosshairs() const;
	[[nodiscard]]
	Q_INVOKABLE QStringList getAvailableStrongCrosshairs() const;

	[[nodiscard]]
	Q_INVOKABLE QVariant getIndicator1State() { return QVariant::fromValue( m_indicatorStates[0] ); }
	[[nodiscard]]
	Q_INVOKABLE QVariant getIndicator2State() { return QVariant::fromValue( m_indicatorStates[1] ); }
	[[nodiscard]]
	Q_INVOKABLE QVariant getIndicator3State() { return QVariant::fromValue( m_indicatorStates[2] ); }

	enum IndicatorAnim { NoAnim, AlertAnim, ActionAnim };
	Q_ENUM( IndicatorAnim );

	[[nodiscard]]
	Q_INVOKABLE QByteArray getIndicatorIconPath( int iconNum ) const;

	HudDataModel();

	Q_SLOT void onHudUpdated( const QByteArray &name );

	void resetFragsFeed() {
		m_fragsFeedModel.reset();
	}

	void addFragEvent( const std::pair<wsw::StringView, int> &victimAndTeam,
					   int64_t timestamp, unsigned meansOfDeath,
					   const std::optional<std::pair<wsw::StringView, int>> &attackerAndTeam );

	void addToMessageFeed( const wsw::StringView &message, int64_t timestamp ) {
		m_messageFeedModel.addMessage( message, timestamp );
	}

	void addAward( const wsw::StringView &award, int64_t timestamp ) {
		m_awardsModel.addAward( award, timestamp );
	}

	void addStatusMessage( const wsw::StringView &message, int64_t timestamp );

	void checkPropertyChanges( int64_t currTime );
	void updateScoreboardData( const ReplicatedScoreboardData &scoreboardData );
};

}

Q_DECLARE_METATYPE( wsw::ui::ObjectiveIndicatorState )

#endif