/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2022 Chasseur de bots

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "trackedeffectssystem.h"
#include "../qcommon/links.h"
#include "../cgame/cg_local.h"

TrackedEffectsSystem::~TrackedEffectsSystem() {
	// TODO: unlinkAndFree() does some unnecessary extra work that slows the dtor down
	unlinkAndFreeItemsInList( m_attachedParticleTrailsHead );
	unlinkAndFreeItemsInList( m_lingeringParticleTrailsHead );

	unlinkAndFreeItemsInList( m_attachedCurvedPolyTrailsHead );
	unlinkAndFreeItemsInList( m_lingeringParticleTrailsHead );

	unlinkAndFreeItemsInList( m_teleEffectsHead );
}

template <typename Effect>
void TrackedEffectsSystem::unlinkAndFreeItemsInList( Effect *head ) {
	for( Effect *effect = head, *next = nullptr; effect; effect = next ) {
		next = effect->next;
		unlinkAndFree( effect );
	}
}

void TrackedEffectsSystem::unlinkAndFree( ParticleTrail *particleTrail ) {
	if( particleTrail->attachmentIndices ) [[unlikely]] {
		wsw::unlink( particleTrail, &m_attachedParticleTrailsHead );
		const auto [entNum, trailIndex] = *particleTrail->attachmentIndices;
		m_attachedEntityEffects[entNum].particleTrails[trailIndex] = nullptr;
	} else {
		wsw::unlink( particleTrail, &m_lingeringParticleTrailsHead );
	}

	cg.particleSystem.destroyTrailFlock( particleTrail->particleFlock );
	particleTrail->~ParticleTrail();
	m_particleTrailsAllocator.free( particleTrail );
}

void TrackedEffectsSystem::unlinkAndFree( StraightPolyTrail *polyTrail ) {
	if( polyTrail->attachedToEntNum ) {
		wsw::unlink( polyTrail, &m_attachedStraightPolyTrailsHead );
		m_attachedEntityEffects[*polyTrail->attachedToEntNum].straightPolyTrail = nullptr;
	} else {
		wsw::unlink( polyTrail, &m_lingeringStraightPolyTrailsHead );
	}

	cg.polyEffectsSystem.destroyStraightBeamEffect( polyTrail->beam );
	polyTrail->~StraightPolyTrail();
	m_straightPolyTrailsAllocator.free( polyTrail );
}

void TrackedEffectsSystem::unlinkAndFree( CurvedPolyTrail *polyTrail ) {
	if( polyTrail->attachedToEntNum ) {
		wsw::unlink( polyTrail, &m_attachedCurvedPolyTrailsHead );
		m_attachedEntityEffects[*polyTrail->attachedToEntNum].curvedPolyTrail = nullptr;
	} else {
		wsw::unlink( polyTrail, &m_lingeringCurvedPolyTrailsHead );
	}

	cg.polyEffectsSystem.destroyCurvedBeamEffect( polyTrail->beam );
	polyTrail->~CurvedPolyTrail();
	m_curvedPolyTrailsAllocator.free( polyTrail );
}

void TrackedEffectsSystem::unlinkAndFree( TeleEffect *teleEffect ) {
	assert( (unsigned)teleEffect->clientNum < (unsigned)MAX_CLIENTS );
	assert( teleEffect->inOutIndex == 0 || teleEffect->inOutIndex == 1 );

	wsw::unlink( teleEffect, &m_teleEffectsHead );
	m_attachedClientEffects[teleEffect->clientNum].teleEffects[teleEffect->inOutIndex] = nullptr;
	// TODO: Release the model!!!!
	teleEffect->~TeleEffect();
	// TODO: Release bone poses
	m_teleEffectsAllocator.free( teleEffect );
}

void TrackedEffectsSystem::spawnPlayerTeleEffect( int clientNum, const float *origin, model_s *model, int inOrOutIndex ) {
	assert( (unsigned)clientNum < (unsigned)MAX_CLIENTS );
	assert( inOrOutIndex == 0 || inOrOutIndex == 1 );

	TeleEffect **ppEffect = &m_attachedClientEffects[clientNum].teleEffects[inOrOutIndex];
	if( !*ppEffect ) [[likely]] {
		assert( !m_teleEffectsAllocator.isFull() );
		*ppEffect = new( m_teleEffectsAllocator.allocOrNull() )TeleEffect;
		wsw::link( *ppEffect, &m_teleEffectsHead );
	}

	TeleEffect *const __restrict effect = *ppEffect;
	VectorCopy( origin, effect->origin );
	effect->spawnTime = m_lastTime;
	effect->lifetime = 3000u;
	effect->clientNum = clientNum;
	effect->inOutIndex = inOrOutIndex;
	// TODO: Alloc bones, try reusing bones
	effect->model = model;
}

