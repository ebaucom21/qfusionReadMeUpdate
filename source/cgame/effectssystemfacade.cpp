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

static const vec4_t kExplosionSmokeInitialColors[3] {
	{ 0.5f, 0.5f, 0.5f, 0.0f },
	{ 0.5f, 0.5f, 0.5f, 0.0f },
	{ 0.5f, 0.5f, 0.5f, 0.0f },
};

static const vec4_t kExplosionSmokeFadedInColors[3] {
	{ 0.25f, 0.25f, 0.25f, 0.075f },
	{ 0.50f, 0.50f, 0.50f, 0.075f },
	{ 0.75f, 0.75f, 0.75f, 0.075f },
};

static const vec4_t kExplosionSmokeFadedOutColors[3] {
	{ 0.2f, 0.2f, 0.2f, 0.0f },
	{ 0.4f, 0.4f, 0.4f, 0.0f },
	{ 0.6f, 0.6f, 0.6f, 0.0f },
};

static const vec4_t kExplosionSparksInitialColors[3] {
	{ 1.0f, 1.0f, 1.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f, 0.0f },
};

static const vec4_t kExplosionSparksFadedInColors[3] {
	{ 1.0f, 0.6f, 0.3f, 1.0f },
	{ 1.0f, 0.8f, 0.4f, 1.0f },
	{ 1.0f, 0.7f, 0.5f, 1.0f }
};

static const vec4_t kExplosionSparksFadedOutColors[3] {
	{ 0.5f, 0.5f, 0.5f, 0.3f },
	{ 0.5f, 0.5f, 0.5f, 0.3f },
	{ 0.5f, 0.5f, 0.5f, 0.3f }
};

