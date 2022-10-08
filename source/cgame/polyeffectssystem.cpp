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
	for( TracerEffect *tracer = m_tracerEffectsHead, *next = nullptr; tracer; tracer = next ) { next = tracer->next;
		destroyTracerEffect( tracer );
	}
}

auto PolyEffectsSystem::createCurvedBeamEffect( shader_s *material ) -> CurvedBeam * {
	assert( !m_curvedLaserBeamsAllocator.isFull() );
	auto *effect = new( m_curvedLaserBeamsAllocator.allocOrNull() )CurvedBeamEffect;
	wsw::link( effect, &m_curvedLaserBeamsHead );
	effect->poly.material = material;
	return effect;
}

void PolyEffectsSystem::updateCurvedBeamEffect( CurvedBeam *handle, const float *color, float width, float tileLength,
												std::span<const vec3_t> points ) {
	auto *const __restrict effect = (CurvedBeamEffect *)handle;
	assert( (uintptr_t)effect == (uintptr_t)handle );
	assert( points.size() <= std::size( effect->poly.points ) );

	Vector4Copy( color, effect->poly.color );
	effect->poly.width      = width;
	effect->poly.tileLength = tileLength;
	effect->poly.numPoints  = (unsigned)points.size();
	std::memcpy( effect->poly.points, points.data(), sizeof( vec3_t ) * points.size() );

	if( !points.empty() ) [[likely]] {
		BoundsBuilder boundsBuilder;
		for( const float *point: points ) {
			boundsBuilder.addPoint( point );
		}
		// Consider this to be a good estimation
		boundsBuilder.storeToWithAddedEpsilon( effect->poly.cullMins, effect->poly.cullMaxs, width + 1.0f );
	}
}

auto PolyEffectsSystem::CurvedBeamPoly::getStorageRequirements() const -> std::pair<unsigned, unsigned> {
	assert( numPoints );
	return { 2 * 4 * numPoints, 2 * 6 * numPoints };
}

[[nodiscard]]
auto PolyEffectsSystem::CurvedBeamPoly::fillMeshBuffers( const float *__restrict,
														 const float *__restrict,
														 vec4_t *__restrict positions,
														 vec2_t *__restrict texCoords,
														 byte_vec4_t *__restrict colors,
														 uint16_t *__restrict indices ) const
	-> std::pair<unsigned, unsigned> {
	assert( numPoints );

	assert( std::isfinite( width ) && width > 0 );
	assert( std::isfinite( tileLength ) && tileLength > 0 );

	assert( tileLength > 0.0f );

	const byte_vec4_t byteColor {
		( uint8_t )( color[0] * 255 ),
		( uint8_t )( color[1] * 255 ),
		( uint8_t )( color[2] * 255 ),
		( uint8_t )( color[3] * 255 )
	};

	unsigned numVertices = 0;
	unsigned numIndices  = 0;

	const float ymin = -0.5f * width;
	const float ymax = +0.5f * width;

	float totalLengthSoFar    = 0.0f;
	const float rcpTileLength = Q_Rcp( tileLength );

	// TODO:!!!!!!!!! Don't submit separate quads, utilize adjacency
	for( unsigned segmentNum = 0; segmentNum + 1 < numPoints; ++segmentNum ) {
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
			const unsigned firstSegmentIndex = numVertices;
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

				Vector2Copy( texCoords[-1 - 4], texCoords[0] );
				Vector2Copy( texCoords[-2 - 4], texCoords[1] );
			} else {
				firstUntransformedVertexInQuad = 0;

				Vector4Set( positions[0], xmin, 0.0f, ymin, 1.0f );
				Vector4Set( positions[1], xmin, 0.0f, ymax, 1.0f );

				Vector2Set( texCoords[0], 0.0f, 0.0f );
				Vector2Set( texCoords[1], 0.0f, 1.0f );
			}

			Vector4Set( positions[2], xmax, 0.0f, ymax, 1.0f );
			Vector4Set( positions[3], xmax, 0.0f, ymin, 1.0f );

			Vector2Set( texCoords[2], stx, 1.0f );
			Vector2Set( texCoords[3], stx, 0.0f );

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
			}

			numVertices += 4;
			numIndices += 6;

			positions += 4, colors += 4, texCoords += 4;
			indices += 6;
		}
	}

	return { numVertices, numIndices };
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

