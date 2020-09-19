#ifndef WSW_0646db93_a5f6_4b71_9267_800aa42abe4b_H
#define WSW_0646db93_a5f6_4b71_9267_800aa42abe4b_H

#include <optional>

struct ReplicatedScoreboardData;

namespace wsw { class StringView; }

namespace wsw::ui {

class UISystem {
public:
	virtual ~UISystem() = default;

	static void init( int width, int height );
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> UISystem *;

	[[nodiscard]]
	static auto maybeInstance() -> std::optional<UISystem *>;

	enum RefreshFlags : unsigned {
		UseOwnBackground = 0x1u,
		ShowCursor = 0x2u,
	};

	virtual void refresh( unsigned refreshFlags ) = 0;

	virtual void drawSelfInMainContext() = 0;

	virtual void beginRegistration() = 0;
	virtual void endRegistration() = 0;

	[[nodiscard]]
	virtual bool requestsKeyboardFocus() const = 0;
	[[nodiscard]]
	virtual bool handleKeyEvent( int quakeKey, bool keyDown ) = 0;
	[[nodiscard]]
	virtual bool handleCharEvent( int ch ) = 0;
	[[nodiscard]]
	virtual bool handleMouseMove( int frameTime, int dx, int dy ) = 0;

	virtual void forceMenuOn() = 0;
	virtual void forceMenuOff() = 0;

	virtual void toggleInGameMenu() = 0;

	virtual void addToChat( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) = 0;
	virtual void addToTeamChat( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) = 0;

	virtual void handleConfigString( unsigned configStringNum, const wsw::StringView &string ) = 0;

	virtual void updateScoreboard( const ReplicatedScoreboardData &scoreboardData ) = 0;
};

}

#endif
