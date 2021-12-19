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

auto Frustum::computeBinaryResultFor4Planes( const vec4_t mins, const vec4_t maxs ) const -> int {
	LOAD_BOX_COMPONENTS( mins, maxs )
	LOAD_COMPONENTS_OF_4_FRUSTUM_PLANES( this )

	// Select mins/maxs using masks for respective plane signbits
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( , )

	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( , )

	return _mm_movemask_ps( _mm_sub_ps( xmmDot, xmmPlaneD ) );
}

auto Frustum::computeTristateResultFor4Planes( const vec_t *mins, const vec_t *maxs ) const -> std::pair<int, int> {
	LOAD_BOX_COMPONENTS( mins, maxs )
	LOAD_COMPONENTS_OF_4_FRUSTUM_PLANES( this )

	// Select mins/maxs using masks for respective plane signbits
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _nearest, )
	// Select maxs/mins using masks for respective plane signbits (as if masks were inverted)
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _farthest, )

	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _nearest, )

	// If some bit is set, the nearest dot is < plane distance at least for some plane (separating plane in this case)
	const int nearestOutside = _mm_movemask_ps( _mm_sub_ps( xmmDot_nearest, xmmPlaneD ) );

	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _farthest, )

	// If some bit is set, the farthest dot is < plane distance at least for some plane
	const int farthestOutside = _mm_movemask_ps( _mm_sub_ps( xmmDot_farthest, xmmPlaneD ) );
	return { nearestOutside, farthestOutside };
}

auto Frustum::computeBinaryResultFor8Planes( const vec4_t mins, const vec4_t maxs ) const -> int {
	LOAD_BOX_COMPONENTS( mins, maxs )
	LOAD_COMPONENTS_OF_8_FRUSTUM_PLANES( this )

	// Select mins/maxs using masks for respective plane signbits, planes 0..3
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _03, _03 )
	// Select mins/maxs using masks for respective plane signbits, planes 4..7
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _47, _47 )

	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03, _03 )
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47, _47 )
	
	const __m128 xmmDiff_03 = _mm_sub_ps( xmmDot_03, xmmPlaneD_03 );
	const __m128 xmmDiff_47 = _mm_sub_ps( xmmDot_47, xmmPlaneD_47 );

	// We do not need an exact match with masks of AVX version, just checking for non-zero bits is sufficient
	return _mm_movemask_ps( _mm_or_ps( xmmDiff_03, xmmDiff_47 ) );
}

auto Frustum::computeTristateResultFor8Planes( const vec4_t mins, const vec4_t maxs ) const -> std::pair<int, int> {
	LOAD_BOX_COMPONENTS( mins, maxs )
	COMPUTE_TRISTATE_RESULT_FOR_8_PLANES( this, const int r1, const int r2 )
	return { r1, r2 };
}

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
	assert( indexOfPlaneToReplicate >= 4 && indexOfPlaneToReplicate < 8 );
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
	const auto *const pvs         = Mod_ClusterPVS( rf.viewcluster, rsh.worldModel );
	const unsigned numWorldLeaves = rsh.worldBrushModel->numvisleafs;
	const auto leaves             = rsh.worldBrushModel->visleafs;
	unsigned *const visibleLeaves = m_visibleLeavesBuffer.data.get();

	const auto before = Sys_Microseconds();

	unsigned numVisibleLeaves = 0;
	for( unsigned leafNum = 0; leafNum < numWorldLeaves; ++leafNum ) {
		const mleaf_t *__restrict const leaf = leaves[leafNum];
		// TODO: Handle area bits as well
		// TODO: Can we just iterate over all leaves in the cluster
		if( pvs[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) {
			// TODO: Re-add partial visibility of leaves
			const int nonZeroIfFullyOutside = m_frustum.computeBinaryResultFor4Planes( leaf->mins, leaf->maxs );
			visibleLeaves[numVisibleLeaves] = leafNum;
			numVisibleLeaves += ( nonZeroIfFullyOutside == 0 );
		}
	}

	Com_Printf( "Culling of leaves by the primary frustum took %d micros\n", (int)( Sys_Microseconds() - before ) );

	return { visibleLeaves, visibleLeaves + numVisibleLeaves };
}

