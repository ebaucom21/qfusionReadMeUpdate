#include "../bot.h"
#include "../combat/tacticalspotsregistry.h"
#include "../combat/advantageproblemsolver.h"
#include "../combat/coverproblemsolver.h"

inline const AiAasRouteCache *BotTacticalSpotsCache::RouteCache() {
	return bot->RouteCache();
}

inline float BotTacticalSpotsCache::Skill() const {
	return bot->Skill();
}

inline bool BotTacticalSpotsCache::BotHasAlmostSameOrigin( const Vec3 &unpackedOrigin ) const {
	constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	return unpackedOrigin.SquareDistanceTo( bot->Origin() ) <= squareDistanceError;
}

template <typename ProblemParams>
inline bool BotTacticalSpotsCache::FindForOrigin( const ProblemParams &problemParams,
												  const Vec3 &origin, float searchRadius, vec3_t result ) {
	if( BotHasAlmostSameOrigin( origin ) ) {
		// Provide a bot entity to aid trace checks
		AdvantageProblemSolver::OriginParams originParams( game.edicts + bot->EntNum(), searchRadius, RouteCache() );
		return AdvantageProblemSolver( originParams, problemParams ).findSingle( result );
	}
	TacticalSpotsRegistry::OriginParams originParams( origin.Data(), searchRadius, RouteCache() );
	return AdvantageProblemSolver( originParams, problemParams ).findSingle( result );
}

const short *BotTacticalSpotsCache::GetSingleOriginSpot( SingleOriginSpotsCache *cache, const short *origin,
														 const short *enemyOrigin, SingleOriginFindMethod findMethod ) {
	short *cachedSpot;
	if( cache->TryGetCachedSpot( origin, enemyOrigin, &cachedSpot ) ) {
		return cachedSpot;
	}

	CachedSpot<short[3]> *newSpot = cache->Alloc();
	// Can't allocate a spot. It also means a limit of such tactical spots per think frame has been exceeded.
	if( !newSpot ) {
		return nullptr;
	}

	VectorCopy( origin, newSpot->origin );
	VectorCopy( enemyOrigin, newSpot->enemyOrigin );

	vec3_t foundSpotOrigin;
	if( !( this->*findMethod )( GetUnpacked4uVec( origin ), GetUnpacked4uVec( enemyOrigin ), foundSpotOrigin ) ) {
		newSpot->succeeded = false;
		return nullptr;
	}

	for( unsigned i = 0; i < 3; ++i )
		newSpot->spotData[i] = (short)( ( (int)foundSpotOrigin[i] ) / 4 );

	newSpot->succeeded = true;
	return newSpot->spotData;
}

const short *BotTacticalSpotsCache::GetDualOriginSpot( DualOriginSpotsCache *cache, const short *origin,
													   const short *enemyOrigin, DualOriginFindMethod findMethod ) {
	short *cachedSpot;
	if( cache->TryGetCachedSpot( origin, enemyOrigin, &cachedSpot ) ) {
		return cachedSpot;
	}

	CachedSpot<short[6]> *newSpot = cache->Alloc();
	// Can't allocate a spot. It also means a limit of such tactical spots per think frame has been exceeded.
	if( !newSpot ) {
		return nullptr;
	}

	VectorCopy( origin, newSpot->origin );
	VectorCopy( enemyOrigin, newSpot->enemyOrigin );

	vec3_t foundOrigins[2];
	if( !( this->*findMethod )( GetUnpacked4uVec( origin ), GetUnpacked4uVec( enemyOrigin ), foundOrigins ) ) {
		newSpot->succeeded = false;
		return nullptr;
	}

	for( unsigned i = 0; i < 3; ++i ) {
		newSpot->spotData[i + 0] = (short)( ( ( foundOrigins[0][i] ) / 4 ) );
		newSpot->spotData[i + 3] = (short)( ( ( foundOrigins[1][i] ) / 4 ) );
	}

	newSpot->succeeded = true;
	return newSpot->spotData;
}

