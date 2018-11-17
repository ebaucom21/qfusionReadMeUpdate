#include "FloorClusterAreasCache.h"
#include "MovementLocal.h"
#include "../combat/TacticalSpotsRegistry.h"

inline bool FloorClusterAreasCache::AreaPassesCollisionTest( Context *context, int areaNum ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 3.0f;
	}

	vec3_t mins, maxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( mins, maxs );
	return AreaPassesCollisionTest( start, areaNum, mins, maxs );
}

bool SameFloorClusterAreasCache::AreaPassesCollisionTest( const Vec3 &start,
														  int areaNum,
														  const vec3_t mins,
														  const vec3_t maxs ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 1.0f + ( -playerbox_stand_mins[2] );

	// We deliberately have to check against entities, like the tank on wbomb1 A spot, and not only solid world
	trace_t trace;
	float *start_ = const_cast<float *>( start.Data() );
	float *mins_ = const_cast<float *>( mins );
	float *maxs_ = const_cast<float *>( maxs );
	G_Trace( &trace, start_, mins_, maxs_, areaPoint.Data(), game.edicts + bot->EntNum(), MASK_AISOLID );
	return trace.fraction == 1.0f;
}

bool NextFloorClusterAreasCache::AreaPassesCollisionTest( const Vec3 &start,
														  int areaNum,
														  const vec3_t mins,
														  const vec3_t maxs ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 1.0f + ( -playerbox_stand_mins[2] );

	trace_t trace;
	// We cannot ensure walkability with the same degree of certainty as for the same floor cluster.
	// Use a cheaper collision test only against the solid world.
	SolidWorldTrace( &trace, start.Data(), areaPoint.Data(), mins, maxs );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	// Once again, there are no cheap ways to determine whether there is a gap in-between.
	// We are in different floor clusters.
	// This heuristic however should work well.
	// False positives are going to be cut off by movement prediction system.
	// There are no callers that use this cache without prediction.

	// Can't use the hack below if the tested area has greater elevation.
	if( start.Z() + playerbox_stand_mins[2] < area.mins[2] ) {
		return true;
	}

	areaPoint.Z() -= 96.0f;
	// Use a segment instead of a box but check for liquids
	StaticWorldTrace( &trace, start.Data(), areaPoint.Data(), CONTENTS_SOLID | CONTENTS_WATER );
	// If there is a gap in-between the trace is very likely to hit solid below the area floor.
	return trace.fraction != 1.0f && trace.endpos[2] >= area.mins[2];
}

bool SameFloorClusterAreasCache::NeedsToBeComputed( Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *floorClusterNums = aasWorld->AreaFloorClusterNums();

	if( !computedTargetAreaNum ) {
		return true;
	}

	if( floorClusterNums[context->CurrGroundedAasAreaNum()] != floorClusterNums[computedTargetAreaNum] ) {
		return true;
	}

	if( computedTargetAreaPoint.SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( REACHABILITY_RADIUS ) ) {
		return true;
	}

	// Walkability tests in cluster are cheap but sometimes produce false negatives,
	// so do not check for walkability in the first second to prevent choice jitter
	if( level.time - computedAt > 1000 ) {
		if( !aasWorld->IsAreaWalkableInFloorCluster( context->CurrGroundedAasAreaNum(), computedTargetAreaNum ) ) {
			return true;
		}
	}

	return !FloorClusterAreasCache::AreaPassesCollisionTest( context, computedTargetAreaNum );
}

int FloorClusterAreasCache::GetClosestToTargetPoint( Context *context, float *resultPoint, int *resultAreaNum ) const {
	// We have switched to using a cached value as far as it is feasible
	// avoiding computing an actual point almost every frame
	// (it has proven to cause jitter/looping)

	// Check whether an old value is present and is feasible
	if( NeedsToBeComputed( context ) ) {
		computedTargetAreaNum = 0;
		computedTargetAreaPoint.Set( 0, 0, 0 );
		if( ( computedTravelTime = FindClosestToTargetPoint( context, &computedTargetAreaNum ) ) ) {
			computedAt = level.time;
			const auto &area = aasWorld->Areas()[computedTargetAreaNum];
			computedTargetAreaPoint.Set( area.center );
			computedTargetAreaPoint.Z() = area.mins[2] + ( -playerbox_stand_mins[2] );
		}
	}

	if( computedTravelTime ) {
		if( resultAreaNum ) {
			*resultAreaNum = computedTargetAreaNum;
		}
		if( resultPoint ) {
			computedTargetAreaPoint.CopyTo( resultPoint );
		}
		return computedTravelTime;
	}

	return 0;
}