void Frontend::showOccluderSurface( const msurface_t *surface ) {
	const vec4_t *const __restrict allVertices = surface->mesh.xyzArray;
	const uint8_t *const __restrict polyIndices = surface->occluderPolyIndices;
	const unsigned numSurfVertices = surface->numOccluderPolyIndices;
	assert( numSurfVertices >= 4 && numSurfVertices <= 7 );

	vec3_t surfCenter;
	VectorSubtract( surface->maxs, surface->mins, surfCenter );
	VectorMA( surface->mins, 0.5f, surfCenter, surfCenter );
	for( unsigned vertIndex = 0; vertIndex < numSurfVertices; ++vertIndex ) {
		const float *const v1 = allVertices[polyIndices[vertIndex + 0]];
		const float *const v2 = allVertices[polyIndices[( vertIndex + 1 != numSurfVertices ) ? vertIndex + 1 : 0]];
		//addDebugLine( v1, v2, COLOR_RGB( 192, 192, 96 ) );
	}
}

auto Frontend::collectVisibleOccluders( std::span<const unsigned> visibleLeaves ) -> std::span<const SortedOccluder> {
	const auto before = Sys_Microseconds();

	const float *const __restrict viewOrigin = m_state.viewOrigin;
	const float *const __restrict viewAxis   = m_state.viewAxis;

	SortedOccluder *const visibleOccluders = m_visibleOccludersBuffer.data.get();
	const unsigned occludersSelectionFrame = m_occludersSelectionFrame;

	const auto worldLeaves   = rsh.worldBrushModel->visleafs;
	const auto worldSurfaces = rsh.worldBrushModel->surfaces;

	unsigned numVisibleOccluders = 0;
	for( const unsigned leafNum: visibleLeaves ) {
		const mleaf_t *const __restrict leaf = worldLeaves[leafNum];

		// TODO: Separate occluders and surfaces
		for( unsigned i = 0; i < leaf->numOccluderSurfaces; ++i ) {
			const unsigned surfNum  = leaf->occluderSurfaces[i];
			const msurface_s *surf  = worldSurfaces + leaf->occluderSurfaces[i];
			if( surf->occludersSelectionFrame == occludersSelectionFrame ) {
				continue;
			}

			surf->occludersSelectionFrame = occludersSelectionFrame;

			const float absViewDot  = std::abs( DotProduct( viewAxis, surf->plane ) );
			if( absViewDot < 0.3f ) {
				continue;
			}

			const auto [nonZeroIfFullyOutside, nonZeroIfPartiallyOutside] =
				m_frustum.computeTristateResultFor4Planes( surf->mins, surf->maxs );

			// Fully outside the primary frustum
			if( nonZeroIfFullyOutside ) {
				continue;
			}

			// TODO: Store as a field
			const vec3_t surfCenter {
				0.5f * ( surf->mins[0] + surf->maxs[0] ),
				0.5f * ( surf->mins[1] + surf->maxs[1] ),
				0.5f * ( surf->mins[2] + surf->maxs[2] )
			};

			// Partially visible
			if( nonZeroIfPartiallyOutside ) {
				vec3_t toSurf;
				// Hacks, hacks, hacks TODO: Add and use the nearest primary frustum plane?
				VectorSubtract( surfCenter, viewOrigin, toSurf );
				if( DotProduct( viewAxis, toSurf ) < 0 ) {
					continue;
				}
			}

			// TODO: Try using a distance to the poly?
			const float score = Q_RSqrt( DistanceSquared( viewOrigin, surfCenter ) ) * absViewDot;
			visibleOccluders[numVisibleOccluders++] = { surfNum, score };
		}
	}

	// TODO: Don't sort, build a heap instead?
	std::sort( visibleOccluders, visibleOccluders + numVisibleOccluders );

	Com_Printf( "The initial occluders selection took %d micros\n", (int)( Sys_Microseconds() - before ) );

	return { visibleOccluders, visibleOccluders + numVisibleOccluders };
}