auto TrackedEffectsSystem::allocParticleTrail( int entNum, unsigned trailIndex,
											   const float *origin, unsigned particleSystemBin,
											   Particle::AppearanceRules &&appearanceRules ) -> ParticleTrail * {
	// Don't try evicting other effects in case of failure
	// (this could lead to wasting CPU cycles every frame in case when it starts kicking in)
	// TODO: Try picking lingering trails in case if it turns out to be really needed?
	if( void *mem = m_particleTrailsAllocator.allocOrNull() ) [[likely]] {
		auto *__restrict trail = new( mem )ParticleTrail;

		// Don't drop right now, just mark for computing direction next frames
		trail->particleFlock = cg.particleSystem.createTrailFlock( appearanceRules, particleSystemBin );

		VectorCopy( origin, trail->lastDropOrigin );
		if( particleSystemBin == kClippedTrailsBin ) {
			trail->maxParticlesInFlock = ParticleSystem::kMaxClippedTrailFlockSize;
		} else {
			trail->maxParticlesInFlock = ParticleSystem::kMaxNonClippedTrailFlockSize;
		}

		assert( entNum && entNum < MAX_EDICTS );
		assert( trailIndex == 0 || trailIndex == 1 );
		assert( !m_attachedEntityEffects[entNum].particleTrails[trailIndex] );
		trail->attachmentIndices = { (uint16_t)entNum, (uint8_t)trailIndex };

		wsw::link( trail, &m_attachedParticleTrailsHead );
		return trail;
	}

	return nullptr;
}

void TrackedEffectsSystem::updateAttachedParticleTrail( ParticleTrail *trail, const float *origin,
														ConicalFlockParams *params, int64_t currTime ) {
	trail->touchedAt = currTime;

	ParticleFlock *__restrict flock = trail->particleFlock;
	// Prevent an automatic disposal by the particles system
	flock->timeoutAt = std::numeric_limits<int64_t>::max();

	if( trail->lastParticleAt < currTime ) {
		if( flock->numActivatedParticles + flock->numDelayedParticles < trail->maxParticlesInFlock ) {
			const float squareDistance = DistanceSquared( trail->lastDropOrigin, origin );
			if( squareDistance >= trail->dropDistance * trail->dropDistance ) {
				vec3_t dir, stepVec;
				VectorSubtract( trail->lastDropOrigin, origin, dir );

				const float rcpDistance = Q_RSqrt( squareDistance );
				const float distance    = squareDistance * rcpDistance;
				// The dir is directed towards the old position
				VectorScale( dir, rcpDistance, dir );
				// Make steps of trail->dropDistance units towards the new position
				VectorScale( dir, -trail->dropDistance, stepVec );

				VectorCopy( trail->lastDropOrigin, params->origin );
				VectorCopy( dir, params->dir );

				const unsigned numSteps = (unsigned)wsw::max( 1.0f, distance * Q_Rcp( trail->dropDistance ) );
				for( unsigned stepNum = 0; stepNum < numSteps; ++stepNum ) {
					const unsigned numParticlesSoFar = flock->numActivatedParticles + flock->numDelayedParticles;
					if( numParticlesSoFar >= trail->maxParticlesInFlock ) [[unlikely]] {
						break;
					}

					signed fillStride;
					unsigned initialOffset;
					if( params->activationDelay.max == 0 ) {
						// Delayed particles must not be spawned in this case
						assert( !flock->numDelayedParticles );
						fillStride    = +1;
						initialOffset = flock->numActivatedParticles;
					} else {
						fillStride = -1;
						if( flock->delayedParticlesOffset ) {
							initialOffset = flock->delayedParticlesOffset - 1;
						} else {
							initialOffset = trail->maxParticlesInFlock - 1;
						}
					}

					const FillFlockResult fillResult = fillParticleFlock( params, flock->particles + initialOffset,
																		  trail->maxParticlesPerDrop,
																		  std::addressof( flock->appearanceRules ),
																		  &m_rng, currTime, fillStride );
					assert( fillResult.numParticles && fillResult.numParticles <= trail->maxParticlesPerDrop );

					if( params->activationDelay.max == 0 ) {
						flock->numActivatedParticles += fillResult.numParticles;
					} else {
						flock->numDelayedParticles += fillResult.numParticles;
						if( flock->delayedParticlesOffset ) {
							assert( flock->delayedParticlesOffset >= fillResult.numParticles );
							flock->delayedParticlesOffset -= fillResult.numParticles;
						} else {
							assert( trail->maxParticlesInFlock >= fillResult.numParticles );
							flock->delayedParticlesOffset = trail->maxParticlesInFlock - fillResult.numParticles;
						}
						assert( flock->delayedParticlesOffset + flock->numDelayedParticles <= trail->maxParticlesInFlock );
						assert( flock->numActivatedParticles <= flock->delayedParticlesOffset );
					}

					assert( flock->numDelayedParticles + flock->numActivatedParticles <= trail->maxParticlesInFlock );

					VectorAdd( params->origin, stepVec, params->origin );
				}

				VectorCopy( params->origin, trail->lastDropOrigin );
				trail->lastParticleAt = currTime;
			}
		}
	}
}

auto TrackedEffectsSystem::allocStraightPolyTrail( int entNum, shader_s *material, const float *origin )
	-> StraightPolyTrail * {
	// TODO: Reuse lingering trails?

	if( void *mem = m_straightPolyTrailsAllocator.allocOrNull() ) {
		if( auto *beam = cg.polyEffectsSystem.createStraightBeamEffect( material ) ) {
			auto *const trail       = new( mem )StraightPolyTrail;
			trail->attachedToEntNum = entNum;
			trail->beam             = beam;
			VectorCopy( origin, trail->initialOrigin );
			wsw::link( trail, &m_attachedStraightPolyTrailsHead );
			return trail;
		} else {
			m_straightPolyTrailsAllocator.free( mem );
		}
	}

	return nullptr;
}

