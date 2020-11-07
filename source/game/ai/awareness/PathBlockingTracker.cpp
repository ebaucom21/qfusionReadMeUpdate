#include "AwarenessLocal.h"
#include "PathBlockingTracker.h"
#include "../bot.h"
#include "../navigation/AasElementsMask.h"

#include <cmath>

/**
 * A compact representation of {@code TrackedEnemy} that contains precomputed fields
 * required for determining blocking status of map areas in bulk fashion.
 */
class EnemyComputationalProxy {
	friend class DisableMapAreasRequest;

	/**
	 * Use only two areas to prevent computational explosion.
	 * Moreover scanning areas vis list for small areas count could be optimized.
	 * Try to select most important areas.
	 */
	enum { MAX_AREAS = 2 };

	const AiAasWorld *const __restrict aasWorld;
	/**
	 * Points to AAS area vis row for own areas acting as POV areas.
	 * The memory is held by {@code AasElementsMask}. Values are supposedly set by
	 * {@code AasWorld::DecompressAreaVis() and likely by {@code AasWorld::AddToDecompressedAreaVis()} as well.
	 */
	const bool *__restrict ownAreasVisRow;

	vec3_t origin;
	vec3_t lookDir;
	float squareBaseBlockingRadius;
	int floorClusterNum;
	TrackedEnemy::HitFlags hitFlags;
	bool isZooming;

	int ComputeAreaNums( int areaNums[MAX_AREAS] );
	const bool *PrepareAreasVisRow( const int areaNums[MAX_AREAS], int numAreas, bool *row );
	void SaveFloorClusterNum( const int areaNums[MAX_AREAS], int numAreas );

	bool CutOffForFlags( const aas_area_t &area, float squareDistance, int hitFlagsMask ) const;
public:
	EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot, int num );

	bool IsInVis( int areaNum ) const { return ownAreasVisRow[areaNum]; }

	bool MayBlockOtherArea( int areaNum, int hitFlagsMask ) const;
	bool MayBlockGroundedArea( int areaNum, int hitFlagsMask ) const;
};

EnemyComputationalProxy::EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot, int num )
	: aasWorld( AiAasWorld::Instance() )
	, isZooming( enemy->ent->r.client ? (bool)enemy->ent->r.client->ps.stats[PM_STAT_ZOOMTIME] : false ) {
	hitFlags = enemy->GetCheckForWeaponHitFlags( damageToKillBot );

	// Lets use fairly low blocking radii that are still sufficient for narrow hallways.
	// Otherwise bot behaviour is pretty poor as they think every path is blocked.
	float baseBlockingRadius = enemy->HasQuad() ? 384.0f : 40.0f;
	squareBaseBlockingRadius = baseBlockingRadius * baseBlockingRadius;
	enemy->LastSeenOrigin().CopyTo( origin );
	enemy->LookDir().CopyTo( lookDir );

	int ownAreaNums[MAX_AREAS];
	const int numOwnAreas = ComputeAreaNums( ownAreaNums );
	SaveFloorClusterNum( ownAreaNums, numOwnAreas );
	ownAreasVisRow = PrepareAreasVisRow( ownAreaNums, numOwnAreas, AasElementsMask::TmpAreasVisRow( num ) );
}

/**
 * A descendant of {@code AiAasRouteCache::DisableZoneRequest} that provides algorithms
 * for blocking areas by potential blockers in a bulk fashion.
 * Bulk computations are required for optimization as this code is very performance-demanding.
 */
class DisableMapAreasRequest: public AiAasRouteCache::DisableZoneRequest {
public:
	enum { MAX_ENEMIES = 8 };

	static_assert( MAX_ENEMIES == AasElementsMask::TMP_ROW_REDUNDANCY_SCALE, "" );
private:
	const AiAasWorld *const aasWorld;
	vec3_t botOrigin;
	int botAreaNum;

	// Lets put it last even if it has the largest alignment requirements
	// Last elements of this array are unlikely to be accessed.
	// Let the array head be closer to the memory hot spot
	wsw::StaticVector<EnemyComputationalProxy, MAX_ENEMIES> enemyProxies;

	void AddAreas( const uint16_t *__restrict areasList, bool *__restrict blockedAreasTable );

	void AddGroundedAreas( bool *__restrict blockedAreasTable );

	void AddNonGroundedAreas( const uint16_t *__restrict areasList, bool *__restrict blockedAreasTable );
public:
	void FillBlockedAreasTable( bool *__restrict blockedAreasTable ) override;