auto Frontend::buildFrustaOfOccluders( std::span<const SortedOccluder> sortedOccluders ) -> std::span<const Frustum> {
	const float *const viewOrigin     = m_state.viewOrigin;
	const auto *const worldSurfaces   = rsh.worldBrushModel->surfaces;
	Frustum *const occluderFrusta     = m_occluderFrusta;
	const unsigned maxOccluders       = std::min<unsigned>( sortedOccluders.size(), std::size( m_occluderFrusta ) );
	constexpr float selfOcclusionBias = 4.0f;

	const auto before = Sys_Microseconds();

	bool hadCulledFrusta = false;
	// Note: We don't process more occluders due to performance and not memory capacity reasons.
	// Best occluders come first so they should make their way into the final result.
	alignas( 16 )bool isCulledByOtherTable[std::size( m_occluderFrusta )];
	std::memset( isCulledByOtherTable, 0, sizeof( bool ) * maxOccluders );

	// Note: An outer loop over all surfaces would have been allowed to avoid redundant component shuffles
	// but this approach requires building all frusta prior to that, which is more expensive.

	for( unsigned occluderNum = 0; occluderNum < maxOccluders; ++occluderNum ) {
		if( isCulledByOtherTable[occluderNum] ) {
			continue;
		}

		const msurface_t *const __restrict surface  = worldSurfaces + sortedOccluders[occluderNum].surfNum;
		const vec4_t *const __restrict allVertices  = surface->mesh.xyzArray;
		const uint8_t *const __restrict polyIndices = surface->occluderPolyIndices;
		const unsigned numSurfVertices              = surface->numOccluderPolyIndices;

		assert( numSurfVertices >= 4 && numSurfVertices <= 7 );

		Frustum *const __restrict f = &occluderFrusta[occluderNum];

		vec3_t surfCenter;
		VectorSubtract( surface->maxs, surface->mins, surfCenter );
		VectorMA( surface->mins, 0.5f, surfCenter, surfCenter );
		for( unsigned vertIndex = 0; vertIndex < numSurfVertices; ++vertIndex ) {
			const float *const v1 = allVertices[polyIndices[vertIndex + 0]];
			const float *const v2 = allVertices[polyIndices[( vertIndex + 1 != numSurfVertices ) ? vertIndex + 1 : 0]];

			cplane_t plane;
			// TODO: Inline?
			PlaneFromPoints( v1, v2, viewOrigin, &plane );

			// Make the normal point inside the frustum
			// TODO: Build in a such order that we do not have to check this
			if( DotProduct( plane.normal, surfCenter ) - plane.dist < 0 ) {
				VectorNegate( plane.normal, plane.normal );
				plane.dist = -plane.dist;
			}

			f->setPlaneComponentsAtIndex( vertIndex, plane.normal, plane.dist );
		}

		vec4_t cappingPlane;
		Vector4Copy( surface->plane, cappingPlane );
		// Don't let the surface occlude itself
		if( DotProduct( cappingPlane, viewOrigin ) - cappingPlane[3] > 0 ) {
			Vector4Negate( cappingPlane, cappingPlane );
			cappingPlane[3] += selfOcclusionBias;
		} else {
			cappingPlane[3] -= selfOcclusionBias;
		}

		f->setPlaneComponentsAtIndex( numSurfVertices, cappingPlane, cappingPlane[3] );
		f->fillComponentTails( numSurfVertices );

		// We have built the frustum.
		// Cull all other frusta by it.
		// Note that the "culled-by" relation is not symmetrical so we have to check from the beginning.

		for( unsigned otherOccluderNum = 0; otherOccluderNum < maxOccluders; ++otherOccluderNum ) {
			if( otherOccluderNum != occluderNum ) [[likely]] {
				if( !isCulledByOtherTable[otherOccluderNum] ) {
					const msurface_t *__restrict surf = worldSurfaces + sortedOccluders[otherOccluderNum].surfNum;
					LOAD_BOX_COMPONENTS( surf->occluderPolyMins, surf->occluderPolyMaxs );
					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, const int zeroIfFullyInside );
					if( zeroIfFullyInside == 0 ) {
						isCulledByOtherTable[otherOccluderNum] = true;
						hadCulledFrusta = true;
					}
				}
			}
		}
	}

	unsigned numSelectedOccluders = maxOccluders;
	if( hadCulledFrusta ) {
		unsigned numPresevedOccluders = 0;
		for( unsigned occluderNum = 0; occluderNum < maxOccluders; ++occluderNum ) {
			if( !isCulledByOtherTable[occluderNum] ) {
				// TODO: This is a memcpy() call, make the compactification more efficient or use a manual SIMD copy
				occluderFrusta[numPresevedOccluders++] = occluderFrusta[occluderNum];
			}
		}
		numSelectedOccluders = numPresevedOccluders;
	}

	Com_Printf( "Frusta setup took %d micros. Selected %d/%d occluders\n", (int)( Sys_Microseconds() - before ), numSelectedOccluders, maxOccluders );