template <typename ProblemParams>
inline void BotTacticalSpotsCache::TakeEnemiesIntoAccount( ProblemParams &problemParams ) {
	if( Skill() < 0.33f ) {
		return;
	}
	const auto &selectedEnemies = bot->GetSelectedEnemies();
	if( !selectedEnemies.AreValid() ) {
		return;
	}
	// TODO: Provide PrimaryEnemy() getter?
	assert( selectedEnemies.IsPrimaryEnemy( *selectedEnemies.begin() ) );
	problemParams.setImpactfulEnemies( bot->TrackedEnemiesHead(), *selectedEnemies.begin() );
}

bool BotTacticalSpotsCache::FindSniperRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.setMinSpotDistanceToEntity( WorldState::FAR_RANGE_MAX );
	problemParams.setMinHeightAdvantageOverOrigin( std::numeric_limits<float>::min() );
	problemParams.setMinHeightAdvantageOverEntity( std::numeric_limits<float>::min() );
	problemParams.setCheckToAndBackReach( false );
	problemParams.setMaxFeasibleTravelTimeMillis( 12500 );
	problemParams.setOptimizeAggressively( true );
	TakeEnemiesIntoAccount( problemParams );

	return FindForOrigin( problemParams, origin, std::numeric_limits<float>::max(), result );
}

bool BotTacticalSpotsCache::FindFarRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.setMinSpotDistanceToEntity( WorldState::MIDDLE_RANGE_MAX );
	problemParams.setMaxSpotDistanceToEntity( WorldState::FAR_RANGE_MAX );
	problemParams.setEntityWeightFalloffDistanceRatio( 0.25f );
	problemParams.setMinHeightAdvantageOverOrigin( -256.0f );
	problemParams.setMinHeightAdvantageOverEntity( -256.0f );
	problemParams.setCheckToAndBackReach( false );
	problemParams.setMaxFeasibleTravelTimeMillis( 7500 );
	problemParams.setOptimizeAggressively( true );
	TakeEnemiesIntoAccount( problemParams );

	return FindForOrigin( problemParams, origin, std::numeric_limits<float>::max(), result );
}

bool BotTacticalSpotsCache::FindMiddleRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.setMinSpotDistanceToEntity( WorldState::CLOSE_RANGE_MAX );
	problemParams.setMaxSpotDistanceToEntity( WorldState::MIDDLE_RANGE_MAX );
	problemParams.setEntityWeightFalloffDistanceRatio( 0.5f );
	problemParams.setMinHeightAdvantageOverOrigin( -64.0f );
	problemParams.setMinHeightAdvantageOverEntity( +16.0f );
	problemParams.setCheckToAndBackReach( false );
	problemParams.setMaxFeasibleTravelTimeMillis( 4000 );
	TakeEnemiesIntoAccount( problemParams );

	return FindForOrigin( problemParams, origin, std::numeric_limits<float>::max(), result );
}

bool BotTacticalSpotsCache::FindCloseRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	float meleeRange = GS_GetWeaponDef( WEAP_GUNBLADE )->firedef_weak.timeout;
	problemParams.setMinSpotDistanceToEntity( meleeRange );
	problemParams.setMaxSpotDistanceToEntity( WorldState::CLOSE_RANGE_MAX );
	problemParams.setEntityWeightFalloffDistanceRatio( 0.8f );
	problemParams.setMinHeightAdvantageOverOrigin( -64.0f );
	problemParams.setMinHeightAdvantageOverEntity( +16.0f );
	// Bot should be able to retreat from close combat
	problemParams.setCheckToAndBackReach( true );
	problemParams.setMaxFeasibleTravelTimeMillis( 2000 );
	TakeEnemiesIntoAccount( problemParams );

	return FindForOrigin( problemParams, origin, std::numeric_limits<float>::max(), result );
}

bool BotTacticalSpotsCache::FindCoverSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	const float searchRadius = 192.0f + 512.0f * Skill();
	CoverProblemSolver::ProblemParams problemParams( enemyOrigin.Data(), 32.0f );
	problemParams.setMinHeightAdvantageOverOrigin( -searchRadius );
	problemParams.setCheckToAndBackReach( false );
	problemParams.setMaxFeasibleTravelTimeMillis( 1250 );
	TakeEnemiesIntoAccount( problemParams );

	if( BotHasAlmostSameOrigin( origin ) ) {
		TacticalSpotsRegistry::OriginParams originParams( game.edicts + bot->EntNum(), searchRadius, RouteCache() );
		return CoverProblemSolver( originParams, problemParams ).findSingle( result );
	}
	TacticalSpotsRegistry::OriginParams originParams( origin.Data(), searchRadius, RouteCache() );
	return CoverProblemSolver( originParams, problemParams ).findSingle( result );
}

