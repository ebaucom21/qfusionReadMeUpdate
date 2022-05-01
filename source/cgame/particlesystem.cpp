#include "particlesystem.h"
#include "../qcommon/links.h"
#include "../client/client.h"
#include "cg_local.h"

ParticleSystem::ParticleSystem() {
	// TODO: All of this asks for exception-safety

	do {
		if( CMShapeList *list = CM_AllocShapeList( cl.cms ) ) [[likely]] {
			m_freeShapeLists.push_back( list );
		} else {
			throw std::bad_alloc();
		}
	} while( !m_freeShapeLists.full() );

	new( m_bins.unsafe_grow_back() )FlocksBin( kMaxSmallFlockSize, kMaxSmallFlocks );
	new( m_bins.unsafe_grow_back() )FlocksBin( kMaxMediumFlockSize, kMaxMediumFlocks );
	new( m_bins.unsafe_grow_back() )FlocksBin( kMaxLargeFlockSize, kMaxLargeFlocks );
	new( m_bins.unsafe_grow_back() )FlocksBin( kMaxClippedTrailFlockSize, kMaxClippedTrailFlocks );
	auto *lastBin = new( m_bins.unsafe_grow_back() )FlocksBin( kMaxNonClippedTrailFlockSize, kMaxNonClippedTrailFlocks );
	lastBin->needsShapeLists = false;
}

ParticleSystem::~ParticleSystem() {
	for( FlocksBin &bin: m_bins ) {
		ParticleFlock *nextFlock = nullptr;
		for( ParticleFlock *flock = bin.head; flock; flock = nextFlock ) {
			nextFlock = flock->next;
			unlinkAndFree( flock );
		}
	}
	for( CMShapeList *list : m_freeShapeLists ) {
		CM_FreeShapeList( cl.cms, list );
	}
}

// Just to break the header dependency loop
auto ParticleSystem::cgTimeFixme() -> int64_t {
	return cg.time;
}

void ParticleSystem::unlinkAndFree( ParticleFlock *flock ) {
	FlocksBin &bin = m_bins[flock->binIndex];
	wsw::unlink( flock, &bin.head );
	if( flock->shapeList ) {
		m_freeShapeLists.push_back( flock->shapeList );
	}
	flock->~ParticleFlock();
	bin.allocator.free( flock );
}

auto ParticleSystem::createFlock( unsigned binIndex, int64_t currTime ) -> ParticleFlock * {
	assert( binIndex < std::size( m_bins ) );
	FlocksBin &bin = m_bins[binIndex];

	CMShapeList *shapeList = nullptr;
	uint8_t *mem = bin.allocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		ParticleFlock *oldestFlock = nullptr;
		auto leastTimeout = std::numeric_limits<int64_t>::max();
		for( ParticleFlock *flock = bin.head; flock; flock = flock->next ) {
			if( flock->timeoutAt < leastTimeout ) {
				leastTimeout = flock->timeoutAt;
				oldestFlock = flock;
			}
		}
		assert( oldestFlock );
		wsw::unlink( oldestFlock, &bin.head );
		shapeList = oldestFlock->shapeList;
		oldestFlock->~ParticleFlock();
		mem = (uint8_t *)oldestFlock;
	}

	assert( mem );
	if( bin.needsShapeLists && !shapeList ) {
		assert( !m_freeShapeLists.empty() );
		shapeList = m_freeShapeLists.back();
		m_freeShapeLists.pop_back();
	}

	auto *const particles = (Particle *)( mem + sizeof( ParticleFlock ) );
	assert( ( (uintptr_t)particles % 16 ) == 0 );

	auto *const flock = new( mem )ParticleFlock {
		.particles = particles,
		.numParticlesLeft = bin.maxParticlesPerFlock,
		.binIndex = binIndex,
		.shapeList = shapeList,
	};

	wsw::link( flock, &bin.head );
	return flock;
}

auto ParticleSystem::createTrailFlock( const Particle::AppearanceRules &rules, unsigned binIndex ) -> ParticleFlock * {
	assert( binIndex == kClippedTrailFlocksBin || binIndex == kNonClippedTrailFlocksBin );

	// Don't let it evict anything
	const int64_t currTime = std::numeric_limits<int64_t>::min();
	ParticleFlock *flock = createFlock( binIndex, currTime );

	// Externally managed
	flock->timeoutAt = std::numeric_limits<int64_t>::max();
	flock->numParticlesLeft = 0;

	flock->appearanceRules = rules;
	return flock;
}

