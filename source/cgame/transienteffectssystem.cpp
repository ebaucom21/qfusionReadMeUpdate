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

#include "transienteffectssystem.h"
#include "../cgame/cg_local.h"
#include "../qcommon/links.h"

#include <cstdlib>
#include <cstring>

void TransientEffectsSystem::spawnExplosion( const float *origin, const float *color, float radius ) {
	EntityEffect *effect = addSpriteEffect( cgs.media.shaderRocketExplosion, origin, radius, 800u );

	constexpr float lightRadiusScale = 1.0f / 64.0f;
	// 300 for radius of 64
	effect->lightRadius = 300.0f * radius * lightRadiusScale;
	VectorCopy( colorOrange, effect->lightColor );

	(void)addSpriteEffect( cgs.media.shaderRocketExplosion, origin, 0.67f * radius, 500u );
}

void TransientEffectsSystem::spawnCartoonHitEffect( const float *origin, const float *dir, int damage ) {
	if( cg_cartoonHitEffect->integer ) {
		float radius = 0.0f;
		shader_s *material = nullptr;
		if( damage > 64 ) {
			// OUCH!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit3, 24.0f );
		} else if( damage > 50 ) {
			// POW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit, 19.0f );
		} else if( damage > 38 ) {
			// SPLITZOW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit2, 15.0f );
		}

		if( material ) {
			// TODO:
			vec3_t localDir;
			if( !VectorLength( dir ) ) {
				VectorNegate( &cg.view.axis[AXIS_FORWARD], localDir );
			} else {
				VectorNormalize2( dir, localDir );
			}

			vec3_t spriteOrigin;
			// Move effect a bit up from player
			VectorCopy( origin, spriteOrigin );
			spriteOrigin[2] += ( playerbox_stand_maxs[2] - playerbox_stand_mins[2] ) + 1.0f;

			EntityEffect *effect = addSpriteEffect( material, spriteOrigin, radius, 700u );
			effect->entity.rotation = 0.0f;
			// TODO: Add a correct sampling of random sphere points as a random generator method
			for( unsigned i = 0; i < 3; ++i ) {
				effect->velocity[i] = m_rng.nextFloat( -10.0f, +10.0f );
			}
		}
	}
}

void TransientEffectsSystem::spawnElectroboltHitEffect( const float *origin, const float *dir ) {
	EntityEffect *effect = addModelEffect( cgs.media.modElectroBoltWallHit, origin, dir, 600 );
	VectorMA( origin, 4.0f, dir, effect->lightOrigin );
	VectorCopy( colorWhite, effect->lightColor );
	effect->lightRadius = 144.0f;
}

void TransientEffectsSystem::spawnInstagunHitEffect( const float *origin, const float *dir, const float *color ) {
	EntityEffect *effect = addModelEffect( cgs.media.modInstagunWallHit, origin, dir, 600u );
	VectorMA( origin, 4.0f, dir, effect->lightOrigin );
	VectorCopy( colorMagenta, effect->lightColor );
	effect->lightRadius = 144.0f;
}

void TransientEffectsSystem::spawnPlasmaImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *effect = addModelEffect( cgs.media.modPlasmaExplosion, origin, dir, 400u );
	VectorMA( origin, 4.0f, dir, effect->lightOrigin );
	VectorCopy( colorGreen, effect->lightColor );
	effect->lightRadius = 108.0f;
	effect->fadedInScale = effect->fadedOutScale = 5.0f;
}

void TransientEffectsSystem::spawnGunbladeBlastImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *effect = addModelEffect( cgs.media.modBladeWallExplo, origin, dir, 600u );
	VectorMA( origin, 8.0f, dir, effect->lightOrigin );
	VectorCopy( colorYellow, effect->lightColor );
	effect->fadedInScale = effect->fadedOutScale = 5.0f;
	effect->lightRadius = 200.0f;
}

void TransientEffectsSystem::spawnGunbladeBladeImpactEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modBladeWallHit, origin, dir, 300u );
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnBulletLikeImpactEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modBulletExplode, origin, dir, 300u );
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnDustImpactEffect( const float *origin, const float *dir, float radius ) {
	vec3_t axis1, axis2;
	PerpendicularVector( axis2, dir );
	CrossProduct( dir, axis2, axis1 );

	VectorNormalize( axis1 ), VectorNormalize( axis2 );

	float angle = 0.0f;
	constexpr const int count = 12;
	const float speed = 0.67f * radius;
	const float angleStep = (float)M_TWOPI * Q_Rcp( (float)count );
	for( int i = 0; i < count; ++i ) {
		const float scale1 = std::sin( angle ), scale2 = std::cos( angle );
		angle += angleStep;

		vec3_t velocity { 0.0f, 0.0f, 0.0f };
		VectorMA( velocity, speed * scale1, axis1, velocity );
		VectorMA( velocity, speed * scale2, axis2, velocity );

		EntityEffect *effect = addSpriteEffect( cgs.media.shaderSmokePuff2, origin, 10.0f, 700u );
		effect->fadedInScale = 0.33f;
		effect->fadedOutScale = 0.0f;
		effect->initialAlpha = 0.25f;
		effect->fadedOutAlpha = 0.0f;
		VectorCopy( velocity, effect->velocity );
	}
}