#if 1
	vec3_t pointInFrontOfView;
	VectorMA( viewOrigin, 8.0, &m_state.viewAxis[0], pointInFrontOfView );

	// Show preserved frusta
	for( unsigned occluderNum = 0; occluderNum < maxOccluders; ++occluderNum ) {
		if( isCulledByOtherTable[occluderNum] ) {
			continue;
		}

		const msurface_t *const __restrict surface  = worldSurfaces + sortedOccluders[occluderNum].surfNum;
		const vec4_t *const __restrict allVertices  = surface->mesh.xyzArray;
		const uint8_t *const __restrict polyIndices = surface->occluderPolyIndices;
		const unsigned numSurfVertices              = surface->numOccluderPolyIndices;

		//addDebugLine( surface->occluderPolyMins, surface->occluderPolyMaxs, COLOR_RGB( 0, 128, 255 ) );

		vec3_t surfCenter;
		VectorAvg( surface->mins, surface->maxs, surfCenter );
		for( unsigned vertIndex = 0; vertIndex < numSurfVertices; ++vertIndex ) {
			const float *const v1 = allVertices[polyIndices[vertIndex + 0]];
			const float *const v2 = allVertices[polyIndices[( vertIndex + 1 != numSurfVertices ) ? vertIndex + 1 : 0]];

			addDebugLine( v1, pointInFrontOfView );
			addDebugLine( v1, v2 );

			/*
			cplane_t plane;
			// TODO: Inline?
			PlaneFromPoints( v1, v2, viewOrigin, &plane );

			// Make the normal point inside the frustum
			// TODO: Build in a such order that we do not have to check this
			if( DotProduct( plane.normal, surfCenter ) - plane.dist < 0 ) {
				VectorNegate( plane.normal, plane.normal );
				plane.dist = -plane.dist;
			}

			vec3_t midpointOfEdge, normalPoint;
			VectorAvg( v1, v2, midpointOfEdge );
			VectorMA( midpointOfEdge, 8.0f, plane.normal, normalPoint );
			addDebugLine( midpointOfEdge, normalPoint );*/
		}

		vec4_t cappingPlane;
		Vector4Copy( surface->plane, cappingPlane );
		// Don't let the surface occlude itself
		if( DotProduct( cappingPlane, viewOrigin ) - cappingPlane[3] > 0 ) {
			Vector4Negate( cappingPlane, cappingPlane );
			cappingPlane[3] += selfOcclusionBias;
		} else {
			cappingPlane[3] -= selfOcclusionBias;
		}

		vec3_t cappingPlanePoint;
		VectorMA( surfCenter, 32.0f, cappingPlane, cappingPlanePoint );
		addDebugLine( surfCenter, cappingPlanePoint );
	}
#endif

	return { occluderFrusta, occluderFrusta + numSelectedOccluders };
}

