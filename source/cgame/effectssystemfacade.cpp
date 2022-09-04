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

#include "effectssystemfacade.h"

#include "cg_local.h"
#include "../qcommon/qcommon.h"
#include "../client/snd_public.h"

void EffectsSystemFacade::startSound( sfx_s *sfx, const float *origin, float attenuation ) {
	SoundSystem::instance()->startFixedSound( sfx, origin, CHAN_AUTO, cg_volume_effects->value, attenuation );
}

void EffectsSystemFacade::startRelativeSound( sfx_s *sfx, int entNum, float attenuation ) {
	SoundSystem::instance()->startRelativeSound( sfx, entNum, CHAN_AUTO, cg_volume_effects->value, attenuation );
}

void EffectsSystemFacade::spawnRocketExplosionEffect( const float *origin, const float *dir, int mode ) {
	sfx_s *sfx = mode == FIRE_MODE_STRONG ? cgs.media.sfxRocketLauncherStrongHit : cgs.media.sfxRocketLauncherWeakHit;
	const bool addSoundLfe = cg_heavyRocketExplosions->integer != 0;
	spawnExplosionEffect( origin, dir, sfx, 64.0f, addSoundLfe );
}

void EffectsSystemFacade::spawnGrenadeExplosionEffect( const float *origin, const float *dir, int mode ) {
	sfx_s *sfx = mode == FIRE_MODE_STRONG ? cgs.media.sfxGrenadeStrongExplosion : cgs.media.sfxGrenadeWeakExplosion;
	const bool addSoundLfe = cg_heavyGrenadeExplosions->integer != 0;
	spawnExplosionEffect( origin, dir, sfx, 64.0f, addSoundLfe );
}

void EffectsSystemFacade::spawnGenericExplosionEffect( const float *origin, int mode, float radius ) {
	const vec3_t dir { 0.0f, 0.0f, 1.0f };
	spawnExplosionEffect( origin, dir, cgs.media.sfxRocketLauncherStrongHit, radius, true );
}

static const ColorLifespan kExplosionSparksColors[3] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 1.0f, 0.6f, 0.3f, 1.0f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 0.3f },
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.4f, 1.0f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 0.3f },
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.5f, 1.0f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 0.3f },
	},
};

// TODO: std::optional<std::pair<Vec3, Vec3>>
[[nodiscard]]
static bool findWaterHitPointBetweenTwoPoints( const float *checkFromPoint, const float *pointInWater,
											   float *waterHitPoint, float *traceDir = nullptr ) {
	const float squareDistance = DistanceSquared( checkFromPoint, pointInWater );
	if( squareDistance < wsw::square( 1.0f ) ) {
		return false;
	}

	vec3_t tmpTraceDir;
	VectorSubtract( pointInWater, checkFromPoint, tmpTraceDir );
	VectorNormalizeFast( tmpTraceDir );

	// Check if there's a solid surface between pointInWater and checkFromPoint.
	// If there is one, continue checking from a point slightly closer to pointInWater than the check hit point.

	trace_t trace;
	vec3_t airPoint;
	VectorCopy( checkFromPoint, airPoint );
	CG_Trace( &trace, pointInWater, vec3_origin, vec3_origin, airPoint, 0, MASK_SOLID );
	if( trace.fraction != 1.0f && ( trace.contents & CONTENTS_SOLID ) ) {
		VectorMA( trace.endpos, 0.1f, tmpTraceDir, airPoint );
	}

	CG_Trace( &trace, airPoint, vec3_origin, vec3_origin, pointInWater, 0, MASK_SOLID | MASK_WATER );
	// Make sure we didn't start in solid
	if( trace.fraction != 1.0f && !trace.startsolid ) {
		if( ( trace.contents & MASK_WATER ) && !( trace.contents & MASK_SOLID ) ) {
			// Make sure we can retrace after the hit point
			if( DistanceSquared( trace.endpos, pointInWater ) > wsw::square( 1.0f ) ) {
				vec3_t tmpHitPoint;
				VectorCopy( trace.endpos, tmpHitPoint );

				vec3_t retraceStart;
				VectorMA( trace.endpos, 0.1f, tmpTraceDir, retraceStart );

				CG_Trace( &trace, retraceStart, vec3_origin, vec3_origin, pointInWater, 0, MASK_SOLID );
				if( trace.fraction == 1.0f && !trace.startsolid ) {
					if( waterHitPoint ) {
						VectorCopy( tmpHitPoint, waterHitPoint );
					}
					if( traceDir ) {
						VectorCopy( tmpTraceDir, traceDir );
					}
					return true;
				}
			}
		}
	}

	return false;
}

static void addUnderwaterSplashImpactsForKnownWaterZ( const float *fireOrigin, float radius, float waterZ,
													  wsw::RandomGenerator *rng, int impactContents,
													  wsw::StaticVector<Impact, 12> *impacts ) {
	assert( waterZ - fireOrigin[2] > 0.0f && "Make sure directions are normalizable" );

	// TODO: Build a shape list once and clip against it in the loop

	// This number of attempts is sufficient in this case
	const unsigned maxTraceAttempts = ( 3 * impacts->capacity() ) / 2;
	for( unsigned i = 0; i < maxTraceAttempts; ++i ) {
		// Sample a random point on the sphere projection onto the water plane (let it actually be a donut)
		const float phi    = rng->nextFloat( 0.0f, 2.0f * (float)M_PI );
		const float r      = rng->nextFloat( 0.1f * radius, 1.0f * radius );
		const float sinPhi = std::sin( phi ), cosPhi = std::cos( phi );
		const vec3_t traceStart { fireOrigin[0] + r * sinPhi, fireOrigin[1] + r * cosPhi, waterZ + 1.0f };

		trace_t trace;
		CG_Trace( &trace, traceStart, vec3_origin, vec3_origin, fireOrigin, 0, MASK_SOLID | MASK_WATER );
		if( trace.fraction != 1.0f && ( trace.contents & CONTENTS_WATER ) ) {
			vec3_t desiredFlockDir;
			VectorSubtract( traceStart, fireOrigin, desiredFlockDir );
			VectorNormalizeFast( desiredFlockDir );

			impacts->emplace_back( makeWaterImpactForDesiredDirection( trace.endpos, desiredFlockDir, impactContents ) );
			// TODO: Vary limit of spawned impacts by explosion depth
			if( impacts->full() ) [[unlikely]] {
				break;
			}
		}
	}
}

static void addUnderwaterSplashImpactsForUnknownWaterZ( const float *fireOrigin, float radius, float maxZ,
														wsw::RandomGenerator *rng, int impactContents,
														wsw::StaticVector<Impact, 12> *impacts ) {
	assert( maxZ - fireOrigin[2] > 0.0f && "Make sure directions are normalizable" );

	// TODO: Build a shape list once and clip against it in the loop
	// TODO: Use a grid propagation instead?

	const unsigned maxTraceAttempts = 6 * impacts->capacity();
	for( unsigned i = 0; i < maxTraceAttempts; ++i ) {
		// Sample a random point on the sphere projection onto the water plane.
		// Let it actually be a donut with an outer radius larger than the explosion radius
		// (otherwise this approach fails way too much).
		const float phi    = rng->nextFloat( 0.0f, 2.0f * (float)M_PI );
		const float r      = rng->nextFloat( 0.5f * radius, 1.5f * radius );
		const float sinPhi = std::sin( phi ), cosPhi = std::cos( phi );

		vec3_t waterHitPoint, traceDir;
		const vec3_t startPoint { fireOrigin[0] + r * sinPhi, fireOrigin[1] + r * cosPhi, maxZ + 1.0f };
		if( findWaterHitPointBetweenTwoPoints( startPoint, fireOrigin, waterHitPoint, traceDir ) ) {
			vec3_t desiredFlockDir;
			VectorNegate( traceDir, desiredFlockDir );

			impacts->emplace_back( makeWaterImpactForDesiredDirection( waterHitPoint, desiredFlockDir, impactContents ) );
			if( impacts->full() ) [[unlikely]] {
				break;
			}
		}
	}
}

static void makeRegularExplosionImpacts( const float *fireOrigin, float radius, wsw::RandomGenerator *rng,
										 wsw::StaticVector<Impact, 12> *solidImpacts,
										 wsw::StaticVector<Impact, 12> *waterImpacts ) {
	const unsigned numTraceAttempts = 3 * solidImpacts->capacity();
	for( unsigned i = 0; i < numTraceAttempts; ++i ) {
		trace_t trace;
		vec3_t traceEnd;
		// Don't check for similarity with the last direction.
		// Otherwise, columns/flat walls do not produce noticeable effects.
		const float *const traceDir = kPredefinedDirs[rng->nextBounded( std::size( kPredefinedDirs ) )];
		// TODO: Make the trace depth it match the visual radius (with is not really equal to `radius`)
		VectorMA( fireOrigin, 0.5f * radius, traceDir, traceEnd );
		// TODO: Build a shape list and clip against it
		CG_Trace( &trace, fireOrigin, vec3_origin, vec3_origin, traceEnd, 0, MASK_SOLID | MASK_WATER );
		if( trace.fraction != 1.0f && !trace.startsolid && !trace.allsolid ) {
			if( !( trace.surfFlags & ( SURF_FLESH | SURF_NOIMPACT ) ) ) {
				// TODO: Make different impacts for solid/water (this does not suit water)

				bool addedOnThisStep = false;
				if( trace.contents & MASK_WATER ) {
					if( !waterImpacts->full() ) {
						vec3_t desiredFlockDir;
						VectorReflect( traceDir, trace.plane.normal, 0.0f, desiredFlockDir );
						waterImpacts->emplace_back( makeWaterImpactForDesiredDirection( trace.endpos, desiredFlockDir,
																						trace.contents ) );
						addedOnThisStep = true;
					}
				} else {
					if( !solidImpacts->full() ) {
						const auto surfFlags = getSurfFlagsForImpact( trace, traceDir );
						const auto material  = decodeSurfImpactMaterial( (unsigned)surfFlags );
						// Make sure it adds something to visuals in the desired way.
						if( material != SurfImpactMaterial::Unknown && material != SurfImpactMaterial::Metal ) {
							solidImpacts->emplace_back( Impact {
								.origin    = { trace.endpos[0], trace.endpos[1], trace.endpos[2] },
								.normal    = { trace.plane.normal[0], trace.plane.normal[1], trace.plane.normal[2] },
								.dir       = { -traceDir[0], -traceDir[1], -traceDir[2] },
								.surfFlags = surfFlags,
							});
							addedOnThisStep = true;
						}
					}
				}
				if( addedOnThisStep ) {
					if( solidImpacts->full() && waterImpacts->full() ) {
						break;
					}
				}
			}
		}
	}
}

static const ColorLifespan kExplosionSmokeColors[3] {
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.5f, 0.5f, 0.5f, 0.2f },
		.fadedOutColor = { 0.9f, 0.9f, 0.9f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.67f, .startFadingOutAtLifetimeFrac = 0.76f,
	},
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.6f, 0.6f, 0.6f, 0.2f },
		.fadedOutColor = { 0.9f, 0.9f, 0.9f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.67f, .startFadingOutAtLifetimeFrac = 0.76f,
	},
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.7f, 0.7f, 0.7f, 0.2f },
		.fadedOutColor = { 0.9f, 0.9f, 0.9f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.67f, .startFadingOutAtLifetimeFrac = 0.76f,
	},
};