const BotTacticalSpotsCache::NearbyEntitiesCache::NearbyEntitiesCacheEntry
*BotTacticalSpotsCache::NearbyEntitiesCache::TryGetCachedEntities( const Vec3 &origin, float radius ) {
	for( unsigned i = 0; i < numEntries; ++i ) {
		NearbyEntitiesCacheEntry &entry = entries[i];
		if( !VectorCompare( origin.Data(), entry.botOrigin ) ) {
			continue;
		}
		if( radius != entry.radius ) {
			continue;
		}
		return &entry;
	}

	return nullptr;
}

int BotTacticalSpotsCache::FindNearbyEntities( const Vec3 &origin, float radius, int **entNums ) {
	if( const auto *nearbyEntitiesCacheEntry = nearbyEntitiesCache.TryGetCachedEntities( origin, radius ) ) {
		*entNums = (int *)nearbyEntitiesCacheEntry->entNums;
		return nearbyEntitiesCacheEntry->numEntities;
	}

	auto *nearbyEntitiesCacheEntry = nearbyEntitiesCache.Alloc();
	if( !nearbyEntitiesCacheEntry ) {
		return 0;
	}

	VectorCopy( origin.Data(), nearbyEntitiesCacheEntry->botOrigin );
	nearbyEntitiesCacheEntry->radius = radius;

	constexpr int maxCachedEntities = NearbyEntitiesCache::MAX_CACHED_NEARBY_ENTITIES;
	// Find more than maxCachedEntities entities in radius (most entities will usually be filtered out)
	constexpr int maxRadiusEntities = 2 * maxCachedEntities;

	int radiusEntNums[maxRadiusEntities];
	// Note that this value might be greater than maxRadiusEntities (an actual number of entities is returned)
	int numRadiusEntities = GClip_FindInRadius( const_cast<float *>( origin.Data() ), radius, radiusEntNums, maxRadiusEntities );

	int numEntities = 0;
	// Copy to locals for faster access (a compiler might be paranoid about aliasing)
	edict_t *gameEdicts = game.edicts;
	int *triggerEntNums = nearbyEntitiesCacheEntry->entNums;

	if( numRadiusEntities <= maxCachedEntities ) {
		// In this case we can avoid buffer capacity checks on each step
		for( int i = 0, end = std::min( numRadiusEntities, maxRadiusEntities ); i < end; ++i ) {
			edict_t *ent = gameEdicts + radiusEntNums[i];
			if( !ent->r.inuse ) {
				continue;
			}
			if( !ent->classname ) {
				continue;
			}

			triggerEntNums[numEntities++] = radiusEntNums[i];
		}
	} else {
		for( int i = 0, end = std::min( numRadiusEntities, maxRadiusEntities ); i < end; ++i ) {
			edict_t *ent = game.edicts + radiusEntNums[i];
			if( !ent->r.inuse ) {
				continue;
			}
			if( !ent->classname ) {
				continue;
			}

			triggerEntNums[numEntities++] = radiusEntNums[i];
			if( numEntities == maxCachedEntities ) {
				break;
			}
		}
	}

	nearbyEntitiesCacheEntry->numEntities = numEntities;
	*entNums = (int *)nearbyEntitiesCacheEntry->entNums;
	return numEntities;
}

