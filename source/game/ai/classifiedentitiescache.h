#ifndef WSW_367671cf_02ef_4de7_9728_36bbe2aeab04_H
#define WSW_367671cf_02ef_4de7_9728_36bbe2aeab04_H

#include "../../qcommon/wswstaticvector.h"
#include "ailocal.h"

template <typename> class SingletonHolder;

namespace wsw::ai {

class ClassifiedEntitiesCache {
	template <typename> friend class SingletonHolder;
public:
	static constexpr unsigned kMaxClassTriggerEnts = 32;
private:
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allTeleporters;
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allJumppads;
	wsw::StaticVector<uint16_t, kMaxClassTriggerEnts> m_allPlatforms;
	wsw::StaticVector<uint16_t, MAX_EDICTS> m_allOtherTriggers;
	wsw::BitSet<MAX_EDICTS> m_persistentEntitiesMask;
	bool m_hasRetrievedPersistentEntities { false };

	void retrievePersistentEntities();
public:
	static void init();
	static void shutdown();
	[[nodiscard]]
	static auto instance() -> ClassifiedEntitiesCache *;

	void update();

	[[nodiscard]]
	auto getAllPersistentMapTeleporters() const -> std::span<const uint16_t> {
		return { m_allTeleporters.begin(), m_allTeleporters.end() };
	}
	[[nodiscard]]
	auto getAllPersistentMapJumppads() const -> std::span<const uint16_t> {
		return { m_allJumppads.begin(), m_allJumppads.end() };
	}
	[[nodiscard]]
	auto getAllPersistentMapPlatforms() const -> std::span<const uint16_t> {
		return { m_allPlatforms.begin(), m_allPlatforms.end() };
	}
	[[nodiscard]]
	auto getAllOtherTriggersInThisFrame() const -> std::span<const uint16_t> {
		return { m_allOtherTriggers.begin(), m_allOtherTriggers.end() };
	}
};

}

#endif