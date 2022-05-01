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
	for( ParticleTrail *trail = m_attachedTrailsHead, *next = nullptr; trail; trail = next ) { next = trail->next;
		unlinkAndFree( trail );
	}
	for( ParticleTrail *trail = m_lingeringTrailsHead, *next = nullptr; trail; trail = next ) { next = trail->next;
		unlinkAndFree( trail );
	}
	for( TeleEffect *effect = m_teleEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFree( effect );
	}
}

void TrackedEffectsSystem::unlinkAndFree( ParticleTrail *particleTrail ) {
	if( particleTrail->attachmentIndices ) [[unlikely]] {
		wsw::unlink( particleTrail, &m_attachedTrailsHead );
	} else {
		wsw::unlink( particleTrail, &m_lingeringTrailsHead );
	}

	cg.particleSystem.destroyTrailFlock( particleTrail->particleFlock );
	particleTrail->~ParticleTrail();
	m_particleTrailsAllocator.free( particleTrail );
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

		wsw::link( trail, &m_attachedTrailsHead );
		return trail;
	}

	return nullptr;
}

void TrackedEffectsSystem::updateAttachedParticleTrail( ParticleTrail *trail, const float *origin,
														ConeFlockParams *params, int64_t currTime ) {
	trail->touchedAt = currTime;

	ParticleFlock *__restrict flock = trail->particleFlock;
	// Prevent an automatic disposal by the particles system
	flock->timeoutAt = std::numeric_limits<int64_t>::max();

	if( trail->lastParticleAt < currTime && flock->numParticlesLeft < trail->maxParticlesInFlock ) {
		const float squareDistance = DistanceSquared( trail->lastDropOrigin, origin );
		if( squareDistance >= trail->dropDistance * trail->dropDistance ) {
			vec3_t dir, stepVec;
			VectorSubtract( trail->lastDropOrigin, origin, dir );

			const float rcpDistance = Q_RSqrt( squareDistance );
			const float distance = Q_Rcp( rcpDistance );
			// The dir is directed towards the old position
			VectorScale( dir, rcpDistance, dir );
			// Make steps of trail->dropDistance units towards the new position
			VectorScale( dir, -trail->dropDistance, stepVec );

			VectorCopy( trail->lastDropOrigin, params->origin );

			const unsigned numSteps = (unsigned)std::max( 1.0f, distance * Q_Rcp( trail->dropDistance ) );
			for( unsigned i = 0; i < numSteps; ++i ) {
				if( flock->numParticlesLeft + trail->maxParticlesPerDrop >= trail->maxParticlesInFlock ) [[unlikely]] {
					break;
				}

				Particle *particlesToFill = flock->particles + flock->numParticlesLeft;
				// Creates not less than 1 particle
				const auto [_, numParticles] = fillParticleFlock( params, particlesToFill, trail->maxParticlesPerDrop,
																  std::addressof( flock->appearanceRules ),
																  &m_rng, currTime );
				assert( numParticles );
				flock->numParticlesLeft += numParticles;
				VectorAdd( params->origin, stepVec, params->origin );
			}

			VectorCopy( params->origin, trail->lastDropOrigin );
			trail->lastParticleAt = currTime;
		}
	}
}

static const vec4_t kRocketTrailInitialColor { 1.0f, 0.7f, 0.3f, 1.0f };
static const vec4_t kRocketTrailFadedInColor { 1.0f, 1.0f, 1.0f, 0.3f };
static const vec4_t kRocketTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

static const vec4_t kRocketFireTrailInitialColor { 1.0f, 0.5f, 0.0f, 0.0f };
static const vec4_t kRocketFireTrailFadedInColor { 1.0f, 0.7f, 0.3f, 1.0f };
static const vec4_t kRocketFireTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

void TrackedEffectsSystem::touchRocketTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.initialColors  = &kRocketTrailInitialColor,
				.fadedInColors  = &kRocketTrailFadedInColor,
				.fadedOutColors = &kRocketTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 20.0f,
				.fadeInLifetimeFrac  = 0.45f,
				.fadeOutLifetimeFrac = 0.50f,
				.lifetimeFracOffsetMillis = 16,
				.sizeBehaviour = Particle::Expanding
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &m_rocketParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.initialColors  = &kRocketFireTrailInitialColor,
				.fadedInColors  = &kRocketFireTrailFadedInColor,
				.fadedOutColors = &kRocketFireTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 8.0f,
				.fadeInLifetimeFrac  = 0.30f,
				.fadeOutLifetimeFrac = 0.45f,
				.lifetimeFracOffsetMillis = 8,
				.sizeBehaviour = Particle::Shrinking
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &m_rocketFireParticlesFlockParams, currTime );
		}
	}
}

static const vec4_t kGrenadeFuseTrailInitialColor { 1.0f, 0.7f, 0.3f, 0.0f };
static const vec4_t kGrenadeFuseTrailFadedInColor { 1.0f, 0.7f, 0.3f, 1.0f };
static const vec4_t kGrenadeFuseTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

static const vec4_t kGrenadeSmokeTrailInitialColor { 1.0f, 0.7f, 0.3f, 0.0f };
static const vec4_t kGrenadeSmokeTrailFadedInColor { 1.0f, 1.0f, 1.0f, 0.3f };
static const vec4_t kGrenadeSmokeTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

