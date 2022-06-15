#include "hazardsselector.h"
#include "awarenessmodule.h"
#include "../bot.h"
#include "../../../qcommon/links.h"

void HazardsSelector::BeginUpdate() {
	if( primaryHazard ) {
		primaryHazard->DeleteSelf();
	}

	primaryHazard = nullptr;
}

void HazardsSelector::EndUpdate() {
	// Set the primary hazard timeout after all (TODO: why not from beginning?)
	if( primaryHazard ) {
		primaryHazard->timeoutAt = level.time + Hazard::TIMEOUT;
	}
}

bool HazardsSelector::TryAddHazard( float damageScore,
									const vec3_t hitPoint,
									const vec3_t direction,
									const edict_t *owner,
									float splashRadius ) {
	if( primaryHazard ) {
		if( primaryHazard->damage >= damageScore ) {
			return false;
		}
	}

	if( Hazard *hazard = hazardsPool.New() ) {
		hazard->damage = damageScore;
		hazard->hitPoint.Set( hitPoint );
		hazard->direction.Set( direction );
		hazard->attacker = owner;
		hazard->splashRadius = splashRadius;
		if( primaryHazard ) {
			primaryHazard->DeleteSelf();
		}
		primaryHazard = hazard;
		return true;
	}

	return false;
}

class PlasmaBeam {
	friend class PlasmaBeamsBuilder;

	PlasmaBeam() = default;
public:
	explicit PlasmaBeam( const edict_t *firstProjectile )
		: startProjectile( firstProjectile )
		, endProjectile( firstProjectile )
		, owner( game.edicts + firstProjectile->s.ownerNum )
		, damage( firstProjectile->projectileInfo.maxDamage ) {}

	const edict_t *startProjectile { nullptr };
	const edict_t *endProjectile { nullptr };
	const edict_t *owner { nullptr }; // May be null if this beam consists of projectiles of many players
	float damage { 0.0f };

	inline Vec3 start() { return Vec3( startProjectile->s.origin ); }
	inline Vec3 end() { return Vec3( endProjectile->s.origin ); }

	inline void AddProjectile( const edict_t *nextProjectile ) {
		endProjectile = nextProjectile;
		// If the beam is combined from projectiles of many players, the beam owner is unknown
		if( owner != nextProjectile->r.owner ) {
			owner = nullptr;
		}
		damage += nextProjectile->projectileInfo.maxDamage;
	}
};

struct EntAndLineParam {
	int entNum;
	float t;

	inline EntAndLineParam( int entNum_, float t_ ) : entNum( entNum_ ), t( t_ ) {}
	inline bool operator<( const EntAndLineParam &that ) const { return t < that.t; }
};

class SameDirBeamsList {
	friend class PlasmaBeamsBuilder;

	static constexpr float DIST_TO_RAY_THRESHOLD = 200.0f;
	static constexpr float DIR_DOT_THRESHOLD = 0.995f;
	static constexpr float PRJ_PROXIMITY_THRESHOLD = 300.0f;

	PlasmaBeam *plasmaBeams { nullptr };
	EntAndLineParam *sortedProjectiles { nullptr };
	Vec3 avgDirection;
	// All projectiles in this list belong to this line defined as a (point, direction) pair
	Vec3 lineEqnPoint;
	unsigned projectilesCount { 0 };
	unsigned plasmaBeamsCount { 0 };
	bool isAlreadySkipped { false };
public:
	SameDirBeamsList( const edict_t *firstEntity, const edict_t *bot );

	~SameDirBeamsList();

	bool TryAddProjectile( const edict_t *projectile );

	void BuildBeams();

	inline float ComputeLineEqnParam( const edict_t *projectile ) {
		const float *origin = projectile->s.origin;

		if( fabsf( avgDirection.X() ) > 0.1f ) {
			return ( origin[0] - lineEqnPoint.X() ) / avgDirection.X();
		}
		if( fabsf( avgDirection.Y() ) > 0.1f ) {
			return ( origin[1] - lineEqnPoint.Y() ) / avgDirection.Y();
		}
		return ( origin[2] - lineEqnPoint.Z() ) / avgDirection.Z();
	}
};

class PlasmaBeamsBuilder {
	wsw::StaticVector<SameDirBeamsList, 1024> sameDirLists;

	static constexpr float SQ_HAZARD_RADIUS = 300.0f * 300.0f;

	const edict_t *const bot;
	HazardsSelector *const hazardSelector;
public:
	PlasmaBeamsBuilder( const edict_t *bot_, HazardsSelector *hazardSelector_ )
		: bot( bot_ ), hazardSelector( hazardSelector_ ) {}