auto UniformFlockFiller::fill( Particle *__restrict particles, unsigned maxParticles,
							   wsw::RandomGenerator *__restrict rng, int64_t currTime ) __restrict
	-> std::pair<int64_t, unsigned> {
	const vec3_t initialOrigin { origin[0] + offset[0], origin[1] + offset[1], origin[2] + offset[2] };

	unsigned numParticles = maxParticles;
	// We do not specify the exact bounds but a percentage
	// so we can use the same filler for different bin flocks.
	if( this->minPercentage != 1.0f && this->maxPercentage != 1.0f ) {
		assert( this->minPercentage >= 0.0f && this->minPercentage <= 1.0f );
		assert( this->maxPercentage >= 0.0f && this->minPercentage <= 1.0f );
		const float percentage = rng->nextFloat( this->minPercentage, this->maxPercentage );
		numParticles = (unsigned)( (float)maxParticles * percentage );
		numParticles = std::clamp( numParticles, 1u, maxParticles );
	}

	assert( minSpeed >= 0.0f && minSpeed <= 1000.0f );
	assert( maxSpeed >= 0.0f && maxSpeed <= 1000.0f );
	assert( minSpeed <= maxSpeed );

	const vec3_t *__restrict dirs = ::kPredefinedDirs;

	assert( minTimeout && minTimeout <= maxTimeout && maxTimeout < 3000 );
	const unsigned timeoutSpread = maxTimeout - minTimeout;
	auto resultTimeout = std::numeric_limits<int64_t>::min();

	for( unsigned i = 0; i < numParticles; ++i ) {
		Particle *const __restrict p = particles + i;
		Vector4Set( p->oldOrigin, initialOrigin[0], initialOrigin[1], initialOrigin[2], 0.0f );
		Vector4Set( p->accel, 0, 0, -gravity, 0 );
		p->bouncesLeft = bounceCount;

		// We shouldn't really care of bias for some dirs in this case
		const float *randomDir = dirs[rng->nextBoundedFast( NUMVERTEXNORMALS )];
		const float speed = rng->nextFloat( minSpeed, maxSpeed );

		VectorScale( randomDir, speed, p->velocity );
		p->velocity[3] = 0.0f;

		p->spawnTime = currTime;
		p->lifetime = minTimeout + rng->nextBoundedFast( timeoutSpread );
		// TODO: Branchless?
		resultTimeout = std::max( p->spawnTime + p->lifetime, resultTimeout );

		const uint32_t randomDword = rng->next();
		p->instanceWidthFraction   = (int8_t)( ( randomDword >> 0 ) & 0xFF );
		p->instanceLengthFraction  = (int8_t)( ( randomDword >> 8 ) & 0xFF );
		p->instanceRadiusFraction  = (int8_t)( ( randomDword >> 16 ) & 0xFF );
	}

	return { resultTimeout, numParticles };
}

auto ConeFlockFiller::fill( Particle *__restrict particles, unsigned maxParticles,
							wsw::RandomGenerator *__restrict rng, int64_t currTime ) __restrict
	-> std::pair<int64_t, unsigned> {
	const vec3_t initialOrigin { origin[0] + offset[0], origin[1] + offset[1], origin[2] + offset[2] };

	unsigned numParticles = maxParticles;
	if( this->minPercentage != 1.0f && this->maxPercentage != 1.0f ) {
		assert( this->minPercentage >= 0.0f && this->minPercentage <= 1.0f );
		assert( this->maxPercentage >= 0.0f && this->minPercentage <= 1.0f );
		// We do not specify the exact bounds but a percentage
		// so we can use the same filler for different bin flocks.
		const float percentage = rng->nextFloat( this->minPercentage, this->maxPercentage );
		numParticles = (unsigned)( (float)maxParticles * percentage );
		numParticles = std::clamp( numParticles, 1u, maxParticles );
	}

	// TODO: Supply a cosine value as a parameter?
	const float minZ = std::cos( (float)DEG2RAD( angle ) );
	const float r = Q_Sqrt( 1.0f - minZ * minZ );

	assert( minTimeout && minTimeout <= maxTimeout && maxTimeout < 3000 );
	const unsigned timeoutSpread = maxTimeout - minTimeout;
	auto resultTimeout = std::numeric_limits<int64_t>::min();

	mat3_t transformMatrix;
	Matrix3_ForRotationOfDirs( &axis_identity[AXIS_UP], dir, transformMatrix );

	// TODO: Make cached conical samples for various angles?
	for( unsigned i = 0; i < numParticles; ++i ) {
		Particle *const __restrict p = particles + i;
		Vector4Set( p->oldOrigin, initialOrigin[0], initialOrigin[1], initialOrigin[2], 0.0f );
		Vector4Set( p->accel, 0, 0, -gravity, 0 );
		p->bouncesLeft = bounceCount;

		// https://math.stackexchange.com/a/205589
		const float z = minZ + ( 1.0f - minZ ) * rng->nextFloat( -1.0f, 1.0f );
		const float phi = 2.0f * (float)M_PI * rng->nextFloat();

		const float speed = rng->nextFloat( minSpeed, maxSpeed );
		const vec3_t untransformed { speed * r * std::cos( phi ), speed * r * std::sin( phi ), speed * z };
		Matrix3_TransformVector( transformMatrix, untransformed, p->velocity );

		p->spawnTime = currTime;
		p->lifetime = minTimeout + rng->nextBoundedFast( timeoutSpread );
		// TODO: Branchless?
		resultTimeout = std::max( p->spawnTime + p->lifetime, resultTimeout );

		const uint32_t randomDword = rng->next();
		p->instanceWidthFraction   = (int8_t)( ( randomDword >> 0 ) & 0xFF );
		p->instanceLengthFraction  = (int8_t)( ( randomDword >> 8 ) & 0xFF );
		p->instanceRadiusFraction  = (int8_t)( ( randomDword >> 16 ) & 0xFF );
	}

	return { resultTimeout, numParticles };
}