auto Frontend::cullLeavesByOccluders( std::span<const unsigned> indicesOfLeaves,
									  std::span<const Frustum> occluderFrusta )
									  -> std::pair<std::span<const unsigned>, std::span<const unsigned>> {
	unsigned *const partiallyVisibleLeaves = m_occluderPassPartiallyVisibleLeavesBuffer.data.get();
	unsigned *const fullyVisibleLeaves     = m_occluderPassPartiallyVisibleLeavesBuffer.data.get();
	const unsigned numOccluders = occluderFrusta.size();
	const auto leaves           = rsh.worldBrushModel->visleafs;
	unsigned numPartiallyVisibleLeaves = 0;
	unsigned numFullyVisibleLeaves     = 0;

	const auto before = Sys_Microseconds();

	for( const unsigned leafIndex: indicesOfLeaves ) {
		const mleaf_t *const leaf = leaves[leafIndex];
		int wasPartiallyInside   = 0;
		int wasFullyInside       = 0;
		unsigned frustumNum      = 0;

		LOAD_BOX_COMPONENTS( leaf->mins, leaf->maxs );

		do {
			const Frustum *const __restrict f = &occluderFrusta[frustumNum];

			COMPUTE_TRISTATE_RESULT_FOR_8_PLANES( f, const int nonZeroIfOutside, const int nonZeroIfPartiallyOutside )

			if( !( nonZeroIfOutside | nonZeroIfPartiallyOutside ) ) {
				wasFullyInside = true;
				addDebugLine( leaf->mins, leaf->maxs, COLOR_RGB( 255, 0, 255 ) );
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

	Com_Printf( "Culling of leaves by %d occluders took %d micros\n", numOccluders, (int)( Sys_Microseconds() - before ) );

	return { { fullyVisibleLeaves, numFullyVisibleLeaves }, { partiallyVisibleLeaves, numPartiallyVisibleLeaves } };
}

void Frontend::cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfVisibleLeaves,
												   std::span<const Frustum> occluderFrusta,
												   int8_t *surfVisibilityTable ) {
	assert( !occluderFrusta.empty() );

	const msurface_t *const surfaces = rsh.worldBrushModel->surfaces;
	const auto leaves = rsh.worldBrushModel->visleafs;

	const auto before = Sys_Microseconds();

	// Cull individual surfaces by up to 16 best frusta
	const unsigned numBestOccluders = std::min<unsigned>( 16, occluderFrusta.size() );
	for( const unsigned leafNum: indicesOfVisibleLeaves ) {
		const mleaf_s *const leaf             = leaves[leafNum];
		const unsigned *const leafSurfaceNums = leaf->visSurfaces;
		const unsigned numLeafSurfaces        = leaf->numVisSurfaces;

		for( unsigned surfIndex = 0; surfIndex < numLeafSurfaces; ++surfIndex ) {
			const unsigned surfNum = leafSurfaceNums[surfIndex];
			if( surfVisibilityTable[surfNum] < 0 ) {
				const msurface_t *__restrict surf = surfaces + leafSurfaceNums[surfIndex];

				LOAD_BOX_COMPONENTS( surf->mins, surf->maxs );

				bool surfVisible = true;
				unsigned frustumNum = 0;
				do {
					const Frustum *__restrict f = &occluderFrusta[frustumNum];

					COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, const int zeroIfFullyInside )

					if( !zeroIfFullyInside ) [[unlikely]] {
						surfVisible = false;
						addDebugLine( surf->mins, surf->maxs, COLOR_RGB( 192, 0, 0 ) );
						break;
					}
				} while( ++frustumNum != numBestOccluders );

				surfVisibilityTable[surfNum] = (int8_t)surfVisible;
			}
		}
	}

	Com_Printf( "Culling of surfaces by %d occluders took %d micros\n", (int)numBestOccluders, (int)( Sys_Microseconds() - before ) );
}

void Frontend::markSurfacesOfLeavesAsVisible( std::span<const unsigned> indicesOfLeaves,
											  int8_t *surfVisibilityTable ) {
	const auto leaves = rsh.worldBrushModel->visleafs;
	for( const unsigned leafNum: indicesOfLeaves ) {
		const auto *__restrict leaf = leaves[leafNum];
		for( unsigned i = 0; i < leaf->numVisSurfaces; ++i ) {
			surfVisibilityTable[leaf->visSurfaces[i]] = (int8_t)true;
		}
	}
}

}