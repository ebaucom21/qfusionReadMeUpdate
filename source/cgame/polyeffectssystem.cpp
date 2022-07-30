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

#include "cg_local.h"
#include "../qcommon/links.h"

PolyEffectsSystem::~PolyEffectsSystem() {
	for( CurvedBeamEffect *beam = m_curvedLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		destroyCurvedBeamEffect( beam );
	}
	for( StraightBeamEffect *beam = m_straightLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		destroyStraightBeamEffect( beam );
	}
	for( TransientBeamEffect *beam = m_transientBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		destroyTransientBeamEffect( beam );
	}
}

auto PolyEffectsSystem::createCurvedBeamEffect( shader_s *material ) -> CurvedBeam * {
	assert( !m_curvedLaserBeamsAllocator.isFull() );
	auto *effect = new( m_curvedLaserBeamsAllocator.allocOrNull() )CurvedBeamEffect;
	wsw::link( effect, &m_curvedLaserBeamsHead );
	effect->poly.material    = material;
	effect->poly.numVertices = 0;
	effect->poly.numIndices  = 0;
	effect->poly.positions   = effect->storageOfPositions;
	effect->poly.colors      = effect->storageOfColors;
	effect->poly.texcoords   = effect->storageOfTexCoords;
	effect->poly.indices     = effect->storageOfIndices;
	effect->poly.normals     = nullptr;
	return effect;
}

void PolyEffectsSystem::updateCurvedBeamEffect( CurvedBeam *handle, const float *color, float width,
												float tileLength, std::span<const vec3_t> points ) {
	static_assert( kMaxCurvedBeamSegments == MAX_CURVELASERBEAM_SUBDIVISIONS );
	assert( points.size() <= kMaxCurvedBeamSegments + 1 );

	auto *const __restrict effect = (CurvedBeamEffect *)handle;
	assert( (uintptr_t)effect == (uintptr_t)handle );

	assert( tileLength > 0.0f );

	BoundsBuilder boundsBuilder;

	const byte_vec4_t byteColor {
		( uint8_t )( color[0] * 255 ),
		( uint8_t )( color[1] * 255 ),
		( uint8_t )( color[2] * 255 ),
		( uint8_t )( color[3] * 255 )
	};

	effect->poly.numVertices = 0;
	effect->poly.numIndices  = 0;

	const float ymin = -0.5f * width;
	const float ymax = +0.5f * width;

	float totalLengthSoFar    = 0.0f;
	const float rcpTileLength = Q_Rcp( tileLength );

	vec4_t *__restrict positions   = effect->poly.positions;
	byte_vec4_t *__restrict colors = effect->poly.colors;
	vec2_t *__restrict texcoords   = effect->poly.texcoords;
	uint16_t *__restrict indices   = effect->poly.indices;

	// TODO:!!!!!!!!! Don't submit separate quads, utilize adjacency
	for( unsigned segmentNum = 0; segmentNum + 1 < points.size(); ++segmentNum ) {
		const float *const from = points[segmentNum + 0];
		const float *const to   = points[segmentNum + 1];
		const float squaredLength = DistanceSquared( from, to );
		// Interrupting in this case seems to be the most reasonable option.
		if( squaredLength < 1.0f ) [[unlikely]] {
			break;
		}

		const float rcpLength = Q_RSqrt( squaredLength );
		const float length = Q_Rcp( rcpLength );

		const float xmin = 0.0f;
		const float xmax = length;

		totalLengthSoFar += length;
		const float stx = totalLengthSoFar * rcpTileLength;

		for( unsigned planeNum = 0; planeNum < 2; ++planeNum ) {
			const unsigned firstSegmentIndex = effect->poly.numVertices;
			VectorSet( indices + 0, firstSegmentIndex + 0, firstSegmentIndex + 1, firstSegmentIndex + 2 );
			VectorSet( indices + 3, firstSegmentIndex + 0, firstSegmentIndex + 2, firstSegmentIndex + 3 );

			for( unsigned i = 0; i < 4; ++i ) {
				Vector4Copy( byteColor, colors[i] );
			}

			unsigned firstUntransformedVertexInQuad;
			if( segmentNum ) [[likely]] {
				firstUntransformedVertexInQuad = 2;

				// TODO: Don't add copies of previous vertices, utilize indexing
				Vector4Copy( positions[-1 - 4], positions[0] );
				Vector4Copy( positions[-2 - 4], positions[1] );

				Vector2Copy( texcoords[-1 - 4], texcoords[0] );
				Vector2Copy( texcoords[-2 - 4], texcoords[1] );
			} else {
				firstUntransformedVertexInQuad = 0;

				Vector4Set( positions[0], xmin, 0.0f, ymin, 1.0f );
				Vector4Set( positions[1], xmin, 0.0f, ymax, 1.0f );

				Vector2Set( texcoords[0], 0.0f, 0.0f );
				Vector2Set( texcoords[1], 0.0f, 1.0f );
			}

			Vector4Set( positions[2], xmax, 0.0f, ymax, 1.0f );
			Vector4Set( positions[3], xmax, 0.0f, ymin, 1.0f );

			Vector2Set( texcoords[2], stx, 1.0f );
			Vector2Set( texcoords[3], stx, 0.0f );

			vec3_t dir, angles;
			VectorSubtract( to, from, dir );
			VectorScale( dir, rcpLength, dir );
			VecToAngles( dir, angles );
			angles[ROLL] += 90.0f * (float)planeNum;

			mat3_t axis, localAxis;
			AnglesToAxis( angles, axis );
			Matrix3_Transpose( axis, localAxis );

			for( unsigned vertexInQuad = firstUntransformedVertexInQuad; vertexInQuad < 4; ++vertexInQuad ) {
				vec3_t tmp;
				Matrix3_TransformVector( localAxis, positions[vertexInQuad], tmp );
				VectorAdd( tmp, from, positions[vertexInQuad] );
				boundsBuilder.addPoint( positions[vertexInQuad] );
			}

			effect->poly.numVertices += 4;
			effect->poly.numIndices += 6;

			positions += 4, colors += 4, texcoords += 4;
			indices += 6;
		}
	}

	if( effect->poly.numVertices ) [[likely]] {
		boundsBuilder.storeToWithAddedEpsilon( effect->poly.mins, effect->poly.maxs );
	}
}