void EffectsSystemFacade::spawnExplosionEffect( const float *origin, const float *dir, sfx_s *sfx,
												float radius, bool addSoundLfe ) {
	vec3_t spriteOrigin, almostExactOrigin;
	VectorMA( origin, 8.0f, dir, spriteOrigin );
	VectorAdd( origin, dir, almostExactOrigin );

	startSound( sfx, almostExactOrigin, ATTN_DISTANT );

	if( addSoundLfe ) {
		startSound( cgs.media.sfxExplosionLfe, almostExactOrigin, ATTN_NORM );
	}

	if( cg_explosionsSmoke->integer ) {
		Particle::AppearanceRules appearanceRules {
			.materials           = cgs.media.shaderFlareParticle.getAddressOfHandle(),
			.initialColors       = kExplosionSmokeInitialColors,
			.fadedInColors       = kExplosionSmokeFadedInColors,
			.fadedOutColors      = kExplosionSmokeFadedOutColors,
			.numColors           = std::size( kExplosionSmokeFadedInColors ),
			.kind                = Particle::Sprite,
			.radius              = 10.0f,
			.radiusSpread        = 5.0f,
			.fadeInLifetimeFrac  = 0.35,
			.fadeOutLifetimeFrac = 0.20f
		};

		EllipsoidalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { dir[0], dir[1], dir[2] },
			.stretchScale  = 1.25f,
			.gravity       = -50.0f,
			.restitution   = 0.33f,
			.minSpeed      = 10.0f,
			.maxSpeed      = 60.0f,
			.minShiftSpeed = 20.0f,
			.maxShiftSpeed = 35.0f,
			.minPercentage = 1.0f,
			.maxPercentage = 1.0f,
			.minTimeout    = 1250,
			.maxTimeout    = 1750,
		};

		cg.particleSystem.addLargeParticleFlock( appearanceRules, flockParams );
	}

	if( cg_particles->integer ) {
		Particle::AppearanceRules appearanceRules {
			.materials      = cgs.media.shaderDebrisParticle.getAddressOfHandle(),
			.initialColors  = kExplosionSparksInitialColors,
			.fadedInColors  = kExplosionSparksFadedInColors,
			.fadedOutColors = kExplosionSparksFadedOutColors,
			.numColors      = std::size( kExplosionSparksFadedOutColors ),
			.kind           = Particle::Sprite,
			.radius         = 1.25f,
			.radiusSpread   = 0.25f
		};

		EllipsoidalFlockParams flockParams {
			.origin        = { origin[0], origin[1], origin[2] },
			.offset        = { dir[0], dir[1], dir[2] },
			.gravity       = 0.25f * GRAVITY,
			.drag          = 0.025f,
			.restitution   = 0.33f,
			.minSpeed      = 150.0f,
			.maxSpeed      = 400.0f,
			.minShiftSpeed = 200.0f,
			.maxShiftSpeed = 300.0f,
			.minPercentage = 0.3f,
			.maxPercentage = 0.6f,
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

		flockParams.minSpeed      = 450;
		flockParams.maxSpeed      = 600;
		flockParams.drag          = 0.01f;
		flockParams.minTimeout    = 100;
		flockParams.maxTimeout    = 150;
		flockParams.minPercentage = 0.5f;
		flockParams.maxPercentage = 1.0f;
		flockParams.minShiftSpeed = 50.0f;
		flockParams.maxShiftSpeed = 100.0f;

		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

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
			.initialColors  = &kPlasmaInitialColor,
			.fadedInColors  = &kPlasmaFadedInColor,
			.fadedOutColors = &kPlasmaFadedOutColor,
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

static const vec4_t kBloodInitialColors[5] {
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 1.0f, 0.0f, 0.0f, 1.0f },
	{ 0.0f, 1.0f, 0.5f, 1.0f },
	{ 0.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f }
};

static const vec4_t kBloodFadedInColors[5] {
	{ 1.0f, 0.3f, 0.7f, 1.0f },
	{ 1.0f, 0.6f, 0.3f, 1.0f },
	{ 0.3f, 1.0f, 0.5f, 1.0f },
	{ 0.3f, 0.7f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
};

static const vec4_t kBloodFadedOutColors[5] {
	{ 0.9f, 0.3f, 0.7f, 1.0f },
	{ 0.9f, 0.5f, 0.0f, 1.0f },
	{ 0.0f, 0.5f, 0.0f, 1.0f },
	{ 0.0f, 0.7f, 1.0f, 1.0f },
	{ 0.3f, 0.3f, 0.3f, 1.0f },
};

static_assert( std::size( kBloodInitialColors ) == std::size( kBloodFadedInColors ) );
static_assert( std::size( kBloodInitialColors ) == std::size( kBloodFadedOutColors ) );

shader_s *EffectsSystemFacade::s_bloodMaterials[3];

void EffectsSystemFacade::spawnPlayerHitEffect( const float *origin, const float *dir, int damage ) {
	if( const int palette        = cg_bloodTrailPalette->integer ) {
		const int indexForStyle  = wsw::clamp<int>( palette - 1, 0, std::size( kBloodInitialColors ) - 1 );
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
		if( !s_bloodMaterials[0] ) [[unlikely]] {
			static_assert( std::size( s_bloodMaterials ) == 3 );
			// Looks nicer than std::fill in this case, even if it's "wrong" from a purist POV
			s_bloodMaterials[0] = cgs.media.shaderBloodParticle;
			s_bloodMaterials[1] = cgs.media.shaderBloodParticle;
			s_bloodMaterials[2] = cgs.media.shaderBlastParticle;
		}
		Particle::AppearanceRules appearanceRules {
			.materials      = s_bloodMaterials,
			.initialColors  = kBloodInitialColors + indexForStyle,
			.fadedInColors  = kBloodFadedInColors + indexForStyle,
			.fadedOutColors = kBloodFadedOutColors + indexForStyle,
			.numMaterials   = std::size( s_bloodMaterials ),
			.kind           = Particle::Sprite,
			.radius         = 1.50f,
			.radiusSpread   = 0.75f,
			.sizeBehaviour  = Particle::Expanding
		};
		cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
		const float *effectColor = kBloodFadedInColors[indexForStyle];
		m_transientEffectsSystem.spawnBleedingVolumeEffect( origin, dir, damage, effectColor, (unsigned)baseTime );
	}

	m_transientEffectsSystem.spawnCartoonHitEffect( origin, dir, damage );
}

static ParticleColorsForTeamHolder electroboltParticleColorsHolder {
	.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
	.fadedInColor  = { 0.7f, 0.7f, 1.0f, 1.0f },
	.fadedOutColor = { 0.1f, 0.1f, 1.0f, 0.0f }
};

[[nodiscard]]
auto getTeamForOwner( int ownerNum ) -> int {
	if( ownerNum && ownerNum < gs.maxclients + 1 ) [[likely]] {
		return cg_entities[ownerNum].current.team;
	}
	return TEAM_SPECTATOR;
}

void EffectsSystemFacade::spawnElectroboltHitEffect( const float *origin, const float *impactNormal,
													 const float *impactDir, int ownerNum ) {
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

		ParticleColorsForTeamHolder *colorsHolder = &::electroboltParticleColorsHolder;
		const vec4_t *initialColors, *fadedInColors, *fadedOutColors;
		if( useTeamColor ) {
			std::tie( initialColors, fadedInColors, fadedOutColors ) = colorsHolder->getColorsForTeam( team, teamColor );
		} else {
			std::tie( initialColors, fadedInColors, fadedOutColors ) = colorsHolder->getDefaultColors();
		}

		ConicalFlockParams flockParams {
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
		Particle::AppearanceRules appearanceRules {
			.materials           = cgs.media.shaderDebrisParticle.getAddressOfHandle(),
			.initialColors       = initialColors,
			.fadedInColors       = fadedInColors,
			.fadedOutColors      = fadedOutColors,
			.kind                = Particle::Spark,
			.length              = 12.5f,
			.width               = 2.0f,
			.lengthSpread        = 2.5f,
			.widthSpread         = 1.0f,
			.fadeInLifetimeFrac  = 0.05f,
			.fadeOutLifetimeFrac = 0.50f,
		};
		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnElectroboltHitEffect( origin, impactNormal, decalColor, energyColor );
}

static ParticleColorsForTeamHolder instagunParticleColorsHolder {
	.initialColor  = { 1.0f, 1.0f, 1.0f, 1.0f },
	.fadedInColor  = { 1.0f, 0.0f, 1.0f, 0.5f },
	.fadedOutColor = { 0.0f, 0.0f, 1.0f, 0.0f }
};

void EffectsSystemFacade::spawnInstagunHitEffect( const float *origin, const float *impactNormal,
												  const float *impactDir, int ownerNum ) {
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

		ParticleColorsForTeamHolder *colorsHolder = &::instagunParticleColorsHolder;
		const vec4_t *initialColors, *fadedInColors, *fadedOutColors;
		if( useTeamColor ) {
			std::tie( initialColors, fadedInColors, fadedOutColors ) = colorsHolder->getColorsForTeam( team, teamColor );
		} else {
			std::tie( initialColors, fadedInColors, fadedOutColors ) = colorsHolder->getDefaultColors();
		}

		ConicalFlockParams flockParams {
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

		Particle::AppearanceRules appearanceRules {
			.materials           = cgs.media.shaderSparkParticle.getAddressOfHandle(),
			.initialColors       = initialColors,
			.fadedInColors       = fadedInColors,
			.fadedOutColors      = fadedOutColors,
			.kind                = Particle::Spark,
			.length              = 10.0f,
			.width               = 1.5f,
			.lengthSpread        = 2.5f,
			.widthSpread         = 0.5f,
			.fadeInLifetimeFrac  = 0.05f,
			.fadeOutLifetimeFrac = 0.25f,
		};

		cg.particleSystem.addSmallParticleFlock( appearanceRules, flockParams );
	}

	// TODO: Don't we need an IG-specific sound
	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnInstagunHitEffect( origin, impactNormal, decalColor, energyColor );
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

			if( cg_particles->integer ) {
				ConicalFlockParams flockParams {
					.origin = { pos[0], pos[1], pos[2] },
					.offset = { dir[0], dir[1], dir[2] },
					.dir    = { dir[0], dir[1], dir[2] },
					.angle  = 60
				};
				Particle::AppearanceRules appearanceRules {
					.materials      = cgs.media.shaderSparkParticle.getAddressOfHandle(),
					.initialColors  = &kGunbladeHitInitialColor,
					.fadedInColors  = &kGunbladeHitFadedInColor,
					.fadedOutColors = &kGunbladeHitFadedOutColor,
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

static const vec4_t kGunbladeBlastInitialColors[3] {
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
};

static const vec4_t kGunbladeBlastFadedInColors[3] {
	{ 1.0f, 0.8f, 0.5f, 0.7f },
	{ 1.0f, 0.8f, 0.4f, 0.7f },
	{ 1.0f, 0.7f, 0.4f, 0.7f },
};

static const vec4_t kGunbladeBlastFadedOutColors[3] {
	{ 0.5f, 0.3f, 0.1f, 0.0f },
	{ 0.7f, 0.3f, 0.1f, 0.0f },
	{ 0.9f, 0.3f, 0.1f, 0.0f },
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
			.initialColors  = kGunbladeBlastInitialColors,
			.fadedInColors  = kGunbladeBlastFadedInColors,
			.fadedOutColors = kGunbladeBlastFadedOutColors,
			.numColors      = 3,
			.kind           = Particle::Sprite,
			.radius         = 1.50f,
			.radiusSpread   = 0.25f
		};
		cg.particleSystem.addMediumParticleFlock( appearanceRules, flockParams );
	}

	m_transientEffectsSystem.spawnGunbladeBlastImpactEffect( origin, dir );
}

[[nodiscard]]
static bool canShowBulletLikeImpactForHit( const trace_t *trace ) {
	if( trace->surfFlags & SURF_NOIMPACT ) [[unlikely]]	{
		return false;
	}
	if( const int entNum = trace->ent; entNum > 0 ) {
		if( const unsigned entType = cg_entities[entNum].type; entType == ET_PLAYER || entType == ET_CORPSE ) {
			return false;
		}
	}
	return true;
}

static void makeRicochetDir( const float *impactDir, const float *impactNormal, wsw::RandomGenerator *rng, float *result ) {
	VectorReflect( impactDir, impactNormal, 0.0f, result );

	const float coneAngleCosine = Q_Sqrt( rng->nextFloat( 0.30f, 0.95f ) );
	addRandomRotationToDir( result, rng, coneAngleCosine );
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

static const vec4_t kBulletRicochetInitialColor { 1.0f, 1.0f, 1.0f, 1.0f };
static const vec4_t kBulletRicochetFadedInColor { 0.9f, 0.9f, 1.0f, 1.0f };
static const vec4_t kBulletRicochetFadedOutColor { 0.9f, 0.9f, 0.8f, 1.0f };

static const Particle::AppearanceRules kBulletImpactAppearanceRules {
	.initialColors  = &kBulletRicochetInitialColor,
	.fadedInColors  = &kBulletRicochetFadedInColor,
	.fadedOutColors = &kBulletRicochetFadedOutColor,
	.kind           = Particle::Spark,
	.length         = 16.0f,
	.width          = 1.0f,
	.lengthSpread   = 4.0f,
	.widthSpread    = 0.1f,
	.sizeBehaviour  = Particle::Shrinking,
};

static const ConicalFlockParams kBulletImpactFlockParams {
	.gravity       = GRAVITY,
	.angle         = 45,
	.innerAngle    = 15,
	.minSpeed      = 550.0f,
	.maxSpeed      = 800.0f,
	.minTimeout    = 75,
	.maxTimeout    = 150,
};

static const Particle::AppearanceRules kBulletRicochetAppearanceRules {
	.initialColors  = &kBulletRicochetInitialColor,
	.fadedInColors  = &kBulletRicochetFadedInColor,
	.fadedOutColors = &kBulletRicochetFadedOutColor,
	.lightColor     = kBulletRicochetFadedInColor,
	.kind           = Particle::Spark,
	.length         = 5.0f,
	.width          = 0.75f,
	.lengthSpread   = 1.0f,
	.widthSpread    = 0.05f,
	.lightRadius    = 16.0f,
	.sizeBehaviour  = Particle::Expanding,
};

static const ConicalFlockParams kBulletRicochetFlockParams {
	.gravity     = GRAVITY,
	.drag        = 0.006f,
	.restitution = 0.5f,
	.angle       = 18.0f,
	.minSpeed    = 700.0f,
	.maxSpeed    = 950.0f,
	.minTimeout  = 250,
	.maxTimeout  = 300,
};

static const vec4_t kBulletImpactDebrisInitialColors[3] {
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
};

static const vec4_t kBulletImpactDebrisFadedInColors[3] {
	{ 1.0f, 0.8f, 0.5f, 1.0f },
	{ 1.0f, 0.8f, 0.4f, 1.0f },
	{ 1.0f, 0.7f, 0.3f, 1.0f },
};

static const vec4_t kBulletImpactDebrisFadedOutColors[3] {
	{ 1.0f, 0.8f, 0.5f, 1.0f },
	{ 1.0f, 0.8f, 0.4f, 1.0f },
	{ 1.0f, 0.7f, 0.3f, 1.0f },
};

static const Particle::AppearanceRules kBulletDebrisAppearanceRules {
	.initialColors  = kBulletImpactDebrisInitialColors,
	.fadedInColors  = kBulletImpactDebrisFadedInColors,
	.fadedOutColors = kBulletImpactDebrisFadedOutColors,
	.lightColor     = kBulletImpactDebrisFadedInColors[0],
	.numColors      = std::size( kBulletImpactDebrisFadedInColors ),
	.kind           = Particle::Spark,
	.length         = 2.5f,
	.width          = 0.75f,
	.lengthSpread   = 1.0f,
	.widthSpread    = 0.05f,
	.lightRadius    = 16.0f,
	.sizeBehaviour  = Particle::Expanding,
};

static const ConicalFlockParams kBulletDebrisFlockParams {
	.gravity        = GRAVITY,
	.restitution    = 0.3f,
	.angle          = 30.0f,
	.minBounceCount = 1,
	.maxBounceCount = 3,
	.minSpeed       = 75.0f,
	.maxSpeed       = 125.0f,
	.minTimeout     = 200,
	.maxTimeout     = 700,
};

[[nodiscard]]
static inline auto scaleSpeedForUpShift( float baseSpeed, float upShiftScale ) -> float {
	assert( upShiftScale >= 0.0f && upShiftScale <= 1.0f );
	// Apply the upper bound to avoid triggering an assertion on speed feasibility
	return wsw::min( 999.9f, baseSpeed * ( 1.0f + upShiftScale ) );
}

void EffectsSystemFacade::spawnBulletImpactEffect( const trace_t *trace, const float *impactDir ) {
	if( canShowBulletLikeImpactForHit( trace ) ) {
		const float *const impactNormal = trace->plane.normal;
		const float *const impactOrigin = trace->endpos;

		vec3_t flockDir;
		// Vary the entire flock direction every hit
		makeRicochetDir( impactDir, impactNormal, &m_rng, flockDir );

		// Spawn the impact rosette regardless of the var value
		// TODO: Check surface type (we don't really have a sufficient information on that)

		// TODO: Copying does not seem to be justified in this case
		Particle::AppearanceRules impactAppearanceRules( kBulletImpactAppearanceRules );
		impactAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();

		ConicalFlockParams impactFlockParams( kBulletImpactFlockParams );
		VectorCopy( impactOrigin, impactFlockParams.origin );
		VectorCopy( impactNormal, impactFlockParams.offset );
		VectorCopy( flockDir, impactFlockParams.dir );
		impactFlockParams.minPercentage = 1.0f;
		impactFlockParams.maxPercentage = 1.0f;

		cg.particleSystem.addSmallParticleFlock( impactAppearanceRules, impactFlockParams );

		if( cg_particles->integer ) {
			const float upShiftScale = Q_Sqrt( wsw::max( 0.0f, impactNormal[2] ) );

			Particle::AppearanceRules ricochetAppearanceRules( kBulletRicochetAppearanceRules );
			ricochetAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();

			ConicalFlockParams ricochetFlockParams( kBulletRicochetFlockParams );
			VectorCopy( impactOrigin, ricochetFlockParams.origin );
			VectorCopy( impactNormal, ricochetFlockParams.offset );
			VectorCopy( flockDir, ricochetFlockParams.dir );
			ricochetFlockParams.minPercentage = 0.7f;
			ricochetFlockParams.maxPercentage = 1.0f;
			ricochetFlockParams.minShiftSpeed = 100.0f * upShiftScale;
			ricochetFlockParams.maxShiftSpeed = 150.0f * upShiftScale;
			ricochetFlockParams.minSpeed      = scaleSpeedForUpShift( ricochetFlockParams.minSpeed, upShiftScale );
			ricochetFlockParams.maxSpeed      = scaleSpeedForUpShift( ricochetFlockParams.maxSpeed, upShiftScale );

			cg.particleSystem.addSmallParticleFlock( ricochetAppearanceRules, ricochetFlockParams );

			Particle::AppearanceRules debrisAppearanceRules( kBulletDebrisAppearanceRules );
			debrisAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();

			ConicalFlockParams debrisFlockParams( kBulletDebrisFlockParams );
			VectorCopy( impactOrigin, debrisFlockParams.origin );
			VectorCopy( impactNormal, debrisFlockParams.offset );
			VectorCopy( flockDir, debrisFlockParams.dir );
			debrisFlockParams.minPercentage = 0.3f;
			debrisFlockParams.maxPercentage = 0.9f;
			debrisFlockParams.minShiftSpeed = 150.0f * upShiftScale;
			debrisFlockParams.maxShiftSpeed = 200.0f * upShiftScale;
			debrisFlockParams.minSpeed      = scaleSpeedForUpShift( debrisFlockParams.minSpeed, upShiftScale );
			debrisFlockParams.maxSpeed      = scaleSpeedForUpShift( debrisFlockParams.maxSpeed, upShiftScale );

			cg.particleSystem.addSmallParticleFlock( debrisAppearanceRules, debrisFlockParams );
		}

		m_transientEffectsSystem.spawnBulletLikeImpactEffect( impactOrigin, impactNormal );
	}
}

void EffectsSystemFacade::spawnPelletImpactEffect( const trace_s *trace, const float *impactDir,
												   unsigned index, unsigned total ) {
	if( canShowBulletLikeImpactForHit( trace ) ) {
		const float *const impactNormal = trace->plane.normal;
		const float *const impactOrigin = trace->endpos;

		vec3_t flockDir;
		makeRicochetDir( impactDir, impactNormal, &m_rng, flockDir );

		// Spawn the impact rosette regardless of the var value
		// TODO: Check surface type (we don't really have a sufficient information on that)

		Particle::AppearanceRules impactAppearanceRules( kBulletImpactAppearanceRules );
		impactAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();

		ConicalFlockParams impactFlockParams( kBulletImpactFlockParams );
		VectorCopy( impactOrigin, impactFlockParams.origin );
		VectorCopy( impactNormal, impactFlockParams.offset );
		VectorCopy( flockDir, impactFlockParams.dir );
		impactFlockParams.minPercentage = 0.3f;
		impactFlockParams.maxPercentage = 0.7f;

		cg.particleSystem.addSmallParticleFlock( impactAppearanceRules, impactFlockParams );

		if( cg_particles->integer ) {
			[[maybe_unused]] const float upShiftScale = Q_Sqrt( wsw::max( 0.0f, impactNormal[2] ) );

			if( m_rng.tryWithChance( 0.5f ) ) {
				Particle::AppearanceRules ricochetAppearanceRules( kBulletRicochetAppearanceRules );
				ricochetAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();
				ricochetAppearanceRules.lightFrameAffinityIndex = index;
				ricochetAppearanceRules.lightFrameAffinityModulo = total;

				ConicalFlockParams ricochetFlockParams( kBulletRicochetFlockParams );
				VectorCopy( impactOrigin, ricochetFlockParams.origin );
				VectorCopy( impactNormal, ricochetFlockParams.offset );
				VectorCopy( flockDir, ricochetFlockParams.dir );
				ricochetFlockParams.minPercentage = 0.0f;
				ricochetFlockParams.maxPercentage = 0.3f;
				ricochetFlockParams.minShiftSpeed = 150.0f * upShiftScale;
				ricochetFlockParams.maxShiftSpeed = 200.0f * upShiftScale;
				ricochetFlockParams.minSpeed      = scaleSpeedForUpShift( ricochetFlockParams.minSpeed, upShiftScale );
				ricochetFlockParams.maxSpeed      = scaleSpeedForUpShift( ricochetFlockParams.maxSpeed, upShiftScale );

				cg.particleSystem.addSmallParticleFlock( ricochetAppearanceRules, ricochetFlockParams );
			}

			if( m_rng.tryWithChance( 0.5f ) ) {
				Particle::AppearanceRules debrisAppearanceRules( kBulletDebrisAppearanceRules );
				debrisAppearanceRules.materials = cgs.media.shaderSparkParticle.getAddressOfHandle();
				debrisAppearanceRules.lightFrameAffinityIndex = index;
				debrisAppearanceRules.lightFrameAffinityModulo = total;

				ConicalFlockParams debrisFlockParams( kBulletDebrisFlockParams );
				VectorCopy( impactOrigin, debrisFlockParams.origin );
				VectorCopy( impactNormal, debrisFlockParams.offset );
				VectorCopy( flockDir, debrisFlockParams.dir );
				debrisFlockParams.minPercentage = 0.0f;
				debrisFlockParams.maxPercentage = 0.3f;
				debrisFlockParams.minShiftSpeed = 150.0f * upShiftScale;
				debrisFlockParams.maxShiftSpeed = 200.0f * upShiftScale;
				debrisFlockParams.minSpeed      = scaleSpeedForUpShift( debrisFlockParams.minSpeed, upShiftScale );
				debrisFlockParams.maxSpeed      = scaleSpeedForUpShift( debrisFlockParams.maxSpeed, upShiftScale );

				cg.particleSystem.addSmallParticleFlock( debrisAppearanceRules, debrisFlockParams );
			}
		}

		m_transientEffectsSystem.spawnBulletLikeImpactEffect( impactOrigin, impactNormal );
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
	cg.polyEffectsSystem.spawnTransientBeamEffect( start, end, {
		.material      = material,
		.color         = color,
		.lightColor    = color,
		.width         = wsw::clamp( cg_ebbeam_width->value, 0.0f, 128.0f ),
		.tileLength    = 128.0f,
		.lightRadius   = 200.0f,
		.timeout       = (unsigned)( 1.0f * 1000 * timeoutSeconds ),
		.lightTimeout  = (unsigned)( 0.2f * 1000 * timeoutSeconds ),
		.fadeOutOffset = (unsigned)( 0.5f * 1000 * timeoutSeconds ),
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
	cg.polyEffectsSystem.spawnTransientBeamEffect( start, end, {
		.material      = cgs.media.shaderInstaBeam,
		.color         = color,
		.lightColor    = color,
		.width         = wsw::clamp( cg_instabeam_width->value, 0.0f, 128.0f ),
		.tileLength    = 128.0f,
		.lightRadius   = 250.0f,
		.timeout       = (unsigned)( 1.0f * 1000 * timeoutSeconds ),
		.lightTimeout  = (unsigned)( 0.2f * 1000 * timeoutSeconds ),
		.fadeOutOffset = (unsigned)( 0.5f * 1000 * timeoutSeconds )
	});
}

void EffectsSystemFacade::spawnWorldLaserBeam( const float *from, const float *to, float width ) {
	// TODO: Either disable fading out or make it tracked
	const auto timeout = wsw::max( 2u, cgs.snapFrameTime );
	cg.polyEffectsSystem.spawnTransientBeamEffect( from, to, {
		.material      = cgs.media.shaderLaser,
		.timeout       = timeout,
		.fadeOutOffset = timeout - 1u
	});
}

void EffectsSystemFacade::spawnGameDebugBeam( const float *from, const float *to, const float *color, int ) {
	// TODO: Utilize the parameter
	cg.polyEffectsSystem.spawnTransientBeamEffect( from, to, {
		.material      = cgs.media.shaderLaser,
		.color         = color,
		.width         = 8.0f,
		.timeout       = 500u,
		.fadeOutOffset = 450u
	});
}