void ParticleSystem::runFrame( int64_t currTime, DrawSceneRequest *request ) {
	// Limit delta by sane bounds
	const float deltaSeconds = 1e-3f * (float)std::clamp( (int)( currTime - m_lastTime ), 1, 33 );
	m_lastTime = currTime;

	// We split simulation/rendering loops for a better instructions cache utilization
	for( FlocksBin &bin: m_bins ) {
		ParticleFlock *nextFlock = nullptr;
		for( ParticleFlock *flock = bin.head; flock; flock = nextFlock ) {
			nextFlock = flock->next;
			if( currTime < flock->timeoutAt ) [[likely]] {
				flock->simulate( currTime, deltaSeconds );
			} else {
				unlinkAndFree( flock );
			}
		}
	}

	for( FlocksBin &bin: m_bins ) {
		for( ParticleFlock *flock = bin.head; flock; flock = flock->next ) {
			if( const unsigned numParticles = flock->numParticlesLeft ) [[likely]] {
				request->addParticles( flock->mins, flock->maxs, flock->appearanceRules, flock->particles, numParticles );
			}
		}
	}
}

[[nodiscard]]
static inline auto computeParticleLifetimeFrac( int64_t currTime, const Particle &__restrict particle,
												const Particle::AppearanceRules &__restrict rules ) -> float {
	assert( (unsigned)rules.lifetimeFracOffsetMillis < (unsigned)particle.lifetime );
	const auto offset                 = (int)rules.lifetimeFracOffsetMillis;
	const auto correctedDuration      = (int)particle.lifetime - offset;
	const auto lifetimeSoFar          = (int)( currTime - particle.spawnTime );
	const auto correctedLifetimeSoFar = std::max( 0, lifetimeSoFar - offset );
	return (float)correctedLifetimeSoFar * Q_Rcp( (float)correctedDuration );
}