	DisableMapAreasRequest( const TrackedEnemy **begin, const TrackedEnemy **end,
							const float *botOrigin_, float damageToKillBot_ );
};

inline void PathBlockingTracker::ClearBlockedAreas() {
	if( didClearAtLastUpdate ) {
		return;
	}

	bot->routeCache->ClearDisabledZones();
	didClearAtLastUpdate = true;
}

void PathBlockingTracker::Update() {
	if( bot->ShouldRushHeadless() ) {
		ClearBlockedAreas();
		return;
	}

	const edict_t *self = game.edicts + bot->EntNum();

	float damageToKillBot = DamageToKill( self, g_armor_protection->value, g_armor_degradation->value );
	if( HasShell( self ) ) {
		damageToKillBot *= QUAD_DAMAGE_SCALE;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( self ) && damageToKillBot > 50.0f ) {
		damageToKillBot *= 2.0f;
	}

	const int botBestWeaponTier = FindBestWeaponTier( self->r.client );

	wsw::StaticVector<const TrackedEnemy *, DisableMapAreasRequest::MAX_ENEMIES> potentialBlockers;

	const TrackedEnemy *enemy = self->bot->TrackedEnemiesHead();
	for(; enemy; enemy = enemy->NextInTrackedList() ) {
		if( !IsAPotentialBlocker( enemy, damageToKillBot, botBestWeaponTier ) ) {
			continue;
		}
		potentialBlockers.push_back( enemy );
		if( potentialBlockers.size() == potentialBlockers.capacity() ) {
			break;
		}
	}

	if( potentialBlockers.empty() ) {
		ClearBlockedAreas();
		return;
	}

	DisableMapAreasRequest request( potentialBlockers.begin(), potentialBlockers.end(), bot->Origin(), damageToKillBot );
	// SetDisabledZones() interface expects array of polymorphic objects
	// that should consequently be referred via pointers
	DisableMapAreasRequest *requests[1] = { &request };
	bot->routeCache->SetDisabledZones( (AiAasRouteCache::DisableZoneRequest **)requests, 1 );
	didClearAtLastUpdate = false;
}

bool PathBlockingTracker::IsAPotentialBlocker( const TrackedEnemy *enemy,
											   float damageToKillBot,
											   int botBestWeaponTier ) const {
	if( !enemy->IsValid() ) {
		return false;
	}

	int enemyWeaponTier;
	if( const auto *client = enemy->ent->r.client ) {
		enemyWeaponTier = FindBestWeaponTier( client );
		if( enemyWeaponTier < 1 && !HasPowerups( enemy->ent ) ) {
			return false;
		}
	} else {
		// Try guessing...
		enemyWeaponTier = (int)( 1.0f + BoundedFraction( enemy->ent->aiIntrinsicEnemyWeight, 3.0f ) );
	}

	float damageToKillEnemy = DamageToKill( enemy->ent, g_armor_protection->value, g_armor_degradation->value );

	if( HasShell( enemy->ent ) ) {
		damageToKillEnemy *= QUAD_DAMAGE_SCALE;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( enemy->ent ) && damageToKillEnemy > 50 ) {
		damageToKillEnemy *= 2.0f;
	}

	const float offensiveness = bot->GetEffectiveOffensiveness();

	if( damageToKillBot < 50 && damageToKillEnemy < 50 ) {
		// Just check weapons. Note: GB has 0 tier, GL has 1 tier, the rest of weapons have a greater tier
		return ( std::min( 1, enemyWeaponTier ) / (float)std::min( 1, botBestWeaponTier ) ) > 0.7f + 0.8f * offensiveness;
	}

	const auto &selectedEnemies = bot->GetSelectedEnemies();
	// Don't block if is in squad, except they have a quad runner
	if( bot->IsInSquad() ) {
		if( !( selectedEnemies.AreValid() && selectedEnemies.HaveQuad() ) ) {
			return false;
		}
	}

	float ratioThreshold = 1.25f;
	if( selectedEnemies.AreValid() ) {
		// If the bot is outnumbered
		if( selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
			ratioThreshold *= 1.25f;
		}
	}

	if( selectedEnemies.AreValid() && selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
		if( selectedEnemies.end() - selectedEnemies.begin() > 1 ) {
			ratioThreshold *= 1.25f;
		}
	}

	ratioThreshold -= ( botBestWeaponTier - enemyWeaponTier ) * 0.25f;
	if( damageToKillEnemy / damageToKillBot < ratioThreshold ) {
		return false;
	}

	return damageToKillEnemy / damageToKillBot > 1.0f + 2.0f * ( offensiveness * offensiveness );
}