static const Particle::AppearanceRules kExplosionSmokeAppearanceRules {
	.colors       = kExplosionSmokeColors,
	.kind         = Particle::Sprite,
	.radius       = 9.0f,
	.radiusSpread = 5.0f,
};

static const EllipsoidalFlockParams kExplosionSmokeFlockParams {
	.stretchScale  = 1.25f,
	.gravity       = -65.0f,
	.restitution   = 0.33f,
	.minSpeed      = 35.0f,
	.maxSpeed      = 65.0f,
	.minShiftSpeed = 55.0f,
	.maxShiftSpeed = 70.0f,
	.minPercentage = 0.7f,
	.maxPercentage = 0.9f,
	.minTimeout    = 1200,
	.maxTimeout    = 1750,
};

void EffectsSystemFacade::spawnExplosionEffect( const float *origin, const float *dir, sfx_s *sfx,
												float radius, bool addSoundLfe ) {
	vec3_t fireOrigin, almostExactOrigin;
	VectorMA( origin, 8.0f, dir, fireOrigin );
	VectorAdd( origin, dir, almostExactOrigin );

	wsw::StaticVector<Impact, 12> solidImpacts;
	wsw::StaticVector<Impact, 12> waterImpacts;

	trace_t fireOriginTrace;
	CG_Trace( &fireOriginTrace, origin, vec3_origin, vec3_origin, fireOrigin, 0, MASK_SOLID );
	if( fireOriginTrace.fraction != 1.0f ) {
		VectorMA( origin, 0.5f * fireOriginTrace.fraction, dir, fireOrigin );
		if( DistanceSquared( fireOriginTrace.endpos, origin ) < wsw::square( 1.0f ) ) {
			VectorAvg( origin, fireOriginTrace.endpos, almostExactOrigin );
		}
	}

	std::optional<int> liquidContentsAtFireOrigin;
	if( cg_explosionsSmoke->integer || cg_particles->integer ) {
		if( const int contents = CG_PointContents( fireOrigin ); contents & MASK_WATER ) {
			liquidContentsAtFireOrigin = contents;
		}
	}

	vec3_t tmpSmokeOrigin;
	const float *smokeOrigin = nullptr;
	if( cg_explosionsSmoke->integer || cg_particles->integer ) {
		if( liquidContentsAtFireOrigin ) {
			VectorCopy( fireOrigin, tmpSmokeOrigin );
			tmpSmokeOrigin[2] += radius;

			if( !( CG_PointContents( tmpSmokeOrigin ) & MASK_WATER ) ) {
				vec3_t waterHitPoint;
				if( findWaterHitPointBetweenTwoPoints( tmpSmokeOrigin, fireOrigin, waterHitPoint ) ) {
					if( waterHitPoint[2] - fireOrigin[2] > 1.0f ) {
						if( cg_explosionsSmoke->integer ) {
							VectorCopy( waterHitPoint, tmpSmokeOrigin );
							tmpSmokeOrigin[2] += 1.0f;
							smokeOrigin = tmpSmokeOrigin;
						}

						waterImpacts.emplace_back( Impact {
							.origin   = { waterHitPoint[0], waterHitPoint[1], waterHitPoint[2] },
							.contents = *liquidContentsAtFireOrigin
						});

						const float waterZ = waterHitPoint[2];
						addUnderwaterSplashImpactsForKnownWaterZ( fireOrigin, radius, waterZ, &m_rng,
																  *liquidContentsAtFireOrigin, &waterImpacts );
					} else if( radius > 2.0f ) {
						// Generate impacts from a point above the fire origin but within the fire radius
						const vec3_t shiftedFireOrigin { fireOrigin[0], fireOrigin[1], fireOrigin[2] + 0.75f * radius };
						makeRegularExplosionImpacts( shiftedFireOrigin, radius, &m_rng, &solidImpacts, &waterImpacts );

						if( cg_explosionsSmoke->integer ) {
							VectorCopy( shiftedFireOrigin, tmpSmokeOrigin );
							smokeOrigin = tmpSmokeOrigin;
						}
					}
				} else {
					const float maxZ = tmpSmokeOrigin[2];
					addUnderwaterSplashImpactsForUnknownWaterZ( fireOrigin, radius, maxZ, &m_rng,
																*liquidContentsAtFireOrigin, &waterImpacts );
				}
			}
		} else {
			if( cg_explosionsSmoke->integer ) {
				smokeOrigin = fireOrigin;
			}

			makeRegularExplosionImpacts( fireOrigin, radius, &m_rng, &solidImpacts, &waterImpacts );
		}
	}

	startSound( sfx, almostExactOrigin, ATTN_DISTANT );

	if( addSoundLfe ) {
		startSound( cgs.media.sfxExplosionLfe, almostExactOrigin, ATTN_NORM );
	}

	if( cg_particles->integer && !liquidContentsAtFireOrigin ) {
		Particle::AppearanceRules appearanceRules {
			.materials    = cgs.media.shaderDebrisParticle.getAddressOfHandle(),
			.colors       = kExplosionSparksColors,
			.kind         = Particle::Sprite,
			.radius       = 1.25f,
			.radiusSpread = 0.25f
		};

		EllipsoidalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { dir[0], dir[1], dir[2] },
			.gravity       = 0.25f * GRAVITY,
			.drag          = 0.025f,
			.restitution   = 0.33f,
			.minSpeed      = 150.0f,
			.maxSpeed      = 400.0f,
			.minShiftSpeed = 100.0f,
			.maxShiftSpeed = 200.0f,
			.minPercentage = 0.5f,
			.maxPercentage = 0.8f,
			.minTimeout    = 400,
			.maxTimeout    = 750
		};

		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );

		appearanceRules.kind         = Particle::Spark;
		appearanceRules.length       = 25.0f;
		appearanceRules.lengthSpread = 7.5f;
		appearanceRules.width        = 4.0f;
		appearanceRules.widthSpread  = 1.0f;
		appearanceRules.sizeBehaviour = Particle::Shrinking;

		flockParams.minSpeed      = 550;
		flockParams.maxSpeed      = 650;
		flockParams.drag          = 0.01f;
		flockParams.minTimeout    = 100;
		flockParams.maxTimeout    = 150;
		flockParams.minPercentage = 0.5f;
		flockParams.maxPercentage = 1.0f;
		flockParams.minShiftSpeed = 50.0f;
		flockParams.maxShiftSpeed = 100.0f;

		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );

		if( cg_explosionsSmoke->integer ) {
			flockParams.minSpeed = 125.0f;
			flockParams.maxSpeed = 175.0f;
		} else {
			flockParams.minSpeed = 150.0f;
			flockParams.maxSpeed = 225.0f;
		}

		flockParams.minTimeout    = 350;
		flockParams.maxTimeout    = 450;
		flockParams.minPercentage = 1.0f;
		flockParams.maxPercentage = 1.0f;

		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	if( smokeOrigin ) {
		EllipsoidalFlockParams flockParams( kExplosionSmokeFlockParams );
		VectorCopy( smokeOrigin, flockParams.origin );
		Particle::AppearanceRules appearanceRules( kExplosionSmokeAppearanceRules );
		appearanceRules.materials = cgs.media.shaderFlareParticle.getAddressOfHandle();
		m_transientEffectsSystem.addDelayedParticleEffect( 300, TransientEffectsSystem::ParticleFlockBin::Large,
														   flockParams, appearanceRules );
	}

	m_transientEffectsSystem.spawnExplosionHulls( fireOrigin, smokeOrigin );

	spawnMultipleExplosionImpactEffects( solidImpacts );
	spawnMultipleLiquidImpactEffects( waterImpacts, 1.0f, { 0.7f, 0.9f }, { 0, 100 } );
}

void EffectsSystemFacade::spawnShockwaveExplosionEffect( const float *origin, const float *dir, int mode ) {
}

static const ColorLifespan kPlasmaParticlesColors[1] {
	{
		.initialColor  = { 0.0f, 1.0f, 0.0f, 0.0f },
		.fadedInColor  = { 0.3f, 1.0f, 0.5f, 1.0f },
		.fadedOutColor = { 0.7f, 1.0f, 0.7f, 0.0f },
	}
};

void EffectsSystemFacade::spawnPlasmaExplosionEffect( const float *origin, const float *impactNormal, int mode ) {
	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	sfx_s *sfx = ( mode == FIRE_MODE_STRONG ) ? cgs.media.sfxPlasmaStrongHit : cgs.media.sfxPlasmaWeakHit;
	startSound( sfx, soundOrigin, ATTN_IDLE );

	if( cg_particles->integer ) {
		EllipsoidalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { impactNormal[0], impactNormal[1], impactNormal[2] },
			.gravity       = 250.0f,
			.minPercentage = 0.5f,
			.maxPercentage = 0.8f,
			.minTimeout    = 125,
			.maxTimeout    = 175,
		};
		Particle::AppearanceRules appearanceRules {
			.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
			.colors         = kPlasmaParticlesColors,
			.kind           = Particle::Sprite,
			.radius         = 1.5f,
			.radiusSpread   = 0.25f,
		};
		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	m_transientEffectsSystem.spawnPlasmaImpactEffect( origin, impactNormal );
}

void EffectsSystemFacade::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	m_transientEffectsSystem.simulateFrameAndSubmit( currTime, request );
	m_trackedEffectsSystem.simulateFrameAndSubmit( currTime, request );
}

void EffectsSystemFacade::spawnGrenadeBounceEffect( int entNum, int mode ) {
	assert( mode == FIRE_MODE_STRONG || mode == FIRE_MODE_WEAK );
	sfx_s *sound = nullptr;
	if( mode == FIRE_MODE_STRONG ) {
		sound = cgs.media.sfxGrenadeStrongBounce[m_rng.nextBounded( 2 )];
	} else {
		sound = cgs.media.sfxGrenadeWeakBounce[m_rng.nextBounded( 2 )];
	}
	startRelativeSound( sound, entNum, ATTN_IDLE );
}

static const ColorLifespan kBloodColors[] {
	{
		.initialColor  = { 1.0f, 0.0f, 0.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.3f, 0.7f, 1.0f },
		.fadedOutColor = { 0.9f, 0.3f, 0.7f, 1.0f },
	},
	{
		.initialColor  = { 1.0f, 0.0f, 0.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.6f, 0.3f, 1.0f },
		.fadedOutColor = { 0.9f, 0.5f, 0.0f, 1.0f },
	},
	{
		.initialColor  = { 0.0f, 1.0f, 0.5f, 1.0f },
		.fadedInColor  = { 0.3f, 1.0f, 0.5f, 1.0f },
		.fadedOutColor = { 0.0f, 0.5f, 0.0f, 1.0f }
	},
	{
		.initialColor  = { 0.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 0.3f, 0.7f, 1.0f, 1.0f },
		.fadedOutColor = { 0.0f, 0.7f, 1.0f, 1.0f },
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedOutColor = { 0.3f, 0.3f, 0.3f, 1.0f },
	},
};