void ParticleFlock::simulate( int64_t currTime, float deltaSeconds ) {
	if( !numParticlesLeft ) [[unlikely]] {
		// Could be awaiting filling, don't modify its timeout
		return;
	}

	if( !shapeList ) [[unlikely]] {
		simulateWithoutClipping( currTime, deltaSeconds );
		return;
	}

	BoundsBuilder boundsBuilder;
	for( unsigned i = 0; i < numParticlesLeft; ++i ) {
		Particle *const __restrict particle = particles + i;
		VectorMA( particle->velocity, deltaSeconds, particle->accel, particle->velocity );

		VectorMA( particle->oldOrigin, deltaSeconds, particle->velocity, particle->origin );
		boundsBuilder.addPoint( particle->origin );
	}

	vec3_t possibleMins, possibleMaxs;
	boundsBuilder.storeToWithAddedEpsilon( possibleMins, possibleMaxs );
	// TODO: Add a fused call
	CM_BuildShapeList( cl.cms, shapeList, possibleMins, possibleMaxs, MASK_SOLID );
	CM_ClipShapeList( cl.cms, shapeList, shapeList, possibleMins, possibleMaxs );

	// TODO: Let the BoundsBuilder store 4-component vectors
	VectorCopy( possibleMins, this->mins );
	VectorCopy( possibleMaxs, this->maxs );
	this->mins[3] = 0.0f, this->maxs[3] = 1.0f;

	trace_t trace;

	auto timeoutOfParticlesLeft = std::numeric_limits<int64_t>::min();
	for( unsigned i = 0; i < numParticlesLeft; ) {
		Particle *const __restrict p = particles + i;
		if( const int64_t particleTimeoutAt = p->spawnTime + p->lifetime; particleTimeoutAt > currTime ) [[likely]] {
			CM_ClipToShapeList( cl.cms, shapeList, &trace, p->oldOrigin, p->origin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f ) [[likely]] {
				// Save the current origin as the old origin
				VectorCopy( p->origin, p->oldOrigin );
				p->lifetimeFrac = computeParticleLifetimeFrac( currTime, *p, appearanceRules );
				timeoutOfParticlesLeft = std::max( particleTimeoutAt, timeoutOfParticlesLeft );
				++i;
				continue;
			}

			if( !( trace.allsolid | trace.startsolid ) && !( trace.contents & CONTENTS_WATER ) ) {
				if( p->bouncesLeft ) {
					p->bouncesLeft--;

					// Reflect the velocity
					vec3_t oldVelocityDir { p->velocity[0], p->velocity[1], p->velocity[2] };
					if( const float oldSquareSpeed = VectorLengthSquared( oldVelocityDir ); oldSquareSpeed > 1.0f ) {
						const float invOldSpeed = Q_RSqrt( oldSquareSpeed );
						VectorScale( oldVelocityDir, invOldSpeed, oldVelocityDir );

						vec3_t reflectedVelocityDir;
						VectorReflect( oldVelocityDir, trace.plane.normal, 0, reflectedVelocityDir );

						const float newSpeed = 0.75f * Q_Rcp( invOldSpeed );
						// Save the reflected velocity
						VectorScale( reflectedVelocityDir, newSpeed, p->velocity );

						// Save the trace endpos with a slight offset as an origin for the next step.
						// This is not really correct but is OK.
						VectorAdd( trace.endpos, reflectedVelocityDir, p->oldOrigin );

						p->lifetimeFrac = computeParticleLifetimeFrac( currTime, *p, appearanceRules );
						timeoutOfParticlesLeft = std::max( particleTimeoutAt, timeoutOfParticlesLeft );
						++i;
						continue;
					}
				}
			}
		}

		// Dispose this particle
		// TODO: Avoid this memcpy call by copying components directly via intrinsics
		--numParticlesLeft;
		particles[i] = particles[numParticlesLeft];
	}

	if( numParticlesLeft ) {
		timeoutAt = timeoutOfParticlesLeft;
	} else {
		// Dispose next frame
		timeoutAt = 0;
	}
}

void ParticleFlock::simulateWithoutClipping( int64_t currTime, float deltaSeconds ) {
	assert( !shapeList && numParticlesLeft );

	auto timeoutOfParticlesLeft = std::numeric_limits<int64_t>::min();

	BoundsBuilder boundsBuilder;
	for( unsigned i = 0; i < numParticlesLeft; ) {
		Particle *const __restrict p = particles + i;
		if( const int64_t particleTimeoutAt = p->spawnTime + p->lifetime; particleTimeoutAt > currTime ) [[likely]] {
			// TODO: Two origins are redundant for non-clipped particles
			VectorMA( p->velocity, deltaSeconds, p->accel, p->velocity );

			VectorMA( p->oldOrigin, deltaSeconds, p->velocity, p->origin );
			VectorCopy( p->origin, p->oldOrigin );

			boundsBuilder.addPoint( p->origin );

			timeoutOfParticlesLeft = std::max( particleTimeoutAt, timeoutOfParticlesLeft );
			// TODO: Cache the reciprocal?
			p->lifetimeFrac = (float)( currTime - p->spawnTime ) * Q_Rcp( p->lifetime );

			++i;
		} else {
			particles[i] = particles[numParticlesLeft];
			numParticlesLeft--;
		}
	}

	// We still have to compute and store bounds as they're used by the renderer culling systems
	if( numParticlesLeft ) {
		vec3_t possibleMins, possibleMaxs;
		boundsBuilder.storeToWithAddedEpsilon( possibleMins, possibleMaxs );
		// TODO: Let the BoundsBuilder store 4-component vectors
		VectorCopy( possibleMins, this->mins );
		VectorCopy( possibleMaxs, this->maxs );
		this->mins[3] = 0.0f, this->maxs[3] = 1.0f;
	} else {
		constexpr float minVal = std::numeric_limits<float>::max(), maxVal = std::numeric_limits<float>::min();
		Vector4Set( this->mins, minVal, minVal, minVal, minVal );
		Vector4Set( this->maxs, maxVal, maxVal, maxVal, maxVal );
	}

	timeoutAt = timeoutOfParticlesLeft;
}