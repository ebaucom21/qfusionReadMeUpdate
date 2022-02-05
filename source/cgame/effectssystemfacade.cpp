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
	SoundSystem::Instance()->StartFixedSound( sfx, origin, CHAN_AUTO, cg_volume_effects->value, attenuation );
}

void EffectsSystemFacade::startRelativeSound( sfx_s *sfx, int entNum, float attenuation ) {
	SoundSystem::Instance()->StartRelativeSound( sfx, entNum, CHAN_AUTO, cg_volume_effects->value, attenuation );
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
	spawnExplosionEffect( origin, vec3_origin, cgs.media.sfxRocketLauncherStrongHit, radius, true );
}

static const vec4_t kExplosionInitialColor { 1.0f, 0.9f, 0.1f, 0.0f };
static const vec4_t kExplosionFadedInColor { 1.0f, 0.7f, 0.1f, 1.0f };
static const vec4_t kExplosionFadedOutColor { 1.0f, 0.5f, 0.1f, 0.0f };

void EffectsSystemFacade::spawnExplosionEffect( const float *origin, const float *offset, sfx_s *sfx,
												float radius, bool addSoundLfe ) {
	vec3_t spriteOrigin, almostExactOrigin;
	VectorMA( origin, 8.0f, offset, spriteOrigin );
	VectorAdd( origin, offset, almostExactOrigin );

	startSound( sfx, almostExactOrigin, ATTN_DISTANT );

	if( addSoundLfe ) {
		startSound( cgs.media.sfxExplosionLfe, almostExactOrigin, ATTN_NORM );
	}

	UniformFlockFiller flockFiller {
		.origin = { origin[0], origin[1], origin[2] },
		.offset = { offset[0], offset[1], offset[2] },
		.gravity = 350.0f, .minSpeed = 150.0f, .maxSpeed = 300.0f,
		.minPercentage = 1.0f, .maxPercentage = 1.0f
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderDebrisParticle,
		.kind     = Particle::Spark,
		.length   = 5.0f,
		.width    = 2.0f,
		.initialColor  = kExplosionInitialColor,
		.fadedInColor  = kExplosionFadedInColor,
		.fadedOutColor = kExplosionFadedOutColor,
	};

	cg.particleSystem.addLargeParticleFlock( appearanceRules, flockFiller );

	m_transientEffectsSystem.spawnExplosion( spriteOrigin );
}

void EffectsSystemFacade::spawnShockwaveExplosionEffect( const float *origin, const float *dir, int mode ) {
}

static const vec4_t kPlasmaInitialColor { 0.0f, 1.0f, 0.0f, 0.0f };
static const vec4_t kPlasmaFadedInColor { 0.3f, 1.0f, 0.5f, 1.0f };
static const vec4_t kPlasmaFadedOutColor { 0.7f, 1.0f, 0.7f, 0.0f };

