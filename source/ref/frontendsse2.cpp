/*
Copyright (C) 2007 Victor Luchits
Copyright (C) 2023 Chasseur de bots

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

#include "local.h"
#include "frontend.h"
#include "frontendcull.inc"

namespace wsw::ref {

auto Frontend::collectVisibleWorldLeavesSse2() -> std::span<const unsigned> {
	return collectVisibleWorldLeavesArch<Sse2>();
}

auto Frontend::collectVisibleOccludersSse2() -> std::span<const SortedOccluder> {
	return collectVisibleOccludersArch<Sse2>();
}

auto Frontend::buildFrustaOfOccludersSse2( std::span<const SortedOccluder> sortedOccluders ) -> std::span<const Frustum> {
	return buildFrustaOfOccludersArch<Sse2>( sortedOccluders );
}

auto Frontend::cullLeavesByOccludersSse2( std::span<const unsigned> indicesOfLeaves, std::span<const Frustum> occluderFrusta )
	-> std::pair<std::span<const unsigned>, std::span<const unsigned>> {
	return cullLeavesByOccludersArch<Sse2>( indicesOfLeaves, occluderFrusta );
}

void Frontend::cullSurfacesInVisLeavesByOccludersSse2( std::span<const unsigned> indicesOfLeaves,
													   std::span<const Frustum> occluderFrusta,
													   MergedSurfSpan *mergedSurfSpans ) {
	return cullSurfacesInVisLeavesByOccludersArch<Sse2>( indicesOfLeaves, occluderFrusta, mergedSurfSpans );
}

auto Frontend::cullEntriesWithBoundsSse2( const void *entries, unsigned numEntries, unsigned boundsFieldOffset,
										  unsigned strideInBytes, const Frustum *__restrict primaryFrustum,
										  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices )
	-> std::span<const uint16_t> {
	return cullEntriesWithBoundsArch<Sse2>( entries, numEntries, boundsFieldOffset, strideInBytes,
											primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullEntryPtrsWithBoundsSse2( const void **entryPtrs, unsigned numEntries, unsigned boundsFieldOffset,
											const Frustum *__restrict primaryFrustum, std::span<const Frustum> occluderFrusta,
											uint16_t *tmpIndices )
	-> std::span<const uint16_t> {
	return cullEntryPtrsWithBoundsArch<Sse2>( entryPtrs, numEntries, boundsFieldOffset,
											  primaryFrustum, occluderFrusta, tmpIndices );
}

}