auto TrackedEffectsSystem::allocCurvedPolyTrail( int entNum, shader_s *material ) -> CurvedPolyTrail * {
	// TODO: Reuse lingering trails?

	if( void *mem = m_curvedPolyTrailsAllocator.allocOrNull() ) {
		if( auto *beam = cg.polyEffectsSystem.createCurvedBeamEffect( material ) ) {
			auto *const trail = new( mem )CurvedPolyTrail;
			wsw::link( trail, &m_attachedCurvedPolyTrailsHead );
			trail->attachedToEntNum = entNum;
			trail->beam             = beam;
			return trail;
		} else {
			m_curvedPolyTrailsAllocator.free( mem );
		}
	}

	return nullptr;
}

void TrackedEffectsSystem::updateAttachedStraightPolyTrail( StraightPolyTrail *trail, const float *origin,
															int64_t currTime, const StraightPolyTrailProps &props ) {
	trail->touchedAt = currTime;
	trail->lastTileLength = 0;
	Vector4Copy( props.fromColor, trail->lastFromColor );
	Vector4Copy( props.toColor, trail->lastToColor );
	VectorCopy( origin, trail->lastTo );
	VectorCopy( origin, trail->lastFrom );

	const float squareDistance = DistanceSquared( origin, trail->initialOrigin );
	if( squareDistance > wsw::square( props.prestep ) ) {
		const float rcpDistance = Q_RSqrt( squareDistance );

		float length = props.maxLength;
		float width  = props.width;
		if( squareDistance < wsw::square( props.prestep + props.maxLength ) ) {
			length = ( squareDistance * rcpDistance ) - props.prestep;
			width  = props.width * ( length * Q_Rcp( props.maxLength ) );
		}

		if( length > 1.0f ) {
			vec3_t dir;
			VectorSubtract( trail->initialOrigin, origin, dir );
			VectorScale( dir, rcpDistance, dir );
			VectorMA( origin, length, dir, trail->lastFrom );
		}

		trail->lastWidth = width;
	}

	cg.polyEffectsSystem.updateStraightBeamEffect( trail->beam, trail->lastFromColor, trail->lastToColor,
												   trail->lastWidth, trail->lastTileLength,
												   trail->lastFrom, trail->lastTo );
}

void TrackedEffectsSystem::updateAttachedCurvedPolyTrail( CurvedPolyTrail *trail, const float *origin,
														  int64_t currTime, const CurvedPolyTrailProps &props ) {
	assert( trail->points.size() == trail->timestamps.size() );

	if( !trail->points.empty() ) {
		unsigned numTimedOutPoints = 0;
		for(; numTimedOutPoints < trail->timestamps.size(); ++numTimedOutPoints ) {
			if( trail->timestamps[numTimedOutPoints] + props.maxNodeLifetime > currTime ) {
				break;
			}
		}
		if( numTimedOutPoints ) {
			// TODO: Use some kind of deque, e.g. StaticDeque?
			trail->points.erase( trail->points.begin(), trail->points.begin() + numTimedOutPoints );
			trail->timestamps.erase( trail->timestamps.begin(), trail->timestamps.begin() + numTimedOutPoints );
		}
	}

	if( trail->points.size() > 1 ) {
		float totalLength = 0.0f;
		const unsigned maxSegmentNum = trail->points.size() - 2;
		for( unsigned segmentNum = 0; segmentNum <= maxSegmentNum; ++segmentNum ) {
			const float *pt1 = trail->points[segmentNum + 0].Data();
			const float *pt2 = trail->points[segmentNum + 1].Data();
			totalLength += DistanceFast( pt1, pt2 );
		}
		if( totalLength > props.maxLength ) {
			unsigned segmentNum = 0;
			for(; segmentNum <= maxSegmentNum; ++segmentNum ) {
				const float *pt1 = trail->points[segmentNum + 0].Data();
				const float *pt2 = trail->points[segmentNum + 1].Data();
				totalLength -= DistanceFast( pt1, pt2 );
				if( totalLength <= props.maxLength ) {
					break;
				}
			}
			// This condition preserves the last segment that breaks the loop.
			// segmentNum - 1 should be used instead if props.maxLength should never be reached.
			if( const unsigned numPointsToDrop = segmentNum ) {
				assert( numPointsToDrop <= trail->points.size() );
				trail->points.erase( trail->points.begin(), trail->points.begin() + numPointsToDrop );
				trail->timestamps.erase( trail->timestamps.begin(), trail->timestamps.begin() + numPointsToDrop );
			}
		}
	}

	bool shouldAddPoint = true;
	if( !trail->points.empty() ) [[likely]] {
		if( DistanceSquared( trail->points.back().Data(), origin ) < wsw::square( props.minDistanceBetweenNodes ) ) {
			shouldAddPoint = false;
		} else {
			if( trail->points.full() ) {
				trail->points.erase( trail->points.begin() );
				trail->timestamps.erase( trail->timestamps.begin() );
			}
		}
	}

	if( shouldAddPoint ) {
		trail->points.push_back( Vec3( origin ) );
		trail->timestamps.push_back( currTime );
	}

	assert( trail->points.size() == trail->timestamps.size() );

	trail->touchedAt      = currTime;
	trail->lastWidth      = props.width;
	trail->lastPointsSpan = { (const vec3_t *)trail->points.data(), trail->points.size() };

	Vector4Copy( props.toColor, trail->lastToColor );
	Vector4Copy( props.fromColor, trail->lastFromColor );

	cg.polyEffectsSystem.updateCurvedBeamEffect( trail->beam, trail->lastFromColor, trail->lastToColor,
												 trail->lastWidth, PolyEffectsSystem::UvModeFit {},
												 trail->lastPointsSpan );
}

