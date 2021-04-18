#ifndef WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H
#define WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H

#include <QString>
#include <array>

namespace wsw { class StringView; }

namespace wsw::ui {

[[nodiscard]]
auto toStyledText( const wsw::StringView &text ) -> QString;

[[nodiscard]]
auto formatPing( int ping ) -> QByteArray;

class NameChangesTracker {
	std::array<unsigned, 32> m_nameCounters;
	std::array<unsigned, 32> m_clanCounters;

	static NameChangesTracker s_instance;
public:
	NameChangesTracker() noexcept {
		m_nameCounters.fill( 0 );
		m_clanCounters.fill( 0 );
	}

	[[nodiscard]]
	static auto instance() -> NameChangesTracker * { return &s_instance; }

	[[nodiscard]]
	auto getLastNicknameUpdateCounter( unsigned playerNum ) const -> unsigned { return m_nameCounters[playerNum]; }
	[[nodiscard]]
	auto getLastClanUpdateCounter( unsigned playerNum ) const -> unsigned { return m_clanCounters[playerNum]; }

	void registerNicknameUpdate( unsigned playerNum ) { m_nameCounters[playerNum]++; }
	void registerClanUpdate( unsigned playerNum ) { m_clanCounters[playerNum]++; }
};

inline NameChangesTracker NameChangesTracker::s_instance;

}

#endif