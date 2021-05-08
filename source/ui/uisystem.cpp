#include "uisystem.h"
#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "actionrequestmodel.h"
#include "callvotesmodel.h"
#include "chatmodel.h"
#include "demos.h"
#include "gametypeoptionsmodel.h"
#include "gametypesmodel.h"
#include "hudlayoutmodel.h"
#include "huddatamodel.h"
#include "nativelydrawnitems.h"
#include "playersmodel.h"
#include "serverlistmodel.h"
#include "keysandbindingsmodel.h"
#include "scoreboardmodel.h"
#include "wswimageprovider.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>
#include <QPointer>
#include <QScopedPointer>
#include <QFontDatabase>
#include <QQmlProperty>

#include <clocale>

QVariant VID_GetMainContextHandle();

bool GLimp_BeginUIRenderingHacks();
bool GLimp_EndUIRenderingHacks();

void R_Set2DMode( bool );
void R_DrawExternalTextureOverlay( unsigned );
shader_t *R_RegisterPic( const char * );
struct model_s *R_RegisterModel( const char * );
void RF_RegisterWorldModel( const char * );
void RF_ClearScene();
void RF_RenderScene( const refdef_t * );
void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
						const vec4_t color, const shader_t *shader );

// Hacks
bool CG_IsSpectator();
bool CG_HasTwoTeams();
int CG_MyRealTeam();
std::optional<unsigned> CG_ActiveChasePov();

namespace wsw::ui {

class QtUISystem : public QObject, public UISystem {
	Q_OBJECT

	// The implementation is borrowed from https://github.com/RSATom/QuickLayer

	template <typename> friend class ::SingletonHolder;
	friend class NativelyDrawnImage;
	friend class NativelyDrawnModel;
public:
	void refresh( unsigned refreshFlags ) override;

	void drawSelfInMainContext() override;

	void beginRegistration() override {};
	void endRegistration() override {};

	[[nodiscard]]
	bool requestsKeyboardFocus() const override;
	[[nodiscard]]
	bool handleKeyEvent( int quakeKey, bool keyDown ) override;
	[[nodiscard]]
	bool handleCharEvent( int ch ) override;
	[[nodiscard]]
	bool handleMouseMove( int frameTime, int dx, int dy ) override;

	void forceMenuOn() override {};
	void forceMenuOff() override {};

	void toggleInGameMenu() override;

	void addToChat( const wsw::StringView &name, const wsw::StringView &message ) override;
	void addToTeamChat( const wsw::StringView &name, const wsw::StringView &message ) override;

	void handleConfigString( unsigned configStringNum, const wsw::StringView &configString ) override;

	void updateScoreboard( const ReplicatedScoreboardData &scoreboardData ) override;

	[[nodiscard]]
	bool isShowingScoreboard() const override;
	void setScoreboardShown( bool shown ) override;

	void toggleChatPopup() override;
	void toggleTeamChatPopup() override;

	void touchActionRequest( const wsw::StringView &tag, unsigned timeout,
						  	 const wsw::StringView &title, const wsw::StringView &actionDesc,
						  	 const std::pair<wsw::StringView, int> *actionsBegin,
						  	 const std::pair<wsw::StringView, int> *actionsEnd ) override;

	void handleOptionsStatusCommand( const wsw::StringView &status ) override;

	void resetObituaries() override;

	void addObituary( const wsw::StringView &victim, unsigned meansOfDeath,
				      const std::optional<wsw::StringView> &maybeAttacker ) override;

	void addToMessageFeed( const wsw::StringView &message ) override;

	void notifyOfFailedConnection( const wsw::StringView &message, ConnectionFailKind kind );

	Q_INVOKABLE void clearFailedConnectionState() { m_clearFailedConnectionState = true; }

	[[nodiscard]]
	bool isShowingChatPopup() const { return m_isShowingChatPopup; }
	[[nodiscard]]
	bool isShowingTeamChatPopup() const { return m_isShowingTeamChatPopup; }
	[[nodiscard]]
	bool hasTeamChat() const { return m_hasTeamChat; }
	[[nodiscard]]
	bool isShowingHud() const { return m_isShowingHud; }
	[[nodiscard]]
	bool isShowingPovHud() const { return m_isShowingPovHud; }

	[[nodiscard]]
	bool isOperator() const { return m_lastFrameState.isOperator; }

	[[nodiscard]]
	auto getCanJoin() const { return m_canJoin; }
	[[nodiscard]]
	auto getCanJoinAlpha() const { return m_canJoinAlpha; }
	[[nodiscard]]
	auto getCanJoinBeta() const { return m_canJoinBeta; }
	[[nodiscard]]
	auto getCanSpectate() const { return m_canSpectate; }

	[[nodiscard]]
	bool isShown() const override;

	[[nodiscard]]
	auto getFrameTimestamp() const -> int64_t { return ::cls.realtime; }

	void enterUIRenderingMode();
	void leaveUIRenderingMode();

	Q_PROPERTY( bool isShowingMainMenu READ isShowingMainMenu NOTIFY isShowingMainMenuChanged );
	Q_PROPERTY( bool isShowingConnectionScreen READ isShowingConnectionScreen NOTIFY isShowingConnectionScreenChanged );
	Q_PROPERTY( bool isShowingInGameMenu READ isShowingInGameMenu NOTIFY isShowingInGameMenuChanged );
	Q_PROPERTY( bool isShowingDemoPlaybackMenu READ isShowingDemoPlaybackMenu NOTIFY isShowingDemoPlaybackMenuChanged );
	Q_PROPERTY( bool isDebuggingNativelyDrawnItems READ isDebuggingNativelyDrawnItems NOTIFY isDebuggingNativelyDrawnItemsChanged );

	Q_PROPERTY( bool isShowingScoreboard READ isShowingScoreboard NOTIFY isShowingScoreboardChanged );

	Q_PROPERTY( bool isShowingChatPopup READ isShowingChatPopup NOTIFY isShowingChatPopupChanged );
	Q_PROPERTY( bool isShowingTeamChatPopup READ isShowingTeamChatPopup NOTIFY isShowingTeamChatPopupChanged );
	Q_PROPERTY( bool hasTeamChat READ hasTeamChat NOTIFY hasTeamChatChanged );

	Q_SIGNAL void isShowingHudChanged( bool isShowingHud );
	Q_PROPERTY( bool isShowingHud READ isShowingHud NOTIFY isShowingHudChanged );
	Q_SIGNAL void isShowingPovHudChanged( bool isShowingPovHud );
	Q_PROPERTY( bool isShowingPovHud READ isShowingPovHud NOTIFY isShowingPovHudChanged );

	Q_PROPERTY( bool isShowingActionRequests READ isShowingActionRequests NOTIFY isShowingActionRequestsChanged );

	Q_INVOKABLE void registerNativelyDrawnItem( QQuickItem *item );
	Q_INVOKABLE void unregisterNativelyDrawnItem( QQuickItem *item );

	Q_SIGNAL void hudOccludersChanged();
	Q_INVOKABLE void registerHudOccluder( QQuickItem *item );
	Q_INVOKABLE void unregisterHudOccluder( QQuickItem *item );
	Q_INVOKABLE void updateHudOccluder( QQuickItem *item );
	[[nodiscard]]
	Q_INVOKABLE bool isHudItemOccluded( QQuickItem *item );

	Q_INVOKABLE void registerNativelyDrawnItemsOccluder( QQuickItem *item );
	Q_INVOKABLE void unregisterNativelyDrawnItemsOccluder( QQuickItem *item );
	// There's no need to track changes of these occluders
	// as natively drawn items and occluders are checked every frame prior to drawing

	Q_INVOKABLE QVariant getCVarValue( const QString &name ) const;
	Q_INVOKABLE void setCVarValue( const QString &name, const QVariant &value );
	Q_INVOKABLE void markPendingCVarChanges( QQuickItem *control, const QString &name, const QVariant &value );
	Q_INVOKABLE bool hasControlPendingCVarChanges( QQuickItem *control ) const;

	Q_PROPERTY( bool hasPendingCVarChanges READ hasPendingCVarChanges NOTIFY hasPendingCVarChangesChanged );
	Q_INVOKABLE void commitPendingCVarChanges();
	Q_INVOKABLE void rollbackPendingCVarChanges();

	Q_INVOKABLE void registerCVarAwareControl( QQuickItem *control );
	Q_INVOKABLE void unregisterCVarAwareControl( QQuickItem *control );

	Q_INVOKABLE void showMainMenu();
	Q_INVOKABLE void returnFromInGameMenu();
	Q_INVOKABLE void returnFromMainMenu();

	Q_INVOKABLE void closeChatPopup();
	Q_INVOKABLE void sendChatMessage( const QString &text, bool team );

	Q_INVOKABLE void connectToAddress( const QByteArray &address );
	Q_INVOKABLE void reconnectWithPassword( const QByteArray &password );
	Q_INVOKABLE void reconnect();

	enum LocalServerFlags {
		LocalServerInsta  = 0x1,
		LocalServerPublic = 0x2
	};

	Q_ENUM( LocalServerFlags );

	Q_INVOKABLE void launchLocalServer( const QByteArray &gametype, const QByteArray &map, int flags, int numBots );

	Q_INVOKABLE void quit();
	Q_INVOKABLE void disconnect();

	Q_INVOKABLE void spectate();
	Q_INVOKABLE void join();
	Q_INVOKABLE void joinAlpha();
	Q_INVOKABLE void joinBeta();

	Q_SIGNAL void canSpectateChanged( bool canSpectate );
	Q_PROPERTY( bool canSpectate READ getCanSpectate NOTIFY canSpectateChanged );

	Q_SIGNAL void canJoinChanged( bool canJoin );
	Q_PROPERTY( bool canJoin READ getCanJoin NOTIFY canJoinChanged );