static const ColorLifespan kRocketSmokeTrailColors[1] {
	{
		.initialColor  = { 1.0f, 0.7f, 0.3f, 1.0f },
		.fadedInColor  = { 0.7f, 0.7f, 0.7f, 0.15f },
		.fadedOutColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.4f,
		.startFadingOutAtLifetimeFrac = 0.5f,
	}
};

static const ColorLifespan kRocketFireTrailColors[1] {
	{
		.initialColor  = { 1.0f, 0.5f, 0.0f, 0.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.3f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.25f,
		.startFadingOutAtLifetimeFrac = 0.50f,
	}
};

static ConicalFlockParams rocketSmokeParticlesFlockParams {
	.gravity         = 0,
	.angle           = 45,
	.innerAngle      = 18,
	.speed           = { .min = 75, .max = 150 },
	.timeout         = { .min = 350, .max = 400 },
	.activationDelay = { .min = 8, .max = 8 },
};

static ConicalFlockParams rocketFireParticlesFlockParams {
	.gravity         = 0,
	.angle           = 15,
	.speed           = { .min = 75, .max = 150 },
	.timeout         = { .min = 125, .max = 250 },
	.activationDelay = { .min = 8, .max = 8 },
};

void TrackedEffectsSystem::touchRocketTrail( int entNum, const float *origin, int64_t currTime, bool useCurvedTrail ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileSmokeTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors        = kRocketSmokeTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 20.0f, .spread = 1.5f }, .sizeBehaviour = Particle::Expanding
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &::rocketSmokeParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors        = kRocketFireTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 7.0f, .spread = 1.0f }, .sizeBehaviour = Particle::Shrinking,
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &::rocketFireParticlesFlockParams, currTime );
		}
	}
	if( cg_projectilePolyTrail->integer ) {
		const bool hasOtherTrails = cg_projectileSmokeTrail->integer || cg_projectileFireTrail->integer;

		[[maybe_unused]] shader_s *material;
		[[maybe_unused]] float trailWidth;
		if( hasOtherTrails ) {
			material   = cgs.media.shaderBlastParticle;
			trailWidth = 20.0f;
		} else {
			material   = cgs.shaderWhite;
			trailWidth = 10.0f;
		}

		if( useCurvedTrail ) {
			if( !effects->curvedPolyTrail ) [[unlikely]] {
				effects->curvedPolyTrail = allocCurvedPolyTrail( entNum, material );
			}
			if( CurvedPolyTrail *trail = effects->curvedPolyTrail ) [[likely]] {
				updateAttachedCurvedPolyTrail( trail, origin, currTime, CurvedPolyTrailProps {
					.maxNodeLifetime = hasOtherTrails ? 300u : 350u,
					.maxLength       = hasOtherTrails ? 300.0f : 400.0f,
					.width           = trailWidth,
				});
			}
		} else {
			if( !effects->straightPolyTrail ) [[unlikely]] {
				effects->straightPolyTrail = allocStraightPolyTrail( entNum, material, origin );
			}
			if( StraightPolyTrail *trail = effects->straightPolyTrail ) [[likely]] {
				updateAttachedStraightPolyTrail( trail, origin, currTime, StraightPolyTrailProps {
					.maxLength = hasOtherTrails ? 250.0f : 325.0f,
					.width     = trailWidth,
				});
			}
		}
	}
}

static const ColorLifespan kGrenadeFuseTrailColors[1] {
	{
		.initialColor  = { 1.0f, 0.7f, 0.3f, 0.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.3f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.075f,
	}
};

static const ColorLifespan kGrenadeSmokeTrailColors[1] {
	{
		.initialColor  = { 1.0f, 0.7f, 0.3f, 0.0f },
		.fadedInColor  = { 0.7f, 0.7f, 0.7f, 0.2f },
		.fadedOutColor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.25f,
		.startFadingOutAtLifetimeFrac = 0.50f,
	}
};

static ConicalFlockParams grenadeFuseParticlesFlockParams {
	.gravity    = 0,
	.angle      = 60,
	.innerAngle = 30,
	.speed      = { .min = 50, .max = 75 },
	.timeout    = { .min = 125, .max = 175 },
};

static ConicalFlockParams grenadeSmokeParticlesFlockParams {
	.gravity         = 0,
	.angle           = 24,
	.innerAngle      = 9,
	.speed           = { .min = 25, .max = 50 },
	.timeout         = { .min = 300, .max = 350 },
	.activationDelay = { .min = 8, .max = 8 },
};

void TrackedEffectsSystem::touchGrenadeTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileSmokeTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors        = kGrenadeSmokeTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 20.0f, .spread = 1.0f }, .sizeBehaviour = Particle::Shrinking,
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &::grenadeSmokeParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors        = kGrenadeFuseTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 7.0f, .spread = 1.0f }, .sizeBehaviour = Particle::Shrinking,
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &::grenadeFuseParticlesFlockParams, currTime );
		}
	}
	if( cg_projectilePolyTrail->integer ) {
		[[maybe_unused]] shader_s *material;
		[[maybe_unused]] float trailWidth;
		[[maybe_unused]] float trailLength;
		if( cg_projectileSmokeTrail->integer || cg_projectileFireTrail->integer ) {
			material    = cgs.media.shaderBlastParticle;
			trailWidth  = 15.0f;
			trailLength = 150;
		} else {
			material    = cgs.shaderWhite;
			trailWidth  = 12.0f;
			trailLength = 250;
		}
		if( !effects->curvedPolyTrail ) [[unlikely]] {
			effects->curvedPolyTrail = allocCurvedPolyTrail( entNum, material );
		}
		if( CurvedPolyTrail *trail = effects->curvedPolyTrail ) [[likely]] {
			updateAttachedCurvedPolyTrail( trail, origin, currTime, CurvedPolyTrailProps {
				.maxNodeLifetime = 300,
				.maxLength       = trailLength,
				.width           = trailWidth,
				.fromColor       = { 1.0f, 1.0f, 1.0f, 0.0f },
				.toColor         = { 0.2f, 0.2f, 1.0f, 0.2f },
			});
		}
	}
}

