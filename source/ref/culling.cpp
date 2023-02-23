/*
Copyright (C) 2007 Victor Luchits
Copyright (C) 2021 Chasseur de bots

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

#include "local.h"
#include "frontend.h"
#include "program.h"
#include "materiallocal.h"

#include <algorithm>

#define SHOW_OCCLUDED( v1, v2, color ) do { /* addDebugLine( v1, v2, color ); */ } while( 0 )
//#define SHOW_OCCLUDERS
//#define SHOW_OCCLUDERS_FRUSTA
//#define DEBUG_OCCLUDERS

// A SSE2 baseline implementation
// https://fgiesen.wordpress.com/2016/04/03/sse-mind-the-gap/
#define wsw_blend( a, b, mask ) _mm_or_ps( _mm_and_ps( a, mask ), _mm_andnot_ps( mask, b ) )

#define LOAD_BOX_COMPONENTS( boxMins, boxMaxs ) \
	const __m128 xmmMins  = _mm_loadu_ps( boxMins ); \
	const __m128 xmmMaxs  = _mm_loadu_ps( boxMaxs ); \
	const __m128 xmmMinsX = _mm_shuffle_ps( xmmMins, xmmMins, _MM_SHUFFLE( 0, 0, 0, 0 ) ); \
	const __m128 xmmMinsY = _mm_shuffle_ps( xmmMins, xmmMins, _MM_SHUFFLE( 1, 1, 1, 1 ) ); \
	const __m128 xmmMinsZ = _mm_shuffle_ps( xmmMins, xmmMins, _MM_SHUFFLE( 2, 2, 2, 2 ) ); \
	const __m128 xmmMaxsX = _mm_shuffle_ps( xmmMaxs, xmmMaxs, _MM_SHUFFLE( 0, 0, 0, 0 ) ); \
	const __m128 xmmMaxsY = _mm_shuffle_ps( xmmMaxs, xmmMaxs, _MM_SHUFFLE( 1, 1, 1, 1 ) ); \
	const __m128 xmmMaxsZ = _mm_shuffle_ps( xmmMaxs, xmmMaxs, _MM_SHUFFLE( 2, 2, 2, 2 ) ); \

#define LOAD_COMPONENTS_OF_4_FRUSTUM_PLANES( frustum ) \
	const __m128 xmmXBlends = _mm_load_ps( (const float *)( frustum )->xBlendMasks ); \
	const __m128 xmmYBlends = _mm_load_ps( (const float *)( frustum )->yBlendMasks ); \
	const __m128 xmmZBlends = _mm_load_ps( (const float *)( frustum )->zBlendMasks ); \
	const __m128 xmmPlaneX  = _mm_load_ps( ( frustum )->planeX ); \
	const __m128 xmmPlaneY  = _mm_load_ps( ( frustum )->planeY ); \
	const __m128 xmmPlaneZ  = _mm_load_ps( ( frustum )->planeZ ); \
	const __m128 xmmPlaneD  = _mm_load_ps( ( frustum )->planeD );

#define LOAD_COMPONENTS_OF_8_FRUSTUM_PLANES( frustum ) \
	const __m128 xmmXBlends_03 = _mm_load_ps( (const float *)( frustum )->xBlendMasks ); \
	const __m128 xmmYBlends_03 = _mm_load_ps( (const float *)( frustum )->yBlendMasks ); \
	const __m128 xmmZBlends_03 = _mm_load_ps( (const float *)( frustum )->zBlendMasks ); \
	const __m128 xmmXBlends_47 = _mm_load_ps( ( (const float *)( frustum )->xBlendMasks ) + 4 ); \
	const __m128 xmmYBlends_47 = _mm_load_ps( ( (const float *)( frustum )->yBlendMasks ) + 4 ); \
	const __m128 xmmZBlends_47 = _mm_load_ps( ( (const float *)( frustum )->zBlendMasks ) + 4 ); \
	const __m128 xmmPlaneX_03  = _mm_load_ps( ( frustum )->planeX ); \
	const __m128 xmmPlaneY_03  = _mm_load_ps( ( frustum )->planeY ); \
	const __m128 xmmPlaneZ_03  = _mm_load_ps( ( frustum )->planeZ ); \
	const __m128 xmmPlaneD_03  = _mm_load_ps( ( frustum )->planeD ); \
	const __m128 xmmPlaneX_47  = _mm_load_ps( ( frustum )->planeX + 4 ); \
	const __m128 xmmPlaneY_47  = _mm_load_ps( ( frustum )->planeY + 4 ); \
	const __m128 xmmPlaneZ_47  = _mm_load_ps( ( frustum )->planeZ + 4 ); \
	const __m128 xmmPlaneD_47  = _mm_load_ps( ( frustum )->planeD + 4 );

#define BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( resultsSuffix, masksSuffix ) \
	const __m128 xmmSelectedX##resultsSuffix = wsw_blend( xmmMinsX, xmmMaxsX, xmmXBlends##masksSuffix ); \
	const __m128 xmmSelectedY##resultsSuffix = wsw_blend( xmmMinsY, xmmMaxsY, xmmYBlends##masksSuffix ); \
	const __m128 xmmSelectedZ##resultsSuffix = wsw_blend( xmmMinsZ, xmmMaxsZ, xmmZBlends##masksSuffix );

#define BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( resultsSuffix, masksSuffix ) \
	const __m128 xmmSelectedX##resultsSuffix = wsw_blend( xmmMaxsX, xmmMinsX, xmmXBlends##masksSuffix ); \
	const __m128 xmmSelectedY##resultsSuffix = wsw_blend( xmmMaxsY, xmmMinsY, xmmYBlends##masksSuffix ); \
	const __m128 xmmSelectedZ##resultsSuffix = wsw_blend( xmmMaxsZ, xmmMinsZ, xmmZBlends##masksSuffix );

#define COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( boxSuffix, planesSuffix ) \
	const __m128 xMulX##boxSuffix = _mm_mul_ps( xmmSelectedX##boxSuffix, xmmPlaneX##planesSuffix ); \
	const __m128 yMulY##boxSuffix = _mm_mul_ps( xmmSelectedY##boxSuffix, xmmPlaneY##planesSuffix ); \
	const __m128 zMulZ##boxSuffix = _mm_mul_ps( xmmSelectedZ##boxSuffix, xmmPlaneZ##planesSuffix ); \
	const __m128 xmmDot##boxSuffix = _mm_add_ps( _mm_add_ps( xMulX##boxSuffix, yMulY##boxSuffix ), zMulZ##boxSuffix );