	Q_SIGNAL void canJoinAlphaChanged( bool canJoinAlpha );
	Q_PROPERTY( bool canJoinAlpha READ getCanJoinAlpha NOTIFY canJoinAlphaChanged );

	Q_SIGNAL void canJoinBetaChanged( bool canJoinBeta );
	Q_PROPERTY( bool canJoinBeta READ getCanJoinBeta NOTIFY canJoinBetaChanged );

	Q_INVOKABLE QVariant colorFromRgbString( const QString &string ) const;

	Q_INVOKABLE QMatrix4x4 makeSkewXMatrix( qreal height, qreal degrees ) const;

	Q_INVOKABLE QByteArray formatPing( int ping ) const;

	enum ServerListFlags {
		ShowEmptyServers = 0x1,
		ShowFullServers  = 0x2
	};
	Q_ENUM( ServerListFlags );

	Q_INVOKABLE void startServerListUpdates( int flags );
	Q_INVOKABLE void stopServerListUpdates();

	Q_INVOKABLE void callVote( const QByteArray &name, const QByteArray &value, bool isOperatorCall );

	Q_SIGNAL void isOperatorChanged( bool isOperator );
	Q_PROPERTY( bool isOperator READ isOperator NOTIFY isOperatorChanged );

	enum ConnectionFailKind_ {
		NoFail = 0,
		DontReconnect    = (unsigned)UISystem::ConnectionFailKind::DontReconnect,
		TryReconnecting  = (unsigned)UISystem::ConnectionFailKind::TryReconnecting,
		PasswordRequired = (unsigned)UISystem::ConnectionFailKind::PasswordRequired
	};
	Q_ENUM( ConnectionFailKind_ );

	Q_SIGNAL void connectionFailKindChanged( ConnectionFailKind_ kind );
	Q_PROPERTY( ConnectionFailKind_ connectionFailKind READ getConnectionFailKind NOTIFY connectionFailKindChanged );

	Q_SIGNAL void connectionFailMessageChanged( const QByteArray &message );
	Q_PROPERTY( QByteArray connectionFailMessage READ getConnectionFailMessage NOTIFY connectionFailMessageChanged );

	Q_PROPERTY( QColor black MEMBER m_colorBlack CONSTANT );
	Q_PROPERTY( QColor red MEMBER m_colorRed CONSTANT );
	Q_PROPERTY( QColor green MEMBER m_colorGreen CONSTANT );
	Q_PROPERTY( QColor yellow MEMBER m_colorYellow CONSTANT );
	Q_PROPERTY( QColor blue MEMBER m_colorBlue CONSTANT );
	Q_PROPERTY( QColor cyan MEMBER m_colorCyan CONSTANT );
	Q_PROPERTY( QColor magenta MEMBER m_colorMagenta CONSTANT );
	Q_PROPERTY( QColor white MEMBER m_colorWhite CONSTANT );
	Q_PROPERTY( QColor orange MEMBER m_colorOrange CONSTANT );
	Q_PROPERTY( QColor grey MEMBER m_colorGrey CONSTANT );
	Q_PROPERTY( QVariantList consoleColors MEMBER m_consoleColors CONSTANT );
signals:
	Q_SIGNAL void isShowingScoreboardChanged( bool isShowingScoreboard );
	Q_SIGNAL void isShowingChatPopupChanged( bool isShowingChatPopup );
	Q_SIGNAL void isShowingTeamChatPopupChanged( bool isShowingTeamChatPopup );
	Q_SIGNAL void hasTeamChatChanged( bool hasTeamChat );

	Q_SIGNAL void isShowingActionRequestsChanged( bool isShowingActionRequests );

	Q_SIGNAL void isShowingMainMenuChanged( bool isShowingMainMenu );
	Q_SIGNAL void isShowingConnectionScreenChanged( bool isShowingConnectionScreen );
	Q_SIGNAL void isShowingInGameMenuChanged( bool isShowingInGameMenu );
	Q_SIGNAL void isShowingDemoPlaybackMenuChanged( bool isShowingDemoMenu );
	Q_SIGNAL void isDebuggingNativelyDrawnItemsChanged( bool isDebuggingNativelyDrawnItems );
	Q_SIGNAL void hasPendingCVarChangesChanged( bool hasPendingCVarChanges );
public slots:
	Q_SLOT void onSceneGraphInitialized();
	Q_SLOT void onRenderRequested();
	Q_SLOT void onSceneChanged();

	Q_SLOT void onComponentStatusChanged( QQmlComponent::Status status );
private:
	static inline QGuiApplication *s_application { nullptr };
	static inline int s_fakeArgc { 0 };
	static inline char **s_fakeArgv { nullptr };
	static inline QString s_charStrings[128];
	static inline cvar_t *s_sensitivityVar { nullptr };
	static inline cvar_t *s_mouseAccelVar { nullptr };
	static inline cvar_t *s_debugNativelyDrawnItemsVar { nullptr };

	int64_t m_lastActiveMaskTime { 0 };
	QPointer<QOpenGLContext> m_externalContext;
	QPointer<QOpenGLContext> m_sharedContext;
	QPointer<QQuickRenderControl> m_control;
	QScopedPointer<QOpenGLFramebufferObject> m_framebufferObject;
	QPointer<QOffscreenSurface> m_surface { nullptr };
	QPointer<QQuickWindow> m_window { nullptr };
	QPointer<QQmlEngine> m_engine;
	QPointer<QQmlComponent> m_component;
	GLuint m_vao { 0 };

	bool m_hasPendingSceneChange { false };
	bool m_hasPendingRedraw { false };
	bool m_isInUIRenderingMode { false };
	bool m_isValidAndReady { false };
	bool m_skipDrawingSelf { false };

	ServerListModel m_serverListModel;
	GametypesModel m_gametypesModel;
	KeysAndBindingsModel m_keysAndBindingsModel;

	ChatModelProxy m_chatModel;
	ChatModelProxy m_teamChatModel;

	CallvotesModelProxy m_callvotesModel;

	ScoreboardModelProxy m_scoreboardModel;

	DemosResolver m_demosResolver;
	DemosModel m_demosModel { &m_demosResolver };
	DemoPlayer m_demoPlayer { this };

	GametypeOptionsModel m_gametypeOptionsModel;

	PlayersModel m_playersModel;

	ActionRequestsModel m_actionRequestsModel;

	HudEditorLayoutModel m_hudEditorLayoutModel;
	InGameHudLayoutModel m_inGameHudLayoutModel;

	HudDataModel m_hudDataModel;

	QByteArray m_connectionFailMessage;
	std::optional<ConnectionFailKind> m_pendingConnectionFailKind;
	std::optional<ConnectionFailKind> m_connectionFailKind;
	bool m_clearFailedConnectionState { false };

	// A copy of last frame client properties for state change detection without intrusive changes to client code.
	// Use a separate scope for clarity and for avoiding name conflicts.
	struct {
		connstate_t clientState { CA_UNINITIALIZED };
		bool isPlayingADemo { false };
		bool isOperator { false };
	} m_lastFrameState;

	enum ActiveMenuMask : unsigned {
		MainMenu             = 0x1,
		ConnectionScreen     = 0x2,
		InGameMenu           = 0x4,
		DemoPlaybackMenu     = 0x8
	};

	unsigned m_backupMenuMask { 0 };
	unsigned m_activeMenuMask { 0 };

	bool m_shouldShowScoreboard { false };
	bool m_isShowingScoreboard { false };

	bool m_shouldShowChatPopup { false };
	bool m_shouldShowTeamChatPopup { false };
	bool m_isShowingChatPopup { false };
	bool m_isShowingTeamChatPopup { false };

	bool m_isShowingActionRequests { false };

	bool m_hasTeamChat { false };
	bool m_isShowingPovHud { false };
	bool m_isShowingHud { false };

	bool m_canJoin { false };
	bool m_canSpectate { false };
	bool m_canJoinAlpha { false };
	bool m_canJoinBeta { false };

	bool m_hasStartedBackgroundMapLoading { false };
	bool m_hasSucceededBackgroundMapLoading { false };

	qreal m_mouseXY[2] { 0.0, 0.0 };

	NativelyDrawn *m_nativelyDrawnListHead { nullptr };

	static constexpr const int kMaxNativelyDrawnItems = 32;
	static constexpr const int kMaxOccludersOfNativelyDrawnItems = 16;

	int m_numNativelyDrawnItems { 0 };

	wsw::Vector<QQuickItem *> m_hudOccluders;
	wsw::Vector<QQuickItem *> m_nativelyDrawnItemsOccluders;

	QSet<QQuickItem *> m_cvarAwareControls;

	QMap<QQuickItem *, QPair<QVariant, cvar_t *>> m_pendingCVarChanges;

	static void initPersistentPart();
	static void registerFonts();
	static void registerFont( const wsw::StringView &path );
	static void registerCustomQmlTypes();
	void registerContextProperties( QQmlContext *context );

	[[nodiscard]]
	static auto colorForNum( int num ) -> QColor {
		const auto *v = color_table[num];
		return QColor::fromRgbF( v[0], v[1], v[2] );
	}

	const QColor m_colorBlack { colorForNum( 0 ) };
	const QColor m_colorRed { colorForNum( 1 ) };
	const QColor m_colorGreen { colorForNum( 2 ) };
	const QColor m_colorYellow { colorForNum( 3 ) };
	const QColor m_colorBlue { colorForNum( 4 ) };
	const QColor m_colorCyan { colorForNum( 5 ) };
	const QColor m_colorMagenta { colorForNum( 6 ) };
	const QColor m_colorWhite { colorForNum( 7 ) };
	const QColor m_colorOrange { colorForNum( 8 ) };
	const QColor m_colorGrey { colorForNum( 9 ) };

	const QVariantList m_consoleColors {
		m_colorBlack, m_colorRed, m_colorGreen, m_colorYellow, m_colorBlue,
		m_colorCyan, m_colorMagenta, m_colorWhite, m_colorOrange, m_colorGrey
	};