void PolyEffectsSystem::destroyCurvedBeamEffect( CurvedBeam *handle ) {
	auto *effect = (CurvedBeamEffect *)handle;
	assert( (uintptr_t)effect == (uintptr_t)handle );
	wsw::unlink( effect, &m_curvedLaserBeamsHead );
	effect->~CurvedBeamEffect();
	m_curvedLaserBeamsAllocator.free( effect );
}

auto PolyEffectsSystem::createStraightBeamEffect( shader_s *material ) -> StraightBeam * {
	assert( !m_straightLaserBeamsAllocator.isFull() );
	auto *effect = new( m_straightLaserBeamsAllocator.allocOrNull() )StraightBeamEffect;
	wsw::link( effect, &m_straightLaserBeamsHead );
	effect->poly.material = material;
	effect->poly.width    = 0.0f;
	effect->poly.length   = 0.0f;
	effect->poly.flags    = QuadPoly::XLike;
	return effect;
}

void PolyEffectsSystem::updateStraightBeamEffect( StraightBeam *handle, const float *color,
												  float width, float tileLength,
												  const float *from, const float *to ) {
	auto *effect = (StraightBeamEffect *)( handle );
	assert( (uintptr_t)effect == (uintptr_t)handle );

	if( effect->poly.material ) [[likely]] {
		if( const float squareLength = DistanceSquared( from, to ); squareLength >= 1.0f ) [[likely]] {
			const float rcpLength = Q_RSqrt( squareLength );
			vec3_t dir;
			VectorSubtract( to, from, dir );
			VectorScale( dir, rcpLength, dir );
			VectorCopy( color, effect->poly.color );
			VectorCopy( from, effect->poly.from );
			VectorCopy( to, effect->poly.to );
			VectorCopy( dir, effect->poly.dir );
			effect->poly.width      = width;
			effect->poly.length     = Q_Rcp( rcpLength );
			effect->poly.tileLength = tileLength;
		} else {
			// Suppress rendering this frame
			effect->poly.width = effect->poly.length = 0.0f;
		}
	}
}