static const ColorLifespan kBlastSmokeTrailColors[1] {
	{
		.initialColor  = { 1.0f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.5f, 0.1f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.1f,
		.startFadingOutAtLifetimeFrac = 0.5f,
	}
};

static const ColorLifespan kBlastIonsTrailColors[] {
	{
		.initialColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		.initialColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.6f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		.initialColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedInColor  = { 1.0f, 0.9f, 0.9f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 1.0f },
	},
	{
		.initialColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedInColor  = { 1.0f, 0.9f, 0.8f, 1.0f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 1.0f },
	}
};

static ConicalFlockParams blastSmokeParticlesFlockParams {
	.gravity = 0,
	.angle   = 24,
	.speed   = { .min = 200, .max = 300 },
	.timeout = { .min = 175, .max = 225 },
};

static ConicalFlockParams blastIonsParticlesFlockParams {
	.gravity         = -75,
	.angle           = 30,
	.speed           = { .min = 200, .max = 300 },
	.timeout         = { .min = 300, .max = 400 },
};

void TrackedEffectsSystem::touchBlastTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileSmokeTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors        = kBlastSmokeTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 10.0f, .spread = 1.0f }, .sizeBehaviour = Particle::Expanding,
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &::blastSmokeParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials     = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors        = kBlastIonsTrailColors,
				.geometryRules = Particle::SpriteRules {
					.radius = { .mean = 3.0f, .spread = 0.75f }, .sizeBehaviour = Particle::Shrinking,
				},
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &::blastIonsParticlesFlockParams, currTime );
		}
	}
	if( cg_projectilePolyTrail->integer ) {
		[[maybe_unused]] shader_s *material;
		[[maybe_unused]] float trailWidth;
		[[maybe_unused]] float trailLength;
		if( cg_projectileSmokeTrail->integer || cg_projectileFireTrail->integer ) {
			material    = cgs.media.shaderBlastParticle;
			trailWidth  = 20.0f;
			trailLength = 250.0f;
		} else {
			material    = cgs.shaderWhite;
			trailWidth  = 12.0f;
			trailLength = 600.0f;
		}
		if( !effects->straightPolyTrail ) {
			effects->straightPolyTrail = allocStraightPolyTrail( entNum, material, origin );
		}
		if( StraightPolyTrail *trail = effects->straightPolyTrail ) {
			updateAttachedStraightPolyTrail( trail, origin, currTime, StraightPolyTrailProps {
				.maxLength = trailLength,
				.width     = trailWidth,
			});
		}
	}
}