DisableMapAreasRequest::DisableMapAreasRequest( const TrackedEnemy **begin,
												const TrackedEnemy **end,
												const float *botOrigin_,
												float damageToKillBot_ )
	: aasWorld( AiAasWorld::Instance() ){
	VectorCopy( botOrigin_, this->botOrigin );
	botAreaNum = aasWorld->FindAreaNum( botOrigin_ );
	assert( end - begin > 0 && end - begin <= MAX_ENEMIES );
	int num = 0;
	for( const TrackedEnemy **iter = begin; iter != end; ++iter ) {
		const TrackedEnemy *enemy = *iter;
		new( enemyProxies.unsafe_grow_back() )EnemyComputationalProxy( enemy, damageToKillBot_, num++ );
	}
}

void DisableMapAreasRequest::FillBlockedAreasTable( bool *__restrict blockedAreasTable ) {
	AddGroundedAreas( blockedAreasTable );

	AddAreas( aasWorld->WalkOffLedgePassThroughAirAreas(), blockedAreasTable );

	// For every area in these list we use "all hit flags" mask
	// as the a is extremely vulnerable while moving through these areas
	// using intended/appropriate travel type for these areas in air.
	// We also test whether an area is grounded and skip tests in this case
	// as the test has been performed earlier.
	// As the total number of these areas is insignificant,
	// this should not has any impact on performance.
	const uint16_t *skipGroundedAreasLists[] = {
		aasWorld->JumppadReachPassThroughAreas(),
		aasWorld->LadderReachPassThroughAreas(),
		aasWorld->ElevatorReachPassThroughAreas()
	};

	for( const auto *areasList: skipGroundedAreasLists ) {
		AddNonGroundedAreas( areasList, blockedAreasTable );
	}

	// Requires sv_pps 62, a simple map and and a single bot
	//for( int i = 1; i < aasWorld->NumAreas(); ++i ) {
	//	if( blockedAreasTable[i] ) {
	//		const auto &area = aasWorld->Areas()[i];
	//		AITools_DrawColorLine( area.mins, area.maxs, COLOR_RGB( 192, 0, 0 ), 0 );
	//	}
	//}
}

void DisableMapAreasRequest::AddAreas( const uint16_t *__restrict areasList,
									   bool *__restrict blockedAreasTable ) {
	const auto hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL;
	const auto *const __restrict aasAreas = aasWorld->Areas();

	// Skip the list size
	const auto listSize = *areasList++;
	for( int i = 0; i < listSize; ++i ) {
		const auto areaNum = areasList[i];
		// Skip nearby areas
		if( DistanceSquared( aasAreas[areaNum].center, botOrigin ) < 192.0f * 192.0f ) {
			continue;
		}
		// Skip the current area as well.
		// This does not deserve its own code path as the number of non-grounded important areas is relatively small.
		if( areaNum == botAreaNum ) {
			continue;
		}
		for( const auto &__restrict enemy: enemyProxies ) {
			if( !enemy.IsInVis( areaNum ) ) {
				continue;
			}
			if( !enemy.MayBlockOtherArea( areaNum, hitFlagsMask ) ) {
				continue;
			}
			blockedAreasTable[areaNum] = true;
			break;
		}
	}
}

