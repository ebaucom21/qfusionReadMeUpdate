#ifndef WSW_f7aab381_683d_4dce_ae3a_57a81e46af6b_H
#define WSW_f7aab381_683d_4dce_ae3a_57a81e46af6b_H

// TODO: Switch to using OpenAL SOFT headers across the entire codebase?
#include "../../third-party/openal-soft/include/AL/efx-presets.h"

namespace wsw { class StringView; }
namespace wsw { class HashedStringView; }

class EfxPresetsRegistry {
	friend struct EfxPresetEntry;
public:
	static EfxPresetsRegistry s_instance;

	[[nodiscard]]
	auto findByName( const wsw::StringView &name ) const -> const EFXEAXREVERBPROPERTIES *;
	[[nodiscard]]
	auto findByName( const wsw::HashedStringView &name ) const -> const EFXEAXREVERBPROPERTIES  *;
private:
	static constexpr auto kNumHashBins = 97;
	struct EfxPresetEntry *m_hashBins[kNumHashBins];
};

#endif