void EffectsSystemFacade::spawnPlayerHitEffect( const float *origin, const float *dir, int damage ) {
	if( const int palette        = cg_bloodTrailPalette->integer ) {
		const int indexForStyle  = wsw::clamp<int>( palette - 1, 0, std::size( kBloodColors ) - 1 );
		const int baseTime       = wsw::clamp<int>( cg_bloodTrailTime->integer, 200, 400 );
		const int timeSpread     = wsw::max( 50, baseTime / 8 );

		ConicalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { 3.0f * dir[0], 3.0f * dir[1], 3.0f * dir[2] },
			.dir           = { dir[0], dir[1], dir[2] },
			.gravity       = -125.0f,
			.angle         = 60.0f,
			.minSpeed      = 35.0f,
			.maxSpeed      = 75.0f,
			.minPercentage = 0.33f,
			.maxPercentage = 0.67f,
			.minTimeout    = (unsigned)( baseTime - timeSpread / 2 ),
			.maxTimeout    = (unsigned)( baseTime + timeSpread / 2 )
		};
		// We have to supply a buffer with a non-stack lifetime
		// Looks nicer than std::fill in this case, even if it's "wrong" from a purist POV
		m_bloodMaterials[0] = cgs.media.shaderBloodParticle;
		m_bloodMaterials[1] = cgs.media.shaderBloodParticle;
		m_bloodMaterials[2] = cgs.media.shaderBlastParticle;
		Particle::AppearanceRules appearanceRules {
			.materials      = m_bloodMaterials,
			.colors         = { &kBloodColors[indexForStyle], 1 },
			.numMaterials   = (uint8_t)std::size( m_bloodMaterials ),
			.kind           = Particle::Sprite,
			.radius         = 1.50f,
			.radiusSpread   = 0.75f,
			.sizeBehaviour  = Particle::Expanding
		};
		cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
		const float *effectColor = kBloodColors[indexForStyle].fadedInColor;
		m_transientEffectsSystem.spawnBleedingVolumeEffect( origin, dir, damage, effectColor, (unsigned)baseTime );
	}

	m_transientEffectsSystem.spawnCartoonHitEffect( origin, dir, damage );
}

static ParticleColorsForTeamHolder electroboltParticleColorsHolder {
	.defaultColors = {
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 0.7f, 0.7f, 1.0f, 1.0f },
		.fadedOutColor = { 0.1f, 0.1f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.05f,
		.startFadingOutAtLifetimeFrac = 0.50f,
	}
};

[[nodiscard]]
auto getTeamForOwner( int ownerNum ) -> int {
	if( ownerNum && ownerNum < gs.maxclients + 1 ) [[likely]] {
		return cg_entities[ownerNum].current.team;
	}
	return TEAM_SPECTATOR;
}

void EffectsSystemFacade::spawnElectroboltHitEffect( const float *origin, const float *impactNormal,
													 const float *impactDir, bool spawnDecal, int ownerNum ) {
	const int team = getTeamForOwner( ownerNum );

	vec4_t teamColor, decalColor, energyColor;
	const bool useTeamColor = getElectroboltTeamColor( team, teamColor );
	if( useTeamColor ) {
		Vector4Copy( teamColor, decalColor );
		Vector4Copy( teamColor, energyColor );
	} else {
		Vector4Copy( colorWhite, decalColor );
		Vector4Set( energyColor, 0.3f, 0.6f, 1.0f, 1.0f );
	}

	if( cg_particles->integer ) {
		vec3_t coneDir;
		VectorReflect( impactDir, impactNormal, 0.0f, coneDir );

		const ColorLifespan *singleColorAddress;
		ParticleColorsForTeamHolder *colorsHolder = &::electroboltParticleColorsHolder;
		if( useTeamColor ) {
			singleColorAddress = colorsHolder->getColorsForTeam( team, teamColor );
		} else {
			singleColorAddress = &colorsHolder->defaultColors;
		}

		const ConicalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { impactNormal[0], impactNormal[1], impactNormal[2] },
			.dir           = { coneDir[0], coneDir[1], coneDir[2] },
			.gravity       = GRAVITY,
			.angle         = 45.0f,
			.minSpeed      = 500.0f,
			.maxSpeed      = 950.0f,
			.minPercentage = 0.33f,
			.maxPercentage = 0.67f,
			.minTimeout    = 100,
			.maxTimeout    = 300
		};

		const Particle::AppearanceRules appearanceRules {
			.materials           = cgs.media.shaderDebrisParticle.getAddressOfHandle(),
			.colors              = { singleColorAddress, 1 },
			.kind                = Particle::Spark,
			.length              = 12.5f,
			.width               = 2.0f,
			.lengthSpread        = 2.5f,
			.widthSpread         = 1.0f,
		};

		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnElectroboltHitEffect( origin, impactNormal, decalColor, energyColor, spawnDecal );
}

static ParticleColorsForTeamHolder instagunParticleColorsHolder {
	.defaultColors = {
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.0f, 1.0f, 0.5f },
		.fadedOutColor = { 0.0f, 0.0f, 1.0f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.05f,
		.startFadingOutAtLifetimeFrac = 0.75f,
	}
};

void EffectsSystemFacade::spawnInstagunHitEffect( const float *origin, const float *impactNormal,
												  const float *impactDir, bool spawnDecal, int ownerNum ) {
	vec4_t teamColor, decalColor, energyColor;
	const int team = getTeamForOwner( ownerNum );
	const bool useTeamColor = getInstagunTeamColor( team, teamColor );
	if( useTeamColor ) {
		Vector4Copy( teamColor, decalColor );
		Vector4Copy( teamColor, energyColor );
	} else {
		Vector4Set( decalColor, 1.0f, 0.0f, 0.4f, 1.0f );
		Vector4Set( energyColor, 1.0f, 0.0f, 0.4f, 1.0f );
	}

	if( cg_particles->integer ) {
		vec3_t coneDir;
		VectorReflect( impactDir, impactNormal, 0.0f, coneDir );

		const ColorLifespan *singleColorAddress;
		ParticleColorsForTeamHolder *colorsHolder = &::instagunParticleColorsHolder;
		if( useTeamColor ) {
			singleColorAddress = colorsHolder->getColorsForTeam( team, teamColor );
		} else {
			singleColorAddress = &colorsHolder->defaultColors;
		}

		const ConicalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { impactNormal[0], impactNormal[1], impactNormal[2] },
			.dir           = { coneDir[0], coneDir[1], coneDir[2] },
			.gravity       = GRAVITY,
			.angle         = 45.0f,
			.minSpeed      = 750.0f,
			.maxSpeed      = 950.0f,
			.minPercentage = 0.5f,
			.maxPercentage = 1.0f,
			.minTimeout    = 150,
			.maxTimeout    = 225
		};

		const Particle::AppearanceRules appearanceRules {
			.materials           = cgs.media.shaderSparkParticle.getAddressOfHandle(),
			.colors              = { singleColorAddress, 1 },
			.kind                = Particle::Spark,
			.length              = 10.0f,
			.width               = 1.5f,
			.lengthSpread        = 2.5f,
			.widthSpread         = 0.5f,
		};

		cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
	}

	// TODO: Don't we need an IG-specific sound
	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnInstagunHitEffect( origin, impactNormal, decalColor, energyColor, spawnDecal );
}

static const ColorLifespan kGunbladeHitColors[1] {
	{
		.initialColor  = { 1.0f, 0.5f, 0.1f, 0.0f },
		.fadedInColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 0.5f },
	}
};

void EffectsSystemFacade::spawnGunbladeBladeHitEffect( const float *pos, const float *dir ) {
	// Find what are we hitting
	vec3_t local_pos, local_dir;
	VectorCopy( pos, local_pos );
	VectorNormalize2( dir, local_dir );
	vec3_t end;
	VectorMA( pos, -1.0, local_dir, end );

	trace_t trace;
	CG_Trace( &trace, local_pos, vec3_origin, vec3_origin, end, cg.view.POVent, MASK_SHOT );

	if( trace.fraction != 1.0 ) {
		bool isHittingFlesh = false;
		if( trace.surfFlags & SURF_FLESH ) {
			isHittingFlesh = true;
		} else if( const int entNum = trace.ent; entNum > 0 ) {
			if( const auto type = cg_entities[entNum].current.type; type == ET_PLAYER || type == ET_CORPSE ) {
				isHittingFlesh = true;
			}
		}

		if( isHittingFlesh ) {
			// TODO: Check sound origin
			startSound( cgs.media.sfxBladeFleshHit[m_rng.nextBounded( 3 )], pos, ATTN_NORM );
		} else {
			m_transientEffectsSystem.spawnGunbladeBladeImpactEffect( trace.endpos, trace.plane.normal );

			// TODO: Check sound origin
			startSound( cgs.media.sfxBladeWallHit[m_rng.nextBounded( 2 )], pos, ATTN_NORM );

			if( cg_particles->integer ) {
				ConicalFlockParams flockParams {
					.origin = { pos[0], pos[1], pos[2] },
					.offset = { dir[0], dir[1], dir[2] },
					.dir    = { dir[0], dir[1], dir[2] },
					.angle  = 60
				};
				Particle::AppearanceRules appearanceRules {
					.materials      = cgs.media.shaderSparkParticle.getAddressOfHandle(),
					.colors         = kGunbladeHitColors,
					.kind           = Particle::Spark,
					.length         = 4.0f,
					.width          = 1.0f,
					.lengthSpread   = 1.0f,
					.widthSpread    = 0.25f,
				};
				cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
			}
		}
	}
}

static const ColorLifespan kGunbladeBlastColors[3] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.5f, 0.7f },
		.fadedOutColor = { 0.5f, 0.3f, 0.1f, 0.0f }
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.4f, 0.7f },
		.fadedOutColor = { 0.7f, 0.3f, 0.1f, 0.0f }
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.4f, 0.7f },
		.fadedOutColor = { 0.9f, 0.3f, 0.1f, 0.0f }
	},
};

void EffectsSystemFacade::spawnGunbladeBlastHitEffect( const float *origin, const float *dir ) {
	startSound( cgs.media.sfxGunbladeStrongHit[m_rng.nextBounded( 2 )], origin, ATTN_IDLE );

	if( cg_particles->integer ) {
		EllipsoidalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { dir[0], dir[1], dir[2] },
			.gravity       = -50.0f,
			.minSpeed      = 50,
			.maxSpeed      = 100,
			.minPercentage = 1.0f,
			.maxPercentage = 1.0f
		};
		Particle::AppearanceRules appearanceRules {
			.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
			.colors         = kGunbladeBlastColors,
			.kind           = Particle::Sprite,
			.radius         = 1.50f,
			.radiusSpread   = 0.25f
		};
		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	m_transientEffectsSystem.spawnGunbladeBlastImpactEffect( origin, dir );
}

auto makeWaterImpactForDesiredDirection( const float *origin, const float *direction, int contents ) -> Impact {
	// TODO: Implement/check what's wrong with water flock spawning code
	direction = &axis_identity[AXIS_UP];
	return Impact {
		.origin   = { origin[0], origin[1], origin[2] },
		.normal   = { -direction[0], -direction[1], -direction[2] },
		.dir      = { -direction[0], -direction[1], -direction[2] },
		.contents = contents,
	};
}

[[nodiscard]]
static auto makeRicochetFlockOrientation( const Impact &impact, wsw::RandomGenerator *rng,
										  const std::pair<float, float> &angleCosineRange = { 0.30f, 0.95f } )
										  -> FlockOrientation {
	vec3_t flockDir;
	VectorReflect( impact.dir, impact.normal, 0.0f, flockDir );

	const float coneAngleCosine = Q_Sqrt( rng->nextFloat( angleCosineRange.first, angleCosineRange.second ) );
	addRandomRotationToDir( flockDir, rng, coneAngleCosine );

	return FlockOrientation {
		.origin = { impact.origin[0], impact.origin[1], impact.origin[2] },
		.offset = { impact.normal[0], impact.normal[1], impact.normal[2] },
		.dir    = { flockDir[0], flockDir[1], flockDir[2] }
	};
}