// Assumes that LOAD_BOX_COMPONENTS was expanded in the scope
#define COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( f, nonZeroIfFullyOutside ) \
	LOAD_COMPONENTS_OF_4_FRUSTUM_PLANES( f ) \
	/* Select mins/maxs using masks for respective plane signbits */ \
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( , ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( , ) \
	nonZeroIfFullyOutside = _mm_movemask_ps( _mm_sub_ps( xmmDot, xmmPlaneD ) );

// Assumes that LOAD_BOX_COMPONENTS was expanded in the scope
#define COMPUTE_TRISTATE_RESULT_FOR_4_PLANES( frustum, nonZeroIfOutside, nonZeroIfPartiallyOutside ) \
	LOAD_COMPONENTS_OF_4_FRUSTUM_PLANES( frustum ) \
	/* Select mins/maxs using masks for respective plane signbits */ \
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _nearest, ) \
	/* Select maxs/mins using masks for respective plane signbits (as if masks were inverted) */ \
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _farthest, ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _nearest, ) \
	/* If some bit is set, the nearest dot is < plane distance at least for some plane (separating plane in this case) */ \
	nonZeroIfOutside = _mm_movemask_ps( _mm_sub_ps( xmmDot_nearest, xmmPlaneD ) ); \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _farthest, ) \
	/* If some bit is set, the farthest dot is < plane distance at least for some plane */ \
	nonZeroIfPartiallyOutside = _mm_movemask_ps( _mm_sub_ps( xmmDot_farthest, xmmPlaneD ) );

// Assumes that LOAD_BOX_COMPONENTS was expanded in the scope
#define COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, zeroIfFullyInside ) \
	LOAD_COMPONENTS_OF_8_FRUSTUM_PLANES( f ) \
    /* Select a farthest corner using masks for respective plane signbits */ \
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _03_farthest, _03 ) \
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _47_farthest, _47 ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03_farthest, _03 ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47_farthest, _47 ) \
	const __m128 xmmDiff_03_farthest = _mm_sub_ps( xmmDot_03_farthest, xmmPlaneD_03 ); \
	const __m128 xmmDiff_47_farthest = _mm_sub_ps( xmmDot_47_farthest, xmmPlaneD_47 ); \
    /* Set non-zero bits if some of these distances were negative (this means the box is not fully inside) */ \
	zeroIfFullyInside = _mm_movemask_ps( _mm_or_ps( xmmDiff_03_farthest, xmmDiff_47_farthest ) );

// Assumes that LOAD_BOX_COMPONENTS was expanded in the scope
#define COMPUTE_TRISTATE_RESULT_FOR_8_PLANES( f, nonZeroIfOutside, nonZeroIfPartiallyOutside ) \
	LOAD_COMPONENTS_OF_8_FRUSTUM_PLANES( f ) \
	/* Select mins/maxs using masks for respective plane signbits, planes 0..3 */ \
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _03_nearest, _03 ) \
	/* Select mins/maxs using masks for respective plane signbits, planes 4..7 */ \
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _47_nearest, _47 ) \
	/* The same but as if masks were inverted */ \
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _03_farthest, _03 ) \
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _47_farthest, _47 ) \
    /* Compute dot products of planes and nearest box corners to check whether the box is fully outside */ \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03_nearest, _03 ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47_nearest, _47 ) \
    /* Compute dot products of planes and farthest box corners to check whether the box is partially outside */ \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03_farthest, _03 ) \
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47_farthest, _47 ) \
    /* Compute distances of corners to planes using the dot products */ \
	const __m128 xmmDiff_03_nearest = _mm_sub_ps( xmmDot_03_nearest, xmmPlaneD_03 ); \
	const __m128 xmmDiff_47_nearest = _mm_sub_ps( xmmDot_47_nearest, xmmPlaneD_47 ); \
	const __m128 xmmDiff_03_farthest = _mm_sub_ps( xmmDot_03_farthest, xmmPlaneD_03 ); \
	const __m128 xmmDiff_47_farthest = _mm_sub_ps( xmmDot_47_farthest, xmmPlaneD_47 ); \
    /* Set non-zero result bits if some distances were negative */ \
    /* We do not need an exact match with masks of AVX version, just checking for non-zero bits is sufficient */ \
    /* Note: We assume that signed zeros are negativ, this is fine for culling purposes */ \
	nonZeroIfOutside = _mm_movemask_ps( _mm_or_ps( xmmDiff_03_nearest, xmmDiff_47_nearest ) ); \
	nonZeroIfPartiallyOutside = _mm_movemask_ps( _mm_or_ps( xmmDiff_03_farthest, xmmDiff_47_farthest ) );

void Frustum::setPlaneComponentsAtIndex( unsigned index, const float *n, float d ) {
	const uint32_t blendsForSign[2] { 0, ~( (uint32_t)0 ) };

	const float nX = planeX[index] = n[0];
	const float nY = planeY[index] = n[1];
	const float nZ = planeZ[index] = n[2];

	planeD[index] = d;

	xBlendMasks[index] = blendsForSign[nX < 0];
	yBlendMasks[index] = blendsForSign[nY < 0];
	zBlendMasks[index] = blendsForSign[nZ < 0];
}

void Frustum::fillComponentTails( unsigned indexOfPlaneToReplicate ) {
	// Sanity check
	assert( indexOfPlaneToReplicate >= 3 && indexOfPlaneToReplicate < 8 );
	for( unsigned i = indexOfPlaneToReplicate; i < 8; ++i ) {
		planeX[i] = planeX[indexOfPlaneToReplicate];
		planeY[i] = planeY[indexOfPlaneToReplicate];
		planeZ[i] = planeZ[indexOfPlaneToReplicate];
		planeD[i] = planeD[indexOfPlaneToReplicate];
		xBlendMasks[i] = xBlendMasks[indexOfPlaneToReplicate];
		yBlendMasks[i] = yBlendMasks[indexOfPlaneToReplicate];
		zBlendMasks[i] = zBlendMasks[indexOfPlaneToReplicate];
	}
}