void BotTacticalSpotsCache::FindReachableClassEntities( const Vec3 &origin, float radius, const char *classname,
														BotTacticalSpotsCache::ReachableEntities &result ) {
	int *triggerEntities;
	int numEntities = FindNearbyEntities( origin, radius, &triggerEntities );

	ReachableEntities candidateEntities;
	// Copy to locals for faster access (a compiler might be paranoid about aliasing)
	edict_t *gameEdicts = game.edicts;

	if( numEntities > (int)candidateEntities.capacity() ) {
		for( int i = 0; i < numEntities; ++i ) {
			edict_t *ent = gameEdicts + triggerEntities[i];
			// Specify expected strcmp() result explicitly to avoid misinterpreting the condition
			// (Strings are equal if an strcmp() result is zero)
			if( strcmp( ent->classname, classname ) != 0 ) {
				continue;
			}
			float distance = DistanceFast( origin.Data(), ent->s.origin );
			candidateEntities.push_back( EntAndScore( triggerEntities[i], radius - distance ) );
			if( candidateEntities.full() ) {
				break;
			}
		}
	} else {
		for( int i = 0; i < numEntities; ++i ) {
			edict_t *ent = gameEdicts + triggerEntities[i];
			if( strcmp( ent->classname, classname ) != 0 ) {
				continue;
			}
			float distance = DistanceFast( origin.Data(), ent->s.origin );
			candidateEntities.push_back( EntAndScore( triggerEntities[i], radius - distance ) );
		}
	}

	const auto *aasWorld = AiAasWorld::instance();
	const auto *routeCache = RouteCache();

	int fromAreaNums[2] { 0, 0 };
	int numFromAreas;
	// If an origin matches actual bot origin
	if( BotHasAlmostSameOrigin( origin ) ) {
		numFromAreas = bot->EntityPhysicsState()->PrepareRoutingStartAreas( fromAreaNums );
	} else {
		fromAreaNums[0] = aasWorld->findAreaNum( origin );
		numFromAreas = fromAreaNums[0] ? 1 : 0;
	}

	for( EntAndScore &candidate: candidateEntities ) {
		const edict_t *ent = gameEdicts + candidate.entNum;

		const int toAreaNum = FindMostFeasibleEntityAasArea( ent, aasWorld );
		if( !toAreaNum ) {
			continue;
		}

		const int travelTime = routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum );
		if( !travelTime ) {
			continue;
		}

		// AAS travel time is in seconds^-2
		const float factor = Q_Sqrt( 1.01f - std::min( travelTime, 200 ) * Q_Rcp( 200 ) );
		result.push_back( EntAndScore( candidate.entNum, candidate.score * factor ) );
	}

	// Sort entities so best entities are first
	std::sort( result.begin(), result.end() );
}

int BotTacticalSpotsCache::FindMostFeasibleEntityAasArea( const edict_t *ent, const AiAasWorld *aasWorld ) const {
	int areaNums[24];
	const Vec3 boxMins( Vec3( -20, -20, -12 ) + ent->r.absmin );
	const Vec3 boxMaxs( Vec3( +20, +20, +12 ) + ent->r.absmax );
	int numAreas = aasWorld->findAreasInBox( boxMins.Data(), boxMaxs.Data(), areaNums, 24 );

	const auto *aasAreaSettings = aasWorld->AreaSettings();
	for( int i = 0; i < numAreas; ++i ) {
		int areaFlags = aasAreaSettings[areaNums[i]].areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & AREA_DISABLED ) {
			continue;
		}
		return areaNums[i];
	}
	return 0;
}

bool BotTacticalSpotsCache::FindRunAwayTeleportOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "trigger_teleport", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = game.edicts + entAndScore.entNum;
		if( !ent->target ) {
			continue;
		}
		edict_t *dest = G_Find( NULL, FOFS( targetname ), ent->target );
		if( !dest ) {
			continue;
		}

		if( !pvsCache->AreInPvs( enemyEnt, dest ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, dest->s.origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Copy trigger destination
		VectorCopy( dest->s.origin, result[1] );
		return true;
	}

	return false;
}

bool BotTacticalSpotsCache::FindRunAwayJumppadOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "trigger_push", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = game.edicts + entAndScore.entNum;
		if( !pvsCache->AreInPvs( enemyEnt, ent ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, ent->target_ent->s.origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Copy trigger destination
		VectorCopy( ent->target_ent->s.origin, result[1] );
		return true;
	}

	return false;
}

bool BotTacticalSpotsCache::FindRunAwayElevatorOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "func_plat", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	edict_t *gameEdicts = game.edicts;
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = gameEdicts + entAndScore.entNum;
		// Can't run away via elevator if the elevator has been always activated
		if( ent->moveinfo.state != STATE_BOTTOM ) {
			continue;
		}

		if( !pvsCache->AreInPvs( enemyEnt, ent ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, ent->moveinfo.end_origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Drop origin to the elevator bottom
		result[0][2] = ent->r.absmin[2] + 16;
		// Copy trigger destination
		VectorCopy( ent->moveinfo.end_origin, result[1] );
		return true;
	}

	return false;
}