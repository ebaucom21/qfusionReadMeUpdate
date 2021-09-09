#include "nearbytriggerscache.h"
#include "../classifiedentitiescache.h"
#include "../ailocal.h"

namespace wsw::ai::movement {

NearbyTriggersCache::NearbyTriggersCache() {
	triggerTravelFlags[0] = TRAVEL_JUMPPAD;
	triggerTravelFlags[1] = TRAVEL_TELEPORT;
	triggerTravelFlags[2] = TRAVEL_ELEVATOR;

	triggerNumEnts[0] = &numJumppadEnts;
	triggerNumEnts[1] = &numTeleportEnts;
	triggerNumEnts[2] = &numPlatformEnts;

	triggerEntNums[0] = &jumppadEntNums[0];
	triggerEntNums[1] = &teleportEntNums[0];
	triggerEntNums[2] = &platformEntNums[0];
}

auto NearbyTriggersCache::clipToRegion( const ArrayRange<uint16_t> &entNums, uint16_t *dest, size_t destSize ) -> unsigned {
	const auto *__restrict regionMins = lastComputedForMins;
	const auto *__restrict regionMaxs = lastComputedForMaxs;
	const auto *__restrict gameEnts = game.edicts;

	size_t numDestEnts = 0;
	if( entNums.size() <= destSize ) {
		for( auto num: entNums ) {
			const auto *__restrict ent = gameEnts + num;
			if( BoundsIntersect( regionMins, regionMaxs, ent->r.absmin, ent->r.absmax ) ) {
				dest[numDestEnts++] = num;
			}
		}
	} else {
		for( auto num: entNums ) {
			const auto *__restrict ent = gameEnts + num;
			if( BoundsIntersect( regionMins, regionMaxs, ent->r.absmin, ent->r.absmax ) ) {
				dest[numDestEnts++] = num;
				if( numDestEnts == destSize ) {
					break;
				}
			}
		}
	}

	return numDestEnts;
}

void NearbyTriggersCache::ensureValidForBounds( const float *__restrict absMins,
												const float *__restrict absMaxs ) {
	// TODO: Simd ("withinBoundsWithDelta") this
	int i;
	for( i = 0; i < 3; ++i ) {
		if( lastComputedForMins[i] + 192.0f > absMins[i] ) {
			break;
		}
		if( lastComputedForMaxs[i] - 192.0f < absMaxs[i] ) {
			break;
		}
	}

	// If all coords have passed tests
	if( i == 3 ) {
		return;
	}

	VectorSet( lastComputedForMins, -256, -256, -256 );
	VectorSet( lastComputedForMaxs, +256, +256, +256 );
	VectorAdd( absMins, lastComputedForMins, lastComputedForMins );
	VectorAdd( absMaxs, lastComputedForMaxs, lastComputedForMaxs );

	const auto *const __restrict cache = wsw::ai::ClassifiedEntitiesCache::instance();
	numTeleportEnts = clipToRegion( cache->getAllPersistentMapTeleporters(), teleportEntNums, kMaxClassEnts );
	numJumppadEnts = clipToRegion( cache->getAllPersistentMapJumppads(), jumppadEntNums, kMaxClassEnts );
	numPlatformEnts = clipToRegion( cache->getAllPersistentMapPlatforms(), platformEntNums, kMaxClassEnts );
	numOtherEnts = clipToRegion( cache->getAllOtherTriggersInThisFrame(), otherEntNums, kMaxOtherEnts );
}

}