	void AddProjectile( const edict_t *projectile );
	void FindMostHazardousBeams();
};

class CachingAllocator {
	struct alignas( 8 )Chunk {
		Chunk *prev;
		Chunk *next;
		uint8_t *data;
	};

	/**
	 * We have to ensure that the returned data is at least 8-byte aligned (that's the malloc contract)
	 */
	enum { DATA_PADDING = sizeof( Chunk ) % 8 ? 8 - sizeof( Chunk ) % 8 : 0 };

	const size_t chunkDataSize;
	unsigned numUsedChunks { 0 };
	Chunk *freeChunksHead { nullptr };

public:
	explicit CachingAllocator( size_t chunkDataSize_ ): chunkDataSize( chunkDataSize_ ) {}

	~CachingAllocator() {
		// Everything must be released properly by a user at the moment of this call
		assert( !numUsedChunks );

		Chunk *nextChunk;
		for( Chunk *chunk = freeChunksHead; chunk; chunk = nextChunk ) {
			nextChunk = chunk->next;
			Q_free( chunk );
		}
	}

	uint8_t *Alloc() {
		if( freeChunksHead ) {
			auto *const chunk = wsw::unlink( freeChunksHead, &freeChunksHead );
			numUsedChunks++;
			return chunk->data;
		}

		const size_t size = sizeof( Chunk ) + DATA_PADDING + chunkDataSize;
		auto *const mem = (uint8_t *)Q_malloc( size );
		auto *const chunk = (Chunk *)mem;
		chunk->data = mem + sizeof( Chunk ) + DATA_PADDING;
		numUsedChunks++;
		return chunk->data;
	}

	void Free( void *p ) {
		if( !p ) {
			return;
		}
		auto *chunk = (Chunk *)( ( (uint8_t *)p ) - sizeof( Chunk ) - DATA_PADDING );
		wsw::link( chunk, &freeChunksHead );
		numUsedChunks--;
	}
};

static wsw::StaticVector<HazardsSelectorCache, 1> instanceHolder;
HazardsSelectorCache *HazardsSelectorCache::instance = nullptr;

HazardsSelectorCache::HazardsSelectorCache() {
	auto *mem = storageMem = (uint8_t *)Q_malloc( 2 * sizeof( CachingAllocator ) );
	sortedProjectilesAllocator = new( mem )CachingAllocator( sizeof( EntAndLineParam ) * MAX_EDICTS );
	mem += sizeof( CachingAllocator );
	plasmaBeamsAllocator = new( mem )CachingAllocator( sizeof( PlasmaBeam ) * MAX_EDICTS );
}

HazardsSelectorCache::~HazardsSelectorCache() {
	sortedProjectilesAllocator->~CachingAllocator();
	plasmaBeamsAllocator->~CachingAllocator();
	Q_free( storageMem );
}

void HazardsSelectorCache::Init() {
	assert( !instance );
	instance = new( instanceHolder.unsafe_grow_back() )HazardsSelectorCache;
}

void HazardsSelectorCache::Shutdown() {
	if( instance ) {
		instance = nullptr;
		instanceHolder.clear();
	}
}

SameDirBeamsList::SameDirBeamsList( const edict_t *firstEntity, const edict_t *bot )
	: avgDirection( firstEntity->velocity ), lineEqnPoint( firstEntity->s.origin ) {
	if( !avgDirection.normalizeFast() ) {
		isAlreadySkipped = true;
		return;
	}

	// If distance from an infinite line of beam to bot is greater than threshold, skip;
	// Let's compute distance from bot to the beam infinite line;
	Vec3 botOrigin( bot->s.origin );
	float squaredDistanceToBeamLine = ( botOrigin - lineEqnPoint ).Cross( avgDirection ).SquaredLength();
	if( squaredDistanceToBeamLine > DIST_TO_RAY_THRESHOLD * DIST_TO_RAY_THRESHOLD ) {
		isAlreadySkipped = true;
		return;
	}

	auto *const cache = HazardsSelectorCache::Instance();
	sortedProjectiles = (EntAndLineParam *)cache->sortedProjectilesAllocator->Alloc();
	plasmaBeams = (PlasmaBeam *)cache->plasmaBeamsAllocator->Alloc();

	sortedProjectiles[projectilesCount++] = EntAndLineParam( ENTNUM( firstEntity ), ComputeLineEqnParam( firstEntity ) );
}