static ParticleColorsForTeamHolder electroCloudTrailParticleColorsHolder {
	.defaultColors = {
		.initialColor  = { 0.5f, 0.7f, 1.0f, 1.0f },
		.fadedInColor  = { 0.7f, 0.7f, 1.0f, 0.2f },
		.fadedOutColor = { 1.0f, 1.0f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.1f,
		.startFadingOutAtLifetimeFrac = 0.5f,
	}
};

static const ColorLifespan kElectroIonsTrailColors[5] {
	// All components are the same so we omit field designators
	{ { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
	{ { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f } },
	{ { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f } },
	{ { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f }, { 0.5f, 0.7f, 1.0f, 1.0f } },
	{ { 0.3f, 0.3f, 1.0f, 1.0f }, { 0.3f, 0.3f, 1.0f, 1.0f }, { 0.3f, 0.3f, 1.0f, 1.0f } },
};

static ParticleColorsForTeamHolder electroIonsParticleColorsHolder {
	.defaultColors = {
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 1.0f }
	}
};

static ConicalFlockParams electroCloudParticlesFlockParams {
	.gravity = 0,
	.angle   = 30,
	.speed   = { .min = 200, .max = 300 },
	.timeout = { .min = 150, .max = 200 },
};

static ConicalFlockParams electroIonsParticlesFlockParams {
	.gravity         = 0,
	.angle           = 18,
	.speed           = { .min = 200, .max = 300 },
	.timeout         = { .min = 250, .max = 300 },
};

void TrackedEffectsSystem::touchElectroTrail( int entNum, int ownerNum, const float *origin, int64_t currTime ) {
	std::span<const ColorLifespan> cloudColors, ionsColors;

	bool useTeamColors = false;
	[[maybe_unused]] vec4_t teamColor;
	[[maybe_unused]] int team = TEAM_PLAYERS;
	// The trail is not a beam, but should conform to the strong beam color as well
	if( cg_teamColoredBeams->integer ) {
		team = getTeamForOwner( ownerNum );
		if( team == TEAM_ALPHA || team == TEAM_BETA ) {
			CG_TeamColor( team, teamColor );
			useTeamColors = true;
		}
	}

	ParticleColorsForTeamHolder *const cloudColorsHolder = &::electroCloudTrailParticleColorsHolder;
	if( useTeamColors ) {
		cloudColors = { cloudColorsHolder->getColorsForTeam( team, teamColor ), 1 };
		// Use the single color, the trail appearance looks worse than default anyway.
		// TODO: Make ions colors lighter at least
		ionsColors = { ::electroIonsParticleColorsHolder.getColorsForTeam( team, teamColor ), 1 };
	} else {
		cloudColors = { &cloudColorsHolder->defaultColors, 1 };
		ionsColors = kElectroIonsTrailColors;
	}

	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( !effects->particleTrails[0] ) [[unlikely]] {
		effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kNonClippedTrailsBin, {
			.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
			.colors        = cloudColors,
			.geometryRules = Particle::SpriteRules {
				.radius = { .mean = 9.0f, .spread = 1.0f }, .sizeBehaviour = Particle::Expanding,
			},
		});
	}
	if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
		trail->dropDistance = 16.0f;
		updateAttachedParticleTrail( trail, origin, &::electroCloudParticlesFlockParams, currTime );
	}

	if( !effects->particleTrails[1] ) [[unlikely]] {
		effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kNonClippedTrailsBin, {
			.materials     = cgs.media.shaderBlastParticle.getAddressOfHandle(),
			.colors        = ionsColors,
			.geometryRules = Particle::SpriteRules {
				.radius = { .mean = 3.0f, .spread = 0.75f }, .sizeBehaviour = Particle::Shrinking,
			},
		});
	}
	if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
		trail->dropDistance = 16.0f;
		updateAttachedParticleTrail( trail, origin, &::electroIonsParticlesFlockParams, currTime );
	}

	if( !effects->straightPolyTrail ) [[unlikely]] {
		effects->straightPolyTrail = allocStraightPolyTrail( entNum, cgs.media.shaderBlastParticle, origin );
	}
	if( StraightPolyTrail *trail = effects->straightPolyTrail ) [[likely]] {
		updateAttachedStraightPolyTrail( trail, origin, currTime, StraightPolyTrailProps {
			.maxLength = 700.0f,
			.width     = 20.0f,
		});
	}
}

void TrackedEffectsSystem::touchStrongPlasmaTrail( int entNum, const float *origin, int64_t currTime ) {
	assert( entNum > 0 && entNum < MAX_EDICTS );
	if( cg_plasmaTrail->integer && cg_projectilePolyTrail->integer ) {
		AttachedEntityEffects *effects = &m_attachedEntityEffects[entNum];
		if( !effects->straightPolyTrail ) {
			effects->straightPolyTrail = allocStraightPolyTrail( entNum, cgs.media.shaderElectroParticle, origin );
		}
		if( StraightPolyTrail *trail = effects->straightPolyTrail ) {
			updateAttachedStraightPolyTrail( trail, origin, currTime, StraightPolyTrailProps {
				.maxLength = 350,
				.width     = 12.0f,
				.fromColor = { 0.3f, 1.0f, 1.0f, 0.00f },
				.toColor   = { 0.1f, 0.8f, 0.4f, 0.15f },
			});
		}
	}
}

void TrackedEffectsSystem::touchWeakPlasmaTrail( int entNum, const float *origin, int64_t currTime ) {
	assert( entNum > 0 && entNum < MAX_EDICTS );
	if( cg_plasmaTrail->integer && cg_projectilePolyTrail->integer ) {
		AttachedEntityEffects *effects = &m_attachedEntityEffects[entNum];
		if( !effects->curvedPolyTrail ) {
			effects->curvedPolyTrail = allocCurvedPolyTrail( entNum, cgs.media.shaderElectroParticle );
		}
		if( CurvedPolyTrail *trail = effects->curvedPolyTrail ) {
			updateAttachedCurvedPolyTrail( trail, origin, currTime, CurvedPolyTrailProps {
				.maxNodeLifetime = 225,
				.maxLength       = 400,
				.width           = 12.0f,
				.fromColor       = { 0.3f, 1.0f, 1.0f, 0.00f },
				.toColor         = { 0.1f, 0.8f, 0.4f, 0.15f },
			});
		}
	}
}

void TrackedEffectsSystem::makeParticleTrailLingering( ParticleTrail *particleTrail ) {
	wsw::unlink( particleTrail, &m_attachedParticleTrailsHead );
	wsw::link( particleTrail, &m_lingeringParticleTrailsHead );

	const auto [entNum, trailIndex] = *particleTrail->attachmentIndices;
	particleTrail->attachmentIndices = std::nullopt;

	AttachedEntityEffects *entityEffects = &m_attachedEntityEffects[entNum];
	assert( entityEffects->particleTrails[trailIndex] == particleTrail );
	entityEffects->particleTrails[trailIndex] = nullptr;
}

