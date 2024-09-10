/*
Copyright (C) 2024 Chasseur de bots

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

#define IMPLEMENT_cullSurfacesInVisLeavesByOccluders

#define LOAD_BOX_COMPONENTS( mins, maxs ) \
	const __m256 ymmMinsX = _mm256_broadcast_ss( &( mins )[0] ); \
	const __m256 ymmMinsY = _mm256_broadcast_ss( &( mins )[1] ); \
	const __m256 ymmMinsZ = _mm256_broadcast_ss( &( mins )[2] ); \
	const __m256 ymmMaxsX = _mm256_broadcast_ss( &( maxs )[0] ); \
	const __m256 ymmMaxsY = _mm256_broadcast_ss( &( maxs )[1] ); \
	const __m256 ymmMaxsZ = _mm256_broadcast_ss( &( maxs )[2] ); \

#define COMPUTE_RESULT_OF_FULLY_INSIDE_TEST_FOR_8_PLANES( f, zeroIfFullyInside ) \
	const __m256 ymmXBlends = _mm256_loadu_ps( (const float *)( f )->xBlendMasks ); \
	const __m256 ymmYBlends = _mm256_loadu_ps( (const float *)( f )->yBlendMasks ); \
	const __m256 ymmZBlends = _mm256_loadu_ps( (const float *)( f )->zBlendMasks ); \
	const __m256 ymmPlaneX  = _mm256_loadu_ps( ( f )->planeX ); \
	const __m256 ymmPlaneY  = _mm256_loadu_ps( ( f )->planeY ); \
	const __m256 ymmPlaneZ  = _mm256_loadu_ps( ( f )->planeZ ); \
	const __m256 ymmPlaneD  = _mm256_loadu_ps( ( f )->planeD ); \
	/* Select farthest corners */ \
	/* Note that CULLING_BLEND macro arguments for SSE 4.1 counterpart get reordered for actual application */ \
	const __m256 ymmSelectedX = _mm256_blendv_ps( ymmMinsX, ymmMaxsX, ymmXBlends ); \
	const __m256 ymmSelectedY = _mm256_blendv_ps( ymmMinsY, ymmMaxsY, ymmYBlends ); \
	const __m256 ymmSelectedZ = _mm256_blendv_ps( ymmMinsZ, ymmMaxsZ, ymmZBlends ); \
	const __m256 ymmMulX = _mm256_mul_ps( ymmSelectedX, ymmPlaneX ); \
	const __m256 ymmMulY = _mm256_mul_ps( ymmSelectedY, ymmPlaneY ); \
	const __m256 ymmMulZ = _mm256_mul_ps( ymmSelectedZ, ymmPlaneZ ); \
	const __m256 ymmDot  = _mm256_add_ps( _mm256_add_ps( ymmMulX, ymmMulY ), ymmMulZ ); \
	const __m256 ymmDiff = _mm256_sub_ps( ymmDot, ymmPlaneD ); \
	zeroIfFullyInside    = _mm256_movemask_ps( ymmDiff );

#include "frontendcull.inc"

namespace wsw::ref {

void Frontend::cullSurfacesInVisLeavesByOccludersAvx( unsigned cameraIndex,
													  std::span<const unsigned> indicesOfVisibleLeaves,
													  std::span<const Frustum> occluderFrusta,
													  MergedSurfSpan *mergedSurfSpans,
													  uint8_t *surfVisTable ) {
	_mm256_zeroupper();
	cullSurfacesInVisLeavesByOccludersArch<Avx>( cameraIndex, indicesOfVisibleLeaves, occluderFrusta, mergedSurfSpans, surfVisTable );
	_mm256_zeroupper();
}

}