void FloorClusterAreasCache::TryReusingOldHeap( MovementPredictionContext *context,
												int currGroundedAreaNum,
												int expectedClusterNum,
												CandidateAreasHeap &scratchpadHeap ) const {
	scratchpadHeap.clear();
	// If the cached results were computed for specified cluster
	if( aasWorld->AreaFloorClusterNums()[computedTargetAreaNum] == expectedClusterNum ) {
		// If we're still in the same area and there are cached results
		if( currGroundedAreaNum == computedForAreaNum && !oldCandidatesHeap.empty() ) {
			// Populate the current heap (that is going to be modified) by backed heap values
			for( const auto &heapElem: oldCandidatesHeap ) {
				scratchpadHeap.push_back( heapElem );
			}
		}
		return;
	}

	oldCandidatesHeap.clear();

	computedForAreaNum = currGroundedAreaNum;
	const auto maxTravelTimeThreshold = context->TravelTimeToNavTarget();
	// Build new areas heap for the new flood start area
	const auto *clusterAreaNums = aasWorld->FloorClusterData( expectedClusterNum ) + 1;
	// The number of areas in the cluster areas list prepends the first area num
	const auto numClusterAreas = clusterAreaNums[-1];
	BuildCandidateAreasHeap( context, maxTravelTimeThreshold, clusterAreaNums, numClusterAreas, scratchpadHeap );

	// Save the heap
	for( const auto &heapElem: scratchpadHeap ) {
		oldCandidatesHeap.push_back( heapElem );
	}
}

int SameFloorClusterAreasCache::FindClosestToTargetPoint( Context *context, int *resultAreaNum ) const {
	const int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !currGroundedAreaNum ) {
		return false;
	}

	CandidateAreasHeap candidateAreasHeap;
	const int currClusterNum = aasWorld->AreaFloorClusterNums()[currGroundedAreaNum];
	this->TryReusingOldHeap( context, currGroundedAreaNum, currClusterNum, candidateAreasHeap );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );
	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 1.0f;
	}

	while( !candidateAreasHeap.empty() ) {
		std::pop_heap( candidateAreasHeap.begin(), candidateAreasHeap.end() );
		int areaNum = candidateAreasHeap.back().areaNum;
		int travelTime = (int)( -candidateAreasHeap.back().score );
		candidateAreasHeap.pop_back();

		if( !aasWorld->IsAreaWalkableInFloorCluster( currGroundedAreaNum, areaNum ) ) {
			continue;
		}

		// We hope we have done all possible cutoffs at this moment of execution.
		// We still need this collision test since cutoffs are performed using thin rays.
		// This test is expensive that's why we try to defer it as far at it is possible.
		if( !AreaPassesCollisionTest( start, areaNum, traceMins, traceMaxs ) ) {
			continue;
		}

		// Stop on the first (and best since a heap is used) feasible area
		if( resultAreaNum ) {
			*resultAreaNum = areaNum;
		}
		return travelTime;
	}

	return 0;
}