SameDirBeamsList::~SameDirBeamsList() {
	if( isAlreadySkipped ) {
		return;
	}
	auto *const cache = HazardsSelectorCache::Instance();
	cache->sortedProjectilesAllocator->Free( sortedProjectiles );
	cache->plasmaBeamsAllocator->Free( plasmaBeams );
	sortedProjectiles = nullptr;
	plasmaBeams = nullptr;
}

bool SameDirBeamsList::TryAddProjectile( const edict_t *projectile ) {
	Vec3 direction( projectile->velocity );

	if( !direction.normalizeFast() ) {
		return false;
	}

	if( direction.Dot( avgDirection ) < DIR_DOT_THRESHOLD ) {
		return false;
	}

	// Do not process a projectile, but "consume" it anyway...
	if( isAlreadySkipped ) {
		return true;
	}

	// Update average direction
	avgDirection += direction;
	avgDirection.normalizeFastOrThrow();

	sortedProjectiles[projectilesCount++] = EntAndLineParam( ENTNUM( projectile ), ComputeLineEqnParam( projectile ) );
	std::push_heap( sortedProjectiles, sortedProjectiles + projectilesCount );

	return true;
}

void SameDirBeamsList::BuildBeams() {
	if( isAlreadySkipped ) {
		return;
	}

	if( projectilesCount == 0 ) {
		AI_FailWith( "SameDirBeamsList::BuildBeams()", "Projectiles count: %d\n", projectilesCount );
	}

	const edict_t *const gameEdicts = game.edicts;

	// Get the projectile that has a maximal `t`
	std::pop_heap( sortedProjectiles, sortedProjectiles + projectilesCount );
	const edict_t *prevProjectile = gameEdicts + sortedProjectiles[--projectilesCount].entNum;

	plasmaBeams[plasmaBeamsCount++] = PlasmaBeam( prevProjectile );

	while( projectilesCount > 0 ) {
		// Get the projectile that has a maximal `t` atm
		std::pop_heap( sortedProjectiles, sortedProjectiles + projectilesCount );
		const edict_t *currProjectile = gameEdicts + sortedProjectiles[--projectilesCount].entNum;

		float prevToCurrLen = ( Vec3( prevProjectile->s.origin ) - currProjectile->s.origin ).SquaredLength();
		if( prevToCurrLen < PRJ_PROXIMITY_THRESHOLD * PRJ_PROXIMITY_THRESHOLD ) {
			// Add the projectile to the last beam
			plasmaBeams[plasmaBeamsCount - 1].AddProjectile( currProjectile );
		} else {
			// Construct new plasma beam at the end of beams array
			plasmaBeams[plasmaBeamsCount++] = PlasmaBeam( currProjectile );
		}
	}
}

void PlasmaBeamsBuilder::AddProjectile( const edict_t *projectile ) {
	for( unsigned i = 0; i < sameDirLists.size(); ++i ) {
		if( sameDirLists[i].TryAddProjectile( projectile ) ) {
			return;
		}
	}
	new ( sameDirLists.unsafe_grow_back() )SameDirBeamsList( projectile, bot );
}