void PolyEffectsSystem::destroyStraightBeamEffect( StraightBeam *handle ) {
	auto *effect = ( StraightBeamEffect * )( handle );
	assert( (uintptr_t)effect == (uintptr_t)handle );
	wsw::unlink( effect, &m_straightLaserBeamsHead );
	effect->~StraightBeamEffect();
	m_straightLaserBeamsAllocator.free( effect );
}

void PolyEffectsSystem::destroyTransientBeamEffect( TransientBeamEffect *effect ) {
	wsw::unlink( effect, &m_transientBeamsHead );
	effect->~TransientBeamEffect();
	m_transientBeamsAllocator.free( effect );
}

void PolyEffectsSystem::spawnTransientBeamEffect( const float *from, const float *to, TransientBeamParams &&params ) {
	if( params.width < 1.0f || !params.material ) [[unlikely]] {
		return;
	}

	const float squareLength = DistanceSquared( from, to );
	if( squareLength < params.width * params.width ) [[unlikely]] {
		return;
	}

	vec3_t dir;
	VectorSubtract( to, from, dir );
	const float rcpLength = Q_RSqrt( squareLength );
	VectorScale( dir, rcpLength, dir );

	void *mem = m_transientBeamsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		assert( m_transientBeamsHead );
		// Pick the oldest one to reuse.
		// Note: extra bookkeeping of the list tail is not worth it.
		// Note: it's actually possible to extract in a very cheap fashion using links the of underlying allocator.
		TransientBeamEffect *oldestEffect = m_transientBeamsHead->next;
		while( oldestEffect ) {
			if( oldestEffect->next ) {
				oldestEffect = oldestEffect->next;
			} else {
				break;
			}
		}
		wsw::unlink( oldestEffect, &m_transientBeamsHead );
		oldestEffect->~TransientBeamEffect();
		mem = oldestEffect;
	}

	assert( params.timeout < std::numeric_limits<uint16_t>::max() );

	auto *effect = new( mem )TransientBeamEffect;

	effect->spawnTime     = m_lastTime;
	effect->timeout       = params.timeout;
	effect->colorLifespan = params.beamColorLifespan,
	effect->lightProps    = params.lightProps;

	effect->poly.width      = params.width;
	effect->poly.length     = Q_Rcp( rcpLength );
	effect->poly.material   = params.material;
	effect->poly.tileLength = params.tileLength;

	VectorCopy( from, effect->poly.from );
	VectorCopy( to, effect->poly.to );
	VectorCopy( dir, effect->poly.dir );

	wsw::link( effect, &m_transientBeamsHead );
}

void PolyEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	for( CurvedBeamEffect *beam = m_curvedLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->poly.material && beam->poly.numVertices && beam->poly.numIndices ) [[likely]] {
			request->addPoly( &beam->poly );
		}
	}

	for( StraightBeamEffect *beam = m_straightLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->poly.material && beam->poly.length > 1.0f && beam->poly.width > 1.0f ) [[likely]] {
			request->addPoly( &beam->poly );
		}
	}

	for( TransientBeamEffect *beam = m_transientBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->spawnTime + beam->timeout <= currTime ) [[unlikely]] {
			destroyTransientBeamEffect( beam );
			continue;
		}

		if( beam->poly.material && beam->poly.length > 1.0f && beam->poly.width > 1.0f ) [[likely]] {
			const float colorLifetimeFrac = (float)( currTime - beam->spawnTime ) * Q_Rcp( (float)beam->timeout );
			beam->colorLifespan.getColorForLifetimeFrac( colorLifetimeFrac, beam->poly.color );

			if( beam->lightProps ) {
				const auto &[lightTimeout, lightLifespan] = *beam->lightProps;
				if( beam->spawnTime + lightTimeout > currTime ) {
					const float lightLifetimeFrac = (float)( currTime - beam->spawnTime ) * Q_Rcp( (float)lightTimeout );

					float lightRadius, lightColor[3];
					lightLifespan.getRadiusAndColorForLifetimeFrac( lightLifetimeFrac, &lightRadius, lightColor );
					if( lightRadius > 1.0f ) {
						float lightOrigin[3];
						VectorLerp( beam->poly.from, lightLifetimeFrac, beam->poly.to, lightOrigin );
						request->addLight( lightOrigin, lightRadius, lightRadius, lightColor );
					}
				}
			}

			request->addPoly( &beam->poly );
		}
	}

	m_lastTime = currTime;
}