void EffectsSystemFacade::spawnPlasmaExplosionEffect( const float *origin, const float *impactNormal, int mode ) {
	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	sfx_s *sfx = ( mode == FIRE_MODE_STRONG ) ? cgs.media.sfxPlasmaStrongHit : cgs.media.sfxPlasmaWeakHit;
	startSound( sfx, soundOrigin, ATTN_IDLE );

	UniformFlockFiller flockFiller {
		.origin = { origin[0], origin[1], origin[2] },
		.offset = { impactNormal[0], impactNormal[1], impactNormal[2] },
		.minTimeout = 50, .maxTimeout = 200
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderSparkParticle,
		.kind     = Particle::Spark,
		.length   = 3.0f,
		.width    = 1.0f,
		.initialColor  = kPlasmaInitialColor,
		.fadedInColor  = kPlasmaFadedInColor,
		.fadedOutColor = kPlasmaFadedOutColor
	};

	cg.particleSystem.addMediumParticleFlock( appearanceRules, flockFiller );

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

static const vec4_t kBloodInitialColor { 1.0f, 0.0f, 1.0f, 0.0f };
static const vec4_t kBloodFadedInColor { 1.0f, 0.0f, 1.0f, 1.0f };
static const vec4_t kBloodFadedOutColor { 1.0f, 0.0f, 1.0f, 0.3f };

void EffectsSystemFacade::spawnPlayerHitEffect( const float *origin, const float *dir, int damage ) {
	if( cg_showBloodTrail->integer && cg_bloodTrail->integer ) {
		ConeFlockFiller flockFiller {
			.origin = { origin[0], origin[1], origin[2] },
			.offset = { dir[0], dir[1], dir[2] },
			.dir    = { dir[0], dir[1], dir[2] },
			.gravity = 150.0f, .bounceCount = 0,
			.minSpeed = 50.0f, .maxSpeed = 75.0f,
			.minPercentage = 0.5f, .maxPercentage = 0.5f
		};

		Particle::AppearanceRules appearanceRules {
			.material = cgs.media.shaderBloodParticle,
			.kind     = Particle::Spark,
			.length   = 3.0f,
			.width    = 1.5f,
			.initialColor  = kBloodInitialColor,
			.fadedInColor  = kBloodFadedInColor,
			.fadedOutColor = kBloodFadedOutColor
		};

		cg.particleSystem.addSmallParticleFlock( appearanceRules, flockFiller );
	}

	m_transientEffectsSystem.spawnCartoonHitEffect( origin, dir, damage );
}

static const vec4_t kElectroboltHitInitialColor { 0.1f, 0.3f, 1.0f, 0.0f };
static const vec4_t kElectroboltHitFadedInColor { 0.1f, 0.5f, 1.0f, 1.0f };
static const vec4_t kElectroboltHitFadedOutColor { 0.1f, 0.5f, 1.0f, 1.0f };

void EffectsSystemFacade::spawnElectroboltHitEffect( const float *origin, const float *dir ) {
	UniformFlockFiller flockFiller {
		.origin = { origin[0], origin[1], origin[2] }, .offset = { dir[0], dir[1], dir[2] }
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderSparkParticle,
		.kind     = Particle::Spark,
		.length   = 8.0f,
		.width    = 2.0f,
		.initialColor  = kElectroboltHitInitialColor,
		.fadedInColor  = kElectroboltHitFadedInColor,
		.fadedOutColor = kElectroboltHitFadedOutColor
	};

	cg.particleSystem.addLargeParticleFlock( appearanceRules, flockFiller );

	const vec3_t soundOrigin { origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnElectroboltHitEffect( origin, dir );
}

static vec4_t instagunHitInitialColorForTeam[2];
static vec4_t instagunHitFadedInColorForTeam[2];
static vec4_t instagunHitFadedOutColorForTeam[2];

static const vec4_t kInstagunHitInitialColor { 1.0f, 1.0f, 1.0f, 0.0f };
static const vec4_t kInstagunHitFadedInColor { 1.0f, 1.0f, 1.0f, 1.0f };
static const vec4_t kInstagunHitFadedOutColor { 1.0f, 1.0f, 1.0f, 0.0f };

void EffectsSystemFacade::spawnInstagunHitEffect( const float *origin, const float *dir, int ownerNum ) {
	const float *initialColor  = kInstagunHitInitialColor;
	const float *fadedInColor  = kInstagunHitFadedInColor;
	const float *fadedOutColor = kInstagunHitFadedOutColor;
	if( cg_teamColoredInstaBeams->integer && ownerNum && ( ownerNum < gs.maxclients + 1 ) ) {
		if( const int team = cg_entities[ownerNum].current.team; ( team == TEAM_ALPHA ) || ( team == TEAM_BETA ) ) {
			vec3_t teamColor;
			CG_TeamColor( team, teamColor );
			VectorScale( teamColor, 0.67f, teamColor );

			float *const initialColorBuffer  = instagunHitInitialColorForTeam[team - TEAM_ALPHA];
			float *const fadedInColorBuffer  = instagunHitFadedInColorForTeam[team - TEAM_ALPHA];
			float *const fadedOutColorBuffer = instagunHitFadedOutColorForTeam[team - TEAM_ALPHA];

			VectorCopy( teamColor, initialColorBuffer );
			VectorCopy( teamColor, fadedInColorBuffer );
			VectorCopy( teamColor, fadedOutColorBuffer );

			// Preserve the reference alpha
			initialColorBuffer[3]  = kInstagunHitInitialColor[3];
			fadedInColorBuffer[3]  = kInstagunHitFadedInColor[3];
			fadedOutColorBuffer[3] = kInstagunHitFadedOutColor[3];

			initialColor  = initialColorBuffer;
			fadedInColor  = fadedInColorBuffer;
			fadedOutColor = fadedOutColorBuffer;
		}
	}

	UniformFlockFiller flockFiller {
		.origin = { origin[0], origin[1], origin[2] }, .offset = { dir[0], dir[1], dir[2] }
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderSparkParticle,
		.kind     = Particle::Spark,
		.length   = 8.0f,
		.width    = 1.0,
		.initialColor  = initialColor,
		.fadedInColor  = fadedInColor,
		.fadedOutColor = fadedOutColor
	};

	cg.particleSystem.addLargeParticleFlock( appearanceRules, flockFiller );

	// TODO: Don't we need an IG-specific sound
	const vec3_t soundOrigin { origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnInstagunHitEffect( origin, dir, fadedInColor );
}

static const vec4_t kGunbladeHitInitialColor { 1.0f, 0.5f, 0.1f, 0.0f };
static const vec4_t kGunbladeHitFadedInColor { 1.0f, 1.0f, 1.0f, 1.0f };
static const vec4_t kGunbladeHitFadedOutColor { 0.5f, 0.5f, 0.5f, 0.5f };

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

			ConeFlockFiller flockFiller {
				.origin = { pos[0], pos[1], pos[2] },
				.offset = { dir[0], dir[1], dir[2] },
				.dir    = { dir[0], dir[1], dir[2] },
				.angle  = 60
			};

			Particle::AppearanceRules appearanceRules {
				.material = cgs.media.shaderSparkParticle,
				.kind     = Particle::Spark,
				.length   = 4.0f,
				.width    = 1.0f,
				.initialColor  = kGunbladeHitInitialColor,
				.fadedInColor  = kGunbladeHitFadedInColor,
				.fadedOutColor = kGunbladeHitFadedOutColor
			};

			cg.particleSystem.addMediumParticleFlock( appearanceRules, flockFiller );
		}
	}
}