void TrackedEffectsSystem::touchGrenadeTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.initialColors  = &kGrenadeSmokeTrailInitialColor,
				.fadedInColors  = &kGrenadeSmokeTrailFadedInColor,
				.fadedOutColors = &kGrenadeSmokeTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 16.0f,
				.fadeInLifetimeFrac  = 0.25f,
				.fadeOutLifetimeFrac = 0.35f,
				.lifetimeFracOffsetMillis = 8,
				.sizeBehaviour = Particle::Shrinking
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &m_grenadeSmokeParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.initialColors  = &kGrenadeFuseTrailInitialColor,
				.fadedInColors  = &kGrenadeFuseTrailFadedInColor,
				.fadedOutColors = &kGrenadeFuseTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 8.0f,
				.fadeInLifetimeFrac  = 0.075f,
				.fadeOutLifetimeFrac = 0.150f,
				.sizeBehaviour = Particle::Shrinking
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) {
			updateAttachedParticleTrail( trail, origin, &m_grenadeFuseParticlesFlockParams, currTime );
		}
	}
}

static const vec4_t kBlastSmokeTrailInitialColor { 1.0f, 0.5f, 0.4f, 0.0f };
static const vec4_t kBlastSmokeTrailFadedInColor { 1.0f, 0.8f, 0.4f, 0.25f };
static const vec4_t kBlastSmokeTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

static const vec4_t kBlastIonsTrailInitialColor { 1.0f, 0.5f, 0.4f, 1.0f };
static const vec4_t kBlastIonsTrailFadedInColor { 1.0f, 0.8f, 0.4f, 1.0f };
static const vec4_t kBlastIonsTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 1.0f };

void TrackedEffectsSystem::touchBlastTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.initialColors  = &kBlastSmokeTrailInitialColor,
				.fadedInColors  = &kBlastSmokeTrailFadedInColor,
				.fadedOutColors = &kBlastSmokeTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 10.0f,
				.fadeInLifetimeFrac  = 0.10f,
				.fadeOutLifetimeFrac = 0.20f,
				.lifetimeFracOffsetMillis = 8,
				.sizeBehaviour = Particle::Expanding
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &m_blastSmokeParticlesFlockParams, currTime );
		}
	}
	if( cg_projectileFireTrail->integer ) {
		if( !effects->particleTrails[1] ) [[unlikely]] {
			effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kClippedTrailsBin, {
				.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.initialColors  = &kBlastIonsTrailInitialColor,
				.fadedInColors  = &kBlastIonsTrailFadedInColor,
				.fadedOutColors = &kBlastIonsTrailFadedOutColor,
				.kind     = Particle::Sprite,
				.radius   = 5.0f,
				.lifetimeFracOffsetMillis = 8,
				.sizeBehaviour = Particle::Shrinking
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &m_blastIonsParticlesFlockParams, currTime );
		}
	}
}

static const vec4_t kElectroTrailInitialColor { 0.5f, 0.7f, 1.0f, 1.0f };
static const vec4_t kElectroTrailFadedInColor { 0.7f, 0.7f, 1.0f, 1.0f };
static const vec4_t kElectroTrailFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

void TrackedEffectsSystem::touchElectroTrail( int entNum, const float *origin, int64_t currTime ) {
	if( cg_projectileTrail->integer ) {
		AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
		if( !effects->particleTrails[0] ) {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kNonClippedTrailsBin, {
				.materials      = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.initialColors  = &kElectroTrailInitialColor,
				.fadedInColors  = &kElectroTrailFadedInColor,
				.fadedOutColors = &kElectroTrailFadedOutColor,
				.kind           = Particle::Sprite,
				.radius         = 6.0f,
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[0] ) {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &m_electroParticlesFlockParams, currTime );
		}
	}
}

void TrackedEffectsSystem::makeParticleTrailLingering( ParticleTrail *particleTrail ) {
	wsw::unlink( particleTrail, &m_attachedTrailsHead );
	wsw::link( particleTrail, &m_lingeringTrailsHead );

	const auto [entNum, trailIndex] = *particleTrail->attachmentIndices;
	particleTrail->attachmentIndices = std::nullopt;

	AttachedEntityEffects *entityEffects = &m_attachedEntityEffects[entNum];
	assert( entityEffects->particleTrails[trailIndex] == particleTrail );
	entityEffects->particleTrails[trailIndex] = nullptr;
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
}

void TrackedEffectsSystem::updateStraightLaserBeam( int ownerNum, const float *from, const float *to, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->straightLaserBeam ) {
		effects->straightLaserBeam = cg.polyEffectsSystem.createStraightBeamEffect( cgs.media.shaderLaserGunBeam );
	}

	effects->straightLaserBeamTouchedAt = currTime;
	cg.polyEffectsSystem.updateStraightBeamEffect( effects->straightLaserBeam, colorWhite, 12.0f, 64.0f, from, to );
}

void TrackedEffectsSystem::updateCurvedLaserBeam( int ownerNum, std::span<const vec3_t> points, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->curvedLaserBeam ) {
		effects->curvedLaserBeam = cg.polyEffectsSystem.createCurvedBeamEffect( cgs.media.shaderLaserGunBeam );
	}

	effects->curvedLaserBeamTouchedAt = currTime;
	cg.polyEffectsSystem.updateCurvedBeamEffect( effects->curvedLaserBeam, colorWhite, 12.0f, 64.0f, points );
}

void TrackedEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *drawSceneRequest ) {
	// Collect orphans
	for( ParticleTrail *trail = m_attachedTrailsHead, *next = nullptr; trail; trail = next ) { next = trail->next;
		if( trail->touchedAt != currTime ) [[unlikely]] {
			makeParticleTrailLingering( trail );
		}
	}

	for( ParticleTrail *trail = m_lingeringTrailsHead, *next = nullptr; trail; trail = next ) { next = trail->next;
		if( trail->particleFlock->numParticlesLeft ) {
			// Prevent an automatic disposal of the flock
			trail->particleFlock->timeoutAt = std::numeric_limits<int64_t>::max();
		} else {
			unlinkAndFree( trail );
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

	// TODO: Submit fire trails

	// The actual drawing of trails is performed by the particle system

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