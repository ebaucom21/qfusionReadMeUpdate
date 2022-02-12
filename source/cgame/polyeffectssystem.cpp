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

	BoundsBuilder boundsBuilder;

	const byte_vec4_t byteColor {
		( uint8_t )( color[0] * 255 ),
		( uint8_t )( color[1] * 255 ),
		( uint8_t )( color[2] * 255 ),
		( uint8_t )( color[3] * 255 )
	};

	effect->poly.numVertices = 0;
	effect->poly.numIndices  = 0;

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
		const float ymin = -0.5f * width;
		const float ymax = +0.5f * width;

		// TODO: Tile correctly
		float stx = 1.0f, sty = 1.0f;

		vec4_t *const positions   = effect->poly.positions + 4 * segmentNum;
		byte_vec4_t *const colors = effect->poly.colors    + 4 * segmentNum;
		vec2_t *const texcoords   = effect->poly.texcoords + 4 * segmentNum;
		uint16_t *const indices   = effect->poly.indices   + 6 * segmentNum;

		const unsigned firstSegmentIndex = effect->poly.numVertices;
		VectorSet( indices + 0, firstSegmentIndex + 0, firstSegmentIndex + 1, firstSegmentIndex + 2 );
		VectorSet( indices + 3, firstSegmentIndex + 0, firstSegmentIndex + 2, firstSegmentIndex + 3 );

		Vector4Set( positions[0], xmin, 0.0f, ymin, 1.0f );
		Vector4Set( positions[1], xmin, 0.0f, ymax, 1.0f );
		Vector4Set( positions[2], xmax, 0.0f, ymax, 1.0f );
		Vector4Set( positions[3], xmax, 0.0f, ymin, 1.0f );

		Vector2Set( texcoords[0], 0.0f, 0.0f );
		Vector2Set( texcoords[1], 0.0f, sty );
		Vector2Set( texcoords[2], stx, sty );
		Vector2Set( texcoords[3], stx, 0.0f );

		vec3_t dir;
		VectorSubtract( to, from, dir );
		VectorScale( dir, rcpLength, dir );

		mat3_t axis, localAxis;
		VectorCopy( dir, axis );
		MakeNormalVectors( axis, axis + 3, axis + 6 );
		Matrix3_Transpose( axis, localAxis );

		for( unsigned vertexInQuad = 0; vertexInQuad < 4; ++vertexInQuad ) {
			vec3_t tmp;
			Matrix3_TransformVector( localAxis, positions[vertexInQuad], tmp );
			VectorAdd( tmp, from, positions[vertexInQuad] );
			Vector4Copy( byteColor, colors[vertexInQuad] );
			boundsBuilder.addPoint( positions[vertexInQuad] );
		}

		effect->poly.numVertices += 4;
		effect->poly.numIndices  += 6;
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

void PolyEffectsSystem::spawnTransientBeamEffect( const float *from, const float *to, float width,
												  float tileLength, shader_s *material,
												  const float *color, unsigned timeout, unsigned fadeOutOffset ) {
	if( width < 1.0f || !material ) [[unlikely]] {
		return;
	}

	const float squareLength = DistanceSquared( from, to );
	if( squareLength < width * width ) [[unlikely]] {
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

	assert( fadeOutOffset && fadeOutOffset < timeout && timeout < std::numeric_limits<uint16_t>::max() );

	auto *effect            = new( mem )TransientBeamEffect;
	effect->spawnTime       = m_lastTime;
	effect->timeout         = timeout;
	effect->fadeOutOffset   = fadeOutOffset;
	effect->rcpFadeOutTime  = Q_Rcp( (float)( timeout - fadeOutOffset ) );
	effect->poly.width      = width;
	effect->poly.length     = Q_Rcp( rcpLength );
	effect->poly.material   = material;
	effect->poly.tileLength = tileLength;
	VectorCopy( from, effect->poly.from );
	VectorCopy( to, effect->poly.to );
	VectorCopy( dir, effect->poly.dir );
	Vector4Copy( color, effect->initialColor );
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
			const int64_t startFadeOutAt = beam->spawnTime + beam->fadeOutOffset;
			if( currTime <= startFadeOutAt ) {
				Vector4Copy( beam->initialColor, beam->poly.color );
			} else {
				const auto frac = 1.0f - ( (float)( currTime - startFadeOutAt ) * beam->rcpFadeOutTime );
				assert( frac > 0.0f && frac < 1.0f );
				Vector4Scale( beam->initialColor, frac, beam->poly.color );
			}

			request->addPoly( &beam->poly );
		}
	}

	m_lastTime = currTime;
}