void addRandomRotationToDir( float *dir, wsw::RandomGenerator *rng, float minConeAngleCosine, float maxConeAngleCosine ) {
	const float coneAngleCosine = rng->nextFloat( minConeAngleCosine, maxConeAngleCosine );
	addRandomRotationToDir( dir, rng, coneAngleCosine );
}

void addRandomRotationToDir( float *dir, wsw::RandomGenerator *rng, float coneAngleCosine ) {
	assert( coneAngleCosine > 0.0f && coneAngleCosine < 1.0f );
	const float z     = coneAngleCosine;
	const float r     = Q_Sqrt( 1.0f - z * z );
	const float phi   = rng->nextFloat( 0.0f, 2.0f * (float)M_PI );
	const vec3_t newZDir { r * std::cos( phi ), r * std::sin( phi ), z };

	mat3_t transformMatrix;
	Matrix3_ForRotationOfDirs( &axis_identity[AXIS_UP], newZDir, transformMatrix );

	vec3_t result;
	Matrix3_TransformVector( transformMatrix, dir, result );
	// wtf?
	VectorNormalizeFast( result );
	VectorCopy( result, dir );
}

template <typename FlockParams>
static inline void assignUpShiftAndModifyBaseSpeed( FlockParams *flockParams, float upShiftScale,
													float minShiftSpeed, float maxShiftSpeed ) {
	assert( upShiftScale >= 0.0f && upShiftScale <= 1.0f );
	flockParams->minShiftSpeed = minShiftSpeed * upShiftScale;
	flockParams->maxShiftSpeed = maxShiftSpeed * upShiftScale;
	const float baseSpeedScale = 1.0f + upShiftScale;
	// Apply the upper bound to avoid triggering an assertion on speed feasibility
	flockParams->minSpeed = wsw::min( 999.9f, flockParams->minSpeed * baseSpeedScale );
	flockParams->maxSpeed = wsw::min( 999.9f, flockParams->maxSpeed * baseSpeedScale );
}

static const ColorLifespan kBulletRosetteColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 0.9f, 0.9f, 1.0f, 1.0f },
		.fadedOutColor = { 0.9f, 0.9f, 0.8f, 1.0f },
	}
};

static const Particle::AppearanceRules kBulletRosetteAppearanceRules {
	.colors         = kBulletRosetteColors,
	.kind           = Particle::Spark,
	.length         = 16.0f,
	.width          = 1.0f,
	.lengthSpread   = 4.0f,
	.widthSpread    = 0.1f,
	.sizeBehaviour  = Particle::Shrinking,
};

static const ConicalFlockParams kBulletRosetteFlockParams {
	.gravity       = GRAVITY,
	.minSpeed      = 550.0f,
	.maxSpeed      = 800.0f,
	.minTimeout    = 75,
	.maxTimeout    = 150,
};

void EffectsSystemFacade::spawnBulletGenericImpactRosette( const FlockOrientation &orientation,
														   float minPercentage, float maxPercentage ) {
	Particle::AppearanceRules appearanceRules( kBulletRosetteAppearanceRules );
	appearanceRules.materials    = cgs.media.shaderSparkParticle.getAddressOfHandle();
	appearanceRules.length       = 0.75f * appearanceRules.length;
	appearanceRules.lengthSpread = 0.75f * appearanceRules.lengthSpread;

	ConicalFlockParams flockParams( kBulletRosetteFlockParams );
	flockParams.angle         = 45.0f;
	flockParams.innerAngle    = 15.0f;
	flockParams.minPercentage = minPercentage;
	flockParams.maxPercentage = maxPercentage;
	flockParams.minTimeout    = ( 3 * flockParams.minTimeout ) / 4;
	flockParams.maxTimeout    = ( 3 * flockParams.maxTimeout ) / 4;

	orientation.copyToFlockParams( &flockParams );
	cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
}

void EffectsSystemFacade::spawnBulletMetalImpactRosette( const FlockOrientation &orientation ) {
	Particle::AppearanceRules appearanceRules( kBulletRosetteAppearanceRules );
	appearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();

	ConicalFlockParams flockParams( kBulletRosetteFlockParams );
	flockParams.angle         = 24.0f;
	flockParams.innerAngle    = 0.0f;
	flockParams.minPercentage = 0.2f;
	flockParams.maxPercentage = 0.6f;

	orientation.copyToFlockParams( &flockParams );
	cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );

	appearanceRules.length       = 0.67f * appearanceRules.length;
	appearanceRules.lengthSpread = 0.67f * appearanceRules.lengthSpread;

	flockParams.innerAngle    = flockParams.angle;
	flockParams.angle         = 60.0f;
	flockParams.minPercentage = 1.0f;
	flockParams.maxPercentage = 1.0f;
	flockParams.minTimeout    = ( 2 * flockParams.minTimeout ) / 3;
	flockParams.maxTimeout    = ( 2 * flockParams.maxTimeout ) / 3;
	cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
}

static const ColorLifespan kBulletMetalRicochetColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 0.9f, 0.9f, 1.0f, 1.0f },
		.fadedOutColor = { 0.9f, 0.9f, 0.8f, 1.0f },
	}
};

void EffectsSystemFacade::spawnBulletMetalRicochetParticles( unsigned delay, const FlockOrientation &orientation,
															 float upShiftScale, unsigned,
															 float minPercentage, float maxPercentage ) {
	const Particle::AppearanceRules appearanceRules {
		.materials      = cgs.media.shaderSparkParticle.getAddressOfHandle(),
		.colors         = kBulletMetalRicochetColors,
		.kind           = Particle::Spark,
		.length         = 5.0f,
		.width          = 0.75f,
		.lengthSpread   = 1.0f,
		.widthSpread    = 0.05f,
		.sizeBehaviour  = Particle::Expanding,
	};

	ConicalFlockParams flockParams {
		.gravity       = GRAVITY,
		.drag          = 0.008f,
		.restitution   = 0.5f,
		.angle         = 18.0f,
		.minSpeed      = 700.0f,
		.maxSpeed      = 950.0f,
		.minPercentage = minPercentage,
		.maxPercentage = maxPercentage,
		.minTimeout    = 300,
		.maxTimeout    = 350,
	};

	orientation.copyToFlockParams( &flockParams );
	assignUpShiftAndModifyBaseSpeed( &flockParams, upShiftScale, 100.0f, 150.0f );
	spawnOrPostponeImpactParticleEffect( delay, flockParams, appearanceRules );
}

static const ColorLifespan kBulletMetalDebrisColors[3] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.5f, 1.0f },
		.fadedOutColor = { 1.0f, 0.8f, 0.5f, 1.0f }
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.8f, 0.4f, 1.0f },
		.fadedOutColor = { 1.0f, 0.8f, 0.4f, 1.0f }
	},
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.7f, 0.3f, 1.0f },
		.fadedOutColor = { 1.0f, 0.7f, 0.3f, 1.0f }
	},
};

void EffectsSystemFacade::spawnBulletMetalDebrisParticles( unsigned delay, const FlockOrientation &orientation,
														   float upShiftScale, unsigned,
														   float minPercentage, float maxPercentage ) {
	const Particle::AppearanceRules appearanceRules {
		.materials      = cgs.media.shaderSparkParticle.getAddressOfHandle(),
		.colors         = kBulletMetalDebrisColors,
		.kind           = Particle::Spark,
		.length         = 2.5f,
		.width          = 0.75f,
		.lengthSpread   = 1.0f,
		.widthSpread    = 0.05f,
		.sizeBehaviour  = Particle::Expanding,
	};

	ConicalFlockParams flockParams {
		.gravity        = GRAVITY,
		.restitution    = 0.3f,
		.angle          = 30.0f,
		.minBounceCount = 1,
		.maxBounceCount = 3,
		.minSpeed       = 75.0f,
		.maxSpeed       = 125.0f,
		.minPercentage  = minPercentage,
		.maxPercentage  = maxPercentage,
		.minTimeout     = 200,
		.maxTimeout     = 700,
	};

	orientation.copyToFlockParams( &flockParams );
	assignUpShiftAndModifyBaseSpeed( &flockParams, upShiftScale, 150.0f, 200.0f );
	spawnOrPostponeImpactParticleEffect( delay, flockParams, appearanceRules );
}

static const ColorLifespan kGreyDustColors[1] {
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.1f },
		.fadedInColor  = { 0.5f, 0.5f, 0.5f, 0.1f },
		.fadedOutColor = { 0.5f, 0.5f, 0.5f, 0.0f },
		.startFadingOutAtLifetimeFrac = 0.67f
	}
};

void EffectsSystemFacade::spawnStoneDustParticles( unsigned delay, const FlockOrientation &orientation,
												   float upShiftScale, unsigned materialParam,
												   float dustPercentageScale ) {
	const Particle::AppearanceRules appearanceRules {
		.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors              = kGreyDustColors,
		.kind                = Particle::Sprite,
		.radius              = 35.0f,
		.radiusSpread        = 7.5f,
		.applyVertexDynLight = true,
		.sizeBehaviour       = Particle::Expanding,
	};

	ConicalFlockParams flockParams {
		.gravity       = 50.0f,
		.drag          = 0.03f,
		.restitution   = 1.0f,
		.angle         = 30.0f,
		.minSpeed      = 100.0f,
		.maxSpeed      = 500.0f,
		.minPercentage = 0.7f * dustPercentageScale,
		.maxPercentage = 1.0f * dustPercentageScale,
		.minTimeout    = 750,
		.maxTimeout    = 1000,
	};

	orientation.copyToFlockParams( &flockParams );
	assignUpShiftAndModifyBaseSpeed( &flockParams, upShiftScale, 10.0f, 20.0f );
	spawnOrPostponeImpactParticleEffect( delay, flockParams, appearanceRules );
}

void EffectsSystemFacade::spawnStuccoDustParticles( unsigned delay, const FlockOrientation &orientation,
													float upShiftScale, unsigned materialParam ) {
	const Particle::AppearanceRules appearanceRules {
		.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors              = kGreyDustColors,
		.kind                = Particle::Sprite,
		.radius              = 55.0f,
		.radiusSpread        = 10.0f,
		.applyVertexDynLight = true,
		.sizeBehaviour       = Particle::Expanding,
	};

	ConicalFlockParams flockParams {
		.gravity       = 50.0f,
		.drag          = 0.03f,
		.restitution   = 1.0f,
		.angle         = 30.0f,
		.minSpeed      = 100.0f,
		.maxSpeed      = 500.0f,
		.minPercentage = 0.7f,
		.maxPercentage = 1.0f,
		.minTimeout    = 1250,
		.maxTimeout    = 1750,
	};

	flockParams.minPercentage = 0.7f;
	flockParams.maxPercentage = 1.0f;
	flockParams.minTimeout    = 1500;
	flockParams.maxTimeout    = 2000;

	orientation.copyToFlockParams( &flockParams );
	assignUpShiftAndModifyBaseSpeed( &flockParams, upShiftScale, 20.0f, 30.0f );
	spawnOrPostponeImpactParticleEffect( delay, flockParams, appearanceRules );
}