static const vec4_t kGunbladeBlastInitialColor { 1.0f, 0.8f, 0.1f, 0.0f };
static const vec4_t kGunbladeBlastFadedInColor { 1.0f, 0.8f, 0.1f, 1.0f };
static const vec4_t kGunbladeBlastFadedOutColor { 1.0f, 0.8f, 0.1f, 0.0f };

void EffectsSystemFacade::spawnGunbladeBlastHitEffect( const float *origin, const float *dir ) {
	startSound( cgs.media.sfxGunbladeStrongHit[m_rng.nextBounded( 2 )], origin, ATTN_IDLE );

	UniformFlockFiller flockFiller {
		.origin  = { origin[0], origin[1], origin[2] },
		.offset  = { dir[0], dir[1], dir[2] },
		.gravity = 0.0f,
		.bounceCount = 1
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderDebrisParticle,
		.kind     = Particle::Spark,
		.length   = 4.0f,
		.width    = 2.0f,
		.initialColor  = kGunbladeBlastInitialColor,
		.fadedInColor  = kGunbladeBlastFadedInColor,
		.fadedOutColor = kGunbladeBlastFadedOutColor
	};

	cg.particleSystem.addLargeParticleFlock( appearanceRules, flockFiller );

	m_transientEffectsSystem.spawnGunbladeBlastImpactEffect( origin, dir );
}

static const vec4_t kBulletImpactInitialColor { 1.0f, 0.7f, 0.1f, 1.0f };
static const vec4_t kBulletImpactFadedInColor { 1.0f, 1.0f, 1.0f, 1.0f };
static const vec4_t kBulletImpactFadedOutColor { 1.0f, 1.0f, 1.0f, 1.0f };

void EffectsSystemFacade::spawnBulletLikeImpactEffect( const trace_t *trace, float minPercentage, float maxPercentage ) {
	if( trace->surfFlags & SURF_NOIMPACT ) {
		return;
	}

	if( const int entNum = trace->ent; entNum > 0 ) {
		if( const unsigned entType = cg_entities[entNum].type; entType == ET_PLAYER || entType == ET_CORPSE ) {
			return;
		}
	}

	// TODO: Vary percentage by surface type too

	ConeFlockFiller flockFiller {
		.origin        = { trace->endpos[0], trace->endpos[1], trace->endpos[2] },
		.offset        = { trace->plane.normal[0], trace->plane.normal[1], trace->plane.normal[2] },
		.dir           = { trace->plane.normal[0], trace->plane.normal[1], trace->plane.normal[2] },
		.gravity       = 900.0f,
		.minPercentage = minPercentage,
		.maxPercentage = maxPercentage
	};

	Particle::AppearanceRules appearanceRules {
		.material = cgs.media.shaderDebrisParticle,
		.kind     = Particle::Spark,
		.length   = 5.0f,
		.width    = 1.0f,
		.initialColor  = kBulletImpactInitialColor,
		.fadedInColor  = kBulletImpactFadedInColor,
		.fadedOutColor = kBulletImpactFadedOutColor
	};

	cg.particleSystem.addSmallParticleFlock( appearanceRules, flockFiller );

	m_transientEffectsSystem.spawnBulletLikeImpactEffect( trace->endpos, trace->plane.normal );
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
