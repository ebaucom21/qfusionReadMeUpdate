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
														ConicalFlockParams *params, int64_t currTime ) {
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

			const unsigned numSteps = (unsigned)wsw::max( 1.0f, distance * Q_Rcp( trail->dropDistance ) );
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
	.gravity     = -250,
	.angle       = 45,
	.innerAngle  = 18,
	.minSpeed    = 75,
	.maxSpeed    = 150,
	.minTimeout  = 350,
	.maxTimeout  = 400
};

static ConicalFlockParams rocketFireParticlesFlockParams {
	.gravity     = -250,
	.angle       = 15,
	.minSpeed    = 75,
	.maxSpeed    = 150,
	.minTimeout  = 125,
	.maxTimeout  = 250
};

void TrackedEffectsSystem::touchRocketTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials             = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors                = kRocketSmokeTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 20.0f,
				.radiusSpread          = 1.5f,
				.sizeBehaviour         = Particle::Expanding,
				.lifetimeOffsetMillis  = 8,
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
				.materials             = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors                = kRocketFireTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 7.0f,
				.radiusSpread          = 1.0f,
				.sizeBehaviour         = Particle::Shrinking,
				.lifetimeOffsetMillis  = 8,
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &::rocketFireParticlesFlockParams, currTime );
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
	.gravity     = -250,
	.angle       = 60,
	.innerAngle  = 30,
	.minSpeed    = 50,
	.maxSpeed    = 75,
	.minTimeout  = 100,
	.maxTimeout  = 150
};

static ConicalFlockParams grenadeSmokeParticlesFlockParams {
	.gravity     = -250,
	.angle       = 30,
	.innerAngle  = 12,
	.minSpeed    = 50,
	.maxSpeed    = 75,
	.minTimeout  = 200,
	.maxTimeout  = 250
};

void TrackedEffectsSystem::touchGrenadeTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials             = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors                = kGrenadeSmokeTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 20.0f,
				.radiusSpread          = 1.0f,
				.sizeBehaviour         = Particle::Shrinking,
				.lifetimeOffsetMillis  = 8,
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
				.materials             = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors                = kGrenadeFuseTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 7.0f,
				.radiusSpread          = 1.0f,
				.sizeBehaviour         = Particle::Shrinking,
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) {
			trail->dropDistance = 8.0f;
			updateAttachedParticleTrail( trail, origin, &::grenadeFuseParticlesFlockParams, currTime );
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
	.gravity     = -250,
	.angle       = 24,
	.minSpeed    = 200,
	.maxSpeed    = 300,
	.minTimeout  = 175,
	.maxTimeout  = 225
};

static ConicalFlockParams blastIonsParticlesFlockParams {
	.gravity     = -250,
	.angle       = 30,
	.minSpeed    = 200,
	.maxSpeed    = 300,
	.minTimeout  = 250,
	.maxTimeout  = 300
};

void TrackedEffectsSystem::touchBlastTrail( int entNum, const float *origin, int64_t currTime ) {
	AttachedEntityEffects *const __restrict effects = &m_attachedEntityEffects[entNum];
	if( cg_projectileTrail->integer ) {
		if( !effects->particleTrails[0] ) [[unlikely]] {
			effects->particleTrails[0] = allocParticleTrail( entNum, 0, origin, kClippedTrailsBin, {
				.materials             = cgs.media.shaderFlareParticle.getAddressOfHandle(),
				.colors                = kBlastSmokeTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 10.0f,
				.radiusSpread          = 1.0f,
				.sizeBehaviour         = Particle::Expanding,
				.lifetimeOffsetMillis  = 4,
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
				.materials             = cgs.media.shaderBlastParticle.getAddressOfHandle(),
				.colors                = kBlastIonsTrailColors,
				.kind                  = Particle::Sprite,
				.radius                = 3.0f,
				.radiusSpread          = 0.75f,
				.sizeBehaviour         = Particle::Shrinking,
				.lifetimeOffsetMillis  = 4,
			});
		}
		if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
			updateAttachedParticleTrail( trail, origin, &::blastIonsParticlesFlockParams, currTime );
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
	.gravity     = 0,
	.angle       = 30,
	.minSpeed    = 200,
	.maxSpeed    = 300,
	.minTimeout  = 150,
	.maxTimeout  = 200
};

static ConicalFlockParams electroIonsParticlesFlockParams {
	.gravity     = 0,
	.angle       = 18,
	.minSpeed    = 200,
	.maxSpeed    = 300,
	.minTimeout  = 250,
	.maxTimeout  = 300
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
			.kind          = Particle::Sprite,
			.radius        = 9.0f,
			.radiusSpread  = 1.0f,
			.sizeBehaviour = Particle::Expanding
		});
	}
	if( ParticleTrail *trail = effects->particleTrails[0] ) [[likely]] {
		trail->dropDistance = 16.0f;
		updateAttachedParticleTrail( trail, origin, &::electroCloudParticlesFlockParams, currTime );
	}

	if( !effects->particleTrails[1] ) [[unlikely]] {
		effects->particleTrails[1] = allocParticleTrail( entNum, 1, origin, kNonClippedTrailsBin, {
			.materials             = cgs.media.shaderBlastParticle.getAddressOfHandle(),
			.colors                = ionsColors,
			.kind                  = Particle::Sprite,
			.radius                = 3.0f,
			.radiusSpread          = 0.75f,
			.sizeBehaviour         = Particle::Shrinking
		});
	}
	if( ParticleTrail *trail = effects->particleTrails[1] ) [[likely]] {
		trail->dropDistance = 16.0f;
		updateAttachedParticleTrail( trail, origin, &::electroIonsParticlesFlockParams, currTime );
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

static void getLaserColorOverlayForOwner( int ownerNum, vec4_t color ) {
	if( cg_teamColoredBeams->integer ) {
		if( int team = getTeamForOwner( ownerNum ); team == TEAM_ALPHA || team == TEAM_BETA ) {
			CG_TeamColor( team, color );
			return;
		}
	}
	Vector4Copy( colorWhite, color );
}

void TrackedEffectsSystem::updateStraightLaserBeam( int ownerNum, const float *from, const float *to, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->straightLaserBeam ) {
		effects->straightLaserBeam = cg.polyEffectsSystem.createStraightBeamEffect( cgs.media.shaderLaserGunBeam );
	}

	vec4_t color;
	getLaserColorOverlayForOwner( ownerNum, color );

	effects->straightLaserBeamTouchedAt = currTime;
	cg.polyEffectsSystem.updateStraightBeamEffect( effects->straightLaserBeam, color, 12.0f, 64.0f, from, to );
}

void TrackedEffectsSystem::updateCurvedLaserBeam( int ownerNum, std::span<const vec3_t> points, int64_t currTime ) {
	assert( ownerNum && ownerNum <= MAX_CLIENTS );
	AttachedClientEffects *effects = &m_attachedClientEffects[ownerNum - 1];
	if( !effects->curvedLaserBeam ) {
		effects->curvedLaserBeam = cg.polyEffectsSystem.createCurvedBeamEffect( cgs.media.shaderLaserGunBeam );
	}

	vec4_t color;
	getLaserColorOverlayForOwner( ownerNum, color );

	effects->curvedLaserBeamTouchedAt = currTime;
	cg.polyEffectsSystem.updateCurvedBeamEffect( effects->curvedLaserBeam, color, 12.0f, 64.0f, points );
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