void DisableMapAreasRequest::AddGroundedAreas( bool *__restrict blockedAreasTable ) {
	// TODO: We already ignore RAIL hit flags mask in the actual test fn., do we?
	const auto hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL & ~(int)( TrackedEnemy::HitFlags::RAIL );
	// Do not take "rail" hit flags into account while testing grounded areas for blocking.
	// A bot can easily dodge rail-like weapons using regular movement on ground.
	const auto *const __restrict groundedAreaNums = aasWorld->GroundedPrincipalRoutingAreas() + 1;
	const auto *const __restrict aasAreas = aasWorld->Areas();

	const float squareDistanceThreshold = 192.0f * 192.0f;
	// Check whether we are not in a huge area so we do not have to check the area num additionally
	if( DistanceSquared( aasAreas[botAreaNum].center, botOrigin ) <= squareDistanceThreshold ) {
		for( int i = 0; i < groundedAreaNums[-1]; ++i ) {
			const int areaNum = groundedAreaNums[i];
			// Skip nearby areas (including the current one)
			// These are coarse tests that are however sufficient to prevent blocking nearby areas around bot in most cases.
			// Note that the additional area test is performed to prevent blocking of the current area
			// if its huge and the bot is far from its center.  We could have used multiple areas
			// (AiEntityPhysicsState::PrepareRoutingStartAreas()) but this is dropped for performance reasons.
			if( DistanceSquared( aasAreas[areaNum].center, botOrigin ) < squareDistanceThreshold ) {
				continue;
			}
			for( const auto &__restrict enemy: enemyProxies ) {
				if( !enemy.IsInVis( areaNum ) ) {
					continue;
				}
				if( !enemy.MayBlockGroundedArea( areaNum, hitFlagsMask ) ) {
					continue;
				}
				blockedAreasTable[areaNum] = true;
				break;
			}
		}
		return;
	}

	// Handle the rare but possible case when the current area is so huge that the distance threshold test fails for it
	for( int i = 0; i < groundedAreaNums[-1]; ++i ) {
		const int areaNum = groundedAreaNums[i];
		// Skip nearby areas
		if( DistanceSquared( aasAreas[areaNum].center, botOrigin ) < squareDistanceThreshold ) {
			continue;
		}
		// Skip the current area that must be huge
		if( areaNum == botAreaNum ) {
			continue;
		}
		for( const auto &__restrict enemy: enemyProxies ) {
			if( !enemy.IsInVis( areaNum ) ) {
				continue;
			}
			if( !enemy.MayBlockGroundedArea( areaNum, hitFlagsMask ) ) {
				continue;
			}
			blockedAreasTable[areaNum] = true;
			break;
		}
	}
}

void DisableMapAreasRequest::AddNonGroundedAreas( const uint16_t *__restrict areasList,
												  bool *__restrict blockedAreasTable ) {
	const int hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL;
	const auto *const __restrict aasAreaSettings = aasWorld->AreaSettings();
	const auto *const __restrict aasAreas = aasWorld->Areas();
	// The first list element is its size
	for( int i = 0; i < areasList[0]; ++i ) {
		const auto areaNum = areasList[i + 1];
		// We actually test this instead of skipping during list building
		// as AiAasWorld() getter signatures would have look awkward otherwise...
		// This is not an expensive operation as the number of such areas is very limited.
		// Still fetching only area flags from a tightly packed flags array would have been better.
		if( aasAreaSettings[areaNum].areaflags & AREA_GROUNDED ) {
			continue;
		}
		// Skip nearby areas
		if( DistanceSquared( aasAreas[areaNum].center, botOrigin ) < 192.0f * 192.0f ) {
			continue;
		}
		// Skip the current area as well.
		// This does not deserve its own code path as the number of non-grounded important areas is relatively small.
		if( areaNum == botAreaNum ) {
			continue;
		}
		for( const auto &__restrict enemy: enemyProxies ) {
			if( !enemy.IsInVis( areaNum ) ) {
				continue;
			}
			if( !enemy.MayBlockOtherArea( areaNum, hitFlagsMask ) ) {
				continue;
			}
			blockedAreasTable[areaNum] = true;
			break;
		}
	}
}

bool EnemyComputationalProxy::CutOffForFlags( const aas_area_t &area, const float squareDistance, int hitFlagsMask ) const {
	const auto effectiveFlags = (int)hitFlags & hitFlagsMask;
	// If there were no weapon hit flags set for powerful weapons
	if( !effectiveFlags ) {
		return true;
	}
	if( !( effectiveFlags & (int)TrackedEnemy::HitFlags::RAIL ) ) {
		if( squareDistance > 1000 * 1000 ) {
			return true;
		}
	}
	// If only a rocket
	if( hitFlags == TrackedEnemy::HitFlags::ROCKET ) {
		if( origin[2] < area.mins[2] ) {
			if( squareDistance > 125 * 125 ) {
				return true;
			}
		}
	}

	Vec3 toAreaDir( area.center );
	toAreaDir -= origin;

	const float invDistance = Q_RSqrt( squareDistance );
	const float dot = toAreaDir.Dot( lookDir ) * invDistance;
	return !isZooming ? dot < 0.5f : dot < 0.8f;
}

bool EnemyComputationalProxy::MayBlockOtherArea( int areaNum, int hitFlagsMask ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	float squareDistance = DistanceSquared( origin, area.center );
	if( squareDistance > squareBaseBlockingRadius ) {
		if( CutOffForFlags( area, squareDistance, hitFlagsMask ) ) {
			return false;
		}
	}

	return true;
}

