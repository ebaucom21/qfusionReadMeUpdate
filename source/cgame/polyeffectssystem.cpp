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
	if( auto *mem = m_curvedLaserBeamsAllocator.allocOrNull() ) [[likely]] {
		auto *effect = new( mem )CurvedBeamEffect;
		wsw::link( effect, &m_curvedLaserBeamsHead );
		effect->poly.material = material;
		return effect;
	}
	return nullptr;
}

void PolyEffectsSystem::updateCurvedBeamEffect( CurvedBeam *handle, const float *fromColor, const float *toColor,
												float width, const CurvedBeamUVMode &uvMode,
												std::span<const vec3_t> points ) {
	auto *const __restrict effect = (CurvedBeamEffect *)handle;
	assert( (uintptr_t)effect == (uintptr_t)handle );

	Vector4Copy( fromColor, effect->poly.fromColor );
	Vector4Copy( toColor, effect->poly.toColor );

	effect->poly.width  = width;
	effect->poly.uvMode = uvMode;

	// Caution! We borrow the reference to an external buffer
	effect->poly.points = points;

	if( points.size() > 1 ) [[likely]] {
		BoundsBuilder boundsBuilder;
		for( const float *point: points ) {
			boundsBuilder.addPoint( point );
		}
		// Consider this to be a good estimation
		boundsBuilder.storeToWithAddedEpsilon( effect->poly.cullMins, effect->poly.cullMaxs, width + 1.0f );
	}
}

auto PolyEffectsSystem::CurvedBeamPoly::getStorageRequirements( const float *, const float *, float ) const
	-> std::optional<std::pair<unsigned, unsigned>> {
	assert( points.size() > 1 );
	return std::make_pair( (unsigned)( 4 * points.size() ), (unsigned)( 6 * points.size() ) );
}