void FloorClusterAreasCache::BuildCandidateAreasHeap( MovementPredictionContext *context,
													  int maxTravelTimeThreshold,
													  const uint16_t *clusterAreaNums,
													  int numClusterAreas,
													  CandidateAreasHeap &result ) const {
	result.clear();

	const auto *hazardToEvade = bot->PrimaryHazard();
	// Reduce branching in the loop below
	if( bot->ShouldRushHeadless() || ( hazardToEvade && !hazardToEvade->SupportsImpactTests() ) ) {
		hazardToEvade = nullptr;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *routeCache = bot->RouteCache();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int currAreaNum = context->CurrAasAreaNum();
	const int toAreaNum = context->NavTargetAasAreaNum();

	const float squareNearThreshold = areaSelectionNearThreshold * areaSelectionNearThreshold;
	const float squareFarThreshold = areaSelectionFarThreshold * areaSelectionFarThreshold;

	bool actuallyUsesHeap = false;
	for( int i = 0; i < numClusterAreas; ++i ) {
		const int areaNum = clusterAreaNums[i];

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 1 + ( -playerbox_stand_mins[2] );

		const float squareDistance = areaPoint.SquareDistanceTo( entityPhysicsState.Origin() );
		if( squareDistance < squareNearThreshold ) {
			continue;
		}

		// Cut off very far points as it leads to looping in some cases on vast open areas
		if( squareDistance > squareFarThreshold ) {
			continue;
		}

		if( hazardToEvade && hazardToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		// Skip routing calls for areas that are not in PVS.
		// Area-to-area PVS test is quite cheap.
		// We eliminate necessity in having a large heap of candidates
		// by rejecting a-priori not visible areas this early.
		// That's why the heap vector size has been reduced.
		if( !aasWorld->AreAreasInPvs( currAreaNum, areaNum ) ) {
			continue;
		}

		const int areaTime = routeCache->FastestRouteToGoalArea( areaNum, toAreaNum );
		if( !areaTime || areaTime >= maxTravelTimeThreshold ) {
			continue;
		}

		// If we still have some room for new areas, just push an element back.
		if( result.size() < result.capacity() ) {
			new ( result.unsafe_grow_back() )AreaAndScore( areaNum, areaTime );
			continue;
		}

		if( actuallyUsesHeap ) {
			std::pop_heap( result.begin(), result.end() );
			result.pop_back();
		} else {
			// Convert plain vector to a heap on demand
			std::make_heap( result.begin(), result.end() );
			std::pop_heap( result.begin(), result.end() );
			result.pop_back();
			actuallyUsesHeap = true;
		}

		new( result.unsafe_grow_back() )AreaAndScore( areaNum, areaTime );
		std::push_heap( result.begin(), result.end() );
	}

	// We have set scores so worst area got evicted first, invert scores now so the best area is retrieved first
	for( auto &areaAndScore: result ) {
		areaAndScore.score = -areaAndScore.score;
	}

	// TODO: If it is already a heap we could use a cheaper conversion
	// but it's unlikely that a heap is actually used for an overwhelming majority of floor clusters
	std::make_heap( result.begin(), result.end() );
}

bool NextFloorClusterAreasCache::NeedsToBeComputed( Context *context ) const {
	const auto *floorClusterNums = aasWorld->AreaFloorClusterNums();

	// There were no data computed
	if( !computedTargetAreaNum ) {
		return true;
	}

	// We have reached the cluster of the result
	if( floorClusterNums[context->CurrGroundedAasAreaNum()] == floorClusterNums[computedTargetAreaNum] ) {
		return true;
	}

	return !FloorClusterAreasCache::AreaPassesCollisionTest( context, computedTargetAreaNum );
}

int NextFloorClusterAreasCache::FindClosestToTargetPoint( Context *context, int *resultAreaNum ) const {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !currGroundedAreaNum ) {
		return false;
	}

	const auto *const __restrict routeCache = bot->RouteCache();
	const auto *const __restrict aasReach = aasWorld->Reachabilities();
	const auto *const __restrict floorClusterNums = aasWorld->AreaFloorClusterNums();

	// We try to find a number of next floor cluster in reach chain
	int nextClusterNum = 0;
	// Could be zero if we are not currently in some floor cluster.
	// We are still interested in getting next floor cluster in this case.
	const int startClusterNum = floorClusterNums[currGroundedAreaNum];
	int pathAreaNum = currGroundedAreaNum;
	const int targetAreaNum = context->NavTargetAasAreaNum();
	while( pathAreaNum != targetAreaNum ) {
		int reachNum;
		if( !routeCache->PreferredRouteToGoalArea( pathAreaNum, targetAreaNum, &reachNum ) ) {
			return false;
		}
		const auto &reach = aasReach[reachNum];
		const auto travelType = reach.traveltype & TRAVELTYPE_MASK;
		// We have met non bhop-compatible travel type prior to entering next floor cluster
		if( travelType != TRAVEL_WALK && travelType != TRAVEL_WALKOFFLEDGE ) {
			return false;
		}
		pathAreaNum = reach.areanum;
		nextClusterNum = floorClusterNums[pathAreaNum];
		// We have found a next cluster
		if( nextClusterNum && nextClusterNum != startClusterNum ) {
			break;
		}
	}

	if( !nextClusterNum ) {
		return false;
	}

	// If we are currently in a floor cluster and its very likely that the next cluster is not visible
	if( startClusterNum && !aasWorld->AreFloorClustersCertainlyVisible( startClusterNum, nextClusterNum ) ) {
		return false;
	}

	CandidateAreasHeap candidateAreasHeap;
	TryReusingOldHeap( context, currGroundedAreaNum, nextClusterNum, candidateAreasHeap );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );
	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 4.0f;
	}

	while( !candidateAreasHeap.empty() ) {
		std::pop_heap( candidateAreasHeap.begin(), candidateAreasHeap.end() );
		int areaNum = candidateAreasHeap.back().areaNum;
		int travelTime = (int)( -candidateAreasHeap.back().score );
		candidateAreasHeap.pop_back();

		// Unfortunately no cheaper cutoffs are possible for this
		// TODO: we can precompute floor cluster to floor cluster vis table
		// at start to reject the entire attempt of testing next cluster quickly
		if( !AreaPassesCollisionTest( start, areaNum, traceMins, traceMaxs ) ) {
			continue;
		}

		// Stop on the first (and best since a heap is used) feasible area
		if( resultAreaNum ) {
			*resultAreaNum = areaNum;
		}
		return travelTime;
	}

	return 0;
}