static const ColorLifespan kWoodImpactColors[1] {
	{
		.initialColor  = { 0.5f, 0.4f, 0.3f, 1.0f },
		.fadedInColor  = { 0.5f, 0.4f, 0.3f, 1.0f },
		.fadedOutColor = { 0.5f, 0.4f, 0.3f, 1.0f },
	}
};

static const ColorLifespan kWoodDustColors[1] {
	{
		.initialColor  = { 0.5f, 0.4f, 0.3f, 0.0f },
		.fadedInColor  = { 0.5f, 0.4f, 0.3f, 0.1f },
		.fadedOutColor = { 0.5f, 0.4f, 0.3f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.1f,
		.startFadingOutAtLifetimeFrac = 0.7f
	}
};

void EffectsSystemFacade::spawnWoodBulletImpactParticles( unsigned delay, const FlockOrientation &orientation,
														  float upShiftScale, unsigned materialParam,
														  float debrisPercentageScale ) {
	const Particle::AppearanceRules burstAppearanceRules {
		.materials      = cgs.media.shaderDebrisParticle.getAddressOfHandle(),
		.colors         = kWoodImpactColors,
		.kind           = Particle::Spark,
		.length         = 20.0f,
		.width          = 3.0f,
		.lengthSpread   = 3.0f,
		.widthSpread    = 0.5f,
		.sizeBehaviour  = Particle::Shrinking
	};

	ConicalFlockParams burstFlockParams {
		.angle         = 15,
		.minSpeed      = 700,
		.maxSpeed      = 900,
		.minPercentage = 0.3f,
		.maxPercentage = 0.6f,
		.minTimeout    = 75,
		.maxTimeout    = 150,
	};

	const Particle::AppearanceRules dustAppearanceRules {
		.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors              = kWoodDustColors,
		.kind                = Particle::Sprite,
		.radius              = 12.5f,
		.radiusSpread        = 2.5f,
		.applyVertexDynLight = true,
		.sizeBehaviour       = Particle::Expanding
	};

	ConicalFlockParams dustFlockParams {
		.gravity       = 25.0f,
		.angle         = 24.0f,
		.minSpeed      = 50.0f,
		.maxSpeed      = 150.0f,
		.minPercentage = 1.0f,
		.maxPercentage = 1.0f,
		.minTimeout    = 350,
		.maxTimeout    = 450
	};

	const Particle::AppearanceRules debrisAppearanceRules {
		.materials      = cgs.media.shaderBlastParticle.getAddressOfHandle(),
		.colors         = kWoodImpactColors,
		.kind           = Particle::Spark,
		.length         = 5.0f,
		.width          = 1.5f,
		.lengthSpread   = 1.5f,
		.widthSpread    = 0.5f,
	};

	ConicalFlockParams debrisFlockParams {
		.gravity            = 0.75f * GRAVITY,
		.drag               = 0.02f,
		.restitution        = 0.5f,
		.angle              = 30.0f,
		.minBounceCount     = 2,
		.maxBounceCount     = 3,
		.minSpeed           = 400.0f,
		.maxSpeed           = 700.0f,
		.minAngularVelocity = 3.0f * 360.0f,
		.maxAngularVelocity = 9.0f * 360.0f,
		.minPercentage      = 0.3f * debrisPercentageScale,
		.maxPercentage      = 0.6f * debrisPercentageScale,
		.minTimeout         = 350,
		.maxTimeout         = 500,
	};

	orientation.copyToFlockParams( &burstFlockParams );
	spawnOrPostponeImpactParticleEffect( delay, burstFlockParams, burstAppearanceRules,
										 TransientEffectsSystem::ParticleFlockBin::Medium );

	orientation.copyToFlockParams( &dustFlockParams );
	spawnOrPostponeImpactParticleEffect( delay, dustFlockParams, dustAppearanceRules );

	orientation.copyToFlockParams( &debrisFlockParams );
	assignUpShiftAndModifyBaseSpeed( &debrisFlockParams, upShiftScale, 75.0f, 125.0f );
	spawnOrPostponeImpactParticleEffect( delay, debrisFlockParams, debrisAppearanceRules );
}

static const ColorLifespan kDirtImpactColors[1] {
	{
		.initialColor  = { 0.3f, 0.25f, 0.1f, 1.0f },
		.fadedInColor  = { 0.3f, 0.25f, 0.1f, 1.0f },
		.fadedOutColor = { 0.3f, 0.25f, 0.1f, 0.0f },
	}
};

static const ColorLifespan kDirtDustColors[1] {
	{
		.initialColor  = { 0.3f, 0.25f, 0.1f, 0.0f },
		.fadedInColor  = { 0.3f, 0.25f, 0.1f, 0.3f },
		.fadedOutColor = { 0.3f, 0.25f, 0.1f, 0.0f },
	}
};

void EffectsSystemFacade::spawnDirtImpactParticles( unsigned delay, const FlockOrientation &orientation,
													float upShiftScale, unsigned materialParam ) {
	ConicalFlockParams burstStripesFlockParams {
		.gravity       = GRAVITY,
		.angle         = 12,
		.minSpeed      = 500,
		.maxSpeed      = 700,
		.minPercentage = 0.5f,
		.maxPercentage = 1.0f,
		.minTimeout    = 100,
		.maxTimeout    = 200
	};

	Particle::AppearanceRules burstStripesAppearanceRules {
		.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors        = kDirtImpactColors,
		.kind          = Particle::Spark,
		.length        = 30.0f,
		.width         = 4.0f,
		.lengthSpread  = 10.0f,
		.widthSpread   = 1.0f,
		.sizeBehaviour = Particle::Shrinking
	};

	ConicalFlockParams burstParticlesFlockParams {
		.gravity       = GRAVITY,
		.drag          = 0.01f,
		.angle         = 12,
		.minSpeed      = 500,
		.maxSpeed      = 700,
		.minPercentage = 0.5f,
		.maxPercentage = 1.0f,
		.minTimeout    = 350,
		.maxTimeout    = 1000,
	};

	const Particle::AppearanceRules burstParticlesAppearanceRules {
		.materials     = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors        = kDirtImpactColors,
		.kind          = Particle::Sprite,
		.radius        = 3.0f,
		.sizeBehaviour = Particle::Shrinking
	};

	ConicalFlockParams dustFlockParams {
		.gravity       = 100.0f,
		.angle         = 45.0f,
		.minSpeed      = 25,
		.maxSpeed      = 50,
		.minPercentage = 0.0f,
		.maxPercentage = 0.5f,
		.minTimeout    = 750,
		.maxTimeout    = 1000,
	};

	Particle::AppearanceRules dustAppearanceRules {
		.materials      = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors         = kDirtDustColors,
		.kind           = Particle::Sprite,
		.radius         = 30.0f,
		.radiusSpread   = 7.5f,
		.sizeBehaviour  = Particle::Expanding
	};

	orientation.copyToFlockParams( &burstStripesFlockParams );
	// Never delay stripes
	cg.particleSystem.addSmallParticleFlock( burstStripesAppearanceRules, burstStripesFlockParams );

	orientation.copyToFlockParams( &burstParticlesFlockParams );
	assignUpShiftAndModifyBaseSpeed( &burstParticlesFlockParams, upShiftScale, 150.0f, 200.0f );
	// Never delay burst
	cg.particleSystem.addMediumParticleFlock( burstParticlesAppearanceRules, burstParticlesFlockParams );

	orientation.copyToFlockParams( &dustFlockParams );
	assignUpShiftAndModifyBaseSpeed( &dustFlockParams, upShiftScale, 50.0f, 125.0f );
	spawnOrPostponeImpactParticleEffect( delay, dustFlockParams, dustAppearanceRules );
}

static const ColorLifespan kSandImpactColors[1] {
	{
		.initialColor  = { 0.8f, 0.7f, 0.5f, 0.7f },
		.fadedInColor  = { 0.8f, 0.7f, 0.5f, 0.7f },
		.fadedOutColor = { 0.8f, 0.7f, 0.5f, 0.0f },
		.startFadingOutAtLifetimeFrac = 0.67f,
	}
};

static const ColorLifespan kSandDustColors[1] {
	{
		.initialColor  = { 0.8f, 0.7f, 0.5f, 0.3f },
		.fadedInColor  = { 0.8f, 0.7f, 0.5f, 0.3f },
		.fadedOutColor = { 0.8f, 0.7f, 0.5f, 0.0f },
		.startFadingOutAtLifetimeFrac = 0.67f,
	}
};

void EffectsSystemFacade::spawnSandImpactParticles( unsigned delay, const FlockOrientation &orientation,
													float upShiftScale, unsigned materialParam,
													float dustPercentageScale ) {
	ConicalFlockParams burstFlockParams {
		.gravity       = GRAVITY,
		.angle         = 12,
		.minSpeed      = 300,
		.maxSpeed      = 700,
		.minPercentage = 0.7f,
		.maxPercentage = 1.0f,
		.minTimeout    = 300,
		.maxTimeout    = 400,
	};

	const Particle::AppearanceRules burstParticlesAppearanceRules {
		.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors              = kSandImpactColors,
		.kind                = Particle::Sprite,
		.radius              = 3.0f,
		.applyVertexDynLight = true,
		.sizeBehaviour       = Particle::Shrinking
	};

	orientation.copyToFlockParams( &burstFlockParams );
	assignUpShiftAndModifyBaseSpeed( &burstFlockParams, upShiftScale, 150.0f, 200.0f );
	// Never delay burst
	cg.particleSystem.addSmallParticleFlock( burstParticlesAppearanceRules, burstFlockParams );

	EllipsoidalFlockParams dustFlockParams {
		.stretchScale  = 0.33f,
		.gravity       = 100.0f,
		.minSpeed      = 20,
		.maxSpeed      = 50,
		.minPercentage = 0.7f * dustPercentageScale,
		.maxPercentage = 1.0f * dustPercentageScale,
		.minTimeout    = 750,
		.maxTimeout    = 1000,
	};

	const Particle::AppearanceRules dustAppearanceRules {
		.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
		.colors              = kSandDustColors,
		.kind                = Particle::Sprite,
		.radius              = 35.0f,
		.radiusSpread        = 7.5f,
		.applyVertexDynLight = true,
		.sizeBehaviour       = Particle::Expanding
	};

	orientation.copyToFlockParams( &dustFlockParams );
	assignUpShiftAndModifyBaseSpeed( &dustFlockParams, upShiftScale, 20.0f, 30.0f );
	spawnOrPostponeImpactParticleEffect( delay, dustFlockParams, dustAppearanceRules );
}

static const ColorLifespan kGlassDebrisColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 0.8f, 1.0f, 0.9f, 1.0f },
		.fadedOutColor = { 0.8f, 1.0f, 0.9f, 0.1f },
	}
};

void EffectsSystemFacade::spawnGlassImpactParticles( unsigned delay, const FlockOrientation &orientation,
													 float upShiftScale, unsigned materialParam ) {
	Particle::AppearanceRules appearanceRules {
		.materials      = cgs.media.shaderSparkParticle.getAddressOfHandle(),
		.colors         = kGlassDebrisColors,
		.kind           = Particle::Spark,
		.length         = 10.0f,
		.width          = 1.0f,
		.lengthSpread   = 2.0f,
		.widthSpread    = 0.1f,
	};

	ConicalFlockParams flockParams {
		.gravity       = 0.0f,
		.angle         = 15.0f,
		.minSpeed      = 400.0f,
		.maxSpeed      = 700.0f,
		.minPercentage = 1.0f,
		.maxPercentage = 1.0f,
		.minTimeout    = 75,
		.maxTimeout    = 125
	};

	orientation.copyToFlockParams( &flockParams );
	spawnOrPostponeImpactParticleEffect( delay, flockParams, appearanceRules );
}