void Frustum::setupFor4Planes( const float *viewOrigin, const mat3_t viewAxis, float fovX, float fovY ) {
	const float *const forward = &viewAxis[AXIS_FORWARD];
	const float *const left    = &viewAxis[AXIS_RIGHT];
	const float *const up      = &viewAxis[AXIS_UP];

	const vec3_t right { -left[0], -left[1], -left[2] };

	const float xRotationAngle = 90.0f - 0.5f * fovX;
	const float yRotationAngle = 90.0f - 0.5f * fovY;

	vec3_t planeNormals[4];
	RotatePointAroundVector( planeNormals[0], up, forward, -xRotationAngle );
	RotatePointAroundVector( planeNormals[1], up, forward, +xRotationAngle );
	RotatePointAroundVector( planeNormals[2], right, forward, +yRotationAngle );
	RotatePointAroundVector( planeNormals[3], right, forward, -yRotationAngle );

	for( unsigned i = 0; i < 4; ++i ) {
		setPlaneComponentsAtIndex( i, planeNormals[i], DotProduct( viewOrigin, planeNormals[i] ) );
	}
}

namespace wsw::ref {

auto Frontend::collectVisibleWorldLeaves() -> std::span<const unsigned> {
	const auto *const pvs             = Mod_ClusterPVS( rf.viewcluster, rsh.worldModel );
	const unsigned numWorldLeaves     = rsh.worldBrushModel->numvisleafs;
	const auto leaves                 = rsh.worldBrushModel->visleafs;
	unsigned *const visibleLeaves     = m_visibleLeavesBuffer.data.get();
	const Frustum *__restrict frustum = &m_frustum;

	unsigned numVisibleLeaves = 0;
	for( unsigned leafNum = 0; leafNum < numWorldLeaves; ++leafNum ) {
		const mleaf_t *__restrict const leaf = leaves[leafNum];
		// TODO: Handle area bits as well
		// TODO: Can we just iterate over all leaves in the cluster
		if( pvs[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) {
			LOAD_BOX_COMPONENTS( leaf->mins, leaf->maxs )
			// TODO: Re-add partial visibility of leaves
			COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( frustum, const int nonZeroIfFullyOutside )
			visibleLeaves[numVisibleLeaves] = leafNum;
			numVisibleLeaves += ( nonZeroIfFullyOutside == 0 );
		}
	}

	return { visibleLeaves, visibleLeaves + numVisibleLeaves };
}

static bool isLockingOccluders = false;
static vec3_t lockedViewOrigin;
static vec3_t lockedViewAxis;

auto Frontend::collectVisibleOccluders() -> std::span<const SortedOccluder> {
	const Frustum *__restrict frustum = &m_frustum;
	unsigned *const visibleOccluders  = m_visibleOccludersBuffer.data.get();
	unsigned numVisibleOccluders      = 0;

#ifdef DEBUG_OCCLUDERS
	if( Cvar_Integer( "lockOccluders" ) ) {
		if( !isLockingOccluders ) {
			VectorCopy( m_state.viewOrigin, lockedViewOrigin );
			VectorCopy( m_state.viewAxis, lockedViewAxis );
			isLockingOccluders = true;
		}
	}
#endif

	const OccluderBoundsEntry *const occluderBoundsEntries = rsh.worldBrushModel->occluderBoundsEntries;
	const OccluderDataEntry *const occluderDataEntries     = rsh.worldBrushModel->occluderDataEntries;
	const unsigned numWorldModelOccluders                  = rsh.worldBrushModel->numOccluders;

	for( unsigned occluderNum = 0; occluderNum < numWorldModelOccluders; ++occluderNum ) {
		visibleOccluders[numVisibleOccluders] = occluderNum;
#ifdef DEBUG_OCCLUDERS
		numVisibleOccluders++;
#else
		const OccluderBoundsEntry &__restrict occluderBounds = occluderBoundsEntries[occluderNum];
		LOAD_BOX_COMPONENTS( occluderBounds.mins, occluderBounds.maxs );
		COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( frustum, const int nonZeroIfFullyOutside );
		numVisibleOccluders += ( nonZeroIfFullyOutside == 0 );
#endif
	}

	const float *__restrict viewOrigin = m_state.viewOrigin;
	const float *__restrict viewAxis   = m_state.viewAxis;
	if( isLockingOccluders ) {
		viewOrigin = lockedViewOrigin;
		viewAxis   = lockedViewAxis;
	}

	SortedOccluder *const sortedOccluders = m_sortedOccludersBuffer.data.get();
	unsigned numSortedOccluders           = 0;

	for( unsigned i = 0; i < numVisibleOccluders; ++i ) {
		const unsigned occluderNum                   = visibleOccluders[i];
		const OccluderDataEntry &__restrict occluder = occluderDataEntries[occluderNum];

		const float absViewDot = std::abs( DotProduct( viewAxis, occluder.plane ) );
		if( absViewDot < 0.1f ) {
			continue;
		}

		if( std::fabs(DotProduct( viewOrigin, occluder.plane ) - occluder.plane[3] ) < 16.0f ) {
			continue;
		}

		vec3_t toOccluderVec;
		// Hacks, hacks, hacks TODO: Add and use the nearest primary frustum plane?
		VectorSubtract( occluder.innerPolyPoint, viewOrigin, toOccluderVec );

		if( DotProduct( viewAxis, toOccluderVec ) <= 0 ) {
			continue;
		}

		VectorNormalizeFast( toOccluderVec );
		if( std::fabs( DotProduct( toOccluderVec, occluder.plane ) ) < 0.3f ) {
			continue;
		}

		// TODO: Try using a distance to the poly?
		const float score = Q_RSqrt( DistanceSquared( viewOrigin, occluder.innerPolyPoint ) ) * absViewDot;
		sortedOccluders[numSortedOccluders++] = { occluderNum, score };
	}

	// TODO: Don't sort, build a heap instead?
	std::sort( sortedOccluders, sortedOccluders + numSortedOccluders );

#ifdef SHOW_OCCLUDERS
	for( unsigned i = 0; i < numSortedOccluders; ++i ) {
		const OccluderDataEntry &__restrict occluder = occluderDataEntries[sortedOccluders[i].occluderNum];

		for( unsigned vertIndex = 0; vertIndex < occluder.numVertices; ++vertIndex ) {
			const float *const v1 = occluder.data[vertIndex + 0];
			const float *const v2 = occluder.data[( vertIndex + 1 != occluder.numVertices ) ? vertIndex + 1 : 0];
			addDebugLine( v1, v2, COLOR_RGB( 192, 192, 96 ) );
		}
	}
#endif

	return { sortedOccluders, sortedOccluders + numSortedOccluders };
}

auto Frontend::buildFrustaOfOccluders( std::span<const SortedOccluder> sortedOccluders ) -> std::span<const Frustum> {
	Frustum *const occluderFrusta     = m_occluderFrusta;
	const unsigned maxOccluders       = wsw::min<unsigned>( sortedOccluders.size(), std::size( m_occluderFrusta ) );
	constexpr float selfOcclusionBias = 1.0f;

	const float *__restrict viewOrigin                = m_state.viewOrigin;
	[[maybe_unused]] const float *__restrict viewAxis = m_state.viewAxis;

#ifdef DEBUG_OCCLUDERS
	if( isLockingOccluders ) {
		viewOrigin = lockedViewOrigin;
		viewAxis   = lockedViewAxis;
	}
#endif

	bool hadCulledFrusta = false;
	// Note: We don't process more occluders due to performance and not memory capacity reasons.
	// Best occluders come first so they should make their way into the final result.
	alignas( 16 )bool isCulledByOtherTable[kMaxOccluderFrusta];
	// MSVC fails to get the member array count in compile time
	assert( std::size( isCulledByOtherTable ) == std::size( m_occluderFrusta ) );
	std::memset( isCulledByOtherTable, 0, sizeof( bool ) * maxOccluders );

	// Note: An outer loop over all surfaces would have been allowed to avoid redundant component shuffles
	// but this approach requires building all frusta prior to that, which is more expensive.

	const OccluderBoundsEntry *const occluderBoundsEntries = rsh.worldBrushModel->occluderBoundsEntries;
	const OccluderDataEntry *const occluderDataEntries     = rsh.worldBrushModel->occluderDataEntries;

	for( unsigned occluderIndex = 0; occluderIndex < maxOccluders; ++occluderIndex ) {
		if( isCulledByOtherTable[occluderIndex] ) {
			continue;
		}

		const OccluderDataEntry *const __restrict occluder = occluderDataEntries + sortedOccluders[occluderIndex].occluderNum;

		Frustum *const __restrict f = &occluderFrusta[occluderIndex];

		for( unsigned vertIndex = 0; vertIndex < occluder->numVertices; ++vertIndex ) {
			const float *const v1 = occluder->data[vertIndex + 0];
			const float *const v2 = occluder->data[( vertIndex + 1 != occluder->numVertices ) ? vertIndex + 1 : 0];

			cplane_t plane;
			// TODO: Cache?
			PlaneFromPoints( v1, v2, viewOrigin, &plane );

			// Make the normal point inside the frustum
			if( DotProduct( plane.normal, occluder->innerPolyPoint ) - plane.dist < 0 ) {
				VectorNegate( plane.normal, plane.normal );
				plane.dist = -plane.dist;
			}

			f->setPlaneComponentsAtIndex( vertIndex, plane.normal, plane.dist );
		}

		vec4_t cappingPlane;
		Vector4Copy( occluder->plane, cappingPlane );
		// Don't let the surface occlude itself
		if( DotProduct( cappingPlane, viewOrigin ) - cappingPlane[3] > 0 ) {
			Vector4Negate( cappingPlane, cappingPlane );
			cappingPlane[3] += selfOcclusionBias;
		} else {
			cappingPlane[3] -= selfOcclusionBias;
		}

		f->setPlaneComponentsAtIndex( occluder->numVertices, cappingPlane, cappingPlane[3] );
		f->fillComponentTails( occluder->numVertices );

#ifndef DEBUG_OCCLUDERS
		// We have built the frustum.
		// Cull all other frusta by it.
		// Note that the "culled-by" relation is not symmetrical so we have to check from the beginning.

		for( unsigned otherOccluderIndex = 0; otherOccluderIndex < maxOccluders; ++otherOccluderIndex ) {
			if( otherOccluderIndex != occluderIndex ) [[likely]] {
				if( !isCulledByOtherTable[otherOccluderIndex] ) {
					const unsigned otherOccluderNum       = sortedOccluders[otherOccluderIndex].occluderNum;
					const OccluderBoundsEntry &thatBounds = occluderBoundsEntries[otherOccluderNum];
					LOAD_BOX_COMPONENTS( thatBounds.mins, thatBounds.maxs );
					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, const int zeroIfFullyInside );
					if( zeroIfFullyInside == 0 ) {
						isCulledByOtherTable[otherOccluderIndex] = true;
						hadCulledFrusta = true;
					}
				}
			}
		}
#endif
	}

	unsigned numSelectedOccluders = maxOccluders;
	if( hadCulledFrusta ) {
		unsigned numPreservedOccluders = 0;
		for( unsigned occluderNum = 0; occluderNum < maxOccluders; ++occluderNum ) {
			if( !isCulledByOtherTable[occluderNum] ) {
				// TODO: This is a memcpy() call, make the compactification more efficient or use a manual SIMD copy
				occluderFrusta[numPreservedOccluders++] = occluderFrusta[occluderNum];
			}
		}
		numSelectedOccluders = numPreservedOccluders;
	}

#ifdef SHOW_OCCLUDERS_FRUSTA
	vec3_t pointInFrontOfView;
	VectorMA( viewOrigin, 8.0, &m_state.viewAxis[0], pointInFrontOfView );

	// Show preserved frusta
	for( unsigned occluderNum = 0; occluderNum < maxOccluders; ++occluderNum ) {
		if( isCulledByOtherTable[occluderNum] ) {
			continue;
		}

#ifdef DEBUG_OCCLUDERS
		if( Cvar_Integer( "pinnedOccluderNum" ) != (int)( 1 + occluderNum ) ) {
			continue;
		}

		const OccluderDataEntry *const occluderData = occluderDataEntries + sortedOccluders[occluderNum].occluderNum;

		const float absViewDot = std::abs( DotProduct( viewAxis, occluderData->plane ) );

		const float distanceToPlane = DotProduct( viewOrigin, occluderData->plane ) - occluderData->plane[3];

		vec3_t toOccluderVec;
		// Hacks, hacks, hacks TODO: Add and use the nearest primary frustum plane?
		VectorSubtract( occluderData->innerPolyPoint, viewOrigin, toOccluderVec );

		const float viewAxisDotToOccluder = DotProduct( viewAxis, toOccluderVec );

		VectorNormalizeFast( toOccluderVec );
		const float planeDotToOccluder = DotProduct( toOccluderVec, occluderData->plane );

		Com_Printf( "Abs view dot=%f distanceToPlane=%f viewAxisDotToOccluder=%f planeDotToOccluder=%f\n",
					absViewDot, distanceToPlane, viewAxisDotToOccluder, planeDotToOccluder );
#endif

		//addDebugLine( surface->occluderPolyMins, surface->occluderPolyMaxs, COLOR_RGB( 0, 128, 255 ) );

		for( unsigned vertIndex = 0; vertIndex < occluderData->numVertices; ++vertIndex ) {
			const float *const v1 = occluderData->data[vertIndex + 0];
			const float *const v2 = occluderData->data[( vertIndex + 1 != occluderData->numVertices ) ? vertIndex + 1 : 0];

			addDebugLine( v1, pointInFrontOfView );
			addDebugLine( v1, v2, COLOR_RGB( 255, 0, 255 ) );

			cplane_t plane;
			// TODO: Inline?
			PlaneFromPoints( v1, v2, viewOrigin, &plane );

			// Make the normal point inside the frustum
			if( DotProduct( plane.normal, occluderData->innerPolyPoint ) - plane.dist < 0 ) {
				VectorNegate( plane.normal, plane.normal );
				plane.dist = -plane.dist;
			}

			vec3_t midpointOfEdge, normalPoint;
			VectorAvg( v1, v2, midpointOfEdge );
			VectorMA( midpointOfEdge, 8.0f, plane.normal, normalPoint );
			addDebugLine( midpointOfEdge, normalPoint );
		}

		vec4_t cappingPlane;
		Vector4Copy( occluderData->plane, cappingPlane );
		// Don't let the surface occlude itself
		if( DotProduct( cappingPlane, viewOrigin ) - cappingPlane[3] > 0 ) {
			Vector4Negate( cappingPlane, cappingPlane );
			cappingPlane[3] += selfOcclusionBias;
		} else {
			cappingPlane[3] -= selfOcclusionBias;
		}

		vec3_t cappingPlanePoint;
		VectorMA( occluderData->innerPolyPoint, 32.0f, cappingPlane, cappingPlanePoint );
		addDebugLine( occluderData->innerPolyPoint, cappingPlanePoint );
	}
#endif

	return { occluderFrusta, occluderFrusta + numSelectedOccluders };
}

auto Frontend::cullLeavesByOccluders( std::span<const unsigned> indicesOfLeaves,
									  std::span<const Frustum> occluderFrusta )
									  -> std::pair<std::span<const unsigned>, std::span<const unsigned>> {
#ifdef DEBUG_OCCLUDERS
	const int pinnedOccluderNum = Cvar_Integer( "pinnedOccluderNum" );
#endif

	unsigned *const partiallyVisibleLeaves = m_occluderPassPartiallyVisibleLeavesBuffer.data.get();
	unsigned *const fullyVisibleLeaves     = m_occluderPassPartiallyVisibleLeavesBuffer.data.get();
	const unsigned numOccluders = occluderFrusta.size();
	const auto leaves           = rsh.worldBrushModel->visleafs;
	unsigned numPartiallyVisibleLeaves = 0;
	unsigned numFullyVisibleLeaves     = 0;

	for( const unsigned leafIndex: indicesOfLeaves ) {
		const mleaf_t *const leaf = leaves[leafIndex];
		int wasPartiallyInside   = 0;
		int wasFullyInside       = 0;
		unsigned frustumNum      = 0;

		LOAD_BOX_COMPONENTS( leaf->mins, leaf->maxs );

		do {
			const Frustum *const __restrict f = &occluderFrusta[frustumNum];

#ifdef DEBUG_OCCLUDERS
			if( pinnedOccluderNum && pinnedOccluderNum != (int)( 1 + frustumNum ) ) {
				continue;
			}
#endif

			COMPUTE_TRISTATE_RESULT_FOR_8_PLANES( f, const int nonZeroIfOutside, const int nonZeroIfPartiallyOutside )

			if( !( nonZeroIfOutside | nonZeroIfPartiallyOutside ) ) {
				wasFullyInside = true;
				SHOW_OCCLUDED( leaf->mins, leaf->maxs, COLOR_RGB( 255, 0, 255 ) );
				break;
			}

			wasPartiallyInside |= nonZeroIfPartiallyOutside;
		} while( ++frustumNum != numOccluders );

		if( !wasFullyInside ) [[likely]] {
			if( wasPartiallyInside ) [[unlikely]] {
				partiallyVisibleLeaves[numPartiallyVisibleLeaves++] = leafIndex;
			} else {
				fullyVisibleLeaves[numFullyVisibleLeaves++] = leafIndex;
			}
		}
	}

	return { { fullyVisibleLeaves, numFullyVisibleLeaves }, { partiallyVisibleLeaves, numPartiallyVisibleLeaves } };
}

void Frontend::cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfVisibleLeaves,
												   std::span<const Frustum> occluderFrusta,
												   MergedSurfSpan *mergedSurfSpans ) {
	assert( !occluderFrusta.empty() );

#ifdef DEBUG_OCCLUDERS
	const int pinnedOccluderNum = Cvar_Integer( "pinnedOccluderNum" );
#endif

	const msurface_t *const surfaces = rsh.worldBrushModel->surfaces;
	const auto leaves = rsh.worldBrushModel->visleafs;
	const unsigned occlusionCullingFrame = m_occlusionCullingFrame;

	// Cull individual surfaces by up to 16 best frusta
	const unsigned numBestOccluders = wsw::min<unsigned>( 16, occluderFrusta.size() );
	for( const unsigned leafNum: indicesOfVisibleLeaves ) {
		const mleaf_s *const leaf             = leaves[leafNum];
		const unsigned *const leafSurfaceNums = leaf->visSurfaces;
		const unsigned numLeafSurfaces        = leaf->numVisSurfaces;

		for( unsigned surfIndex = 0; surfIndex < numLeafSurfaces; ++surfIndex ) {
			const unsigned surfNum = leafSurfaceNums[surfIndex];
			const msurface_t *const __restrict surf = surfaces + surfNum;
			if( surf->occlusionCullingFrame != occlusionCullingFrame ) {
				surf->occlusionCullingFrame = occlusionCullingFrame;

				LOAD_BOX_COMPONENTS( surf->mins, surf->maxs );

				bool surfVisible = true;
				unsigned frustumNum = 0;
				do {
					const Frustum *__restrict f = &occluderFrusta[frustumNum];

#ifdef DEBUG_OCCLUDERS
					if( pinnedOccluderNum && pinnedOccluderNum != (int)( 1 + frustumNum ) ) {
						continue;
					}
#endif

					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, const int zeroIfFullyInside )

					if( !zeroIfFullyInside ) [[unlikely]] {
						surfVisible = false;
						SHOW_OCCLUDED( surf->mins, surf->maxs, COLOR_RGB( 192, 0, 0 ) );
						break;
					}
				} while( ++frustumNum != numBestOccluders );

				if( surfVisible ) {
					assert( surf->drawSurf > 0 );
					const unsigned mergedSurfNum = surf->drawSurf - 1;
					MergedSurfSpan *const __restrict span = &mergedSurfSpans[mergedSurfNum];
					// TODO: Branchless min/max
					span->firstSurface = wsw::min( span->firstSurface, (int)surfNum );
					span->lastSurface = wsw::max( span->lastSurface, (int)surfNum );
				}
			}
		}
	}
}

