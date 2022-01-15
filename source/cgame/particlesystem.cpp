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
	new( m_bins.unsafe_grow_back() )FlocksBin( kMaxTrailFlockSize, kMaxTrailFlocks );
}

ParticleSystem::~ParticleSystem() {
	for( FlocksBin &bin: m_bins ) {
		ParticleFlock *nextFlock = nullptr;
		for( ParticleFlock *flock = bin.head; flock; flock = nextFlock ) {
			nextFlock = flock->next;
			releaseFlock( flock );
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

void ParticleSystem::releaseFlock( ParticleFlock *flock ) {
	FlocksBin &bin = m_bins[flock->binIndex];
	wsw::unlink( flock, &bin.head );
	m_freeShapeLists.push_back( flock->shapeList );
	flock->~ParticleFlock();
	bin.allocator.free( flock );
}

auto ParticleSystem::createFlock( unsigned binIndex, int64_t currTime ) -> ParticleFlock * {
	assert( binIndex < std::size( m_bins ) );
	FlocksBin &bin = m_bins[binIndex];

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
		oldestFlock->~ParticleFlock();
		mem = (uint8_t *)oldestFlock;
	}

	assert( mem && !m_freeShapeLists.empty() );
	CMShapeList *const shapeList = m_freeShapeLists.back();
	m_freeShapeLists.pop_back();

	auto *const particles = (BaseParticle *)( mem + sizeof( ParticleFlock ) );
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

auto UniformFlockFiller::fill( BaseParticle *__restrict particles, unsigned maxParticles,
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
	auto resultTimeout = std::numeric_limits<int64_t>::min();
	const int64_t baseTimeoutAt  = currTime + minTimeout;
	const unsigned timeoutSpread = maxTimeout - minTimeout;

	for( unsigned i = 0; i < numParticles; ++i ) {
		BaseParticle *const __restrict p = particles + i;
		Vector4Set( p->oldOrigin, initialOrigin[0], initialOrigin[1], initialOrigin[2], 0.0f );
		Vector4Set( p->accel, 0, 0, -gravity, 0 );
		p->bouncesLeft = bounceCount;

		// We shouldn't really care of bias for some dirs in this case
		const float *randomDir = dirs[rng->nextBoundedFast( NUMVERTEXNORMALS )];
		const float speed = rng->nextFloat( minSpeed, maxSpeed );

		VectorScale( randomDir, speed, p->velocity );
		p->velocity[3] = 0.0f;

		p->timeoutAt = baseTimeoutAt + rng->nextBoundedFast( timeoutSpread );
		// TODO: Branchless/track the relative part?
		resultTimeout = std::max( p->timeoutAt, resultTimeout );
	}

	return { resultTimeout, numParticles };
}

auto ConeFlockFiller::fill( BaseParticle *__restrict particles, unsigned maxParticles,
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
	auto resultTimeout = std::numeric_limits<int64_t>::min();
	const int64_t baseTimeoutAt  = currTime + minTimeout;
	const unsigned timeoutSpread = maxTimeout - minTimeout;

	mat3_t transformMatrix;
	VectorCopy( dir, transformMatrix );
	MakeNormalVectors( dir, transformMatrix + 3, transformMatrix + 6 );

	// TODO: Make cached conical samples for various angles?
	for( unsigned i = 0; i < numParticles; ++i ) {
		BaseParticle *const __restrict p = particles + i;
		Vector4Set( p->oldOrigin, initialOrigin[0], initialOrigin[1], initialOrigin[2], 0.0f );
		Vector4Set( p->accel, 0, 0, -gravity, 0 );
		p->bouncesLeft = bounceCount;

		// https://math.stackexchange.com/a/205589
		const float z = minZ + ( 1.0f - minZ ) * rng->nextFloat( -1.0f, 1.0f );
		const float phi = 2.0f * (float)M_PI * rng->nextFloat();

		const float speed = rng->nextFloat( minSpeed, maxSpeed );
		const vec3_t untransformed { speed * r * std::cos( phi ), speed * r * std::sin( phi ), speed * z };
		Matrix3_TransformVector( transformMatrix, untransformed, p->velocity );

		p->timeoutAt = baseTimeoutAt + rng->nextBoundedFast( timeoutSpread );
		// TODO: Branchless/track the relative part?
		resultTimeout = std::max( p->timeoutAt, resultTimeout );
	}

	return { resultTimeout, numParticles };
}

void ParticleSystem::runFrame( int64_t currTime, DrawSceneRequest * ) {
	// Limit delta by sane bounds
	const float deltaSeconds = 1e-3f * (float)std::clamp( (int)( currTime - m_lastTime ), 1, 33 );
	m_lastTime = currTime;

	// We split simulation/rendering loops for a better instructions cache utilization
	for( FlocksBin &bin: m_bins ) {
		ParticleFlock *nextFlock = nullptr;
		for( ParticleFlock *flock = bin.head; flock; flock = nextFlock ) {
			nextFlock = flock->next;
			// Safety measure
			if( currTime > flock->timeoutAt ) {
				releaseFlock( flock );
			} else {
				flock->simulate( currTime, deltaSeconds );
			}
		}
	}

	for( FlocksBin &bin: m_bins ) {
		for( ParticleFlock *flock = bin.head; flock; flock = flock->next ) {
			// TODO: Submit for rendering
		}
	}
}

void ParticleFlock::simulate( int64_t currTime, float deltaSeconds ) {
	if( !numParticlesLeft ) {
		return;
	}

	BoundsBuilder boundsBuilder;
	for( unsigned i = 0; i < numParticlesLeft; ++i ) {
		BaseParticle *const __restrict particle = particles + i;
		VectorMA( particle->velocity, deltaSeconds, particle->accel, particle->velocity );

		VectorMA( particle->oldOrigin, deltaSeconds, particle->velocity, particle->origin );
		boundsBuilder.addPoint( particle->origin );

		// Just for debugging as we can't draw yet
		vec3_t dir;
		VectorCopy( particle->velocity, dir );
		VectorNormalizeFast( dir );

		vec3_t origin2;
		VectorMA( particle->origin, 4.0f, dir, origin2 );
		CG_PLink( particle->origin, origin2, color, 0 );
	}

	vec3_t possibleMins, possibleMaxs;
	boundsBuilder.storeToWithAddedEpsilon( possibleMins, possibleMaxs );
	// TODO: Add a fused call
	CM_BuildShapeList( cl.cms, shapeList, possibleMins, possibleMaxs, MASK_SOLID );
	CM_ClipShapeList( cl.cms, shapeList, shapeList, possibleMins, possibleMaxs );

	trace_t trace;

	auto timeoutOfParticlesLeft = std::numeric_limits<int64_t>::min();
	for( unsigned i = 0; i < numParticlesLeft; ) {
		BaseParticle *const __restrict p = particles + i;
		if( p->timeoutAt > currTime ) [[likely]] {
			CM_ClipToShapeList( cl.cms, shapeList, &trace, p->oldOrigin, p->origin, vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction == 1.0f ) [[likely]] {
				// Save the current origin as the old origin
				VectorCopy( p->origin, p->oldOrigin );
				timeoutOfParticlesLeft = std::max( p->timeoutAt, timeoutOfParticlesLeft );
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

						++i;
						timeoutOfParticlesLeft = std::max( p->timeoutAt, timeoutOfParticlesLeft );
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