bool EnemyComputationalProxy::MayBlockGroundedArea( int areaNum, int hitFlagsMask ) const {
	const auto &__restrict area = aasWorld->Areas()[areaNum];
	const float squareDistance = DistanceSquared( origin, area.center );
	// Hit flags are combined from ROCKET, SHAFT.
	// The limit for SHAFT is the greatest limit
	if( squareDistance > 900.0f * 900.0f ) {
		return false;
	}

	// Put the likely case first
	if( squareDistance > squareBaseBlockingRadius ) {
		const auto effectiveFlags = (int)hitFlags & hitFlagsMask;
		// If there were no weapon hit flags set for powerful weapons
		if( !effectiveFlags ) {
			return false;
		}

		// If we should check for rocket
		if( effectiveFlags & (int)TrackedEnemy::HitFlags::ROCKET ) {
			// If only a rocket
			if( effectiveFlags == (int)TrackedEnemy::HitFlags::ROCKET ) {
				if( origin[2] < area.mins[2] ) {
					if( squareDistance > 125 * 125 ) {
						return false;
					}
				}
				if( squareDistance > 550 * 550 ) {
					return false;
				}
			}
		}

		const float invDistance = Q_RSqrt( squareDistance );
		Vec3 toAreaDir( area.center );
		toAreaDir -= origin;
		if( toAreaDir.Dot( lookDir ) * invDistance < 0.7f ) {
			return false;
		}
	}

	return true;
}

int EnemyComputationalProxy::ComputeAreaNums( int areaNums[MAX_AREAS] ) {
	// We can't reuse entity area nums the origin differs (its the last seen origin)
	Vec3 enemyBoxMins( playerbox_stand_mins );
	Vec3 enemyBoxMaxs( playerbox_stand_maxs );
	enemyBoxMins += origin;
	enemyBoxMaxs += origin;

	static_assert( MAX_AREAS == 2, "The entire logic assumes only two areas to select" );

	int numAreas = 0;
	areaNums[0] = 0;
	areaNums[1] = 0;

	// We should select most important areas to store.
	// Find some areas in the box.

	int rawAreaNums[8];
	int numRawAreas = aasWorld->BBoxAreas( enemyBoxMins, enemyBoxMaxs, rawAreaNums, 8 );

	int areaFlags[8];
	const auto *const __restrict areaSettings = aasWorld->AreaSettings();
	for( int i = 0; i < numRawAreas; ++i ) {
		// TODO: an AAS world should supply a list of area flags (without other data that wastes bandwidth)
		areaFlags[i] = areaSettings[rawAreaNums[i]].areaflags;
	}

	// Try to select non-junk grounded areas first
	for( int i = 0; i < numRawAreas; ++i ) {
		if( !( areaFlags[i] & AREA_JUNK ) ) {
			if( areaFlags[i] & AREA_GROUNDED ) {
				areaNums[numAreas++] = rawAreaNums[i];
				if( numAreas == MAX_AREAS ) {
					return numAreas;
				}
			}
		}
	}

	// Compare area numbers to the (maybe) added first area

	// Try adding non-junk arbitrary areas
	for( int i = 0; i < numRawAreas; ++i ) {
		if( !( areaFlags[i] & AREA_JUNK ) ) {
			if( areaNums[0] != rawAreaNums[i] ) {
				areaNums[numAreas++] = rawAreaNums[i];
				if( numAreas == MAX_AREAS ) {
					return numAreas;
				}
			}
		}
	}

	// Try adding any area left
	for( int i = 0; i < numRawAreas; ++i ) {
		if( areaNums[0] != rawAreaNums[i] ) {
			areaNums[numAreas++] = rawAreaNums[i];
			if( numAreas == MAX_AREAS ) {
				return numAreas;
			}
		}
	}

	return numAreas;
}

void EnemyComputationalProxy::SaveFloorClusterNum( const int *ownAreaNums, int numOwnAreas ) {
	const auto *const aasWorld = AiAasWorld::Instance();

	floorClusterNum = 0;
	for( int i = 0; i < numOwnAreas; ++i ) {
		if( ( floorClusterNum = aasWorld->FloorClusterNum( ownAreaNums[i] ) ) ) {
			return;
		}
	}
}

const bool *EnemyComputationalProxy::PrepareAreasVisRow( const int *ownAreaNums, int numOwnAreas, bool *row ) {
	const auto *const aasWorld = AiAasWorld::Instance();

	if( !numOwnAreas ) {
		memset( row, 0, aasWorld->NumAreas() * sizeof( *row ) );
		return row;
	}

	aasWorld->DecompressAreaVis( ownAreaNums[0], row );
	for( int i = 1; i < numOwnAreas; ++i ) {
		aasWorld->AddToDecompressedAreaVis( ownAreaNums[i], row );
	}

	return row;
}