	[[nodiscard]]
	bool isShowingMaskElement( unsigned bit ) const {
		// We do not want to complicate mask/stack state management.
		// Just test the separate scoreboard flag on every check.
		return ( m_activeMenuMask & bit ) != 0 && !m_isShowingScoreboard;
	}

	[[nodiscard]]
	bool isShowingDemoPlaybackMenu() const { return isShowingMaskElement( DemoPlaybackMenu ); }
	[[nodiscard]]
	bool isShowingMainMenu() const { return isShowingMaskElement( MainMenu ); }
	[[nodiscard]]
	bool isShowingConnectionScreen() const { return isShowingMaskElement( ConnectionScreen ); }
	[[nodiscard]]
	bool isShowingInGameMenu() const { return isShowingMaskElement( InGameMenu ); }

	[[nodiscard]]
	bool isShowingActionRequests() const { return m_isShowingActionRequests;}

	[[nodiscard]]
	bool isDebuggingNativelyDrawnItems() const;

	[[nodiscard]]
	bool hasPendingCVarChanges() const { return !m_pendingCVarChanges.isEmpty(); }

	[[nodiscard]]
	auto getConnectionFailMessage() const -> QByteArray { return m_connectionFailMessage; }
	[[nodiscard]]
	auto getConnectionFailKind() const -> ConnectionFailKind_ {
		return m_connectionFailKind ? (ConnectionFailKind_)*m_connectionFailKind : NoFail;
	};

	explicit QtUISystem( int width, int height );

	template <typename Value>
	void appendSetCVarCommand( const wsw::StringView &name, const Value &value );

	[[nodiscard]]
	auto findCVarOrThrow( const QByteArray &name ) const -> cvar_t *;

	void updateCVarAwareControls();
	void checkPropertyChanges();
	void setActiveMenuMask( unsigned activeMask, std::optional<unsigned> backupMask = std::nullopt );
	void renderQml();

	[[nodiscard]]
	auto getPressedMouseButtons() const -> Qt::MouseButtons;
	[[nodiscard]]
	auto getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers;

	bool tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown );

	using ItemsHeap = wsw::StaticVector<NativelyDrawn *, kMaxNativelyDrawnItems>;
	using NativelyDrawnOccluderBounds = wsw::StaticVector<QRectF, 4>;

	void drawBackgroundMapIfNeeded();

	[[nodiscard]]
	auto convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key>;
};

[[nodiscard]]
static bool isAPrintableChar( int ch ) {
	// See https://en.cppreference.com/w/cpp/string/byte/isprint
	return ch >= 0 &&  ch <= 127 && std::isprint( (unsigned char)ch );
}

void QtUISystem::initPersistentPart() {
	if( !s_application ) {
		s_application = new QGuiApplication( s_fakeArgc, s_fakeArgv );
		// Fix the overwritten locale, if any
		(void)std::setlocale( LC_ALL, "C" );

		registerFonts();
		registerCustomQmlTypes();

		// Initialize the table of textual strings corresponding to characters
		for( const QString &s: s_charStrings ) {
			const auto offset = (int)( std::addressof( s ) - s_charStrings );
			if( isAPrintableChar( offset ) ) {
				s_charStrings[offset] = QString::asprintf( "%c", (char)offset );
			}
		}

		s_sensitivityVar = Cvar_Get( "ui_sensitivity", "1.0", CVAR_ARCHIVE );
		s_mouseAccelVar = Cvar_Get( "ui_mouseAccel", "0.25", CVAR_ARCHIVE );
		s_debugNativelyDrawnItemsVar = Cvar_Get( "ui_debugNativelyDrawnItems", "0", 0 );
	}
}

void QtUISystem::registerCustomQmlTypes() {
	const QString reason( "This type is a native code bridge and cannot be instantiated" );
	const char *const uri = "net.warsow";
	qmlRegisterUncreatableType<QtUISystem>( uri, 2, 6, "Wsw", reason );
	qmlRegisterUncreatableType<ChatModel>( uri, 2, 6, "ChatModel", reason );
	qmlRegisterUncreatableType<CallvotesListModel>( uri, 2, 6, "CallvotesModel", reason );
	qmlRegisterUncreatableType<GametypeDef>( uri, 2, 6, "GametypeDef", reason );
	qmlRegisterUncreatableType<GametypesModel>( uri, 2, 6, "GametypesModel", reason );
	qmlRegisterUncreatableType<ScoreboardModelProxy>( uri, 2, 6, "Scoreboard", reason );
	qmlRegisterUncreatableType<ScoreboardTeamModel>( uri, 2, 6, "ScoreboardTeamModel", reason );
	qmlRegisterUncreatableType<ScoreboardSpecsModel>( uri, 2, 6, "ScoreboardSpecsModel", reason );
	qmlRegisterUncreatableType<KeysAndBindingsModel>( uri, 2, 6, "KeysAndBindings", reason );
	qmlRegisterUncreatableType<ServerListModel>( uri, 2, 6, "ServerListModel", reason );
	qmlRegisterUncreatableType<DemosResolver>( uri, 2, 6, "DemosResolver", reason );
	qmlRegisterUncreatableType<DemoPlayer>( uri, 2, 6, "DemoPlayer", reason );
	qmlRegisterUncreatableType<GametypeOptionsModel>( uri, 2, 6, "GametypeOptionsModel", reason );
	qmlRegisterUncreatableType<HudLayoutModel>( uri, 2, 6, "HudLayoutModel", reason );
	qmlRegisterUncreatableType<HudEditorLayoutModel>( uri, 2, 6, "HudEditorLayoutModel", reason );
	qmlRegisterUncreatableType<InGameHudLayoutModel>( uri, 2, 6, "InGameHudLayoutModel", reason );
	qmlRegisterUncreatableType<HudDataModel>( uri, 2, 6, "HudDataModel", reason );
	qmlRegisterType<NativelyDrawnImage>( uri, 2, 6, "NativelyDrawnImage_Native" );
	qmlRegisterType<NativelyDrawnModel>( uri, 2, 6, "NativelyDrawnModel_Native" );
}

static const char *kFontSuffixes[] {
	"-B", "-BI", "-C", "-L", "-LI", "-M", "-MI", "-R", "-RI", "-Th",
	"Mono-B", "Mono-BI", "Mono-R", "Mono-RI"
};

void QtUISystem::registerFonts() {
	QFontDatabase::removeAllApplicationFonts();

	wsw::StaticString<64> path;
	path.append( "fonts/Ubuntu"_asView );
	const auto pathPrefixLen = path.size();
	for( const char *suffix: kFontSuffixes ) {
		path.erase( pathPrefixLen );
		path.append( wsw::StringView( suffix ) );
		path.append( ".ttf"_asView );
		registerFont( path.asView() );
	}

	QGuiApplication::setFont( QFont( "Ubuntu", 12 ) );
}

void QtUISystem::registerFont( const wsw::StringView &path ) {
	if( auto handle = wsw::fs::openAsReadHandle( path ) ) {
		const size_t size = handle->getInitialFileSize();
		QByteArray data( (int)size, Qt::Uninitialized );
		if( handle->readExact( data.data(), size ) ) {
			if( QFontDatabase::addApplicationFontFromData( data ) >= 0 ) {
				return;
			}
		}
	}

	wsw::StaticString<256> message;
	message << "Failed to register "_asView << path;
	throw std::runtime_error( message.data() );
}

void QtUISystem::registerContextProperties( QQmlContext *context ) {
	context->setContextProperty( "wsw", this );
	context->setContextProperty( "serverListModel", &m_serverListModel );
	context->setContextProperty( "keysAndBindings", &m_keysAndBindingsModel );
	context->setContextProperty( "gametypesModel", &m_gametypesModel );
	context->setContextProperty( "compactChatModel", m_chatModel.getCompactModel() );
	context->setContextProperty( "richChatModel", m_chatModel.getRichModel() );
	context->setContextProperty( "compactTeamChatModel", m_teamChatModel.getCompactModel() );
	context->setContextProperty( "richTeamChatModel", m_teamChatModel.getRichModel() );
	context->setContextProperty( "regularCallvotesModel", m_callvotesModel.getRegularModel() );
	context->setContextProperty( "operatorCallvotesModel", m_callvotesModel.getOperatorModel() );
	context->setContextProperty( "scoreboard", &m_scoreboardModel );
	context->setContextProperty( "scoreboardSpecsModel", m_scoreboardModel.getSpecsModel() );
	context->setContextProperty( "scoreboardPlayersModel", m_scoreboardModel.getPlayersModel() );
	context->setContextProperty( "scoreboardAlphaModel", m_scoreboardModel.getAlphaModel() );
	context->setContextProperty( "scoreboardBetaModel", m_scoreboardModel.getBetaModel() );
	context->setContextProperty( "scoreboardMixedModel", m_scoreboardModel.getMixedModel() );
	context->setContextProperty( "scoreboardChasersModel", m_scoreboardModel.getChasersModel() );
	context->setContextProperty( "scoreboardChallengersModel", m_scoreboardModel.getChallengersModel() );
	context->setContextProperty( "demosModel", &m_demosModel );
	context->setContextProperty( "demosResolver", &m_demosResolver );
	context->setContextProperty( "demoPlayer", &m_demoPlayer );
	context->setContextProperty( "playersModel", &m_playersModel );
	context->setContextProperty( "actionRequestsModel", &m_actionRequestsModel );
	context->setContextProperty( "gametypeOptionsModel", &m_gametypeOptionsModel );
	context->setContextProperty( "hudEditorLayoutModel", &m_hudEditorLayoutModel );
	context->setContextProperty( "inGameHudLayoutModel", &m_inGameHudLayoutModel );
	context->setContextProperty( "hudDataModel", &m_hudDataModel );
}