void PolyEffectsSystem::destroyTracerEffect( TracerEffect *effect ) {
	wsw::unlink( effect, &m_tracerEffectsHead );
	effect->~TracerEffect();
	m_tracerEffectsAllocator.free( effect );
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

static constexpr float kTracerMinSpeed      = 1000.0f;
static constexpr unsigned kTracerTimeMillis = 100;

void PolyEffectsSystem::spawnTracerEffect( const float *from, const float *to, TracerParams &&params ) {
	assert( params.prestep >= 1.0f && params.width > 0.0f && params.length >= 1.0f );

	const float squareDistance = DistanceSquared( from, to );
	if( squareDistance < wsw::square( params.prestep + params.length ) ) [[unlikely]] {
		return;
	}

	vec3_t dir;
	VectorSubtract( to, from, dir );
	const float rcpDistance = Q_RSqrt( squareDistance );
	VectorScale( dir, rcpDistance, dir );

	void *mem = m_tracerEffectsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		assert( m_tracerEffectsHead );
		TracerEffect *oldestEffect = m_tracerEffectsHead->next;
		while( oldestEffect ) {
			if( oldestEffect->next ) {
				oldestEffect = oldestEffect->next;
			} else {
				break;
			}
		}
		wsw::unlink( oldestEffect, &m_tracerEffectsHead );
		oldestEffect->~TracerEffect();
		mem = oldestEffect;
	}

	// TODO: The speed estimation does not take the fact we also advance by `length` every submission frame
	const float distance          = squareDistance * rcpDistance;
	const float tracerTimeSeconds = 1e-3f * (float)kTracerTimeMillis;
	const float speed             = std::max( kTracerMinSpeed, distance * Q_Rcp( tracerTimeSeconds ) );

	auto *effect            = new( mem )TracerEffect;
	effect->spawnTime       = m_lastTime;
	effect->speed           = speed;
	effect->totalDistance   = distance;
	effect->distanceSoFar   = params.prestep;
	effect->poly.width      = params.width;
	effect->poly.length     = params.length;
	effect->poly.tileLength = params.length;
	effect->poly.material   = params.material;

	VectorCopy( from, effect->from );
	effect->from[2] -= 18.0f;
	VectorCopy( to, effect->to );
	VectorCopy( dir, effect->poly.dir );
	// TODO: autosprite
	effect->poly.flags |= QuadPoly::XLike;
	Vector4Copy( params.color, effect->poly.color );

	wsw::link( effect, &m_tracerEffectsHead );
}

void PolyEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	for( CurvedBeamEffect *beam = m_curvedLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->poly.material && beam->poly.numPoints ) [[likely]] {
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

	const float timeDeltaSeconds = 1e-3f * std::min<float>( 33, (float)( currTime - m_lastTime ) );
	for( TracerEffect *tracer = m_tracerEffectsHead, *next = nullptr; tracer; tracer = next ) { next = tracer->next;
		if( tracer->spawnTime + kTracerTimeMillis <= currTime ) [[unlikely]] {
			destroyTracerEffect( tracer );
			continue;
		}

		assert( tracer->poly.length >= 1.0f );
		tracer->distanceSoFar += tracer->speed * timeDeltaSeconds;
		if( tracer->distanceSoFar + tracer->poly.length >= tracer->totalDistance ) [[unlikely]] {
			destroyTracerEffect( tracer );
			continue;
		}

		assert( std::fabs( VectorLengthFast( tracer->poly.dir ) - 1.0f ) < 1e-3f );
		VectorMA( tracer->from, tracer->distanceSoFar, tracer->poly.dir, tracer->poly.from );
		VectorMA( tracer->poly.from, tracer->poly.length, tracer->poly.dir, tracer->poly.to );
		tracer->distanceSoFar += tracer->poly.length;

		request->addPoly( &tracer->poly );
	}

	m_lastTime = currTime;
}