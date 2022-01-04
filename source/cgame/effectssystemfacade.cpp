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
	spawnExplosionEffect( origin, dir, sfx, colorOrange, 64.0f, addSoundLfe );
}

void EffectsSystemFacade::spawnGrenadeExplosionEffect( const float *origin, const float *dir, int mode ) {
	sfx_s *sfx = mode == FIRE_MODE_STRONG ? cgs.media.sfxGrenadeStrongExplosion : cgs.media.sfxGrenadeWeakExplosion;
	const bool addSoundLfe = cg_heavyGrenadeExplosions->integer != 0;
	spawnExplosionEffect( origin, dir, sfx, colorOrange, 64.0f, addSoundLfe );
}

void EffectsSystemFacade::spawnGenericExplosionEffect( const float *origin, int mode, float radius ) {
	sfx_s *sfx = cgs.media.sfxRocketLauncherStrongHit;
	spawnExplosionEffect( origin, vec3_origin, sfx, colorOrange, radius, true );
}

void EffectsSystemFacade::spawnExplosionEffect( const float *origin, const float *offset, sfx_s *sfx,
												const float *color, float radius, bool addSoundLfe ) {
	vec3_t spriteOrigin, almostExactOrigin;
	VectorMA( origin, 8.0f, offset, spriteOrigin );
	VectorAdd( origin, offset, almostExactOrigin );

	startSound( sfx, almostExactOrigin, ATTN_DISTANT );

	if( addSoundLfe ) {
		startSound( cgs.media.sfxExplosionLfe, almostExactOrigin, ATTN_NORM );
	}

	UniformFlockFiller flockFiller {
		.origin = origin, .offset = offset, .gravity = 350.0f,
		.minSpeed = 150.0f, .maxSpeed = 300.0f,
		.minPercentage = 1.0f, .maxPercentage = 1.0f
	};

	cg.particleSystem.addLargeParticleFlock( color, flockFiller );

	m_transientEffectsSystem.spawnExplosion( spriteOrigin, color );
}

void EffectsSystemFacade::spawnShockwaveExplosionEffect( const float *origin, const float *dir, int mode ) {
}

void EffectsSystemFacade::spawnPlasmaExplosionEffect( const float *origin, const float *impactNormal, int mode ) {
	const vec3_t soundOrigin { origin[0] + impactNormal[0], origin[1] + impactNormal[1], origin[2] + impactNormal[2] };
	sfx_s *sfx = ( mode == FIRE_MODE_STRONG ) ? cgs.media.sfxPlasmaStrongHit : cgs.media.sfxPlasmaWeakHit;
	startSound( sfx, soundOrigin, ATTN_IDLE );

	UniformFlockFiller flockFiller { .origin = origin, .offset = impactNormal, .minTimeout = 50, .maxTimeout = 200 };
	cg.particleSystem.addMediumParticleFlock( colorGreen, flockFiller );

	m_transientEffectsSystem.spawnPlasmaImpactEffect( origin, impactNormal );
}

void EffectsSystemFacade::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	m_transientEffectsSystem.simulateFrameAndSubmit( currTime, request );
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

void EffectsSystemFacade::spawnPlayerHitEffect( const float *origin, const float *dir, int damage ) {
	if( cg_showBloodTrail->integer && cg_bloodTrail->integer ) {
		ConeFlockFiller flockFiller {
			.origin = origin, .offset = dir, .dir = dir,
			.gravity = 150.0f, .bounceCount = 0,
			.minSpeed = 50.0f, .maxSpeed = 75.0f,
			.minPercentage = 0.5f, .maxPercentage = 0.5f
		};
		cg.particleSystem.addSmallParticleFlock( colorRed, flockFiller );
	}

	m_transientEffectsSystem.spawnCartoonHitEffect( origin, dir, damage );
}

void EffectsSystemFacade::spawnElectroboltHitEffect( const float *origin, const float *dir ) {
	const vec3_t soundOrigin { origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2] };
	UniformFlockFiller flockFiller { .origin = origin, .offset = dir };
	cg.particleSystem.addLargeParticleFlock( colorBlue, flockFiller );

	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnElectroboltHitEffect( origin, dir );
}

void EffectsSystemFacade::spawnInstagunHitEffect( const float *origin, const float *dir, int ownerNum ) {
	float color[4] { 1.0f, 1.0f, 1.0f, 1.0f };
	if( cg_teamColoredInstaBeams->integer && ownerNum && ( ownerNum < gs.maxclients + 1 ) ) {
		if( const int team = cg_entities[ownerNum].current.team; ( team == TEAM_ALPHA ) || ( team == TEAM_BETA ) ) {
			CG_TeamColor( team, color );
			VectorScale( color, 0.67f, color );
		}
	}

	const vec3_t soundOrigin { origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2] };
	UniformFlockFiller flockFiller { .origin = origin, .offset = dir };
	cg.particleSystem.addLargeParticleFlock( color, flockFiller );

	// TODO: Don't we need an IG-specific sound
	startSound( cgs.media.sfxElectroboltHit, soundOrigin, ATTN_STATIC );

	m_transientEffectsSystem.spawnInstagunHitEffect( origin, dir, color );
}

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

			const vec3_t color { 0.30f, 0.30, 0.25f };
			ConeFlockFiller flockFiller { .origin = pos, .offset = dir, .dir = dir, .angle = 60 };
			cg.particleSystem.addMediumParticleFlock( color, flockFiller );
		}
	}
}

void EffectsSystemFacade::spawnGunbladeBlastHitEffect( const float *origin, const float *dir ) {
	startSound( cgs.media.sfxGunbladeStrongHit[m_rng.nextBounded( 2 )], origin, ATTN_IDLE );

	UniformFlockFiller flockFiller { .origin = origin, .offset = dir, .gravity = 0.0f, .bounceCount = 1 };
	cg.particleSystem.addLargeParticleFlock( colorOrange, flockFiller );

	m_transientEffectsSystem.spawnGunbladeBlastImpactEffect( origin, dir );
}

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
		.origin        = trace->endpos,
		.offset        = trace->plane.normal,
		.dir           = trace->plane.normal,
		.gravity       = 900.0f,
		.minPercentage = minPercentage,
		.maxPercentage = maxPercentage
	};

	cg.particleSystem.addSmallParticleFlock( colorWhite, flockFiller );

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