void QtUISystem::onSceneGraphInitialized() {
	auto attachment = QOpenGLFramebufferObject::CombinedDepthStencil;
	m_framebufferObject.reset( new QOpenGLFramebufferObject( m_window->size(), attachment ) );
	m_window->setRenderTarget( m_framebufferObject.get() );
}

void QtUISystem::onRenderRequested() {
	m_hasPendingRedraw = true;
}

void QtUISystem::onSceneChanged() {
	m_hasPendingSceneChange = true;
}

void QtUISystem::onComponentStatusChanged( QQmlComponent::Status status ) {
	if ( QQmlComponent::Ready != status ) {
		if( status == QQmlComponent::Error ) {
			Com_Printf( S_COLOR_RED "The root Qml component loading has failed: %s\n",
				m_component->errorString().toUtf8().constData() );
		}
		return;
	}

	QObject *const rootObject = m_component->create();
	if( !rootObject ) {
		Com_Printf( S_COLOR_RED "Failed to finish the root Qml component creation\n" );
		return;
	}

	auto *const rootItem = qobject_cast<QQuickItem*>( rootObject );
	if( !rootItem ) {
		Com_Printf( S_COLOR_RED "The root Qml component is not a QQuickItem\n" );
		return;
	}

	QQuickItem *const parentItem = m_window->contentItem();
	const QSizeF size( m_window->width(), m_window->height() );
	parentItem->setSize( size );
	rootItem->setParentItem( parentItem );
	rootItem->setSize( size );

	m_isValidAndReady = true;
}

static SingletonHolder<QtUISystem> uiSystemInstanceHolder;
// Hacks for allowing retrieval of a maybe-instance
// (we do not want to add these hacks to SingletonHolder)
static bool initialized = false;

void UISystem::init( int width, int height ) {
	uiSystemInstanceHolder.Init( width, height );
	initialized = true;
}

void UISystem::shutdown() {
	uiSystemInstanceHolder.Shutdown();
	initialized = false;
}

auto UISystem::instance() -> UISystem * {
	return uiSystemInstanceHolder.Instance();
}

auto UISystem::maybeInstance() -> std::optional<UISystem *> {
	if( initialized ) {
		return uiSystemInstanceHolder.Instance();
	}
	return std::nullopt;
}

void QtUISystem::refresh( unsigned refreshFlags ) {
#ifndef _WIN32
	QGuiApplication::processEvents( QEventLoop::AllEvents );
#endif

	checkPropertyChanges();

	if( !m_isValidAndReady ) {
		return;
	}
	if( !m_hasPendingSceneChange && !m_hasPendingRedraw ) {
		return;
	}

	enterUIRenderingMode();
	renderQml();
	leaveUIRenderingMode();
}

