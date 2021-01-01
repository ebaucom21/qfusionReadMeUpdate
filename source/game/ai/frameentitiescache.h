#ifndef WSW_367671cf_02ef_4de7_9728_36bbe2aeab04_H
#define WSW_367671cf_02ef_4de7_9728_36bbe2aeab04_H

#include "../../qcommon/wswstaticvector.h"
#include "ai_local.h"

template <typename> class SingletonHolder;

namespace wsw::ai {

class FrameEntitiesCache {
	template <typename> friend class SingletonHolder;
public:
	static constexpr unsigned kMaxClassTriggerEnts = 32;
private:
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allTeleports;
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allJumppads;
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allPlatforms;
	wsw::StaticVector<uint16_t, MAX_EDICTS> m_allOtherTriggers;
public:
	static void init();
	static void shutdown();
	[[nodiscard]]
	static auto instance() -> FrameEntitiesCache *;

	void update();

	// TODO: Use std::span

	[[nodiscard]]
	auto getAllTeleports() const -> ArrayRange<uint16_t> {
		return ArrayRange( m_allTeleports.begin(), m_allTeleports.end() );
	}
	[[nodiscard]]
	auto getAllJumppads() const -> ArrayRange<uint16_t> {
		return ArrayRange( m_allJumppads.begin(), m_allJumppads.end() );
	}
	[[nodiscard]]
	auto getAllPlatforms() const -> ArrayRange<uint16_t> {
		return ArrayRange( m_allPlatforms.begin(), m_allPlatforms.end() );
	}
	[[nodiscard]]
	auto getAllOtherTriggers() const -> ArrayRange<uint16_t> {
		return ArrayRange( m_allOtherTriggers.begin(), m_allOtherTriggers.end() );
	}
};

}

#endif