#ifndef WSW_65a80be2_b9c9_4018_9d25_db1f7e268aef_H
#define WSW_65a80be2_b9c9_4018_9d25_db1f7e268aef_H

#include "../ailocal.h"

class BotTacticalSpotsCache {
	template <typename SpotData>
	struct CachedSpot {
		short origin[3];
		short enemyOrigin[3];
		SpotData spotData;
		bool succeeded;
	};

	// Use just a plain array for caching spots.
	// High number of distinct (and thus searched for) spots will kill TacticalSpotsRegistry performance first.
	template <typename SpotData>
	struct SpotsCache {
		static constexpr auto MAX_SPOTS = 3;
		CachedSpot<SpotData> spots[MAX_SPOTS];
		unsigned numSpots;

		SpotsCache() { Clear(); }

		void Clear() {
			numSpots = 0;
			memset( spots, 0, sizeof( spots ) );
		}

		CachedSpot<SpotData> *Alloc() {
			if( numSpots == MAX_SPOTS ) {
				return nullptr;
			}
			return &spots[numSpots++];
		}

		bool TryGetCachedSpot( const short *origin, const short *enemyOrigin, short **result ) const {
			for( unsigned i = 0, end = numSpots; i < end; ++i ) {
				const CachedSpot<SpotData> &spot = spots[i];
				if( !VectorCompare( origin, spot.origin ) ) {
					continue;
				}
				if( !VectorCompare( enemyOrigin, spot.enemyOrigin ) ) {
					continue;
				}
				*result = spot.succeeded ? (short *)spot.spotData : nullptr;
				return true;
			}
			return false;
		}
	};

	typedef SpotsCache<short[3]> SingleOriginSpotsCache;
	SingleOriginSpotsCache sniperRangeTacticalSpotsCache;
	SingleOriginSpotsCache farRangeTacticalSpotsCache;
	SingleOriginSpotsCache middleRangeTacticalSpotsCache;
	SingleOriginSpotsCache closeRangeTacticalSpotsCache;
	SingleOriginSpotsCache coverSpotsTacticalSpotsCache;

	typedef SpotsCache<short[6]> DualOriginSpotsCache;
	DualOriginSpotsCache runAwayTeleportOriginsCache;
	DualOriginSpotsCache runAwayJumppadOriginsCache;
	DualOriginSpotsCache runAwayElevatorOriginsCache;

	Bot *const bot;

	bool FindSniperRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindFarRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindMiddleRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindCloseRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );
	bool FindCoverSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result );

	template <typename ProblemParams>
	inline void TakeEnemiesIntoAccount( ProblemParams &problemParams );

	// We can't(?) refer to a nested class in a forward declaration, so declare the parameter as a template one
	template <typename ProblemParams>
	inline bool FindForOrigin( const ProblemParams &problemParams, const Vec3 &origin, float searchRadius, vec3_t result );

	bool FindRunAwayTeleportOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );
	bool FindRunAwayJumppadOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );
	bool FindRunAwayElevatorOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] );

	using ReachableEntities = wsw::StaticVector<EntAndScore, 16>;
	void FindReachableClassEntities( const Vec3 &origin, float radius, const char *classname, ReachableEntities &result );

	// AiAasWorld::findAreaNum() fails so often for teleports/elevators, etc, so we have to use this method.
	// AiAasWorld is provided as an argument to avoid an implicit retrieval of global instance in a loop.
	int FindMostFeasibleEntityAasArea( const edict_t *ent, const AiAasWorld *aasWorld ) const;

	class NearbyEntitiesCache {
public:
		static constexpr unsigned MAX_CACHED_NEARBY_ENTITIES = 32;
		struct NearbyEntitiesCacheEntry {
			int entNums[MAX_CACHED_NEARBY_ENTITIES];
			int numEntities { 0 };
			vec3_t botOrigin;
			float radius { 0 };
		};

private:
		static constexpr unsigned MAX_ENTITIES_CACHE_ENTRIES = 4;
		NearbyEntitiesCacheEntry entries[MAX_ENTITIES_CACHE_ENTRIES];
		unsigned numEntries { 0 };
public:
		void Clear() { numEntries = 0; }
		NearbyEntitiesCacheEntry *Alloc() {
			if( numEntries == MAX_ENTITIES_CACHE_ENTRIES ) {
				return nullptr;
			}
			return &entries[numEntries++];
		}
		const NearbyEntitiesCacheEntry *TryGetCachedEntities( const Vec3 &origin, float radius );
	};

	NearbyEntitiesCache nearbyEntitiesCache;

	int FindNearbyEntities( const Vec3 &origin, float radius, int **entNums );

	// These functions are extracted to be able to mock a bot entity
	// by a player entity easily for testing and tweaking the cache
	inline const class AiAasRouteCache *RouteCache();
	inline float Skill() const;
	inline bool BotHasAlmostSameOrigin( const Vec3 &unpackedOrigin ) const;

	typedef bool (BotTacticalSpotsCache::*SingleOriginFindMethod)( const Vec3 &, const Vec3 &, vec3_t );
	const short *GetSingleOriginSpot( SingleOriginSpotsCache *cachedSpots, const short *origin,
									  const short *enemyOrigin, SingleOriginFindMethod findMethod );

	typedef bool (BotTacticalSpotsCache::*DualOriginFindMethod)( const Vec3 &, const Vec3 &, vec3_t[2] );
	const short *GetDualOriginSpot( DualOriginSpotsCache *cachedSpots, const short *origin,
									const short *enemyOrigin, DualOriginFindMethod findMethod );

public:
	explicit BotTacticalSpotsCache( Bot *bot_ ) : bot( bot_ ) {}

	inline void Clear() {
		sniperRangeTacticalSpotsCache.Clear();
		farRangeTacticalSpotsCache.Clear();
		middleRangeTacticalSpotsCache.Clear();
		closeRangeTacticalSpotsCache.Clear();
		coverSpotsTacticalSpotsCache.Clear();

		nearbyEntitiesCache.Clear();

		runAwayTeleportOriginsCache.Clear();
		runAwayJumppadOriginsCache.Clear();
		runAwayElevatorOriginsCache.Clear();
	}

	const short *GetSniperRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &sniperRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindSniperRangeTacticalSpot );
	}

	const short *GetFarRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &farRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindFarRangeTacticalSpot );
	}

	const short *GetMiddleRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &middleRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindMiddleRangeTacticalSpot );
	}

	const short *GetCloseRangeTacticalSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &closeRangeTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindCloseRangeTacticalSpot );
	}
	inline const short *GetCoverSpot( const short *origin, const short *enemyOrigin ) {
		return GetSingleOriginSpot( &coverSpotsTacticalSpotsCache, origin, enemyOrigin,
									&BotTacticalSpotsCache::FindCoverSpot );
	}

	const short *GetRunAwayTeleportOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayTeleportOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayTeleportOrigin );
	}

	const short *GetRunAwayJumppadOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayJumppadOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayJumppadOrigin );
	}

	const short *GetRunAwayElevatorOrigin( const short *origin, const short *enemyOrigin ) {
		return GetDualOriginSpot( &runAwayElevatorOriginsCache, origin, enemyOrigin,
								  &BotTacticalSpotsCache::FindRunAwayElevatorOrigin );
	}
};

#endif