void EffectsSystemFacade::spawnBulletImpactEffect( const Impact &impact ) {
	const FlockOrientation flockOrientation = makeRicochetFlockOrientation( impact, &m_rng );

	sfx_s *sfx = nullptr;
	if( cg_particles->integer ) {
		const SurfImpactMaterial impactMaterial = decodeSurfImpactMaterial( impact.surfFlags );
		const unsigned materialParam            = decodeSurfImpactMaterialParam( impact.surfFlags );
		spawnBulletImpactParticleEffectForMaterial( flockOrientation, impactMaterial, materialParam );
		// TODO: Using enum (doesn't work with GCC 10)
		using IM = SurfImpactMaterial;
		if( impactMaterial == IM::Metal ) {
			spawnBulletMetalImpactRosette( flockOrientation );
		} else if( impactMaterial == IM::Stone ) {
			spawnBulletGenericImpactRosette( flockOrientation, 0.5f, 1.0f );
		} else if( impactMaterial == IM::Unknown ) {
			spawnBulletGenericImpactRosette( flockOrientation, 0.3f, 1.0f );
		}
		if( impactMaterial == IM::Metal || impactMaterial == IM::Stone || impactMaterial == IM::Unknown ) {
			m_transientEffectsSystem.spawnBulletLikeImpactModel( impact.origin, impact.normal );
		}
		sfx = getSfxForImpactGroup( getImpactSfxGroupForMaterial( impactMaterial ) );
	} else {
		spawnBulletGenericImpactRosette( flockOrientation, 0.5f, 1.0f );
		m_transientEffectsSystem.spawnBulletLikeImpactModel( impact.origin, impact.normal );
		if( const unsigned numSfx= cgs.media.sfxImpactSolid.length() ) {
			sfx = cgs.media.sfxImpactSolid[m_rng.nextBounded( numSfx )];
		}
	}

	if( sfx ) {
		startSoundForImpact( sfx, impact );
	}
}

void EffectsSystemFacade::spawnBulletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
																	  SurfImpactMaterial impactMaterial,
																	  unsigned materialParam ) {
	// TODO: We used to test against impact normal Z
	[[maybe_unused]] const float upShiftScale = Q_Sqrt( wsw::max( 0.0f, flockOrientation.dir[2] ) );

	switch( impactMaterial ) {
		case SurfImpactMaterial::Unknown:
			break;
		case SurfImpactMaterial::Stone:
			spawnStoneDustParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Stucco:
			spawnStuccoDustParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Wood:
			spawnWoodBulletImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Dirt:
			spawnDirtImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Sand:
			spawnSandImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Metal:
			spawnBulletMetalRicochetParticles( 0, flockOrientation, upShiftScale, materialParam, 0.7f, 1.0f );
			spawnBulletMetalDebrisParticles( 0, flockOrientation, upShiftScale, materialParam, 0.3f, 0.9f );
			break;
		case SurfImpactMaterial::Glass:
			spawnGlassImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
	}
}

auto EffectsSystemFacade::getImpactSfxGroupForMaterial( SurfImpactMaterial impactMaterial ) -> unsigned {
	using IM = SurfImpactMaterial;
	if( impactMaterial == IM::Metal ) {
		return 0;
	}
	if( impactMaterial == IM::Stucco || impactMaterial == IM::Dirt || impactMaterial == IM::Sand ) {
		return 1;
	}
	if( impactMaterial == IM::Wood ) {
		return 2;
	}
	if( impactMaterial == IM::Glass ) {
		return 3;
	}
	return 4;
}

auto EffectsSystemFacade::getSfxForImpactGroup( unsigned group ) -> sfx_s * {
	// Build in a lazy fashion, so we don't have to care of lifetimes
	if( !m_impactSfxForGroups.full() ) [[unlikely]] {
		auto &ma = cgs.media;
		m_impactSfxForGroups.push_back( { ma.sfxImpactMetal.getAddressOfHandles(), ma.sfxImpactMetal.length() } );
		m_impactSfxForGroups.push_back( { ma.sfxImpactSoft.getAddressOfHandles(), ma.sfxImpactSoft.length() } );
		m_impactSfxForGroups.push_back( { ma.sfxImpactWood.getAddressOfHandles(), ma.sfxImpactWood.length() } );
		m_impactSfxForGroups.push_back( { ma.sfxImpactGlass.getAddressOfHandles(), ma.sfxImpactGlass.length() } );
		m_impactSfxForGroups.push_back( { ma.sfxImpactSolid.getAddressOfHandles(), ma.sfxImpactSolid.length() } );
	}

	assert( m_impactSfxForGroups.full() && group < m_impactSfxForGroups.size() );
	auto [sfxData, dataLen] = m_impactSfxForGroups[group];
	if( dataLen ) {
		return sfxData[m_rng.nextBounded( dataLen )];
	}
	return nullptr;
}

auto EffectsSystemFacade::getImpactSfxGroupForSurfFlags( int surfFlags ) -> unsigned {
	return getImpactSfxGroupForMaterial( decodeSurfImpactMaterial( surfFlags ) );
}

void EffectsSystemFacade::spawnUnderwaterBulletLikeImpactEffect( const Impact &impact ) {
	m_transientEffectsSystem.spawnBulletLikeImpactModel( impact.origin, impact.normal );
	// TODO: Add rings/bubbles?
}

void EffectsSystemFacade::spawnPelletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
																	  SurfImpactMaterial impactMaterial,
																	  unsigned materialParam,
																	  unsigned index, unsigned total ) {
	// TODO: We used to test against impact normal Z
	[[maybe_unused]] const float upShiftScale = Q_Sqrt( wsw::max( 0.0f, flockOrientation.dir[2] ) );

	switch( impactMaterial ) {
		case SurfImpactMaterial::Unknown:
			break;
		case SurfImpactMaterial::Stone:
			spawnStoneDustParticles( 0, flockOrientation, upShiftScale, materialParam, 0.75f );
			break;
		case SurfImpactMaterial::Stucco:
			spawnStuccoDustParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Wood:
			spawnWoodBulletImpactParticles( 0, flockOrientation, upShiftScale, materialParam, 0.5f );
			break;
		case SurfImpactMaterial::Dirt:
			spawnDirtImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Sand:
			spawnSandImpactParticles( 0, flockOrientation, upShiftScale, materialParam, 0.25f );
			break;
		case SurfImpactMaterial::Metal:
			if( m_rng.tryWithChance( 0.5f ) ) {
				spawnBulletMetalRicochetParticles( 0, flockOrientation, upShiftScale, materialParam, 0.0f, 0.5f );
			}
			if( m_rng.tryWithChance( 0.5f ) ) {
				spawnBulletMetalDebrisParticles( 0, flockOrientation, upShiftScale, materialParam, 0.0f, 0.5f );
			}
			break;
		case SurfImpactMaterial::Glass:
			spawnGlassImpactParticles( 0, flockOrientation, upShiftScale, materialParam );
			break;
	}
}

void EffectsSystemFacade::spawnExplosionImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
																		 SurfImpactMaterial impactMaterial,
																		 unsigned materialParam ) {
	// TODO: We used to test against impact normal Z
	[[maybe_unused]] const float upShiftScale = Q_Sqrt( wsw::max( 0.0f, flockOrientation.dir[2] ) );
	[[maybe_unused]] unsigned delay = 0;

	switch( impactMaterial ) {
		case SurfImpactMaterial::Unknown:
			break;
		case SurfImpactMaterial::Stone:
			delay = 100 + m_rng.nextBoundedFast( 100 );
			spawnStoneDustParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Stucco:
			delay = 100 + m_rng.nextBoundedFast( 100 );
			spawnStuccoDustParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Wood:
			delay = 50 + m_rng.nextBoundedFast( 150 );
			spawnWoodBulletImpactParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Dirt:
			delay = m_rng.nextBoundedFast( 300 );
			spawnDirtImpactParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Sand:
			delay = 100 + m_rng.nextBoundedFast( 300 );
			spawnSandImpactParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
		case SurfImpactMaterial::Metal:
			delay = m_rng.nextBoundedFast( 100 );
			if( m_rng.next() % 2 ) {
				spawnBulletMetalRicochetParticles( delay, flockOrientation, upShiftScale, materialParam, 0.7f, 1.0f );
			} else {
				spawnBulletMetalDebrisParticles( delay, flockOrientation, upShiftScale, materialParam, 0.3f, 0.9f );
			}
			break;
		case SurfImpactMaterial::Glass:
			delay = m_rng.nextBoundedFast( 100 );
			spawnGlassImpactParticles( delay, flockOrientation, upShiftScale, materialParam );
			break;
	}
}

template <typename FlockParams>
void EffectsSystemFacade::spawnOrPostponeImpactParticleEffect( unsigned delay,
															   const FlockParams &flockParams,
															   const Particle::AppearanceRules &appearanceRules,
															   TransientEffectsSystem::ParticleFlockBin bin ) {
	if( delay ) {
		m_transientEffectsSystem.addDelayedParticleEffect( delay, bin, flockParams, appearanceRules );
	} else {
		// TODO: Hide bins from ParticleSystem public interface
		if( bin == TransientEffectsSystem::ParticleFlockBin::Small ) {
			cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
		} else if( bin == TransientEffectsSystem::ParticleFlockBin::Medium ) {
			cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
		} else {
			cg.particleSystem.addLargeParticleFlock( appearanceRules, flockParams );
		}
	}
}

static const ColorLifespan kWaterSplashColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.7f },
		.fadedInColor  = { 1.0f, 1.0f, 1.0f, 0.3f },
		.fadedOutColor = { 0.0f, 0.0f, 1.0f, 0.0f },
	}
};

static const ColorLifespan kWaterDustColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 1.0f, 1.0f, 1.0f, 0.1f },
		.fadedOutColor = { 0.0f, 0.0f, 1.0f, 0.0f },
	}
};

static const ColorLifespan kSlimeSplashColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 0.0f, 0.7f },
		.fadedInColor  = { 0.0f, 1.0f, 0.0f, 0.3f },
		.fadedOutColor = { 0.0f, 1.0f, 0.0f, 0.0f },
	}
};

static const ColorLifespan kSlimeDustColors[1] {
	{
		.initialColor  = { 1.0f, 1.0f, 1.0f, 0.0f },
		.fadedInColor  = { 0.8f, 1.0f, 0.9f, 0.1f },
		.fadedOutColor = { 0.0f, 1.0f, 0.0f, 0.0f },
	}
};

static const ColorLifespan kLavaSplashColors[1] {
	{
		.initialColor  = { 1.0f, 0.67f, 0.0f, 1.0f },
		.fadedInColor  = { 1.0f, 0.67f, 0.0f, 1.0f },
		.fadedOutColor = { 0.5f, 0.3f, 0.3f, 0.0f },
	}
};