QtUISystem::QtUISystem( int initialWidth, int initialHeight ) {
	initPersistentPart();

	QSurfaceFormat format;
	format.setDepthBufferSize( 24 );
	format.setStencilBufferSize( 8 );
	format.setMajorVersion( 3 );
	format.setMinorVersion( 3 );
	format.setRenderableType( QSurfaceFormat::OpenGL );
	format.setProfile( QSurfaceFormat::CoreProfile );

	m_externalContext = new QOpenGLContext;
	m_externalContext->setNativeHandle( VID_GetMainContextHandle() );
	if( !m_externalContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a Qt wrapper of the main rendering context\n" );
		return;
	}

	m_sharedContext = new QOpenGLContext;
	m_sharedContext->setFormat( format );
	m_sharedContext->setShareContext( m_externalContext );
	if( !m_sharedContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL rendering context\n" );
		return;
	}

	m_control = new QQuickRenderControl();
	m_window = new QQuickWindow( m_control );
	m_window->setGeometry( 0, 0, initialWidth, initialHeight );
	m_window->setColor( Qt::transparent );

	QObject::connect( m_window, &QQuickWindow::sceneGraphInitialized, this, &QtUISystem::onSceneGraphInitialized );
	QObject::connect( m_control, &QQuickRenderControl::renderRequested, this, &QtUISystem::onRenderRequested );
	QObject::connect( m_control, &QQuickRenderControl::sceneChanged, this, &QtUISystem::onSceneChanged );

	m_surface = new QOffscreenSurface;
	m_surface->setFormat( m_sharedContext->format() );
	m_surface->create();
	if ( !m_surface->isValid() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL offscreen surface\n" );
		return;
	}

	enterUIRenderingMode();

	bool hadErrors = true;
	if( m_sharedContext->makeCurrent( m_surface ) ) {
		// Bind a dummy VAO in the Qt context. That's something it fails to do on its own.
		auto *const f = m_sharedContext->extraFunctions();
		// TODO: Take care about the VAO lifetime
		f->glGenVertexArrays( 1, &m_vao );
		f->glBindVertexArray( m_vao );
		m_control->initialize( m_sharedContext );
		m_window->resetOpenGLState();
		hadErrors = m_sharedContext->functions()->glGetError() != GL_NO_ERROR;
	} else {
		Com_Printf( S_COLOR_RED "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	leaveUIRenderingMode();

	if( hadErrors ) {
		Com_Printf( S_COLOR_RED "Failed to initialize the Qt Quick render control from the given GL context\n" );
		return;
	}

	m_engine = new QQmlEngine;
	m_engine->addImageProvider( "wsw", new wsw::ui::WswImageProvider );

	registerContextProperties( m_engine->rootContext() );

	m_component = new QQmlComponent( m_engine );

	connect( m_component, &QQmlComponent::statusChanged, this, &QtUISystem::onComponentStatusChanged );
	m_component->loadUrl( QUrl( "qrc:/RootItem.qml" ) );
}

void QtUISystem::renderQml() {
	assert( m_isValidAndReady );
	assert( m_hasPendingSceneChange || m_hasPendingRedraw );

	if( m_hasPendingSceneChange ) {
		m_control->polishItems();
		m_control->sync();
	}

	m_hasPendingSceneChange = m_hasPendingRedraw = false;

	if( !m_sharedContext->makeCurrent( m_surface ) ) {
		// Consider this a fatal error
		Com_Error( ERR_FATAL, "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	m_control->render();

	m_window->resetOpenGLState();

	auto *const f = m_sharedContext->functions();
	f->glFlush();
	f->glFinish();
}

void QtUISystem::enterUIRenderingMode() {
	assert( !m_isInUIRenderingMode );
	m_isInUIRenderingMode = true;

	if( !GLimp_BeginUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to enter the UI rendering mode\n" );
	}
}

void QtUISystem::leaveUIRenderingMode() {
	assert( m_isInUIRenderingMode );
	m_isInUIRenderingMode = false;

	if( !GLimp_EndUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to leave the UI rendering mode\n" );
	}
}

void QtUISystem::drawSelfInMainContext() {
	if( !m_isValidAndReady || m_skipDrawingSelf ) {
		return;
	}

	drawBackgroundMapIfNeeded();

	// Make deeper items get evicted first from a max-heap
	const auto cmp = []( const NativelyDrawn *lhs, const NativelyDrawn *rhs ) {
		return lhs->m_nativeZ > rhs->m_nativeZ;
	};

	assert( m_nativelyDrawnItemsOccluders.size() <= kMaxOccludersOfNativelyDrawnItems );
	wsw::StaticVector<QRectF, kMaxOccludersOfNativelyDrawnItems> occluderBounds;
	for( const QQuickItem *occluder: m_nativelyDrawnItemsOccluders ) {
		occluderBounds.emplace_back( occluder->mapRectToScene( occluder->boundingRect() ) );
	}

	wsw::StaticVector<NativelyDrawn *, kMaxNativelyDrawnItems> underlayHeap, overlayHeap;
	for( NativelyDrawn *nativelyDrawn = m_nativelyDrawnListHead; nativelyDrawn; nativelyDrawn = nativelyDrawn->next ) {
		const QQuickItem *item = nativelyDrawn->m_selfAsItem;
		assert( item );

		const QVariant visibleProperty( QQmlProperty::read( item, "visible" ) );
		assert( !visibleProperty.isNull() && visibleProperty.isValid() );
		if( !visibleProperty.toBool() ) {
			continue;
		}

		if( nativelyDrawn->m_nativeZ < 0 ) {
			underlayHeap.push_back( nativelyDrawn );
			std::push_heap( underlayHeap.begin(), underlayHeap.end(), cmp );
			continue;
		}

		// Don't draw natively drawn items on top of occluders.
		// TODO: We either draw everything or draw nothing, a proper clipping/
		// fragmented drawing would be a correct solution.
		// Still, this the current approach produces acceptable results.

		bool occluded = false;
		const QRectF itemBounds( item->mapRectToScene( item->boundingRect() ) );
		for( const QRectF &bounds: occluderBounds ) {
			if( bounds.intersects( itemBounds ) ) {
				occluded = true;
				break;
			}
		}
		if( occluded ) {
			continue;
		}

		overlayHeap.push_back( nativelyDrawn );
		std::push_heap( overlayHeap.begin(), overlayHeap.end(), cmp );
	}

	// This is quite inefficient as we switch rendering modes for different kinds of items.
	// Unfortunately this is mandatory for maintaining the desired Z order.
	// Considering the low number of items of this kind the performance impact should be negligible.

	while( !underlayHeap.empty() ) {
		std::pop_heap( underlayHeap.begin(), underlayHeap.end(), cmp );
		underlayHeap.back()->drawSelfNatively();
		underlayHeap.pop_back();
	}

	R_Set2DMode( true );
	R_DrawExternalTextureOverlay( m_framebufferObject->texture() );
	R_Set2DMode( false );

	while( !overlayHeap.empty() ) {
		std::pop_heap( overlayHeap.begin(), overlayHeap.end(), cmp );
		overlayHeap.back()->drawSelfNatively();
		overlayHeap.pop_back();
	}

	if( !m_activeMenuMask ) {
		return;
	}

	R_Set2DMode( true );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// TODO: Check why CL_BeginRegistration()/CL_EndRegistration() never gets called
	auto *cursorMaterial = R_RegisterPic( "gfx/ui/cursor.tga" );
	// TODO: Account for screen pixel density
	RF_DrawStretchPic( (int)m_mouseXY[0], (int)m_mouseXY[1], 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorMaterial );
	R_Set2DMode( false );
}

void QtUISystem::drawBackgroundMapIfNeeded() {
	if( m_lastFrameState.clientState != CA_DISCONNECTED ) {
		m_hasStartedBackgroundMapLoading = false;
		m_hasSucceededBackgroundMapLoading = false;
		return;
	}

	constexpr const char *worldModelName = "maps/ui.bsp";
	if( !m_hasStartedBackgroundMapLoading ) {
		RF_RegisterWorldModel( worldModelName );
		m_hasStartedBackgroundMapLoading = true;
	} else if( !m_hasSucceededBackgroundMapLoading ) {
		if( R_RegisterModel( worldModelName ) ) {
			m_hasSucceededBackgroundMapLoading = true;
		}
	}

	if( !m_hasSucceededBackgroundMapLoading ) {
		return;
	}

	if( !( m_activeMenuMask & MainMenu ) ) {
		return;
	}

	refdef_t rdf;
	memset( &rdf, 0, sizeof( rdf ) );
	rdf.areabits = nullptr;

	const auto widthAndHeight = std::make_pair( m_window->width(), m_window->height() );
	std::tie( rdf.x, rdf.y ) = std::make_pair( 0, 0 );
	std::tie( rdf.width, rdf.height ) = widthAndHeight;

	// This is a copy-paste from Warsow 2.1 map_ui.pk3 CSS
	const vec3_t origin { 302.0f, -490.0f, 120.0f };
	const vec3_t angles { 0, -240, 0 };

	VectorCopy( origin, rdf.vieworg );
	AnglesToAxis( angles, rdf.viewaxis );
	rdf.fov_x = 90.0f;
	rdf.fov_y = CalcFov( 90.0f, rdf.width, rdf.height );
	AdjustFov( &rdf.fov_x, &rdf.fov_y, rdf.width, rdf.height, false );
	rdf.time = 0;

	std::tie( rdf.scissor_x, rdf.scissor_y ) = std::make_pair( 0, 0 );
	std::tie( rdf.scissor_width, rdf.scissor_height ) = widthAndHeight;

	RF_ClearScene();
	RF_RenderScene( &rdf );
}

void QtUISystem::toggleInGameMenu() {
	if( isShowingInGameMenu() ) {
		setActiveMenuMask( m_activeMenuMask & ~InGameMenu );
	} else {
		setActiveMenuMask( m_activeMenuMask | InGameMenu );
	}
}

void QtUISystem::showMainMenu() {
	setActiveMenuMask( MainMenu );
}

void QtUISystem::returnFromInGameMenu() {
	setActiveMenuMask( m_activeMenuMask & ~InGameMenu, 0 );
}

void QtUISystem::returnFromMainMenu() {
	if( m_backupMenuMask ) {
		setActiveMenuMask( m_backupMenuMask, 0 );
	}
}

void QtUISystem::closeChatPopup() {
	const bool wasShowingChatPopup = m_isShowingChatPopup;
	const bool wasShowingTeamChatPopup = m_isShowingTeamChatPopup;

	m_isShowingChatPopup = false;
	m_isShowingTeamChatPopup = false;
	m_shouldShowChatPopup = false;
	m_shouldShowTeamChatPopup = false;

	if( wasShowingChatPopup ) {
		Q_EMIT isShowingChatPopupChanged( false );
	}
	if( wasShowingTeamChatPopup ) {
		Q_EMIT isShowingTeamChatPopupChanged( false );
	}
}

void QtUISystem::setActiveMenuMask( unsigned activeMask, std::optional<unsigned> backupMask ) {
	if( m_activeMenuMask == activeMask ) {
		if( backupMask ) {
			m_backupMenuMask = *backupMask;
		}
		return;
	}

	const auto oldActiveMask = m_activeMenuMask;

	const bool wasShowingMainMenu = isShowingMainMenu();
	const bool wasShowingConnectionScreen = isShowingConnectionScreen();
	const bool wasShowingInGameMenu = isShowingInGameMenu();
	const bool wasShowingDemoPlaybackMenu = isShowingDemoPlaybackMenu();

	m_backupMenuMask = backupMask ? *backupMask : m_activeMenuMask;
	m_activeMenuMask = activeMask;

	const bool _isShowingMainMenu = isShowingMainMenu();
	const bool _isShowingConnectionScreen = isShowingConnectionScreen();
	const bool _isShowingInGameMenu = isShowingInGameMenu();
	const bool _isShowingDemoPlaybackMenu = isShowingDemoPlaybackMenu();

	if( wasShowingMainMenu != _isShowingMainMenu ) {
		Q_EMIT isShowingMainMenuChanged( _isShowingMainMenu );
	}
	if( wasShowingConnectionScreen != _isShowingConnectionScreen ) {
		Q_EMIT isShowingConnectionScreenChanged( _isShowingConnectionScreen );
	}
	if( wasShowingInGameMenu != _isShowingInGameMenu ) {
		Q_EMIT isShowingInGameMenuChanged( _isShowingInGameMenu );
	}
	if( wasShowingDemoPlaybackMenu != _isShowingDemoPlaybackMenu ) {
		Q_EMIT isShowingDemoPlaybackMenuChanged( _isShowingDemoPlaybackMenu );
	}

	if( m_activeMenuMask && !oldActiveMask ) {
		CL_ClearInputState();
	}
}

void QtUISystem::checkPropertyChanges() {
	const auto lastClientState = m_lastFrameState.clientState;
	const auto actualClientState = cls.state;
	m_lastFrameState.clientState = actualClientState;

	const bool wasPlayingADemo = m_lastFrameState.isPlayingADemo;
	const bool isPlayingADemo = cls.demoPlayer.playing;
	m_lastFrameState.isPlayingADemo = isPlayingADemo;

	bool checkMaskChanges = false;
	if( m_lastFrameState.clientState != lastClientState ) {
		checkMaskChanges = true;
	} else if( isPlayingADemo != wasPlayingADemo ) {
		checkMaskChanges = true;
	} else if( m_pendingConnectionFailKind ) {
		checkMaskChanges = true;
	} else if( m_clearFailedConnectionState ) {
		checkMaskChanges = true;
	}

	m_clearFailedConnectionState = false;

	if( checkMaskChanges ) {
		const bool wasShowingScoreboard = m_isShowingScoreboard;
		const bool wasShowingChatPopup = m_isShowingChatPopup;
		const bool wasShowingTeamChatPopup = m_isShowingTeamChatPopup;
		if( m_pendingConnectionFailKind ) {
			m_connectionFailKind = m_pendingConnectionFailKind;
			m_pendingConnectionFailKind = std::nullopt;
			setActiveMenuMask( ConnectionScreen, 0 );
			Q_EMIT connectionFailKindChanged( getConnectionFailKind() );
			Q_EMIT connectionFailMessageChanged( getConnectionFailMessage() );
		} else if( actualClientState == CA_DISCONNECTED ) {
			setActiveMenuMask( MainMenu, 0 );
			m_chatModel.clear();
			m_teamChatModel.clear();
		} else if( actualClientState == CA_ACTIVE ) {
			if( isPlayingADemo ) {
				setActiveMenuMask( DemoPlaybackMenu, 0 );
			} else {
				setActiveMenuMask( InGameMenu, 0 );
			}
			m_callvotesModel.reload();
			m_scoreboardModel.reload();
			m_gametypeOptionsModel.reload();
		} else if( actualClientState >= CA_GETTING_TICKET && actualClientState <= CA_LOADING ) {
			setActiveMenuMask( ConnectionScreen, 0 );
		}
		// Hide scoreboard upon state changes
		m_isShowingScoreboard = false;
		m_shouldShowScoreboard = false;
		if( wasShowingScoreboard ) {
			Q_EMIT isShowingScoreboardChanged( false );
		}
		// Hide chat upon state changes
		m_isShowingChatPopup = false;
		m_shouldShowChatPopup = false;
		m_isShowingTeamChatPopup = false;
		m_shouldShowTeamChatPopup = false;
		if( wasShowingChatPopup ) {
			Q_EMIT isShowingChatPopupChanged( false );
		}
		if( wasShowingTeamChatPopup ) {
			Q_EMIT isShowingTeamChatPopupChanged( false );
		}

		// Reset the fail state (if any) for robustness
		if( actualClientState != CA_DISCONNECTED ) {
			const bool hadKind = m_connectionFailKind != std::nullopt;
			m_connectionFailKind = m_pendingConnectionFailKind = std::nullopt;
			m_connectionFailMessage.clear();
			if( hadKind ) {
				Q_EMIT connectionFailKindChanged( NoFail );
				Q_EMIT connectionFailMessageChanged( m_connectionFailMessage );
			}
		}
	}

	const bool hadTeamChat = m_hasTeamChat;
	m_hasTeamChat = false;
	if( Cmd_Exists( "say_team" ) ) {
		m_hasTeamChat = CG_IsSpectator() || ( GS_TeamBasedGametype() && !GS_InvidualGameType() );
	}

	if( hadTeamChat != m_hasTeamChat ) {
		// Hide all popups forcefully in this case
		m_shouldShowChatPopup = false;
		m_shouldShowTeamChatPopup = false;
		Q_EMIT hasTeamChatChanged( m_hasTeamChat );
	}

	if( m_isShowingScoreboard != m_shouldShowScoreboard ) {
		m_isShowingScoreboard = m_shouldShowScoreboard;
		Q_EMIT isShowingScoreboardChanged( m_isShowingScoreboard );
	}

	if( m_isShowingChatPopup != m_shouldShowChatPopup ) {
		m_isShowingChatPopup = m_shouldShowChatPopup;
		Q_EMIT isShowingChatPopupChanged( m_isShowingChatPopup );
	}

	if( m_isShowingTeamChatPopup != m_shouldShowTeamChatPopup ) {
		m_isShowingTeamChatPopup = m_shouldShowTeamChatPopup;
		Q_EMIT isShowingTeamChatPopupChanged( m_isShowingTeamChatPopup );
	}

	const bool wasShowingHud = m_isShowingHud;
	const bool wasShowingPovHud = m_isShowingPovHud;
	m_isShowingHud = actualClientState == CA_ACTIVE && !( m_activeMenuMask & MainMenu );
	if( m_isShowingHud != wasShowingHud ) {
		Q_EMIT isShowingHudChanged( m_isShowingHud );
	}
	m_isShowingPovHud = m_isShowingHud && ( CG_ActiveChasePov() != std::nullopt );
	if( m_isShowingPovHud != wasShowingPovHud ) {
		Q_EMIT isShowingPovHudChanged( m_isShowingPovHud );
	}

	const bool wasShowingActionRequests = m_isShowingActionRequests;
	m_isShowingActionRequests = !m_actionRequestsModel.empty() && !m_activeMenuMask;
	if( wasShowingActionRequests != m_isShowingActionRequests ) {
		Q_EMIT isShowingActionRequestsChanged( m_isShowingActionRequests );
	}

	if( s_debugNativelyDrawnItemsVar->modified ) {
		Q_EMIT isDebuggingNativelyDrawnItemsChanged( s_debugNativelyDrawnItemsVar->integer != 0 );
		s_debugNativelyDrawnItemsVar->modified = false;
	}

	const bool oldCanSpectate = m_canSpectate;
	const bool oldCanJoin = m_canJoin;
	const bool oldCanJoinAlpha = m_canJoinAlpha;
	const bool oldCanJoinBeta = m_canJoinBeta;

	// TODO: This is fine for now but something more sophisticated should be really used
	m_canSpectate = m_canJoin = m_canJoinAlpha = m_canJoinBeta = false;
	if( actualClientState == CA_ACTIVE && GS_MatchState() <= MATCH_STATE_PLAYTIME ) {
		const int team = CG_MyRealTeam();
		m_canSpectate = team != TEAM_SPECTATOR;
		m_canJoin = team == TEAM_SPECTATOR;
		if( CG_HasTwoTeams() ) {
			m_canJoinAlpha = team != TEAM_ALPHA;
			m_canJoinBeta = team != TEAM_BETA;
		}
	}

	if( oldCanSpectate != m_canSpectate ) {
		Q_EMIT canSpectateChanged( m_canSpectate );
	}
	if( oldCanJoin != m_canJoin ) {
		Q_EMIT canJoinChanged( m_canJoin );
	}
	if( oldCanJoinAlpha != m_canJoinAlpha ) {
		Q_EMIT canJoinAlphaChanged( m_canJoinAlpha );
	}
	if( oldCanJoinBeta != m_canJoinBeta ) {
		Q_EMIT canJoinBetaChanged( m_canJoinBeta );
	}

	m_keysAndBindingsModel.checkUpdates();
	m_demoPlayer.checkUpdates();
	m_actionRequestsModel.update();

	m_hudDataModel.checkPropertyChanges( getFrameTimestamp() );

	updateCVarAwareControls();

	bool isLikelyToDrawSelf = false;
	if( m_activeMenuMask ) {
		isLikelyToDrawSelf = true;
	} else if( m_isShowingHud ) {
		isLikelyToDrawSelf = true;
	} else if( m_isShowingScoreboard || m_isShowingChatPopup || m_isShowingTeamChatPopup ) {
		isLikelyToDrawSelf = true;
	} else if( !m_actionRequestsModel.empty() ) {
		isLikelyToDrawSelf = true;
	}

	if( isLikelyToDrawSelf ) {
		m_skipDrawingSelf = false;
		m_lastActiveMaskTime = Sys_Milliseconds();
	} else if( !m_skipDrawingSelf ) {
		// Give a second for fade-out animations (if any)
		if( m_lastActiveMaskTime + 1000 < Sys_Milliseconds() ) {
			m_skipDrawingSelf = true;
		}
	}
}

bool QtUISystem::handleMouseMove( int frameTime, int dx, int dy ) {
	if( !m_activeMenuMask ) {
		return false;
	}

	if( !dx && !dy ) {
		return true;
	}

	const int bounds[2] = { m_window->width(), m_window->height() };
	const int deltas[2] = { dx, dy };

	if( s_sensitivityVar->modified ) {
		if( s_sensitivityVar->value <= 0.0f || s_sensitivityVar->value > 10.0f ) {
			Cvar_ForceSet( s_sensitivityVar->name, "1.0" );
		}
	}

	if( s_mouseAccelVar->modified ) {
		if( s_mouseAccelVar->value < 0.0f || s_mouseAccelVar->value > 1.0f ) {
			Cvar_ForceSet( s_mouseAccelVar->name, "0.25" );
		}
	}

	float sensitivity = s_sensitivityVar->value;
	if( frameTime > 0 ) {
		sensitivity += (float)s_mouseAccelVar->value * std::sqrt( dx * dx + dy * dy ) / (float)( frameTime );
	}

	for( int i = 0; i < 2; ++i ) {
		if( !deltas[i] ) {
			continue;
		}
		qreal scaledDelta = ( (qreal)deltas[i] * sensitivity );
		// Make sure we won't lose a mouse movement due to fractional part loss
		if( !scaledDelta ) {
			scaledDelta = Q_sign( deltas[i] );
		}
		m_mouseXY[i] += scaledDelta;
		Q_clamp( m_mouseXY[i], 0, bounds[i] );
	}

	QPointF point( m_mouseXY[0], m_mouseXY[1] );
	QMouseEvent event( QEvent::MouseMove, point, Qt::NoButton, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( m_window, &event );
	return true;
}

bool QtUISystem::requestsKeyboardFocus() const {
	return m_activeMenuMask != 0 || ( m_isShowingChatPopup || m_isShowingTeamChatPopup );
}

bool QtUISystem::handleKeyEvent( int quakeKey, bool keyDown ) {
	if( !m_activeMenuMask ) {
		if( !( m_isShowingChatPopup || m_isShowingTeamChatPopup ) ) {
			if( keyDown ) {
				return m_actionRequestsModel.handleKeyEvent( quakeKey );
			}
			return false;
		}
	}

	if( tryHandlingKeyEventAsAMouseEvent( quakeKey, keyDown ) ) {
		return true;
	}

	const auto maybeQtKey = convertQuakeKeyToQtKey( quakeKey );
	if( !maybeQtKey ) {
		return true;
	}

	const auto type = keyDown ? QEvent::KeyPress : QEvent::KeyRelease;
	QKeyEvent keyEvent( type, *maybeQtKey, getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( m_window, &keyEvent );
	return true;
}

bool QtUISystem::handleCharEvent( int ch ) {
	if( !m_activeMenuMask ) {
		if( !( m_isShowingChatPopup || m_isShowingTeamChatPopup ) ) {
			return false;
		}
	}

	if( !isAPrintableChar( ch ) ) {
		return true;
	}

	const auto modifiers = getPressedKeyboardModifiers();
	// The plain cast of `ch` to Qt::Key seems to be correct in this case
	// (all printable characters seem to map 1-1 to Qt key codes)
	QKeyEvent pressEvent( QEvent::KeyPress, (Qt::Key)ch, modifiers, s_charStrings[ch] );
	QCoreApplication::sendEvent( m_window, &pressEvent );
	QKeyEvent releaseEvent( QEvent::KeyRelease, (Qt::Key)ch, modifiers );
	QCoreApplication::sendEvent( m_window, &releaseEvent );
	return true;
}

auto QtUISystem::getPressedMouseButtons() const -> Qt::MouseButtons {
	const auto *const keyHandlingSystem = wsw::cl::KeyHandlingSystem::instance();

	auto result = Qt::MouseButtons();
	if( keyHandlingSystem->isKeyDown( K_MOUSE1 ) ) {
		result |= Qt::LeftButton;
	}
	if( keyHandlingSystem->isKeyDown( K_MOUSE2 ) ) {
		result |= Qt::RightButton;
	}
	if( keyHandlingSystem->isKeyDown( K_MOUSE3 ) ) {
		result |= Qt::MiddleButton;
	}
	return result;
}

auto QtUISystem::getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers {
	const auto *const keyHandlingSystem = wsw::cl::KeyHandlingSystem::instance();

	auto result = Qt::KeyboardModifiers();
	if( keyHandlingSystem->isKeyDown( K_LCTRL ) || keyHandlingSystem->isKeyDown( K_RCTRL ) ) {
		result |= Qt::ControlModifier;
	}
	if( keyHandlingSystem->isKeyDown( K_LALT ) || keyHandlingSystem->isKeyDown( K_RALT ) ) {
		result |= Qt::AltModifier;
	}
	if( keyHandlingSystem->isKeyDown( K_LSHIFT ) || keyHandlingSystem->isKeyDown( K_RSHIFT ) ) {
		result |= Qt::ShiftModifier;
	}
	return result;
}

bool QtUISystem::tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown ) {
	Qt::MouseButton button;
	if( quakeKey == K_MOUSE1 ) {
		button = Qt::LeftButton;
	} else if( quakeKey == K_MOUSE2 ) {
		button = Qt::RightButton;
	} else if( quakeKey == K_MOUSE3 ) {
		button = Qt::MiddleButton;
	} else {
		return false;
	}

	QPointF point( m_mouseXY[0], m_mouseXY[1] );
	QEvent::Type eventType = keyDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
	QMouseEvent event( eventType, point, button, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( m_window, &event );
	return true;
}

auto QtUISystem::convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key> {
	if( quakeKey < 0 ) {
		return std::nullopt;
	}

	static_assert( K_BACKSPACE == 127 );
	if( quakeKey < 127 ) {
		if( quakeKey == K_TAB ) {
			return Qt::Key_Tab;
		}
		if( quakeKey == K_ENTER ) {
			return Qt::Key_Enter;
		}
		if( quakeKey == K_ESCAPE ) {
			return Qt::Key_Escape;
		}
		if( quakeKey == K_SPACE ) {
			return Qt::Key_Space;
		}
		if( std::isprint( quakeKey ) ) {
			return (Qt::Key)quakeKey;
		}
		return std::nullopt;
	}

	if( quakeKey >= K_F1 && quakeKey <= K_F15 ) {
		return (Qt::Key)( Qt::Key_F1 + ( quakeKey - K_F1 ) );
	}

	// Some other seemingly unuseful keys are ignored
	switch( quakeKey ) {
		case K_BACKSPACE: return Qt::Key_Backspace;

		case K_UPARROW: return Qt::Key_Up;
		case K_DOWNARROW: return Qt::Key_Down;
		case K_LEFTARROW: return Qt::Key_Left;
		case K_RIGHTARROW: return Qt::Key_Right;

		case K_LALT:
		case K_RALT:
			return Qt::Key_Alt;

		case K_LCTRL:
		case K_RCTRL:
			return Qt::Key_Control;

		case K_LSHIFT:
		case K_RSHIFT:
			return Qt::Key_Shift;

		case K_INS: return Qt::Key_Insert;
		case K_DEL: return Qt::Key_Delete;
		case K_PGDN: return Qt::Key_PageDown;
		case K_PGUP: return Qt::Key_PageUp;
		case K_HOME: return Qt::Key_Home;
		case K_END: return Qt::Key_End;

		default: return std::nullopt;
	}
}

bool QtUISystem::isDebuggingNativelyDrawnItems() const {
	return s_debugNativelyDrawnItemsVar->integer != 0;
}

void QtUISystem::registerNativelyDrawnItem( QQuickItem *item ) {
	auto *const nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		throw std::logic_error( "An item is not an instance of NativelyDrawn" );
	}
	if( m_numNativelyDrawnItems == kMaxNativelyDrawnItems ) {
		throw std::logic_error( "Too many natively drawn items" );
	}
	wsw::link( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = true;
	m_numNativelyDrawnItems++;
}

void QtUISystem::unregisterNativelyDrawnItem( QQuickItem *item ) {
	auto *nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		throw std::logic_error( "An item is not an instance of NativelyDrawn" );
	}
	if( !nativelyDrawn->m_isLinked ) {
		throw std::logic_error( "The NativelyDrawn instance is not linked to the list" );
	}
	wsw::unlink( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = false;
	m_numNativelyDrawnItems--;
	assert( m_numNativelyDrawnItems >= 0 );
}

void QtUISystem::registerHudOccluder( QQuickItem *item ) {
	if( const auto it = std::find( m_hudOccluders.begin(), m_hudOccluders.end(), item ); it != m_hudOccluders.end() ) {
		throw std::logic_error( "This HUD occluder item has been already registered" );
	}
	m_hudOccluders.push_back( item );
	Q_EMIT hudOccludersChanged();
}

void QtUISystem::unregisterHudOccluder( QQuickItem *item ) {
	if( const auto it = std::find( m_hudOccluders.begin(), m_hudOccluders.end(), item ); it != m_hudOccluders.end() ) {
		m_hudOccluders.erase( it );
		Q_EMIT hudOccludersChanged();
	} else {
		throw std::logic_error( "This HUD occluder item has not been registered" );
	}
}

void QtUISystem::updateHudOccluder( QQuickItem * ) {
	// TODO: Just set a pending update flag and check during properties update?
	Q_EMIT hudOccludersChanged();
}

bool QtUISystem::isHudItemOccluded( QQuickItem *item ) {
	QRectF itemRect( item->mapRectToScene( item->boundingRect() ) );
	itemRect.setWidth( itemRect.width() + 10.0 );
	itemRect.setHeight( itemRect.height() + 10.0 );
	itemRect.moveTopLeft( QPointF( itemRect.x() - 5.0, itemRect.y() - 5.0 ) );
	for( const QQuickItem *occluder : m_hudOccluders ) {
		const QRectF occluderRect( occluder->mapRectToScene( occluder->boundingRect() ) );
		if( occluderRect.intersects( itemRect ) ) {
			return true;
		}
	}
	return false;
}

void QtUISystem::registerNativelyDrawnItemsOccluder( QQuickItem *item ) {
	auto &occluders = m_nativelyDrawnItemsOccluders;
	if( const auto it = std::find( occluders.begin(), occluders.end(), item ); it != occluders.end() ) {
		throw std::logic_error( "This occluder of natively drawn items has been already registered" );
	}
	if( occluders.size() == kMaxOccludersOfNativelyDrawnItems ) {
		throw std::logic_error( "Too many occluders of natively drawn items" );
	}
	occluders.push_back( item );
}

void QtUISystem::unregisterNativelyDrawnItemsOccluder( QQuickItem *item ) {
	auto &occluders = m_nativelyDrawnItemsOccluders;
	const auto it = std::find( occluders.begin(), occluders.end(), item );
	if( it == occluders.end() ) {
		throw std::logic_error( "This occluder of natively drawn items has not been registered" );
	}
	occluders.erase( it );
}

auto QtUISystem::findCVarOrThrow( const QByteArray &name ) const -> cvar_t * {
	if( cvar_t *maybeVar = Cvar_Find( name.constData() ) ) {
		return maybeVar;
	}
	std::string message;
	message += "Failed to find a var \"";
	message += std::string_view( name.data(), (size_t)name.size() );
	message += "\" by name";
	throw std::logic_error( message );
}

QVariant QtUISystem::getCVarValue( const QString &name ) const {
	return QVariant( findCVarOrThrow( name.toLatin1() )->string );
}

void QtUISystem::setCVarValue( const QString &name, const QVariant &value ) {
	const QByteArray nameBytes( name.toLatin1() );
	auto *const cvar = findCVarOrThrow( nameBytes );

	// TODO: What to do with that?
	if( ( cvar->flags & CVAR_LATCH_VIDEO ) || ( cvar->flags & CVAR_LATCH_SOUND ) ) {
		Com_Printf( "Refusing to apply a video/sound-latched var %s value immediately\n", nameBytes.constData() );
		return;
	}

	Cvar_ForceSet( nameBytes.constData(), value.toString().toLatin1().constData() );
}

void QtUISystem::markPendingCVarChanges( QQuickItem *control, const QString &name, const QVariant &value ) {
	auto it = m_pendingCVarChanges.find( control );
	if( it == m_pendingCVarChanges.end() ) {
		// TODO: Use `it` as a hint
		m_pendingCVarChanges.insert( control, { value, findCVarOrThrow( name.toLatin1() ) } );
		if( m_pendingCVarChanges.size() == 1 ) {
			Q_EMIT hasPendingCVarChangesChanged( true );
		}
		return;
	}

	// Check if changes really going to have an effect
	if( QVariant( it->second->string ) != value ) {
		it->first = value;
		return;
	}

	// TODO: Does a repeated check make any sense?
	m_pendingCVarChanges.erase( it );
	if( m_pendingCVarChanges.isEmpty() ) {
		Q_EMIT hasPendingCVarChangesChanged( false );
	}
}

bool QtUISystem::hasControlPendingCVarChanges( QQuickItem *control ) const {
	return m_pendingCVarChanges.contains( control );
}

void QtUISystem::commitPendingCVarChanges() {
	if( m_pendingCVarChanges.isEmpty() ) {
		return;
	}

	auto [restartVideo, restartSound] = std::make_pair( false, false );
	for( const auto &[value, cvar]: m_pendingCVarChanges ) {
		Cvar_ForceSet( cvar->name, value.toString().toLatin1().constData() );
		if( cvar->flags & CVAR_LATCH_VIDEO ) {
			restartVideo = true;
		}
		if( cvar->flags & CVAR_LATCH_SOUND ) {
			restartSound = true;
		}
	}

	m_pendingCVarChanges.clear();
	Q_EMIT hasPendingCVarChangesChanged( false );

	if( restartVideo ) {
		Cbuf_ExecuteText( EXEC_APPEND, "vid_restart" );
	}
	if( restartSound ) {
		Cbuf_ExecuteText( EXEC_APPEND, "s_restart" );
	}
}

void QtUISystem::rollbackPendingCVarChanges() {
	if( m_pendingCVarChanges.isEmpty() ) {
		return;
	}

	QMapIterator<QQuickItem *, QPair<QVariant, cvar_t *>> it( m_pendingCVarChanges );
	while( it.hasNext() ) {
		(void)it.next();
		QMetaObject::invokeMethod( it.key(), "rollbackChanges" );
	}

	m_pendingCVarChanges.clear();
	Q_EMIT hasPendingCVarChangesChanged( false );
}

void QtUISystem::registerCVarAwareControl( QQuickItem *control ) {
	assert( control );
	if( m_cvarAwareControls.contains( control ) ) {
		throw std::logic_error( "A CVar-aware control has been already registered" );
	}
	m_cvarAwareControls.insert( control );
}

void QtUISystem::unregisterCVarAwareControl( QQuickItem *control ) {
	assert( control );
	if( !m_cvarAwareControls.remove( control ) ) {
		throw std::logic_error( "Failed to unregister a CVar-aware control" );
	}
}

void QtUISystem::updateCVarAwareControls() {
	// Check whether pending changes still hold

	const bool hadPendingChanges = !m_pendingCVarChanges.isEmpty();
	QMutableMapIterator<QQuickItem *, QPair<QVariant, cvar_t *>> it( m_pendingCVarChanges );
	while( it.hasNext() ) {
		(void)it.next();
		auto [value, cvar] = it.value();
		if( QVariant( cvar->string ) == value ) {
			it.remove();
		}
	}

	if( hadPendingChanges && m_pendingCVarChanges.isEmpty() ) {
		Q_EMIT hasPendingCVarChangesChanged( false );
	}

	for( QQuickItem *control : m_cvarAwareControls ) {
		QMetaObject::invokeMethod( control, "checkCVarChanges" );
	}
}

void QtUISystem::quit() {
	Cbuf_AddText( "quit" );
}

void QtUISystem::disconnect() {
	Cbuf_AddText( "disconnect" );
}

void QtUISystem::spectate() {
	assert( getCanSpectate() );
	Cbuf_AddText( "spec" );
}

void QtUISystem::join() {
	assert( getCanJoin() );
	Cbuf_AddText( "join" );
}

void QtUISystem::joinAlpha() {
	assert( getCanJoinAlpha() );
	Cbuf_AddText( "join alpha" );
}

void QtUISystem::joinBeta() {
	assert( getCanJoinBeta() );
	Cbuf_AddText( "join beta" );
}

void QtUISystem::callVote( const QByteArray &name, const QByteArray &value, bool isOperatorCall ) {
	wsw::StaticString<1024> command;
	if( isOperatorCall ) {
		command << "opcall"_asView;
	} else {
		command << "callvote"_asView;
	}
	command << ' ' << wsw::StringView( name.data(), (unsigned)name.size() );
	command << ' ' << wsw::StringView( value.data(), (unsigned)value.size() );
	Cbuf_ExecuteText( EXEC_APPEND, command.data() );
}

auto QtUISystem::colorFromRgbString( const QString &string ) const -> QVariant {
	if( int color = COM_ReadColorRGBString( string.toUtf8().constData() ); color != -1 ) {
		return QColor::fromRgb( COLOR_R( color ), COLOR_G( color ), COLOR_B( color ), 255 );
	}
	return QVariant();
}

auto QtUISystem::makeSkewXMatrix( qreal height, qreal degrees ) const -> QMatrix4x4 {
	assert( degrees >= 0.0 && degrees + 0.1 < 90.0 );
	const float angle = (float)DEG2RAD( degrees );
	const float sin = std::sin( angle );
	const float cos = std::cos( angle );
	const float tan = sin * Q_Rcp( cos );
	QMatrix4x4 result {
		+1.0, -sin, +0.0, +0.0,
		+0.0, +1.0, +0.0, +0.0,
		+0.0, +0.0, +1.0, +0.0,
		+0.0, +0.0, +0.0, +1.0
	};
	result.translate( tan * (float)height, 0.0f );
	return result;
}

auto QtUISystem::formatPing( int ping ) const -> QByteArray {
	return wsw::ui::formatPing( ping );
}

void QtUISystem::startServerListUpdates( int flags ) {
	m_serverListModel.clear();
	const bool showEmptyServers = ( flags & ShowEmptyServers ) != 0;
	const bool showFullServers = ( flags & ShowFullServers ) != 0;
	ServerList::instance()->startPushingUpdates( &m_serverListModel, showEmptyServers, showFullServers );
}

void QtUISystem::stopServerListUpdates() {
	m_serverListModel.clear();
	ServerList::instance()->stopPushingUpdates();
}

void QtUISystem::addToChat( const wsw::StringView &name, const wsw::StringView &message ) {
	m_chatModel.addMessage( name, getFrameTimestamp(), message );
}

void QtUISystem::addToTeamChat( const wsw::StringView &name, const wsw::StringView &message ) {
	m_teamChatModel.addMessage( name, getFrameTimestamp(), message );
}

void QtUISystem::handleConfigString( unsigned configStringIndex, const wsw::StringView &string ) {
	// TODO: Let aggregated entities decide whether they can handle?
	if( (unsigned)( configStringIndex - CS_PLAYERINFOS ) < (unsigned)MAX_CLIENTS ) {
		auto *const tracker = wsw::ui::NameChangesTracker::instance();
		const auto playerNum = (unsigned)( configStringIndex - CS_PLAYERINFOS );
		// Consider this a full update for now
		tracker->registerNicknameUpdate( playerNum );
		tracker->registerClanUpdate( playerNum );
	} else if( (unsigned)( configStringIndex - CS_CALLVOTEINFOS ) < (unsigned)MAX_CALLVOTEINFOS ) {
		m_callvotesModel.handleConfigString( configStringIndex, string );
	}
}

void QtUISystem::updateScoreboard( const ReplicatedScoreboardData &scoreboardData ) {
	m_scoreboardModel.update( scoreboardData );
	m_playersModel.update( scoreboardData );
	m_hudDataModel.updateScoreboardData( scoreboardData );
}

bool QtUISystem::isShowingScoreboard() const {
	return m_isShowingScoreboard;
}

void QtUISystem::setScoreboardShown( bool shown ) {
	m_shouldShowScoreboard = shown;
}

void QtUISystem::toggleChatPopup() {
	m_shouldShowChatPopup = !m_shouldShowChatPopup;
}

void QtUISystem::toggleTeamChatPopup() {
	if( m_hasTeamChat ) {
		m_shouldShowTeamChatPopup = !m_shouldShowTeamChatPopup;
	} else {
		m_shouldShowTeamChatPopup = false;
		m_shouldShowChatPopup = !m_shouldShowChatPopup;
	}
}

void QtUISystem::touchActionRequest( const wsw::StringView &tag, unsigned int timeout,
									 const wsw::StringView &title, const wsw::StringView &actionDesc,
									 const std::pair<wsw::StringView, int> *actionsBegin,
									 const std::pair<wsw::StringView, int> *actionsEnd ) {
	m_actionRequestsModel.touch( tag, timeout, title, actionDesc, actionsBegin, actionsEnd );
}

void QtUISystem::handleOptionsStatusCommand( const wsw::StringView &status ) {
	m_gametypeOptionsModel.handleOptionsStatusCommand( status );
}

void QtUISystem::resetObituaries() {
	m_hudDataModel.resetObituaries();
}

void QtUISystem::addObituary( const wsw::StringView &victim, unsigned meansOfDeath,
							  const std::optional<wsw::StringView> &maybeAttacker ) {
	m_hudDataModel.addObituary( victim, getFrameTimestamp(), meansOfDeath, maybeAttacker );
}

void QtUISystem::addToMessageFeed( const wsw::StringView &message ) {
	m_hudDataModel.addToMessageFeed( message, getFrameTimestamp() );
}

void QtUISystem::notifyOfFailedConnection( const wsw::StringView &message, ConnectionFailKind kind ) {
	m_connectionFailMessage.clear();
	// TODO: Add proper overloads that avoid duplicated conversions
	m_connectionFailMessage = toStyledText( message ).toUtf8();
	m_pendingConnectionFailKind = kind;
}

template <typename Value>
void QtUISystem::appendSetCVarCommand( const wsw::StringView &name, const Value &value ) {
	wsw::StaticString<256> command;
	assert( !name.contains( ' ' ) && !name.contains( '\t' ) );
	command << "set "_asView << name << " \""_asView << value << "\";"_asView;
	Com_Printf( "%s\n", command.data() );
	Cbuf_ExecuteText( EXEC_APPEND, command.data() );
}

void QtUISystem::connectToAddress( const QByteArray &address ) {
	wsw::StaticString<256> command;
	command << "connect "_asView << wsw::StringView( address.data(), (unsigned)address.size() );
	Cbuf_ExecuteText( EXEC_APPEND, command.data() );
}

void QtUISystem::reconnectWithPassword( const QByteArray &password ) {
	wsw::StaticString<256> command;
	appendSetCVarCommand( "password"_asView, wsw::StringView( password.data(), password.size() ) );
	Cbuf_ExecuteText( EXEC_APPEND, "reconnect" );
	m_clearFailedConnectionState = true;
}

void QtUISystem::reconnect() {
	// TODO: Check whether we actually can reconnect
	Cbuf_ExecuteText( EXEC_APPEND, "reconnect" );
	// Protect from sticking in this state
	m_clearFailedConnectionState = true;
}

void QtUISystem::launchLocalServer( const QByteArray &gametype, const QByteArray &map, int flags, int numBots ) {
	appendSetCVarCommand( "g_gametype"_asView, gametype );
	appendSetCVarCommand( "g_instagib"_asView, ( flags & LocalServerInsta ) ? 1 : 0 );
	appendSetCVarCommand( "g_numbots"_asView, numBots );
	appendSetCVarCommand( "sv_public"_asView, ( flags & LocalServerPublic ) ? 1 : 0 );

	wsw::StaticString<256> command;
	command << "map "_asView << map;
	Cbuf_ExecuteText( EXEC_APPEND, command.data() );
}

void QtUISystem::sendChatMessage( const QString &text, bool team ) {
	// TODO: This is quite inefficient
	// TODO: Must be unicode-aware
	const QString clearText( text.trimmed().replace( '\r', ' ' ).replace( '\n', ' ' ).constData() );
	if( !clearText.isEmpty() ) {
		Con_SendChatMessage( clearText.toUtf8().constData(), team );
	}
}

bool QtUISystem::isShown() const {
	if( m_isValidAndReady ) {
		return ( m_activeMenuMask || m_isShowingScoreboard || m_isShowingChatPopup || m_isShowingTeamChatPopup );
	}
	return false;
}

}

bool CG_IsScoreboardShown() {
	return wsw::ui::UISystem::instance()->isShowingScoreboard();
}

void CG_ScoresOn_f() {
	wsw::ui::UISystem::instance()->setScoreboardShown( true );
}

void CG_ScoresOff_f() {
	wsw::ui::UISystem::instance()->setScoreboardShown( false );
}

void CG_MessageMode() {
	wsw::ui::UISystem::instance()->toggleChatPopup();
	CL_ClearInputState();
}

void CG_MessageMode2() {
	wsw::ui::UISystem::instance()->toggleTeamChatPopup();
	CL_ClearInputState();
}

#include "uisystem.moc"