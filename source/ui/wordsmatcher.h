#ifndef WSW_04250cc2_5bf4_40d6_8c1b_b5305692255f_H
#define WSW_04250cc2_5bf4_40d6_8c1b_b5305692255f_H

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstdtypes.h"

#include <functional>

namespace wsw::ui {

class WordsMatcher {
	// Use a holder to circumvent initialization order issues
	wsw::StaticVector<std::boyer_moore_searcher<const char *>, 1> m_exactMatcherHolder;
	wsw::Vector<unsigned> m_distanceBuffer;
	wsw::String m_stringDataBuffer;
	wsw::String m_inputBuffer;

	[[nodiscard]]
	auto matchByDistance( const wsw::StringView &input, const wsw::StringView &word, unsigned maxDist )
		-> std::optional<unsigned>;

	[[nodiscard]]
	auto prepareInput( const wsw::StringView &rawInput ) -> wsw::StringView;

	[[nodiscard]]
	auto distance( wsw::StringView a, wsw::StringView b, unsigned maxDist ) -> std::optional<unsigned>;

public:
	explicit WordsMatcher( const wsw::StringView &word );

	[[nodiscard]]
	auto match( const wsw::StringView &input, unsigned maxDist ) -> std::optional<unsigned>;
};

}

#endif