static const ColorLifespan kLavaDropsColors[3] {
	{
		.initialColor  = { 1.0f, 0.67f, 0.1f, 1.0f },
		.fadedInColor  = { 1.0f, 0.67f, 0.01f, 1.0f },
		.fadedOutColor = { 1.0f, 0.67f, 0.075f, 0.3f }
	},
	{
		.initialColor  = { 1.0f, 0.67f, 0.1f, 1.0f },
		.fadedInColor  = { 1.0f, 0.5f, 0.1f, 1.0f },
		.fadedOutColor = { 1.0f, 0.5f, 0.1f, 0.3f },
	},
	{
		.initialColor  = { 1.0f, 0.67f, 0.1f, 1.0f },
		.fadedInColor  = { 0.7f, 0.39f, 0.075f, 1.0f },
		.fadedOutColor = { 0.7f, 0.39f, 0.075f, 0.3f },
	}
};

static const ColorLifespan kLavaDustColors[1] {
	{
		.initialColor  = { 1.0f, 0.67f, 0.0f, 0.00f },
		.fadedInColor  = { 1.0f, 0.67f, 0.0f, 0.05f },
		.fadedOutColor = { 0.5f, 0.3f, 0.3f, 0.00f },
	}
};

void EffectsSystemFacade::spawnLiquidImpactParticleEffect( unsigned delay, const Impact &impact, float percentageScale,
														   std::pair<float, float> randomRotationAngleCosineRange ) {
	std::span<const ColorLifespan> splashColors, dropsColors, dustColors;

	shader_s **materials     = nullptr;
	auto dropParticlesKind   = Particle::Sprite;
	float minDropsPercentage = 0.5f;
	float maxDropsPercentage = 1.0f;

	if( impact.contents & CONTENTS_WATER ) {
		splashColors = kWaterSplashColors;
		dustColors   = kWaterDustColors;
		materials    = cgs.media.shaderFlareParticle.getAddressOfHandle();
	} else if( impact.contents & CONTENTS_SLIME ) {
		// TODO: We don't actually have slime on default maps, do we?

		splashColors = kSlimeSplashColors,
		dustColors   = kSlimeDustColors,
		materials    = cgs.media.shaderFlareParticle.getAddressOfHandle();
	} else if( impact.contents & CONTENTS_LAVA ) {
		splashColors = kLavaSplashColors;
		dustColors   = kLavaDustColors;
		dropsColors  = kLavaDropsColors;

		dropParticlesKind  = Particle::Spark;
		minDropsPercentage = 0.3f;
		maxDropsPercentage = 0.5f;

		materials = cgs.media.shaderSparkParticle.getAddressOfHandle();
	}

	if( materials ) {
		const FlockOrientation flockOrientation = makeRicochetFlockOrientation( impact, &m_rng,
																				randomRotationAngleCosineRange );

		if( dropsColors.empty() ) {
			dropsColors = splashColors;
		}

		ConicalFlockParams splashFlockParams {
			.gravity       = GRAVITY,
			.angle         = 12,
			.minSpeed      = 500,
			.maxSpeed      = 700,
			.minPercentage = 0.7f * percentageScale,
			.maxPercentage = 1.0f * percentageScale,
			.minTimeout    = 100,
			.maxTimeout    = 200
		};

		const Particle::AppearanceRules splashAppearanceRules {
			.materials     = materials,
			.colors        = splashColors,
			.kind          = Particle::Spark,
			.length        = 40.0f,
			.width         = 4.0f,
			.lengthSpread  = 10.0f,
			.widthSpread   = 1.0f,
			.sizeBehaviour = Particle::Shrinking
		};

		ConicalFlockParams dropsFlockParams {
			.gravity        = GRAVITY,
			.drag           = 0.015f,
			.angle          = 15,
			.minBounceCount = 0,
			.maxBounceCount = 0,
			.minSpeed       = 300,
			.maxSpeed       = 900,
			.minPercentage  = minDropsPercentage * percentageScale,
			.maxPercentage  = maxDropsPercentage * percentageScale,
			.minTimeout     = 350,
			.maxTimeout     = 700,
		};

		const Particle::AppearanceRules dropsAppearanceRules {
			.materials     = materials,
			.colors        = dropsColors,
			.kind          = dropParticlesKind,
			.length        = 3.0f,
			.width         = 1.5f,
			.radius        = 1.25f,
			.radiusSpread  = 0.25f,
			.sizeBehaviour = Particle::ExpandingAndShrinking
		};

		ConicalFlockParams dustFlockParams {
			.gravity       = 100.0f,
			.angle         = 7.5f,
			.minSpeed      = 50,
			.maxSpeed      = 100,
			.minShiftSpeed = 450.0f,
			.maxShiftSpeed = 550.0f,
			.minPercentage = 0.4f * percentageScale,
			.maxPercentage = 0.7f * percentageScale,
			.minTimeout    = 100,
			.maxTimeout    = 150,
		};

		const Particle::AppearanceRules dustAppearanceRules {
			.materials      = materials,
			.colors         = dustColors,
			.kind           = Particle::Sprite,
			.radius         = 25.0f,
			.radiusSpread   = 7.5f,
			.sizeBehaviour  = Particle::Expanding
		};

		flockOrientation.copyToFlockParams( &splashFlockParams );
		spawnOrPostponeImpactParticleEffect( delay, splashFlockParams, splashAppearanceRules );

		flockOrientation.copyToFlockParams( &dropsFlockParams );
		spawnOrPostponeImpactParticleEffect( delay, dropsFlockParams, dropsAppearanceRules,
											 TransientEffectsSystem::ParticleFlockBin::Medium );

		flockOrientation.copyToFlockParams( &dustFlockParams );
		spawnOrPostponeImpactParticleEffect( delay, dustFlockParams, dustAppearanceRules );
	}
}

void EffectsSystemFacade::spawnBulletLiquidImpactEffect( const Impact &impact ) {
	spawnLiquidImpactParticleEffect( 0, impact, 1.0f, { 0.70f, 0.95f } );
	if( const unsigned numSfx = cgs.media.sfxImpactWater.length() ) {
		sfx_s *sfx = cgs.media.sfxImpactWater[m_rng.nextBounded( numSfx )];
		startSoundForImpact( sfx, impact );
	}
}

void EffectsSystemFacade::spawnMultiplePelletImpactEffects( std::span<const Impact> impacts ) {
	if( cg_particles->integer ) {
		for( unsigned i = 0; i < impacts.size(); ++i ) {
			const Impact &impact               = impacts[i];
			const SurfImpactMaterial material  = decodeSurfImpactMaterial( impact.surfFlags );
			const unsigned materialParam       = decodeSurfImpactMaterialParam( impact.surfFlags );
			const FlockOrientation orientation = makeRicochetFlockOrientation( impact, &m_rng );
			spawnPelletImpactParticleEffectForMaterial( orientation, material, materialParam, i, impacts.size() );
			using IM = SurfImpactMaterial;
			if( material == IM::Metal || material == IM::Stone || material == IM::Unknown ) {
				spawnBulletGenericImpactRosette( orientation, 0.3f, 0.6f );
				m_transientEffectsSystem.spawnBulletLikeImpactModel( impact.origin, impact.normal );
			}
		}
		spawnImpactSoundsWhenNeededCheckingMaterials( impacts );
	} else {
		for( const Impact &impact: impacts ) {
			const FlockOrientation orientation = makeRicochetFlockOrientation( impact, &m_rng );
			spawnBulletGenericImpactRosette( orientation, 0.3f, 0.6f );
			m_transientEffectsSystem.spawnBulletLikeImpactModel( impact.origin, impact.normal );
		}
		// TODO: Use spans via custom span type
		sfx_s **sfxBegin = cgs.media.sfxImpactSolid.getAddressOfHandles();
		sfx_s **sfxEnd   = sfxBegin + cgs.media.sfxImpactSolid.length();
		spawnImpactSoundsWhenNeededUsingTheseSounds( impacts, sfxBegin, sfxEnd );
	}
}

void EffectsSystemFacade::spawnMultipleExplosionImpactEffects( std::span<const Impact> impacts ) {
	for( const Impact &impact: impacts ) {
		const SurfImpactMaterial material  = decodeSurfImpactMaterial( impact.surfFlags );
		const FlockOrientation orientation = makeRicochetFlockOrientation( impact, &m_rng );
		const unsigned materialParam       = decodeSurfImpactMaterialParam( impact.surfFlags );
		spawnExplosionImpactParticleEffectForMaterial( orientation, material, materialParam );
	}
	spawnImpactSoundsWhenNeededCheckingMaterials( impacts );
}

void EffectsSystemFacade::spawnMultipleLiquidImpactEffects( std::span<const Impact> impacts, float percentageScale,
															std::pair<float, float> randomRotationAngleCosineRange,
															std::pair<unsigned, unsigned> delayRange ) {
	assert( delayRange.first <= delayRange.second );
	if( delayRange.second > 0 && delayRange.first != delayRange.second ) {
		for( const Impact &impact: impacts ) {
			const unsigned delay = delayRange.first + m_rng.nextBoundedFast( delayRange.second - delayRange.first );
			spawnLiquidImpactParticleEffect( delay, impact, percentageScale, randomRotationAngleCosineRange );
		}
	} else {
		for( const Impact &impact: impacts ) {
			spawnLiquidImpactParticleEffect( delayRange.first, impact, percentageScale, randomRotationAngleCosineRange );
		}
	}
	sfx_s **sfxBegin = cgs.media.sfxImpactWater.getAddressOfHandles();
	sfx_s **sfxEnd   = sfxBegin + cgs.media.sfxImpactWater.length();
	spawnImpactSoundsWhenNeededUsingTheseSounds( impacts, sfxBegin, sfxEnd );
}

void EffectsSystemFacade::spawnImpactSoundsWhenNeededUsingTheseSounds( std::span<const Impact> impacts,
																	   sfx_s **sfxBegin, sfx_s **sfxEnd ) {
	if( impacts.empty() ) {
		return;
	}

	auto *const acceptedImpactNums = (unsigned *)alloca( sizeof( unsigned ) * impacts.size() );
	unsigned numAcceptedImpacts    = 0;

	// Spawn the first sound
	acceptedImpactNums[numAcceptedImpacts++] = 0;

	for( unsigned i = 1; i < impacts.size(); ++i ) {
		const Impact &thisImpact = impacts[i];
		bool skipThisImpact      = false;
		for( unsigned j = 0; j < numAcceptedImpacts; ++j ) {
			const Impact &thatImpact = impacts[acceptedImpactNums[j]];
			// TODO: Check whether they belong to the same CM leaf/are mutually visible
			if( DistanceSquared( thisImpact.origin, thatImpact.origin ) < wsw::square( 96.0f ) ) {
				skipThisImpact = true;
				break;
			}
		}
		if( !skipThisImpact ) {
			acceptedImpactNums[numAcceptedImpacts++] = i;
		}
	}

	// Now, actually do spawn sounds
	for( unsigned i = 0; i < numAcceptedImpacts; ++i ) {
		const Impact &impact = impacts[acceptedImpactNums[i]];
		sfx_s *sfx           = sfxBegin[m_rng.nextBounded( (unsigned)( sfxEnd - sfxBegin ) )];
		startSoundForImpact( sfx, impact );
	}
}