void TrackedEffectsSystem::makePolyTrailLingering( StraightPolyTrail *polyTrail ) {
	wsw::unlink( polyTrail, &m_attachedStraightPolyTrailsHead );
	wsw::link( polyTrail, &m_lingeringStraightPolyTrailsHead );

	const unsigned entNum = *polyTrail->attachedToEntNum;
	polyTrail->attachedToEntNum = std::nullopt;

	AttachedEntityEffects *entityEffects = &m_attachedEntityEffects[entNum];
	assert( entityEffects->straightPolyTrail == polyTrail );
	entityEffects->straightPolyTrail = nullptr;
}

void TrackedEffectsSystem::makePolyTrailLingering( CurvedPolyTrail *polyTrail ) {
	wsw::unlink( polyTrail, &m_attachedCurvedPolyTrailsHead );
	wsw::link( polyTrail, &m_lingeringCurvedPolyTrailsHead );
	const unsigned entNum = *polyTrail->attachedToEntNum;
	polyTrail->attachedToEntNum = std::nullopt;

	AttachedEntityEffects *entityEffects = &m_attachedEntityEffects[entNum];
	assert( entityEffects->curvedPolyTrail == polyTrail );
	entityEffects->curvedPolyTrail = nullptr;
}

void TrackedEffectsSystem::resetEntityEffects( int entNum ) {
	const int maybeValidClientNum = entNum - 1;
	if( (unsigned)maybeValidClientNum < (unsigned)MAX_CLIENTS ) [[unlikely]] {
		AttachedClientEffects *effects = &m_attachedClientEffects[maybeValidClientNum];
		if( effects->teleEffects[0] ) {
			unlinkAndFree( effects->teleEffects[0] );
		}
		if( effects->teleEffects[1] ) {
			unlinkAndFree( effects->teleEffects[1] );
		}
		assert( !effects->teleEffects[0] && !effects->teleEffects[1] );
		if( effects->curvedLaserBeam ) {
			cg.polyEffectsSystem.destroyCurvedBeamEffect( effects->curvedLaserBeam );
			effects->curvedLaserBeam = nullptr;
			effects->curvedLaserBeamTouchedAt = 0;
		}
		if( effects->straightLaserBeam ) {
			cg.polyEffectsSystem.destroyStraightBeamEffect( effects->straightLaserBeam );
			effects->straightLaserBeam = nullptr;
			effects->straightLaserBeamTouchedAt = 0;
		}
	}

	assert( entNum >= 0 && entNum < MAX_EDICTS );
	AttachedEntityEffects *effects = &m_attachedEntityEffects[entNum];
	if( effects->particleTrails[0] ) {
		makeParticleTrailLingering( effects->particleTrails[0] );
		assert( !effects->particleTrails[0] );
	}
	if( effects->particleTrails[1] ) {
		makeParticleTrailLingering( effects->particleTrails[1] );
		assert( !effects->particleTrails[1] );
	}
	if( effects->straightPolyTrail ) {
		makePolyTrailLingering( effects->straightPolyTrail );
		assert( !effects->straightPolyTrail );
	}
	if( effects->curvedPolyTrail ) {
		makePolyTrailLingering( effects->curvedPolyTrail );
		assert( !effects->curvedPolyTrail );
	}
}

static void getLaserColorOverlayForOwner( int ownerNum, vec4_t color ) {
	if( cg_teamColoredBeams->integer ) {
		if( int team = getTeamForOwner( ownerNum ); team == TEAM_ALPHA || team == TEAM_BETA ) {
			CG_TeamColor( team, color );
			return;
		}
	}
	Vector4Copy( colorWhite, color );
}

static constexpr float kLaserWidth      = 12.0f;
static constexpr float kLaserTileLength = 64.0f;

void TrackedEffectsSystem::updateStraightLaserBeam( int ownerNum, const float *from, const float *to, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->straightLaserBeam ) {
		effects->straightLaserBeam = cg.polyEffectsSystem.createStraightBeamEffect( cgs.media.shaderLaserGunBeam );
		if( effects->straightLaserBeam ) {
			getLaserColorOverlayForOwner( ownerNum, &effects->laserColor[0] );
		}
	}
	if( effects->straightLaserBeam ) {
		effects->straightLaserBeamTouchedAt = currTime;
		cg.polyEffectsSystem.updateStraightBeamEffect( effects->straightLaserBeam,
													   effects->laserColor, effects->laserColor,
													   kLaserWidth, kLaserTileLength, from, to );
	}
}

void TrackedEffectsSystem::updateCurvedLaserBeam( int ownerNum, std::span<const vec3_t> points, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->curvedLaserBeam ) {
		effects->curvedLaserBeam = cg.polyEffectsSystem.createCurvedBeamEffect( cgs.media.shaderLaserGunBeam );
		if( effects->curvedLaserBeam ) {
			getLaserColorOverlayForOwner( ownerNum, &effects->laserColor[0] );
		}
	}

	if( effects->curvedLaserBeam ) {
		effects->curvedLaserBeamTouchedAt = currTime;
		effects->curvedLaserBeamPoints.clear();
		for( const float *point: points ) {
			effects->curvedLaserBeamPoints.push_back( Vec3( point ) );
		}
		const std::span<const vec3_t> ownedPointsSpan {
			(const vec3_t *)effects->curvedLaserBeamPoints.data(), effects->curvedLaserBeamPoints.size()
		};
		cg.polyEffectsSystem.updateCurvedBeamEffect( effects->curvedLaserBeam,
													 effects->laserColor, effects->laserColor,
													 kLaserWidth, PolyEffectsSystem::UvModeTile { kLaserTileLength },
													 ownedPointsSpan );
	}
}

void TrackedEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *drawSceneRequest ) {
	// Collect orphans.

	// The actual drawing of trails is performed by the particle system.
	for( ParticleTrail *trail = m_attachedParticleTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->touchedAt != currTime ) [[unlikely]] {
			makeParticleTrailLingering( trail );
		}
	}

	for( ParticleTrail *trail = m_lingeringParticleTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->particleFlock->numActivatedParticles ) {
			// Prevent an automatic disposal of the flock
			trail->particleFlock->timeoutAt = std::numeric_limits<int64_t>::max();
		} else {
			unlinkAndFree( trail );
		}
	}

	// The actual drawing of polys is performed by the poly effects system

	for( StraightPolyTrail *trail = m_attachedStraightPolyTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->touchedAt != currTime ) [[unlikely]] {
			// If it is worth to be kept
			if( DistanceSquared( trail->lastFrom, trail->lastTo ) > wsw::square( 8.0f ) ) {
				makePolyTrailLingering( trail );
			} else {
				unlinkAndFree( trail );
			}
		}
	}

	for( CurvedPolyTrail *trail = m_attachedCurvedPolyTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->touchedAt != currTime ) [[unlikely]] {
			// If it is worth to be kept
			if( trail->lastPointsSpan.size() > 1 ) {
				makePolyTrailLingering( trail );
			} else {
				unlinkAndFree( trail );
			}
		}
	}

	constexpr int64_t lingeringLimit  = 192;
	constexpr float rcpLingeringLimit = 1.0f / (float)lingeringLimit;

	for( StraightPolyTrail *trail = m_lingeringStraightPolyTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->touchedAt + lingeringLimit <= currTime ) {
			unlinkAndFree( trail );
		} else {
			const float lingeringFrac = (float)( currTime - trail->touchedAt ) * rcpLingeringLimit;
			vec4_t fadingOutFromColor, fadingOutToColor;
			Vector4Copy( trail->lastFromColor, fadingOutFromColor );
			Vector4Copy( trail->lastToColor, fadingOutToColor );
			fadingOutFromColor[3] *= ( 1.0f - lingeringFrac );
			fadingOutToColor[3]   *= ( 1.0f - lingeringFrac );
			cg.polyEffectsSystem.updateStraightBeamEffect( trail->beam, fadingOutFromColor, fadingOutToColor,
														   trail->lastWidth, trail->lastTileLength,
														   trail->lastFrom, trail->lastTo );
		}
	}

	for( CurvedPolyTrail *trail = m_lingeringCurvedPolyTrailsHead, *nextTrail = nullptr; trail; trail = nextTrail ) {
		nextTrail = trail->next;
		if( trail->touchedAt + lingeringLimit <= currTime ) {
			unlinkAndFree( trail );
		} else {
			const float lingeringFrac = (float)( currTime - trail->touchedAt ) * rcpLingeringLimit;
			vec4_t fadingOutFromColor, fadingOutToColor;
			Vector4Copy( trail->lastFromColor, fadingOutFromColor );
			Vector4Copy( trail->lastToColor, fadingOutToColor );
			fadingOutFromColor[3] *= ( 1.0f - lingeringFrac );
			fadingOutToColor[3]   *= ( 1.0f - lingeringFrac );
			cg.polyEffectsSystem.updateCurvedBeamEffect( trail->beam, fadingOutFromColor, fadingOutToColor,
														 trail->lastWidth, PolyEffectsSystem::UvModeFit {},
														 trail->lastPointsSpan );
		}
	}

	// Simulate
	for( TeleEffect *effect = m_teleEffectsHead, *nextEffect = nullptr; effect; effect = nextEffect ) {
		nextEffect = effect->next;
		if( effect->spawnTime + effect->lifetime >= currTime ) [[unlikely]] {
			unlinkAndFree( effect );
			continue;
		}

		const float frac = (float)( currTime - effect->spawnTime ) * Q_Rcp((float)effect->lifetime );
		// TODO: Simulate
		(void)frac;

		// TODO: Submit
	}

	PolyEffectsSystem *const polyEffectsSystem = &cg.polyEffectsSystem;
	for( unsigned i = 0; i < MAX_CLIENTS; ++i ) {
		AttachedClientEffects *const effects = &m_attachedClientEffects[i];
		if( effects->curvedLaserBeam ) {
			if( effects->curvedLaserBeamTouchedAt < currTime ) {
				polyEffectsSystem->destroyCurvedBeamEffect( effects->curvedLaserBeam );
				effects->curvedLaserBeam = nullptr;
			}
		}
		if( effects->straightLaserBeam ) {
			if( effects->straightLaserBeamTouchedAt < currTime ) {
				polyEffectsSystem->destroyStraightBeamEffect( effects->straightLaserBeam );
				effects->straightLaserBeam = nullptr;
			}
		}
	}
}