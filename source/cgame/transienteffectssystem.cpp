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
#include "../client/client.h"
#include "../qcommon/links.h"

#include <cstdlib>
#include <cstring>

class BasicHullsHolder {
public:
	BasicHullsHolder() {
		constexpr const uint8_t icosahedronFaces[20][3] {
			{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
			{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
			{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
			{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
		};

		constexpr const float a = 0.525731f, b = 0.850651f;
		const vec3_t icosahedronVertices[12] {
			{ -a, +b, +0 }, { +a, +b, +0 }, { -a, -b, +0 }, { +a, -b, +0 },
			{ +0, -a, +b }, { +0, +a, +b }, { +0, -a, -b }, { +0, +a, -b },
			{ +b, +0, -a }, { +b, +0, +a }, { -b, +0, -a }, { -b, +0, +a },
		};

		for( const auto &v : icosahedronVertices ) {
			m_vertices.emplace_back( Vertex { { v[0], v[1], v[2], 1.0f } } );
		}
		for( const auto &f: icosahedronFaces ) {
			m_faces.emplace_back( Face { { f[0], f[1], f[2] } } );
		}

		m_icoshphereEntries.emplace_back( Entry { 0, 3 * (unsigned)m_faces.size(), (unsigned)m_vertices.size() } );

		MidpointMap midpointCache;
		unsigned oldFacesSize = 0, facesSize = m_faces.size();
		while( !m_icoshphereEntries.full() ) {
			for( unsigned i = oldFacesSize; i < facesSize; ++i ) {
				const Face &face = m_faces[i];
				const uint16_t v1 = face.data[0], v2 = face.data[1], v3 = face.data[2];
				assert( v1 != v2 && v2 != v3 && v1 != v3 );
				const uint16_t p = getMidPoint( v1, v2, &midpointCache );
				const uint16_t q = getMidPoint( v2, v3, &midpointCache );
				const uint16_t r = getMidPoint( v3, v1, &midpointCache );
				m_faces.emplace_back( Face { { v1, p, r } } );
				m_faces.emplace_back( Face { { v2, q, p } } );
				m_faces.emplace_back( Face { { v3, r, q } } );
				m_faces.emplace_back( Face { { p, q, r } } );
			}
			oldFacesSize = facesSize;
			facesSize = m_faces.size();
			const unsigned firstIndex = 3 * oldFacesSize;
			const unsigned numIndices = 3 * ( facesSize - oldFacesSize );
			m_icoshphereEntries.emplace_back( Entry {firstIndex, numIndices, (unsigned)m_vertices.size() } );
		}

		m_vertices.shrink_to_fit();
		m_faces.shrink_to_fit();
	}

	[[nodiscard]]
	auto getIcosphereForLevel( unsigned level ) -> std::pair<std::span<const vec4_t>, std::span<const uint16_t>> {
		assert( level < m_icoshphereEntries.size() );
		const auto *vertexData = (const vec4_t *)m_vertices.data();
		const auto *indexData = (const uint16_t *)m_faces.data();
		const Entry &entry = m_icoshphereEntries[level];
		std::span<const vec4_t> verticesSpan { vertexData, entry.numVertices };
		std::span<const uint16_t> indicesSpan { indexData + entry.firstIndex, entry.numIndices };
		return { verticesSpan, indicesSpan };
	}
private:
	struct alignas( 4 ) Vertex { float data[4]; };
	static_assert( sizeof( Vertex ) == sizeof( vec4_t ) );
	struct alignas( 2 ) Face { uint16_t data[3]; };

	using MidpointMap = wsw::TreeMap<unsigned, uint16_t>;

	[[nodiscard]]
	auto getMidPoint( uint16_t i1, uint16_t i2, MidpointMap *midpointCache ) -> uint16_t {
		const unsigned smallest = ( i1 < i2 ) ? i1 : i2;
		const unsigned largest = ( i1 < i2 ) ? i2 : i1;
		const unsigned key = ( smallest << 16 ) | largest;
		if( const auto it = midpointCache->find( key ); it != midpointCache->end() ) {
			return it->second;
		}

		Vertex midpoint {};
		const float *v1 = m_vertices[i1].data, *v2 = m_vertices[i2].data;
		VectorAvg( v1, v2, midpoint.data );
		VectorNormalize( midpoint.data );
		midpoint.data[3] = 1.0f;

		const auto index = (uint16_t)m_vertices.size();
		m_vertices.push_back( midpoint );
		midpointCache->insert( std::make_pair( key, index ) );
		return index;
	}

	wsw::Vector<Vertex> m_vertices;
	wsw::Vector<Face> m_faces;

	struct Entry { const unsigned firstIndex, numIndices, numVertices; };
	wsw::StaticVector<Entry, 5> m_icoshphereEntries;
};

static BasicHullsHolder basicHullsHolder;

TransientEffectsSystem::TransientEffectsSystem() {
	// TODO: Take care of exception-safety
	while( !m_freeShapeLists.full() ) {
		if( auto *shapeList = CM_AllocShapeList( cl.cms ) ) {
			m_freeShapeLists.push_back( shapeList );
		} else {
			throw std::bad_alloc();
		}
	}
}

TransientEffectsSystem::~TransientEffectsSystem() {
	EntityEffect *nextEffect = nullptr;
	for( EntityEffect *effect = m_entityEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;
		unlinkAndFree( effect );
	}
	SimulatedHull *nextHull = nullptr;
	for( SimulatedHull *hull = m_simulatedHullsHead; hull; hull = nextHull ) {
		nextHull = hull->next;
		unlinkAndFree( hull );
	}
	for( CMShapeList *shapeList: m_freeShapeLists ) {
		CM_FreeShapeList( cl.cms, shapeList );
	}
}

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
	void *mem = m_entityEffectsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		EntityEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( EntityEffect *effect = m_entityEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_entityEffectsHead );
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

	wsw::link( effect, &m_entityEffectsHead );
	return effect;
}

auto TransientEffectsSystem::allocSimulatedHull( int64_t currTime, unsigned int lifetime ) -> SimulatedHull * {
	void *mem = m_simulatedHullsAllocator.allocOrNull();
	CMShapeList *hullShapeList;
	if( mem ) [[likely]] {
		hullShapeList = m_freeShapeLists.back();
		m_freeShapeLists.pop_back();
	} else {
		SimulatedHull *oldestHull = nullptr;
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( SimulatedHull *hull = m_simulatedHullsHead; hull; hull = hull->next ) {
			if( oldestSpawnTime > hull->spawnTime ) {
				oldestSpawnTime = hull->spawnTime;
				oldestHull = hull;
			}
		}
		assert( oldestHull );
		wsw::unlink( oldestHull, &m_simulatedHullsHead );
		hullShapeList = oldestHull->shapeList;
		oldestHull->~SimulatedHull();
		mem = oldestHull;
	}

	auto *hull = new( mem )SimulatedHull { .shapeList = hullShapeList, .spawnTime = currTime, .lifetime = lifetime };
	wsw::link( hull, &m_simulatedHullsHead );
	return hull;
}

void TransientEffectsSystem::setupHullVertices( SimulatedHull *hull, const float *origin,
												float speed, const float *color ) {
	const float originX = origin[0], originY = origin[1], originZ = origin[2];
	const auto [verticesSpan, indicesSpan] = ::basicHullsHolder.getIcosphereForLevel( 2 );
	const auto *__restrict vertices = verticesSpan.data();
	assert( verticesSpan.size() == kNumHullVertices );
	vec4_t *const __restrict positions = hull->vertexPositions[0];
	vec3_t *const __restrict velocities = hull->vertexVelocities;
	VectorCopy( origin, hull->origin );
	Vector4Copy( color, hull->color );
	hull->meshIndices = indicesSpan.data();
	hull->numMeshIndices = indicesSpan.size();
	for( unsigned i = 0; i < kNumHullVertices; ++i ) {
		// Vertex positions are absolute to simplify simulation
		Vector4Set( positions[i], originX, originY, originZ, 1.0f );
		// Unit vertices define directions
		VectorScale( vertices[i], speed, velocities[i] );
		hull->vertexMovability[i] = 1.0f;
	}
}

void TransientEffectsSystem::unlinkAndFree( EntityEffect *effect ) {
	wsw::unlink( effect, &m_entityEffectsHead );
	effect->~EntityEffect();
	m_entityEffectsAllocator.free( effect );
}

void TransientEffectsSystem::unlinkAndFree( SimulatedHull *hull ) {
	wsw::unlink( hull, &m_simulatedHullsHead );
	m_freeShapeLists.push_back( hull->shapeList );
	hull->~SimulatedHull();
	m_simulatedHullsAllocator.free( hull );
}

void TransientEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	// Limit the time step
	const float timeDeltaSeconds = 1e-3f * (float)std::min<int64_t>( 33, currTime - m_lastTime );

	simulateEntityEffectsAndSubmit( currTime, timeDeltaSeconds, request );
	simulateHullsAndSubmit( currTime, timeDeltaSeconds, request );

	m_lastTime = currTime;
}

void TransientEffectsSystem::simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds,
															 DrawSceneRequest *request ) {
	const model_s *const dashModel = cgs.media.modDash;
	const float backlerp = 1.0f - cg.lerpfrac;

	EntityEffect *nextEffect = nullptr;
	for( EntityEffect *__restrict effect = m_entityEffectsHead; effect; effect = nextEffect ) {
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
}

void TransientEffectsSystem::simulateHullsAndSubmit( int64_t currTime, float timeDeltaSeconds,
													 DrawSceneRequest *request ) {
	SimulatedHull *nextHull = nullptr;
	for( SimulatedHull *hull = m_simulatedHullsHead; hull; hull = nextHull ) {
		nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds );
		} else {
			unlinkAndFree( hull );
		}
	}