void PlasmaBeamsBuilder::FindMostHazardousBeams() {
	trace_t trace;
	Vec3 botOrigin( bot->s.origin );

	for( unsigned i = 0; i < sameDirLists.size(); ++i ) {
		sameDirLists[i].BuildBeams();
	}

	const auto *weaponDef = GS_GetWeaponDef( WEAP_PLASMAGUN );
	const float splashRadius = 1.2f * 0.5f * ( weaponDef->firedef.splash_radius + weaponDef->firedef_weak.splash_radius );
	float minDamageScore = 0.0f;

	for( const SameDirBeamsList &beamsList: sameDirLists ) {
		if( beamsList.isAlreadySkipped ) {
			continue;
		}

		for( unsigned i = 0; i < beamsList.plasmaBeamsCount; ++i ) {
			PlasmaBeam *beam = beamsList.plasmaBeams + i;

			Vec3 botToBeamStart = beam->start() - botOrigin;
			Vec3 botToBeamEnd = beam->end() - botOrigin;

			if( botToBeamStart.SquaredLength() > SQ_HAZARD_RADIUS && botToBeamEnd.SquaredLength() > SQ_HAZARD_RADIUS ) {
				continue;
			}

			Vec3 beamStartToEnd = beam->end() - beam->start();

			float dotBotToStartWithDir = botToBeamStart.Dot( beamStartToEnd );
			float dotBotToEndWithDir = botToBeamEnd.Dot( beamStartToEnd );

			// If the beam has entirely passed the bot and is flying away, skip it
			if( dotBotToStartWithDir > 0 && dotBotToEndWithDir > 0 ) {
				continue;
			}

			Vec3 tracedBeamStart = beam->start();
			Vec3 tracedBeamEnd = beam->end();

			// It works for single-projectile beams too
			Vec3 beamDir( beam->startProjectile->velocity );
			if( !beamDir.normalizeFast() ) {
				continue;
			}

			tracedBeamEnd += 256.0f * beamDir;

			G_Trace( &trace, tracedBeamStart.Data(), nullptr, nullptr, tracedBeamEnd.Data(), nullptr, MASK_AISOLID );
			if( trace.fraction == 1.0f ) {
				continue;
			}

			// Direct hit
			if( bot == game.edicts + trace.ent ) {
				float damageScore = beam->damage;
				if( damageScore > minDamageScore ) {
					if( hazardSelector->TryAddHazard( damageScore, trace.endpos,
													  beamsList.avgDirection.Data(),
													  beam->owner, 1.5f * splashRadius ) ) {
						minDamageScore = damageScore;
					}
				}
				continue;
			}

			// Splash hit
			float hitVecLen = botOrigin.FastDistanceTo( trace.endpos );
			if( hitVecLen < splashRadius ) {
				float damageScore = beam->damage * ( 1.0f - hitVecLen / splashRadius );
				if( damageScore > minDamageScore ) {
					if( hazardSelector->TryAddHazard( damageScore, trace.endpos,
													  beamsList.avgDirection.Data(),
													  beam->owner, 1.5f * splashRadius ) ) {
						minDamageScore = damageScore;
					}
				}
			}
		}
	}
}

void HazardsSelector::FindWaveHazards( const EntNumsVector &entNums ) {
	auto *const gameEdicts = game.edicts;
	const auto *weaponDef = GS_GetWeaponDef( WEAP_SHOCKWAVE );
	const edict_t *self = game.edicts + bot->EntNum();
	trace_t trace;
	for( auto entNum: entNums ) {
		edict_t *wave = gameEdicts + entNum;
		float hazardRadius;
		if( wave->style == MOD_SHOCKWAVE_S ) {
			hazardRadius = weaponDef->firedef.splash_radius + 32.0f;
		} else {
			hazardRadius = weaponDef->firedef_weak.splash_radius + 24.0f;
		}

		// We try checking whether the wave passes near the bot inflicting a corona damage.
		// TODO: This code assumes that the bot origin remains the same.
		// This is not so bad because hazards are checked each Think() frame
		// and there is some additional extent applied to the damage radius,
		// but it would be nice to predict an actual trajectory intersection.

		// Compute a distance from wave linear movement line to bot
		Vec3 lineDir( wave->velocity );
		float squareSpeed = lineDir.SquaredLength();
		if( squareSpeed < 1 ) {
			continue;
		}

		float waveSpeed = Q_Sqrt( squareSpeed );
		Vec3 botToLinePoint( wave->s.origin );
		botToLinePoint -= self->s.origin;
		Vec3 projection( lineDir );
		projection *= botToLinePoint.Dot( lineDir );
		Vec3 perpendicular( botToLinePoint );
		perpendicular -= projection;
		const float squareDistance =  perpendicular.SquaredLength();
		if( squareDistance > hazardRadius * hazardRadius ) {
			continue;
		}

		// We're sure the wave is in PVS and is visible by bot, that's what HazardsDetector yields
		// Now check whether the wave hits an obstacle on a safe distance.

		Vec3 traceEnd( lineDir );
		traceEnd *= HazardsDetector::DETECT_WAVE_RADIUS / waveSpeed;
		traceEnd += wave->s.origin;
		G_Trace( &trace, wave->s.origin, nullptr, nullptr, traceEnd.Data(), wave, MASK_SHOT );
		bool isDirectHit = false;
		if( trace.fraction != 1.0f ) {
			if( DistanceSquared( trace.endpos, self->s.origin ) > hazardRadius * hazardRadius ) {
				continue;
			}
			isDirectHit = ( trace.ent == ENTNUM( self ) );
		}
		// Put the likely case first
		float damage = wave->projectileInfo.maxDamage;
		if( !isDirectHit ) {
			float distance = Q_Sqrt( squareDistance );
			float damageScore = damage * ( 3.0f - 2.0f * ( distance / hazardRadius ) );
			// Treat the nearest point on the line as a hit point
			// perpendicular = hitPoint - self->s.origin;
			Vec3 hitPoint( perpendicular );
			hitPoint += self->s.origin;
			Vec3 hitDir( perpendicular );
			hitDir *= 1.0f / distance;
			TryAddHazard( damageScore, hitPoint.Data(), hitDir.Data(), gameEdicts + wave->s.ownerNum, hazardRadius );
		} else {
			TryAddHazard( 3.0f * damage, trace.endpos, lineDir.Data(), gameEdicts + wave->s.ownerNum, hazardRadius );
		}
	}
}