void EffectsSystemFacade::spawnImpactSoundsWhenNeededCheckingMaterials( std::span<const Impact> impacts ) {
	if( impacts.empty() ) {
		return;
	}

	auto *const acceptedImpactNums         = (unsigned *)alloca( sizeof( unsigned ) * impacts.size() );
	auto *const sfxGroupsOfAcceptedImpacts = (unsigned *)alloca( sizeof( unsigned ) * impacts.size() );
	unsigned numAcceptedImpacts = 0;

	// Use a simple greedy approach.
	// Spawn the first spawn without conditions.
	acceptedImpactNums[numAcceptedImpacts] = 0;
	sfxGroupsOfAcceptedImpacts[numAcceptedImpacts] = getImpactSfxGroupForSurfFlags( ( impacts.front().surfFlags ) );
	numAcceptedImpacts++;

	for( unsigned i = 1; i < impacts.size(); ++i ) {
		const Impact &thisImpact = impacts[i];
		const unsigned thisGroup = getImpactSfxGroupForSurfFlags( thisImpact.surfFlags );
		bool skipThisImpact      = false;
		for( unsigned j = 0; j < numAcceptedImpacts; ++j ) {
			const Impact &thatImpact = impacts[acceptedImpactNums[j]];
			const unsigned thatGroup = sfxGroupsOfAcceptedImpacts[j];
			if( thisGroup == thatGroup ) {
				// TODO: Check whether they belong to the same CM leaf/are mutually visible
				if( DistanceSquared( thisImpact.origin, thatImpact.origin ) < wsw::square( 96.0f ) ) {
					skipThisImpact = true;
					break;
				}
			}
		}
		if( !skipThisImpact ) {
			acceptedImpactNums[numAcceptedImpacts] = i;
			sfxGroupsOfAcceptedImpacts[numAcceptedImpacts] = thisGroup;
			numAcceptedImpacts++;
		}
	}

	// Now, actually do spawn sounds
	for( unsigned i = 0; i < numAcceptedImpacts; ++i ) {
		const Impact &impact = impacts[acceptedImpactNums[i]];
		const unsigned group = sfxGroupsOfAcceptedImpacts[i];
		startSoundForImpact( getSfxForImpactGroup( group ), impact );
	}
}

void EffectsSystemFacade::startSoundForImpact( sfx_s *sfx, const Impact &impact ) {
	assert( std::fabs( VectorLengthFast( impact.normal ) - 1.0f ) < 1e-2f );
	if( sfx ) {
		vec3_t soundOrigin;
		// HACK HACK HACK TODO Make water impacts consistent with regular ones
		sfx_s **waterSfxBegin = cgs.media.sfxImpactWater.getAddressOfHandles();
		sfx_s **waterSfxEnd   = waterSfxBegin + cgs.media.sfxImpactWater.length();
		if( std::find( waterSfxBegin, waterSfxEnd, sfx ) == waterSfxEnd ) {
			VectorAdd( impact.origin, impact.normal, soundOrigin );
		} else {
			VectorSubtract( impact.origin, impact.normal, soundOrigin );
		}
		assert( !( CG_PointContents( soundOrigin ) & ( MASK_SOLID | MASK_WATER ) ) );
		startSound( sfx, soundOrigin );
	}
}

void EffectsSystemFacade::spawnBulletTracer( int owner, const float *from, const float *to ) {
	vec3_t tmp;
	// TODO: Adjust for 3rd person/other players too?
	if( owner == (int)cg.predictedPlayerState.POVnum ) {
		VectorCopy( from, tmp );
		tmp[2] -= 0.5f * playerbox_stand_viewheight;
		from = tmp;
	}
	cg.polyEffectsSystem.spawnTracerEffect( from, to, PolyEffectsSystem::TracerParams {
		.material = cgs.media.shaderSparkParticle,
		.prestep  = m_rng.nextFloat( 64.0f, 96.0f ),
		.width    = m_rng.nextFloat( 2.0f, 2.5f ),
		.length   = 144.0f,
	});
}

void EffectsSystemFacade::spawnPelletTracers( int owner, const float *from, std::span<const vec3_t> to ) {
	vec3_t tmp;
	if( owner == (int)cg.predictedPlayerState.POVnum ) {
		VectorCopy( from, tmp );
		tmp[2] -= 0.5f * playerbox_stand_viewheight;
		from = tmp;
	}
	for( const float *v: to ) {
		cg.polyEffectsSystem.spawnTracerEffect( from, v, PolyEffectsSystem::TracerParams {
			.material = cgs.media.shaderSparkParticle,
			.prestep  = m_rng.nextFloat( 72.0f, 224.0f ),
			.width    = 1.0f,
			.length   = 48.0f,
			.color    = { 1.0f, 0.9f, 0.8f, 1.0f }
		});
	}
}

void EffectsSystemFacade::spawnDustImpactEffect( const float *origin, const float *dir, float radius ) {
	if( !( CG_PointContents( origin ) & CONTENTS_WATER ) ) [[likely]] {
		m_transientEffectsSystem.spawnDustImpactEffect( origin, dir, radius );
	}
}

void EffectsSystemFacade::spawnDashEffect( const float *oldOrigin, const float *newOrigin ) {
	vec3_t dir { newOrigin[0] - oldOrigin[0], newOrigin[1] - oldOrigin[1], newOrigin[2] - oldOrigin[2] };
	const float squaredLength2D = dir[0] * dir[0] + dir[1] * dir[1];
	const float length2DThreshold = 6.0f;
	if( squaredLength2D > length2DThreshold * length2DThreshold ) {
		if( !( CG_PointContents( newOrigin ) & CONTENTS_WATER ) ) [[likely]] {
			const vec3_t effectOrigin { newOrigin[0], newOrigin[1], newOrigin[2] + playerbox_stand_mins[2] };
			const float rcpLength = Q_RSqrt( squaredLength2D + dir[2] * dir[2] );
			VectorScale( dir, rcpLength, dir );
			m_transientEffectsSystem.spawnDashEffect( effectOrigin, dir );
		}
	}
}

bool getElectroboltTeamColor( int team, float *color ) {
	if( cg_teamColoredBeams->integer && ( ( team == TEAM_ALPHA || team == TEAM_BETA ) ) ) {
		CG_TeamColor( team, color );
		return true;
	}
	return false;
}

void EffectsSystemFacade::spawnElectroboltBeam( const vec3_t start, const vec3_t end, int team ) {
	if( cg_ebbeam_time->value <= 0.0f || cg_ebbeam_width->integer <= 0 ) {
		return;
	}

	vec4_t color;
	if( !getElectroboltTeamColor( team, color ) ) {
		Vector4Copy( colorWhite, color );
	}

	struct shader_s *material = cgs.media.shaderElectroBeam;
	if( cg_teamColoredBeams->integer && ( team == TEAM_ALPHA || team == TEAM_BETA ) ) {
		if( team == TEAM_ALPHA ) {
			material = cgs.media.shaderElectroBeamAlpha;
		} else {
			material = cgs.media.shaderElectroBeamBeta;
		}
	}

	const auto timeoutSeconds = wsw::clamp( cg_ebbeam_time->value, 0.1f, 1.0f );
	const auto timeoutMillis  = (unsigned)( 1.00f * 1000 * timeoutSeconds );
	const auto lightTimeout   = (unsigned)( 0.25f * 1000 * timeoutSeconds );

	cg.polyEffectsSystem.spawnTransientBeamEffect( start, end, {
		.material          = material,
		.beamColorLifespan = ColorLifespan {
			.initialColor  = { 1.0f, 1.0f, 1.0f, color[3] },
			.fadedInColor  = { color[0], color[1], color[2], color[3] },
			.fadedOutColor = { color[0], color[1], color[2], 0.0f },
			.finishFadingInAtLifetimeFrac = 0.2f,
			.startFadingOutAtLifetimeFrac = 0.5f,
		},
		.lightProps        = std::pair<unsigned, LightLifespan> {
			lightTimeout, {
				.initialColor   = { 1.0f, 1.0f, 1.0f },
				.fadedInColor   = { color[0], color[1], color[2] },
				.fadedOutColor  = { color[0], color[1], color[2] },
				.initialRadius  = 100.0f,
				.fadedInRadius  = 250.0f,
				.fadedOutRadius = 100.0f,
			}
		},
		.width      = wsw::clamp( cg_ebbeam_width->value, 0.0f, 128.0f ),
		.tileLength = 128.0f,
		.timeout    = timeoutMillis,
	});
}

bool getInstagunTeamColor( int team, float *color ) {
	if( cg_teamColoredInstaBeams->integer && ( team == TEAM_ALPHA || team == TEAM_BETA ) ) {
		CG_TeamColor( team, color );
		return true;
	}
	return false;
}

void EffectsSystemFacade::spawnInstagunBeam( const vec3_t start, const vec3_t end, int team ) {
	if( cg_instabeam_time->value <= 0.0f || cg_instabeam_width->integer <= 0 ) {
		return;
	}

	vec4_t color;
	if( !getInstagunTeamColor( team, color ) ) {
		Vector4Set( color, 1.0f, 0.0f, 0.4f, 0.35f );
	}

	const auto timeoutSeconds = wsw::clamp( cg_instabeam_time->value, 0.1f, 1.0f );
	const auto timeoutMillis  = (unsigned)( 1.00f * 1000 * timeoutSeconds );
	const auto lightTimeout   = (unsigned)( 0.25f * 1000 * timeoutSeconds );

	cg.polyEffectsSystem.spawnTransientBeamEffect( start, end, {
		.material          = cgs.media.shaderInstaBeam,
		.beamColorLifespan = ColorLifespan {
			.initialColor  = { 1.0f, 1.0f, 1.0f, color[3] },
			.fadedInColor  = { color[0], color[1], color[2], color[3] },
			.fadedOutColor = { color[0], color[1], color[2], 0.0f },
			.finishFadingInAtLifetimeFrac = 0.2f,
			.startFadingOutAtLifetimeFrac = 0.5f,
		},
		.lightProps        = std::pair<unsigned, LightLifespan> {
			lightTimeout, {
				.initialColor   = { 1.0f, 1.0f, 1.0f },
				.fadedInColor   = { color[0], color[1], color[2] },
				.fadedOutColor  = { color[0], color[1], color[2] },
				.initialRadius  = 100.0f,
				.fadedInRadius  = 250.0f,
				.fadedOutRadius = 100.0f,
			}
		},
		.width      = wsw::clamp( cg_instabeam_width->value, 0.0f, 128.0f ),
		.tileLength = 128.0f,
		.timeout    = timeoutMillis,
	});
}

void EffectsSystemFacade::spawnWorldLaserBeam( const float *from, const float *to, float width ) {
	// TODO: Either disable fading out or make it tracked
	const auto timeout = wsw::max( 2u, cgs.snapFrameTime );
	cg.polyEffectsSystem.spawnTransientBeamEffect( from, to, {
		.material      = cgs.media.shaderLaser,
		.timeout       = timeout,
	});
}

void EffectsSystemFacade::spawnGameDebugBeam( const float *from, const float *to, const float *color, int ) {
	// TODO: Utilize the parameter
	cg.polyEffectsSystem.spawnTransientBeamEffect( from, to, {
		.material          = cgs.media.shaderLaser,
		.beamColorLifespan = {
			.initialColor  = { color[0], color[1], color[2] },
			.fadedInColor  = { color[0], color[1], color[2] },
			.fadedOutColor = { color[0], color[1], color[2] },
		},
		.width             = 8.0f,
		.timeout           = 500u,
	});
}