	for( SimulatedHull *hull = m_simulatedHullsHead; hull; hull = nextHull ) {
		submitHull( hull, request );
	}
}

void TransientEffectsSystem::submitHull( SimulatedHull *hull, DrawSceneRequest *request ) {
	const vec4_t *const positions = hull->vertexPositions[hull->positionsFrame];
	for( unsigned i = 0; i < hull->numMeshIndices; i += 3 ) {
		const float *const v1 = positions[hull->meshIndices[i + 0]];
		const float *const v2 = positions[hull->meshIndices[i + 1]];
		const float *const v3 = positions[hull->meshIndices[i + 2]];
		CG_PLink( v1, v2, hull->color, 0 );
		CG_PLink( v1, v3, hull->color, 0 );
		CG_PLink( v2, v3, hull->color, 0 );

		vec3_t _1To2, _1To3, normal;
		VectorSubtract( v2, v1, _1To2 );
		VectorSubtract( v3, v1, _1To3 );
		CrossProduct( _1To2, _1To3, normal );
		VectorNormalize( normal );
		VectorScale( normal, 16.0f, normal );
		VectorAdd( normal, v1, normal );
		CG_PLink( v1, normal, colorRed, 0 );
	}
}

void TransientEffectsSystem::SimulatedHull::simulate( int64_t currTime, float timeDeltaSeconds ) {
	const vec4_t *const __restrict oldPositions = vertexPositions[positionsFrame];
	positionsFrame = ( positionsFrame + 1 ) % 2;
	vec4_t *const __restrict newPositions = vertexPositions[positionsFrame];
	vec3_t *const __restrict velocities = vertexVelocities;

	BoundsBuilder boundsBuilder;
	assert( timeDeltaSeconds < 0.1f );
	const float speedMultiplier = 1.0f - 1.5f * timeDeltaSeconds;
	for( unsigned i = 0; i < kNumHullVertices; ++i ) {
		// Compute ideal positions
		VectorMA( oldPositions[i], timeDeltaSeconds * vertexMovability[i], velocities[i], newPositions[i] );
		VectorScale( velocities[i], speedMultiplier, velocities[i] );
		// TODO: We should be able to supply vec4
		boundsBuilder.addPoint( newPositions[i] );
	}

	vec3_t verticesMins, verticesMaxs;
	boundsBuilder.storeToWithAddedEpsilon( verticesMins, verticesMaxs );

	// TODO: Add a fused call
	CM_BuildShapeList( cl.cms, shapeList, verticesMins, verticesMaxs, MASK_SOLID | MASK_WATER );
	CM_ClipShapeList( cl.cms, shapeList, shapeList, verticesMins, verticesMaxs );

	trace_t trace;
	for( unsigned i = 0; i < kNumHullVertices; ++i ) {
		if( vertexMovability[i] != 0.0f ) {
			CM_ClipToShapeList( cl.cms, shapeList, &trace, oldPositions[i], newPositions[i], vec3_origin, vec3_origin, MASK_SOLID );
			if( trace.fraction != 1.0f ) [[unlikely]] {
				// TODO: Let it slide along the surface
				VectorAdd( trace.endpos, trace.plane.normal, newPositions[i] );
				// Park the vertex at the position
				vertexMovability[i] = 0.0f;
			}
		}
	}
}