void Frontend::markSurfacesOfLeavesAsVisible( std::span<const unsigned> indicesOfLeaves,
											  MergedSurfSpan *mergedSurfSpans ) {
	const auto surfaces = rsh.worldBrushModel->surfaces;
	const auto leaves = rsh.worldBrushModel->visleafs;
	for( const unsigned leafNum: indicesOfLeaves ) {
		const auto *__restrict leaf = leaves[leafNum];
		for( unsigned i = 0; i < leaf->numVisSurfaces; ++i ) {
			const unsigned surfNum = leaf->visSurfaces[i];
			assert( surfaces[surfNum].drawSurf > 0 );
			const unsigned mergedSurfNum = surfaces[surfNum].drawSurf - 1;
			MergedSurfSpan *const __restrict span = &mergedSurfSpans[mergedSurfNum];
			// TODO: Branchless min/max
			span->firstSurface = wsw::min( span->firstSurface, (int)surfNum );
			span->lastSurface = wsw::max( span->lastSurface, (int)surfNum );
		}
	}
}

auto Frontend::cullNullModelEntities( std::span<const entity_t> entitiesSpan,
									  const Frustum *__restrict primaryFrustum,
									  std::span<const Frustum> occluderFrusta,
									  uint16_t *tmpIndices,
									  VisTestedModel *tmpModels )
									  -> std::span<const uint16_t> {
	const auto *const entities  = entitiesSpan.data();
	const unsigned numEntities  = entitiesSpan.size();

	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		VisTestedModel *const __restrict model  = &tmpModels[entIndex];
		Vector4Set( model->absMins, -16, -16, -16, -0 );
		Vector4Set( model->absMaxs, +16, +16, +16, +1 );
		VectorAdd( model->absMins, entity->origin, model->absMins );
		VectorAdd( model->absMaxs, entity->origin, model->absMaxs );
	}

	// The number of bounds has an exact match with the number of entities in this case
	return cullEntriesWithBounds( tmpModels, numEntities, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullAliasModelEntities( std::span<const entity_t> entitiesSpan,
									   const Frustum *__restrict primaryFrustum,
									   std::span<const Frustum> occluderFrusta,
									   uint16_t *tmpIndicesBuffer,
									   VisTestedModel *selectedModelsBuffer )
									   -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned weaponModelFlagEnts[16];
	const model_t *weaponModelFlagLods[16];
	unsigned numWeaponModelFlagEnts = 0;

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		if( entity->flags & RF_VIEWERMODEL ) [[unlikely]] {
			if( !( m_state.renderFlags & ( RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
				continue;
			}
		}

		const model_t *mod = R_AliasModelLOD( entity, m_state.lodOrigin, m_state.lod_dist_scale_for_fov );
		const auto *aliasmodel = ( const maliasmodel_t * )mod->extradata;
		// TODO: Could this ever happen
		if( !aliasmodel ) [[unlikely]] {
			continue;
		}
		if( !aliasmodel->nummeshes ) [[unlikely]] {
			continue;
		}

		// Don't let it to be culled away
		// TODO: Keep it separate from other models?
		if( entity->flags & RF_WEAPONMODEL ) [[unlikely]] {
			assert( numWeaponModelFlagEnts < std::size( weaponModelFlagEnts ) );
			weaponModelFlagEnts[numWeaponModelFlagEnts] = entIndex;
			weaponModelFlagLods[numWeaponModelFlagEnts] = mod;
			numWeaponModelFlagEnts++;
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = mod;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		R_AliasModelLerpBBox( entity, mod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	const size_t numPassedOtherEnts = cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels,
															 offsetof( VisTestedModel, absMins ), sizeof( VisTestedModel ),
															 primaryFrustum, occluderFrusta, tmpIndicesBuffer ).size();

	for( unsigned i = 0; i < numWeaponModelFlagEnts; ++i ) {
		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels];
		visTestedModel->selectedLod               = weaponModelFlagLods[i];
		visTestedModel->indexInEntitiesGroup      = weaponModelFlagEnts[i];

		const entity_t *const __restrict entity = &entities[visTestedModel->indexInEntitiesGroup];

		// Just for consistency?
		R_AliasModelLerpBBox( entity, visTestedModel->selectedLod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;

		tmpIndicesBuffer[numPassedOtherEnts + i] = numSelectedModels;
		numSelectedModels++;
	}

	return { tmpIndicesBuffer, numPassedOtherEnts + numWeaponModelFlagEnts };
}

auto Frontend::cullSkeletalModelEntities( std::span<const entity_t> entitiesSpan,
										  const Frustum *__restrict primaryFrustum,
										  std::span<const Frustum> occluderFrusta,
										  uint16_t *tmpIndicesBuffer,
										  VisTestedModel *selectedModelsBuffer )
										  -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; entIndex++ ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		if( entity->flags & RF_VIEWERMODEL ) [[unlikely]] {
			if( !( m_state.renderFlags & ( RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
				continue;
			}
		}

		const model_t *mod = R_SkeletalModelLOD( entity, m_state.lodOrigin, m_state.lod_dist_scale_for_fov );
		const mskmodel_t *skmodel = ( const mskmodel_t * )mod->extradata;
		if( !skmodel ) [[unlikely]] {
			continue;
		}
		if( !skmodel->nummeshes ) [[unlikely]] {
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = mod;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		R_SkeletalModelLerpBBox( entity, mod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	return cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), primaryFrustum, occluderFrusta, tmpIndicesBuffer );
}

auto Frontend::cullBrushModelEntities( std::span<const entity_t> entitiesSpan,
									   const Frustum *__restrict primaryFrustum,
									   std::span<const Frustum> occluderFrusta,
									   uint16_t *tmpIndicesBuffer,
									   VisTestedModel *selectedModelsBuffer )
									   -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		const model_t *const model              = entity->model;
		const auto *const brushModel            = ( mbrushmodel_t * )model->extradata;
		if( !brushModel->numModelDrawSurfaces ) [[unlikely]] {
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = model;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		// Returns absolute bounds
		R_BrushModelBBox( entity, visTestedModel->absMins, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	return cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), primaryFrustum, occluderFrusta, tmpIndicesBuffer );
}

auto Frontend::cullSpriteEntities( std::span<const entity_t> entitiesSpan,
								   const Frustum *__restrict primaryFrustum,
								   std::span<const Frustum> occluderFrusta,
								   uint16_t *tmpIndices, uint16_t *tmpIndices2, VisTestedModel *tmpModels )
								   -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned numResultEntities    = 0;
	unsigned numVisTestedEntities = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		// TODO: This condition should be eliminated from this path
		if( entity->flags & RF_NOSHADOW ) [[unlikely]] {
			if( m_state.renderFlags & RF_SHADOWMAPVIEW ) [[unlikely]] {
				continue;
			}
		}

		if( entity->radius <= 0 || entity->customShader == nullptr || entity->scale <= 0 ) [[unlikely]] {
			continue;
		}

		// Hacks for ET_RADAR indicators
		if( entity->flags & RF_NODEPTHTEST ) [[unlikely]] {
			tmpIndices[numResultEntities++] = entIndex;
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &tmpModels[numVisTestedEntities++];
		visTestedModel->indexInEntitiesGroup      = entIndex;

		const float *origin = entity->origin;
		const float halfRadius = 0.5f * entity->radius;

		Vector4Set( visTestedModel->absMins, origin[0] - halfRadius, origin[1] - halfRadius, origin[2] - halfRadius, 0.0f );
		Vector4Set( visTestedModel->absMaxs, origin[0] + halfRadius, origin[1] + halfRadius, origin[2] + halfRadius, 1.0f );
	}

	const auto passedTestIndices = cullEntriesWithBounds( tmpModels, numVisTestedEntities,
														  offsetof( VisTestedModel, absMins ), sizeof( VisTestedModel ),
														  primaryFrustum, occluderFrusta, tmpIndices2 );

	for( const auto testedModelIndex: passedTestIndices ) {
		tmpIndices[numResultEntities++] = tmpModels[testedModelIndex].indexInEntitiesGroup;
	}

	return { tmpIndices, numResultEntities };
}

auto Frontend::cullLights( std::span<const Scene::DynamicLight> lightsSpan,
						   const Frustum *__restrict primaryFrustum,
						   std::span<const Frustum> occluderFrusta,
						   uint16_t *tmpAllLightIndices,
						   uint16_t *tmpCoronaLightIndices,
						   uint16_t *tmpProgramLightIndices )
	-> std::tuple<std::span<const uint16_t>, std::span<const uint16_t>, std::span<const uint16_t>> {

	const auto *const lights = lightsSpan.data();
	const unsigned numLights = lightsSpan.size();

	static_assert( offsetof( Scene::DynamicLight, mins ) + 4 * sizeof( float ) == offsetof( Scene::DynamicLight, maxs ) );
	const auto visibleAllLightIndices = cullEntriesWithBounds( lights, numLights, offsetof( Scene::DynamicLight, mins ),
															   sizeof( Scene::DynamicLight ), primaryFrustum,
															   occluderFrusta, tmpAllLightIndices );

	unsigned numPassedCoronaLights  = 0;
	unsigned numPassedProgramLights = 0;

	for( const auto index: visibleAllLightIndices ) {
		const Scene::DynamicLight *light               = &lights[index];
		tmpCoronaLightIndices[numPassedCoronaLights]   = index;
		tmpProgramLightIndices[numPassedProgramLights] = index;
		numPassedCoronaLights  += light->hasCoronaLight;
		numPassedProgramLights += light->hasProgramLight;
	}

	return { visibleAllLightIndices,
			 { tmpCoronaLightIndices,  numPassedCoronaLights },
			 { tmpProgramLightIndices, numPassedProgramLights } };
}

auto Frontend::cullParticleAggregates( std::span<const Scene::ParticlesAggregate> aggregatesSpan,
									   const Frustum *__restrict primaryFrustum,
									   std::span<const Frustum> occluderFrusta,
									   uint16_t *tmpIndices )
									   -> std::span<const uint16_t> {
	static_assert( offsetof( Scene::ParticlesAggregate, mins ) + 16 == offsetof( Scene::ParticlesAggregate, maxs ) );
	return cullEntriesWithBounds( aggregatesSpan.data(), aggregatesSpan.size(), offsetof( Scene::ParticlesAggregate, mins ),
								  sizeof( Scene::ParticlesAggregate ), primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullCompoundDynamicMeshes( std::span<const Scene::CompoundDynamicMesh> meshesSpan,
										  const Frustum *__restrict primaryFrustum,
										  std::span<const Frustum> occluderFrusta,
										  uint16_t *tmpIndices ) -> std::span<const uint16_t> {
	static_assert( offsetof( Scene::CompoundDynamicMesh, cullMins ) + 16 == offsetof( Scene::CompoundDynamicMesh, cullMaxs ) );
	return cullEntriesWithBounds( meshesSpan.data(), meshesSpan.size(), offsetof( Scene::CompoundDynamicMesh, cullMins ),
								  sizeof( Scene::CompoundDynamicMesh ), primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullQuadPolys( QuadPoly **polys, unsigned numPolys,
							  const Frustum *__restrict primaryFrustum,
							  std::span<const Frustum> occluderFrusta,
							  uint16_t *tmpIndices,
							  VisTestedModel *tmpModels )
							  -> std::span<const uint16_t> {
	for( unsigned polyNum = 0; polyNum < numPolys; ++polyNum ) {
		const QuadPoly *const __restrict poly  = polys[polyNum];
		VisTestedModel *const __restrict model = &tmpModels[polyNum];

		Vector4Set( model->absMins, -poly->halfExtent, -poly->halfExtent, -poly->halfExtent, -0 );
		Vector4Set( model->absMaxs, +poly->halfExtent, +poly->halfExtent, +poly->halfExtent, +1 );
		VectorAdd( model->absMins, poly->origin, model->absMins );
		VectorAdd( model->absMaxs, poly->origin, model->absMaxs );
	}

	return cullEntriesWithBounds( tmpModels, numPolys, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullDynamicMeshes( const DynamicMesh **meshes,
								  unsigned numMeshes,
								  const Frustum *__restrict primaryFrustum,
								  std::span<const Frustum> occluderFrusta,
								  uint16_t *tmpIndices ) -> std::span<const uint16_t> {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	constexpr unsigned boundsFieldOffset = offsetof( DynamicMesh, cullMins );
	static_assert( offsetof( DynamicMesh, cullMins ) + 4 * sizeof( float ) == offsetof( DynamicMesh, cullMaxs ) );
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	return cullEntryPtrsWithBounds( (const void **)meshes, numMeshes, boundsFieldOffset, primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullEntriesWithBounds( const void *entries, unsigned numEntries, unsigned boundsFieldOffset, unsigned strideInBytes,
									  const Frustum *__restrict primaryFrustum, std::span<const Frustum> occluderFrusta,
									  uint16_t *tmpIndices ) -> std::span<const uint16_t> {
	if( !numEntries ) [[unlikely]] {
		return { tmpIndices, 0 };
	}

	const Frustum *const __restrict frustaPtr = occluderFrusta.data();

	unsigned entryNum         = 0;
	unsigned growingOffset    = boundsFieldOffset;
	unsigned numPassedEntries = 0;

	if( !occluderFrusta.empty() ) [[likely]] {
		do {
			const auto *__restrict bounds = (const vec4_t *)( (const uint8_t *)entries + growingOffset );
			LOAD_BOX_COMPONENTS( bounds[0], bounds[1] );
			COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( primaryFrustum, const int nonZeroIfFullyOutside );
			if( nonZeroIfFullyOutside == 0 ) {
				bool occluded = false;
				unsigned frustumNum = 0;
				do {
					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( frustaPtr + frustumNum, const int zeroIfFullyInside );
					if( zeroIfFullyInside == 0 ) {
						SHOW_OCCLUDED( bounds[0], bounds[1], COLOR_RGB( 255, 192, 255 ) );
						occluded = true;
						break;
					}
				} while( ++frustumNum < occluderFrusta.size() );

				tmpIndices[numPassedEntries] = (uint16_t)entryNum;
				numPassedEntries += !occluded;
			}

			++entryNum;
			growingOffset += strideInBytes;
		} while ( entryNum < numEntries );
	} else {
		do {
			const auto *__restrict bounds = (const vec4_t *)( (const uint8_t *)entries + growingOffset );
			LOAD_BOX_COMPONENTS( bounds[0], bounds[1] );
			COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( primaryFrustum, const int nonZeroIfFullyOutside );

			tmpIndices[numPassedEntries] = (uint16_t)entryNum;
			numPassedEntries += ( nonZeroIfFullyOutside == 0 );

			++entryNum;
			growingOffset += strideInBytes;
		} while( entryNum < numEntries );
	}

	return { tmpIndices, numPassedEntries };
}

[[nodiscard]]
auto Frontend::cullEntryPtrsWithBounds( const void **entryPtrs, unsigned numEntries, unsigned boundsFieldOffset,
										const Frustum *__restrict primaryFrustum, std::span<const Frustum> occluderFrusta,
										uint16_t *tmpIndices ) -> std::span<const uint16_t> {
	if( !numEntries ) [[unlikely]] {
		return { tmpIndices, 0 };
	}

	const Frustum *const __restrict frustaPtr = occluderFrusta.data();

	unsigned entryNum         = 0;
	unsigned numPassedEntries = 0;
	if( !occluderFrusta.empty() ) [[likely]] {
		do {
			const auto *__restrict bounds = (const vec4_t *)( (const uint8_t *)entryPtrs[entryNum] + boundsFieldOffset );
			LOAD_BOX_COMPONENTS( bounds[0], bounds[1] );
			COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( primaryFrustum, const int nonZeroIfFullyOutside );
			if ( nonZeroIfFullyOutside == 0 ) {
				bool occluded = false;
				unsigned frustumNum = 0;
				do {
					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( frustaPtr + frustumNum, const int zeroIfFullyInside );
					if( zeroIfFullyInside == 0 ) {
						SHOW_OCCLUDED( bounds[0], bounds[1], COLOR_RGB( 255, 192, 255 ) );
						occluded = true;
						break;
					}
				} while( ++frustumNum < occluderFrusta.size() );

				tmpIndices[numPassedEntries] = (uint16_t)entryNum;
				numPassedEntries += !occluded;
			}

			++entryNum;
		} while( entryNum < numEntries );
	} else {
		do {
			const auto *__restrict bounds = (const vec4_t *)( (const uint8_t *)entryPtrs[entryNum] + boundsFieldOffset );
			LOAD_BOX_COMPONENTS( bounds[0], bounds[1] );
			COMPUTE_RESULT_OF_FULLY_OUTSIDE_TEST_FOR_4_PLANES( primaryFrustum, const int nonZeroIfFullyOutside );

			tmpIndices[numPassedEntries] = (uint16_t)entryNum;
			numPassedEntries += ( nonZeroIfFullyOutside == 0 );

			++entryNum;
		} while( entryNum < numEntries );
	}

	return { tmpIndices, numPassedEntries };
}

}