void HazardsSelector::FindPlasmaHazards( const EntNumsVector &entNums ) {
	const edict_t *gameEdicts = game.edicts;
	PlasmaBeamsBuilder plasmaBeamsBuilder( gameEdicts + bot->EntNum(), this );

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		plasmaBeamsBuilder.AddProjectile( gameEdicts + entNums[i] );
	}
	plasmaBeamsBuilder.FindMostHazardousBeams();
}

void HazardsSelector::FindLaserHazards( const EntNumsVector &entNums ) {
	trace_t trace;
	edict_t *const gameEdicts = game.edicts;
	const edict_t *self = game.edicts + bot->EntNum();
	float maxDamageScore = 0.0f;

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		edict_t *beam = gameEdicts + entNums[i];
		G_Trace( &trace, beam->s.origin, vec3_origin, vec3_origin, beam->s.origin2, beam, MASK_AISOLID );
		if( trace.fraction == 1.0f ) {
			continue;
		}

		if( self != game.edicts + trace.ent ) {
			continue;
		}

		edict_t *owner = game.edicts + beam->s.ownerNum;

		Vec3 direction( beam->s.origin2 );
		direction -= beam->s.origin;
		float squareLen = direction.SquaredLength();
		if( squareLen > 1 ) {
			direction *= 1.0f / sqrtf( squareLen );
		} else {
			// Very rare but really seen case - beam has zero length
			vec3_t forward, right, up;
			AngleVectors( owner->s.angles, forward, right, up );
			direction += forward;
			direction += right;
			direction += up;
			direction.normalizeFastOrThrow();
		}

		// Modify potential damage from a beam by its owner accuracy
		float damageScore = 50.0f;
		if( owner->team != self->team && owner->r.client ) {
			const auto &ownerStats = owner->r.client->stats;
			if( ownerStats.accuracy_shots[AMMO_LASERS] > 10 ) {
				float extraDamage = 75.0f;
				extraDamage *= ownerStats.accuracy_hits[AMMO_LASERS];
				extraDamage /= ownerStats.accuracy_shots[AMMO_LASERS];
				damageScore += extraDamage;
			}
		}

		if( damageScore > maxDamageScore ) {
			if( TryAddHazard( damageScore, trace.endpos, direction.Data(), owner, 0.0f ) ) {
				maxDamageScore = damageScore;
			}
		}
	}
}

void HazardsSelector::FindProjectileHazards( const EntNumsVector &entNums ) {
	trace_t trace;
	float minPrjFraction = 1.0f;
	float minDamageScore = 0.0f;
	edict_t *const gameEdicts = game.edicts;

	for( unsigned i = 0; i < entNums.size(); ++i ) {
		edict_t *target = gameEdicts + entNums[i];
		Vec3 end = Vec3( target->s.origin ) + 2.0f * Vec3( target->velocity );
		G_Trace( &trace, target->s.origin, target->r.mins, target->r.maxs, end.Data(), target, MASK_AISOLID );
		if( trace.fraction >= minPrjFraction ) {
			continue;
		}

		minPrjFraction = trace.fraction;
		float hitVecLen = DistanceFast( bot->Origin(), trace.endpos );
		if( hitVecLen >= 1.25f * target->projectileInfo.radius ) {
			continue;
		}

		float damageScore = target->projectileInfo.maxDamage;
		damageScore *= 1.0f - hitVecLen / ( 1.25f * target->projectileInfo.radius );
		if( damageScore <= minDamageScore ) {
			continue;
		}

		// Velocity may be zero for some projectiles (e.g. grenades)
		Vec3 direction( target->velocity );
		float squaredLen = direction.SquaredLength();
		if( squaredLen > 0.1f ) {
			direction *= 1.0f / sqrtf( squaredLen );
		} else {
			direction = Vec3( &axis_identity[AXIS_UP] );
		}
		if( TryAddHazard( damageScore, trace.endpos, direction.Data(),
						  gameEdicts + target->s.ownerNum,
						  1.25f * target->projectileInfo.radius ) ) {
			minDamageScore = damageScore;
		}
	}
}