void TransientEffectsSystem::spawnDashEffect( const float *origin, const float *dir ) {
	// Model orientation/streching hacks
	vec3_t angles;
	VecToAngles( dir, angles );
	angles[1] += 270.0f;
	EntityEffect *effect = addModelEffect( cgs.media.modDash, origin, dir, 700u );
	AnglesToAxis( angles, effect->entity.axis );
	// Scale Z
	effect->entity.axis[2 * 3 + 2] *= 2.0f;
	// Size hacks
	effect->fadedInScale = effect->fadedOutScale = 0.15f;
}

auto TransientEffectsSystem::addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect * {
	EntityEffect *const effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_MODEL;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.model = model;
	effect->entity.customShader = nullptr;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	NormalVectorToAxis( dir, &effect->entity.axis[0] );
	VectorCopy( origin, effect->entity.origin );
	VectorCopy( origin, effect->lightOrigin );

	return effect;
}

auto TransientEffectsSystem::addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect * {
	EntityEffect *effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_SPRITE;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.radius = radius;
	effect->entity.customShader = material;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	Matrix3_Identity( effect->entity.axis );
	VectorCopy( origin, effect->entity.origin );
	VectorCopy( origin, effect->lightOrigin );

	return effect;
}

auto TransientEffectsSystem::allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect * {
	void *mem = m_effectsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		EntityEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( EntityEffect *effect = m_modelEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime < effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_modelEffectsHead );
		oldestEffect->~EntityEffect();
		mem = oldestEffect;
	}

	auto *effect = new( mem )EntityEffect;

	assert( duration <= std::numeric_limits<uint16_t>::max() );
	// Try forcing 16-bit division if a compiler fails to optimize division by constant
	unsigned fadeInDuration = (uint16_t)duration / (uint16_t)10;
	if( fadeInDuration > 33 ) [[likely]] {
		fadeInDuration = 33;
	} else if( fadeInDuration < 1 ) [[unlikely]] {
		fadeInDuration = 1;
	}

	unsigned fadeOutDuration;
	if( duration > fadeInDuration ) [[likely]] {
		fadeOutDuration = duration - fadeInDuration;
	} else {
		fadeOutDuration = fadeInDuration;
		duration = fadeInDuration + 1;
	}

	effect->duration = duration;
	effect->rcpDuration = Q_Rcp( (float)duration );
	effect->fadeInDuration = fadeInDuration;
	effect->rcpFadeInDuration = Q_Rcp( (float)fadeInDuration );
	effect->rcpFadeOutDuration = Q_Rcp( (float)fadeOutDuration );
	effect->spawnTime = currTime;

	wsw::link( effect, &m_modelEffectsHead );
	return effect;
}

void TransientEffectsSystem::unlinkAndFree( EntityEffect *effect ) {
	wsw::unlink( effect, &m_modelEffectsHead );
	effect->~EntityEffect();
	m_effectsAllocator.free( effect );
}

void TransientEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	// Limit the time step
	const float timeDeltaSeconds = 1e-3f * (float)std::min<int64_t>( 33, currTime - m_lastTime );
	const model_s *const dashModel = cgs.media.modDash;
	const float backlerp = 1.0f - cg.lerpfrac;

	EntityEffect *nextEffect = nullptr;
	for( EntityEffect *__restrict effect = m_modelEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;

		if( effect->spawnTime + effect->duration <= currTime ) [[unlikely]] {
			unlinkAndFree( effect );
			continue;
		}

		const auto lifetimeMillis = (unsigned)( currTime - effect->spawnTime );
		assert( lifetimeMillis < effect->duration );

		if( lifetimeMillis >= effect->fadeInDuration ) [[likely]] {
			assert( effect->duration > effect->fadeInDuration );
			const float fadeOutFrac = (float)( lifetimeMillis - effect->fadeInDuration ) * effect->rcpFadeOutDuration;
			effect->entity.scale = std::lerp( effect->fadedInScale, effect->fadedOutScale, fadeOutFrac );
		} else {
			const float fadeInFrac = (float)lifetimeMillis * effect->rcpFadeInDuration;
			effect->entity.scale = effect->fadedInScale * fadeInFrac;
		}

		// Dash model hacks
		if( effect->entity.model == dashModel ) [[unlikely]] {
			float *const zScale = effect->entity.axis + ( 2 * 3 ) + 2;
			*zScale -= 4.0f * timeDeltaSeconds;
			if( *zScale < 0.01f ) {
				unlinkAndFree( effect );
				continue;
			}
		}

		vec3_t moveVec;
		VectorScale( effect->velocity, timeDeltaSeconds, moveVec );
		VectorAdd( effect->entity.origin, moveVec, effect->entity.origin );

		const float lifetimeFrac = (float)lifetimeMillis * effect->rcpDuration;

		effect->entity.backlerp = backlerp;
		const float alpha = std::lerp( effect->initialAlpha, effect->fadedOutAlpha, lifetimeFrac );
		effect->entity.shaderRGBA[3] = (uint8_t)( 255 * alpha );

		request->addEntity( &effect->entity );
		if( effect->lightRadius > 1.0f ) {
			// Move the light as well
			VectorAdd( effect->lightOrigin, moveVec, effect->lightOrigin );
			const float lightFrac = 1.0f - lifetimeFrac;
			if( const float lightRadius = effect->lightRadius * lightFrac; lightRadius > 1.0f ) {
				request->addLight( effect->lightOrigin, lightRadius, 0.0f, effect->lightColor );
			}
		}
	}

	// TODO: Simulate all volumetrics

	m_lastTime = currTime;
}