[[nodiscard]]
auto PolyEffectsSystem::CurvedBeamPoly::fillMeshBuffers( const float *__restrict viewOrigin,
														 const float *__restrict,
														 float,
														 const Scene::DynamicLight *,
														 std::span<const uint16_t>,
														 vec4_t *__restrict positions,
														 vec4_t *__restrict,
														 vec2_t *__restrict texCoords,
														 byte_vec4_t *__restrict colors,
														 uint16_t *__restrict indices ) const
	-> std::pair<unsigned, unsigned> {
	assert( points.size() > 1 && points.size() < 64 );
	assert( std::isfinite( width ) && width > 0 );

	// std::variant interface sucks
	const UvModeTile *const uvModeTile   = std::get_if<UvModeTile>( &uvMode );
	const UvModeClamp *const uvModeClamp = std::get_if<UvModeClamp>( &uvMode );

	// We have to precalculate some parameters for UvModeFit.
	// This doesn't make the code inefficient for other case.

	auto *const __restrict rcpLengthOfSegments  = (float *)alloca( sizeof( float ) * points.size() );

	bool foundDegenerateSegment    = false;
	unsigned numDrawnSegments      = 0;
	float totalLengthOfDrawnPart   = 0.0f;
	float totalLengthOfPointsChain = 0.0f;
	for( unsigned segmentNum = 0; segmentNum + 1 < points.size(); ++segmentNum ) {
		const float *const from = points[segmentNum + 0];
		const float *const to   = points[segmentNum + 1];

		const float squareSegmentLength = DistanceSquared( from, to );
		if( !foundDegenerateSegment ) [[likely]] {
			if( squareSegmentLength < 1.0f ) [[unlikely]] {
				if( !uvModeClamp ) {
					// Interrupt at this - we don't have to know the full chain length for other cases
					break;
				} else {
					totalLengthOfPointsChain += Q_Sqrt( squareSegmentLength );
					foundDegenerateSegment = true;
				}
			} else {
				const float rcpSegmentLength    = Q_RSqrt( squareSegmentLength );
				const float segmentLength       = squareSegmentLength * rcpSegmentLength;
				rcpLengthOfSegments[segmentNum] = rcpSegmentLength;

				totalLengthOfPointsChain += segmentLength;
				totalLengthOfDrawnPart   += segmentLength;
				numDrawnSegments++;
			}
		} else {
			totalLengthOfPointsChain += Q_Sqrt( squareSegmentLength );
		}
	}

	assert( totalLengthOfDrawnPart <= totalLengthOfPointsChain );

	// Lift reciprocal computations out of the loop

	float rcpTileLength = 0.0f;
	if( uvModeTile ) {
		assert( uvModeTile->tileLength > 0.0f );
		rcpTileLength = Q_Rcp( uvModeTile->tileLength );
	}

	float rcpTotalLengthOfDrawnPart = 0.0f;
	if( !uvModeClamp && totalLengthOfDrawnPart > 0.0f ) {
		rcpTotalLengthOfDrawnPart = Q_Rcp( totalLengthOfDrawnPart );
	}

	float rcpTotalLengthOfPointsChain = 0.0f;
	if( uvModeClamp && totalLengthOfPointsChain > 0.0f ) {
		rcpTotalLengthOfPointsChain = Q_Rcp( totalLengthOfPointsChain );
	}

	byte_vec4_t sharedColor;
	const bool hasVaryingColors = !VectorCompare( fromColor, toColor ) || fromColor[3] != toColor[3];
	if( !hasVaryingColors ) {
		for( unsigned i = 0; i < 4; ++i ) {
			sharedColor[i] = (uint8_t)wsw::clamp( 255.0f * fromColor[i], 0.0f, 255.0f );
		}
	}

	bool hasValidPrevSegment = false;
	unsigned numVertices = 0, numIndices = 0;

	const float halfWidth  = 0.5f * width;
	float totalLengthSoFar = 0.0f;
	// Note: we have to submit separate quads as some segments could be discarded
	for( unsigned segmentNum = 0; segmentNum < numDrawnSegments; ++segmentNum ) {
		const float *const from = points[segmentNum + 0];
		const float *const to   = points[segmentNum + 1];

		const float rcpSegmentLength = rcpLengthOfSegments[segmentNum];

		vec3_t segmentDir;
		VectorSubtract( to, from, segmentDir );
		VectorScale( segmentDir, rcpSegmentLength, segmentDir );

		vec3_t mid, viewToMid, right;
		VectorAvg( from, to, mid );

		VectorSubtract( mid, viewOrigin, viewToMid );
		CrossProduct( viewToMid, segmentDir, right );
		const float squareRightLength = VectorLengthSquared( right );
		if( squareRightLength < wsw::square( 0.001f ) ) [[unlikely]] {
			hasValidPrevSegment = false;
			continue;
		}

		const float rcpRightLength = Q_RSqrt( squareRightLength );
		VectorScale( right, rcpRightLength, right );

		if( hasValidPrevSegment ) [[likely]] {
			VectorCopy( positions[-1], positions[0] );
			VectorCopy( positions[-2], positions[1] );
		} else {
			VectorMA( from, +halfWidth, right, positions[0] );
			VectorMA( from, -halfWidth, right, positions[1] );
		}

		VectorMA( to, -halfWidth, right, positions[2] );
		VectorMA( to, +halfWidth, right, positions[3] );
		positions[0][3] = positions[1][3] = positions[2][3] = positions[3][3] = 1.0f;

		VectorSet( indices + 0, numVertices + 0, numVertices + 1, numVertices + 2 );
		VectorSet( indices + 3, numVertices + 0, numVertices + 2, numVertices + 3 );

		const float lengthAt1 = totalLengthSoFar;
		totalLengthSoFar += Q_Rcp( rcpSegmentLength );
		const float lengthAt2 = totalLengthSoFar;

		float stx1, stx2;
		if( uvModeTile ) {
			stx1 = lengthAt1 * rcpTileLength;
			stx2 = lengthAt2 * rcpTileLength;
		} else if( uvModeClamp ) {
			stx1 = wsw::clamp( lengthAt1 * rcpTotalLengthOfPointsChain, 0.0f, 1.0f );
			stx2 = wsw::clamp( lengthAt2 * rcpTotalLengthOfPointsChain, 0.0f, 1.0f );
		} else {
			stx1 = wsw::clamp( lengthAt1 * rcpTotalLengthOfDrawnPart, 0.0f, 1.0f );
			stx2 = wsw::clamp( lengthAt2 * rcpTotalLengthOfDrawnPart, 0.0f, 1.0f );
		}

		Vector2Set( texCoords[0], stx1, 0.0f );
		Vector2Set( texCoords[1], stx1, 1.0f );
		Vector2Set( texCoords[2], stx2, 1.0f );
		Vector2Set( texCoords[3], stx2, 0.0f );

		if( hasVaryingColors ) {
			float colorFrac1, colorFrac2;
			if( uvModeClamp ) {
				colorFrac1 = lengthAt1 * rcpTotalLengthOfPointsChain;
				colorFrac2 = lengthAt2 * rcpTotalLengthOfPointsChain;
			} else {
				colorFrac1 = lengthAt1 * rcpTotalLengthOfDrawnPart;
				colorFrac2 = lengthAt2 * rcpTotalLengthOfDrawnPart;
			}

			assert( colorFrac1 >= 0.0f && colorFrac1 <= 1.01f );
			assert( colorFrac2 >= 0.0f && colorFrac2 <= 1.01f );

			vec4_t colorAt1, colorAt2;
			Vector4Lerp( this->fromColor, colorFrac1, this->toColor, colorAt1 );
			Vector4Lerp( this->fromColor, colorFrac2, this->toColor, colorAt2 );

			for( unsigned i = 0; i < 4; ++i ) {
				colors[0][i] = colors[1][i] = (uint8_t)wsw::clamp( 255.0f * colorAt1[i], 0.0f, 255.0f );
				colors[2][i] = colors[3][i] = (uint8_t)wsw::clamp( 255.0f * colorAt2[i], 0.0f, 255.0f );
			}
		} else {
			Vector4Copy( sharedColor, colors[0] );
			Vector4Copy( sharedColor, colors[1] );
			Vector4Copy( sharedColor, colors[2] );
			Vector4Copy( sharedColor, colors[3] );
		}

		numVertices += 4;
		numIndices += 6;

		positions += 4, colors += 4, texCoords += 4;
		indices += 6;

		hasValidPrevSegment = true;
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
	if( void *mem = m_straightLaserBeamsAllocator.allocOrNull() ) [[likely]] {
		auto *effect = new( mem )StraightBeamEffect;
		wsw::link( effect, &m_straightLaserBeamsHead );
		effect->poly.material   = material;
		effect->poly.halfExtent = 0.0f;
		return effect;
	}
	return nullptr;
}

void PolyEffectsSystem::updateStraightBeamEffect( StraightBeam *handle, const float *fromColor, const float *toColor,
												  float width, float tileLength, const float *from, const float *to ) {
	auto *effect = (StraightBeamEffect *)( handle );
	assert( (uintptr_t)effect == (uintptr_t)handle );

	if( effect->poly.material ) [[likely]] {
		if( const float squareLength = DistanceSquared( from, to ); squareLength >= 1.0f ) [[likely]] {
			const float rcpLength = Q_RSqrt( squareLength );
			vec3_t dir;
			VectorSubtract( to, from, dir );
			VectorScale( dir, rcpLength, dir );
			VectorAvg( from, to, effect->poly.origin );
			effect->poly.halfExtent      = 0.5f * ( squareLength * rcpLength );
			effect->poly.appearanceRules = QuadPoly::ViewAlignedBeamRules {
				.dir        = { dir[0], dir[1], dir[2] },
				.width      = width,
				.tileLength = tileLength,
				.fromColor  = { fromColor[0], fromColor[1], fromColor[2], fromColor[3] },
				.toColor    = { toColor[0], toColor[1], toColor[2], toColor[3] },
			};
		} else {
			// Suppress rendering this frame
			effect->poly.halfExtent = 0.0f;
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

	vec3_t dir;
	VectorSubtract( to, from, dir );
	const float rcpLength = Q_RSqrt( squareLength );
	VectorScale( dir, rcpLength, dir );

	VectorAvg( from, to, effect->poly.origin );

	effect->poly.material        = params.material;
	effect->poly.halfExtent      = 0.5f * ( squareLength * rcpLength );
	effect->poly.appearanceRules = QuadPoly::ViewAlignedBeamRules {
		.dir        = { dir[0], dir[1], dir[2] },
		.width      = params.width,
		.tileLength = params.tileLength,
	};

	wsw::link( effect, &m_transientBeamsHead );
}

void PolyEffectsSystem::spawnTracerEffect( const float *from, const float *to, TracerParams &&params ) {
	assert( params.duration > 50 && params.prestepDistance >= 1.0f && params.width > 0.0f );

	const float squareDistance = DistanceSquared( from, to );
	if( squareDistance < wsw::square( params.prestepDistance ) ) [[unlikely]] {
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

	// Note: Don't include prestep in speed computations.
	// Doing that makes the speed vary depending on the different prestep value.
	const float totalDistance     = squareDistance * rcpDistance;
	const float tracerTimeSeconds = 1e-3f * (float)params.duration;
	const float polyLength        = wsw::max( params.minLength, params.distancePercentage * totalDistance );
	const float speed             = wsw::max( 3000.0f, totalDistance * Q_Rcp( tracerTimeSeconds ) );

	auto *effect                 = new( mem )TracerEffect;
	effect->timeoutAt            = m_lastTime + params.duration;
	effect->alignForPovNum       = params.alignForPovNum;
	effect->speed                = speed;
	effect->prestepDistance      = params.prestepDistance;
	effect->totalDistance        = totalDistance;
	effect->distanceSoFar        = 0.0f;
	effect->initialColorAlpha    = params.color[3];
	effect->lightFadeInDistance  = 2.0f * params.prestepDistance;
	effect->lightFadeOutDistance = 2.0f * params.prestepDistance;
	effect->smoothEdgeDistance   = params.smoothEdgeDistance;
	effect->poly.material        = params.material;
	effect->poly.halfExtent      = polyLength;
	effect->poly.appearanceRules = QuadPoly::ViewAlignedBeamRules {
		.dir        = { dir[0], dir[1], dir[2] },
		.width      = params.width,
		.tileLength = polyLength,
		.fromColor  = { params.color[0], params.color[1], params.color[2], params.color[3] },
		.toColor    = { params.color[0], params.color[1], params.color[2], params.color[3] },
	};

	VectorCopy( from, effect->from );
	VectorCopy( to, effect->to );
	VectorCopy( dir, effect->dir );

	VectorCopy( params.lightColor, effect->lightColor );
	effect->programLightRadius       = params.programLightRadius;
	effect->coronaLightRadius        = params.coronaLightRadius;
	effect->lightFrameAffinityModulo = params.lightFrameAffinityModulo;
	effect->lightFrameAffinityIndex  = params.lightFrameAffinityIndex;

	wsw::link( effect, &m_tracerEffectsHead );
}

void PolyEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	for( CurvedBeamEffect *beam = m_curvedLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->poly.material && beam->poly.points.size() > 1 ) [[likely]] {
			request->addDynamicMesh( &beam->poly );
		}
	}

	for( StraightBeamEffect *beam = m_straightLaserBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->poly.material && beam->poly.halfExtent > 1.0f ) [[likely]] {
			request->addPoly( &beam->poly );
		}
	}

	for( TransientBeamEffect *beam = m_transientBeamsHead, *next = nullptr; beam; beam = next ) { next = beam->next;
		if( beam->spawnTime + beam->timeout <= currTime ) [[unlikely]] {
			destroyTransientBeamEffect( beam );
			continue;
		}

		if( beam->poly.material && beam->poly.halfExtent > 1.0f ) [[likely]] {
			const float colorLifetimeFrac = (float)( currTime - beam->spawnTime ) * Q_Rcp( (float)beam->timeout );

			vec4_t color;
			beam->colorLifespan.getColorForLifetimeFrac( colorLifetimeFrac, color );
			auto *const rules = std::get_if<QuadPoly::ViewAlignedBeamRules>( &beam->poly.appearanceRules );
			Vector4Copy( color, rules->fromColor );
			Vector4Copy( color, rules->toColor );

			if( beam->lightProps ) {
				const auto &[lightTimeout, lightLifespan] = *beam->lightProps;
				if( beam->spawnTime + lightTimeout > currTime ) {
					const float lightLifetimeFrac = (float)( currTime - beam->spawnTime ) * Q_Rcp( (float)lightTimeout );
					assert( lightLifetimeFrac >= 0.0f && lightLifetimeFrac < 1.01f );

					float lightRadius, lightColor[3];
					lightLifespan.getRadiusAndColorForLifetimeFrac( lightLifetimeFrac, &lightRadius, lightColor );
					if( lightRadius > 1.0f ) {
						float lightOrigin[3];

						// The lifetime fraction is in [0, 1] range.
						// The position parameter is in [-1, +1] range.
						// The value -1 gives the "from" point, the value +1 gives the "to" point.
						const float positionParam = 2.0f * lightLifetimeFrac - 1.0f;
						VectorMA( beam->poly.origin, beam->poly.halfExtent * positionParam, rules->dir, lightOrigin );
						request->addLight( lightOrigin, lightRadius, lightRadius, lightColor );
					}
				}
			}

			request->addPoly( &beam->poly );
		}
	}

	const float timeDeltaSeconds = 1e-3f * std::min<float>( 33, (float)( currTime - m_lastTime ) );
	for( TracerEffect *tracer = m_tracerEffectsHead, *next = nullptr; tracer; tracer = next ) { next = tracer->next;
		if( tracer->timeoutAt <= currTime ) [[unlikely]] {
			destroyTracerEffect( tracer );
			continue;
		}

		assert( tracer->poly.halfExtent >= 0.5f );

		[[maybe_unused]] const float oldDistanceSoFar = tracer->distanceSoFar;
		tracer->distanceSoFar += tracer->speed * timeDeltaSeconds;

		const float distanceOfClosestPoint = oldDistanceSoFar - tracer->poly.halfExtent;
		if( distanceOfClosestPoint <= tracer->prestepDistance ) [[unlikely]] {
			// Hide it for now
			continue;
		}

		// Don't let the poly penetrate the target position
		const float distanceOfFarthestPoint = oldDistanceSoFar + tracer->poly.halfExtent;
		if( distanceOfFarthestPoint >= tracer->totalDistance ) [[unlikely]] {
			destroyTracerEffect( tracer );
			continue;
		}

		assert( std::fabs( VectorLengthFast( tracer->dir ) - 1.0f ) < 1e-3f );
		VectorMA( tracer->from, oldDistanceSoFar, tracer->dir, tracer->poly.origin );

		auto *const rules = std::get_if<QuadPoly::ViewAlignedBeamRules>( &tracer->poly.appearanceRules );
		rules->fromColor[3] = tracer->initialColorAlpha;

		// Soften the closest edge

		if( distanceOfClosestPoint < tracer->smoothEdgeDistance ) {
			rules->fromColor[3] *= distanceOfClosestPoint * Q_Rcp( tracer->smoothEdgeDistance );
		}

		// If we should align and are really following the initial POV
		if( tracer->alignForPovNum == std::optional( cg.predictedPlayerState.POVnum ) ) {
			// Pin the farthest point at it's regular origin and align the free tail towards the viewer
			// TODO: Think of creating a separate kind of QuadPoly::AppearanceRules for this case?
			vec3_t farthestPoint, viewOrigin, viewToFarthestPointDir;

			VectorMA( tracer->from, distanceOfFarthestPoint, tracer->dir, farthestPoint );

			VectorCopy( cg.predictedPlayerState.pmove.origin, viewOrigin );
			viewOrigin[2] += cg.predictedPlayerState.viewheight;

			const float squareDistance = DistanceSquared( farthestPoint, viewOrigin );
			if( squareDistance > wsw::square( 0.1f ) ) [[likely]] {
				const float rcpDistance = Q_RSqrt( squareDistance );
				VectorSubtract( farthestPoint, viewOrigin, viewToFarthestPointDir );
				VectorScale( viewToFarthestPointDir, rcpDistance, viewToFarthestPointDir );

				VectorCopy( viewToFarthestPointDir, rules->dir );
				// TODO: Without origin correction, the code actually rotates
				// the poly around it's center instead of farthest point
			} else {
				VectorCopy( tracer->dir, rules->dir );
			}
		} else {
			// Restore the default alignment upon the POV switch (if any)
			VectorCopy( tracer->dir, rules->dir );
		}

		assert( std::fabs( VectorLengthFast( rules->dir ) - 1.0f ) < 1e-3f );

		request->addPoly( &tracer->poly );

		if( tracer->programLightRadius > 0.0f || tracer->coronaLightRadius > 0.0f ) {
			bool shouldAddLight = true;
			// If the light display is tied to certain frames (e.g., every 3rd one, starting from 2nd absolute)
			if( const auto modulo = (unsigned)tracer->lightFrameAffinityModulo; modulo > 1 ) {
				using CountType = decltype( cg.frameCount );
				const auto frameIndexByModulo = cg.frameCount % (CountType) modulo;
				shouldAddLight = frameIndexByModulo == (CountType)tracer->lightFrameAffinityIndex;
			}
			if( shouldAddLight ) {
				float radiusFrac = 1.0f;
				if( tracer->lightFadeInDistance > 0.0f ) {
					if( oldDistanceSoFar < tracer->lightFadeInDistance ) {
						radiusFrac = oldDistanceSoFar * Q_Rcp( tracer->lightFadeInDistance );
					}
				} else if( tracer->lightFadeOutDistance > 0.0f ) {
					const float distanceLeft = tracer->totalDistance - oldDistanceSoFar;
					if( distanceLeft < tracer->lightFadeOutDistance ) {
						radiusFrac = distanceLeft * Q_Rcp( tracer->lightFadeOutDistance );
					}
				}
				assert( radiusFrac >= 0.0f && radiusFrac < 1.01f );
				const float programRadius = radiusFrac * tracer->programLightRadius;
				const float coronaRadius  = radiusFrac * tracer->coronaLightRadius;
				if( programRadius > 1.0f || coronaRadius > 1.0f ) {
					request->addLight( tracer->poly.origin, programRadius, coronaRadius, tracer->lightColor );
				}
			}
		}
	}

	m_lastTime = currTime;
}