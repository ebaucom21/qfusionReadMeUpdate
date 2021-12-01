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
	const __m128 xmmXBlends_03 = _mm_load_ps( (const float *)xBlendMasks ); \
	const __m128 xmmYBlends_03 = _mm_load_ps( (const float *)yBlendMasks ); \
	const __m128 xmmZBlends_03 = _mm_load_ps( (const float *)zBlendMasks ); \
	const __m128 xmmXBlends_47 = _mm_load_ps( ( (const float *)xBlendMasks ) + 4 ); \
	const __m128 xmmYBlends_47 = _mm_load_ps( ( (const float *)yBlendMasks ) + 4 ); \
	const __m128 xmmZBlends_47 = _mm_load_ps( ( (const float *)zBlendMasks ) + 4 ); \
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
	LOAD_COMPONENTS_OF_8_FRUSTUM_PLANES( this )

	// Select mins/maxs using masks for respective plane signbits, planes 0..3
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _03_nearest, _03 )
	// Select mins/maxs using masks for respective plane signbits, planes 4..7
	BLEND_COMPONENT_MINS_AND_MAXS_USING_MASKS( _47_nearest, _47 )

	// The same but as if masks were inverted
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _03_farthest, _03 )
	BLEND_COMPONENT_MAXS_AND_MINS_USING_MASKS( _47_farthest, _47 )

	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03_nearest, _03 )
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47_nearest, _47 )
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _03_farthest, _03 )
	COMPUTE_DOT_PRODUCT_OF_BOX_COMPONENTS_AND_PLANES( _47_farthest, _47 )

	const __m128 xmmDiff_03_nearest = _mm_sub_ps( xmmDot_03_nearest, xmmPlaneD_03 );
	const __m128 xmmDiff_47_nearest = _mm_sub_ps( xmmDot_47_nearest, xmmPlaneD_47 );
	// We do not need an exact match with masks of AVX version, just checking for non-zero bits is sufficient
	const __m128 xmmDiff_or_nearest = _mm_or_ps( xmmDiff_03_nearest, xmmDiff_47_nearest );

	const __m128 xmmDiff_03_farthest = _mm_sub_ps( xmmDot_03_farthest, xmmPlaneD_03 );
	const __m128 xmmDiff_47_farthest = _mm_sub_ps( xmmDot_47_farthest, xmmPlaneD_47 );
	const __m128 xmmDiff_or_farthest = _mm_or_ps( xmmDiff_03_farthest, xmmDiff_47_farthest );

	return { _mm_movemask_ps( xmmDiff_or_nearest ), _mm_movemask_ps( xmmDiff_or_farthest ) };
}

#undef wsw_blend

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

void Frustum::fillComponentTails( unsigned numPlanesSoFar ) {
	// Sanity check
	assert( numPlanesSoFar >= 5 );
	for( unsigned i = numPlanesSoFar; i < 8; ++i ) {
		planeX[i] = planeX[numPlanesSoFar];
		planeY[i] = planeY[numPlanesSoFar];
		planeZ[i] = planeZ[numPlanesSoFar];
		planeD[i] = planeD[numPlanesSoFar];
		xBlendMasks[i] = xBlendMasks[numPlanesSoFar];
		yBlendMasks[i] = yBlendMasks[numPlanesSoFar];
		zBlendMasks[i] = zBlendMasks[numPlanesSoFar];
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

static std::pair<int, int> computeTristateResultNaive( const Frustum *f, const float *mins, const float *maxs ) {
	bool full = false;
	bool partial = false;
	for( unsigned i = 0; i < 4; ++i ) {
		cplane_t plane;
		plane.normal[0] = f->planeX[i];
		plane.normal[1] = f->planeY[i];
		plane.normal[2] = f->planeZ[i];
		plane.dist = f->planeD[i];
		CategorizePlane( &plane );
		const int side = BoxOnPlaneSide( mins, maxs, &plane );
		if( side == 2 ) {
			full = true;
		}
		if( side != 1 ) {
			partial = true;
		}
	}
	return { full, partial };
}

auto Frontend::collectVisibleWorldLeaves() -> std::span<const unsigned> {
	const auto *const pvs = Mod_ClusterPVS( rf.viewcluster, rsh.worldModel );
	const unsigned numWorldLeaves = rsh.worldBrushModel->numvisleafs;
	const auto leaves = rsh.worldBrushModel->visleafs;

	m_visibleLeavesBuffer.reserve( numWorldLeaves );
	unsigned *const __restrict visibleLeaves = m_visibleLeavesBuffer.data.get();

	// TODO: Re-add partial visibility of leaves???

	unsigned numPartiallyVisibleLeaves = 0;
	unsigned numVisibleLeaves = 0;
	// TODO: Handle area bits as well
	// TODO: Can we just iterate over all leaves in the cluster
	for( unsigned i = 0; i < numWorldLeaves; ++i ) {
		const mleaf_t *__restrict const leaf = leaves[i];
		if( pvs[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) {
			const int r = m_frustum.computeBinaryResultFor4Planes( leaf->mins, leaf->maxs );
			if( r == 0 ) {
				// TODO: Branchless
				visibleLeaves[numVisibleLeaves++] = i;
			}

			// Just checking the match
			const auto [r1Simd, r2Simd] = m_frustum.computeTristateResultFor4Planes( leaf->mins, leaf->maxs );
			if(( r != 0 ) != ( r1Simd != 0 ) ) {
				abort();
			}

			const auto [r1Naive, r2Naive] = computeTristateResultNaive( &m_frustum, leaf->mins, leaf->maxs );
			if( ( ( r1Simd != 0 ) != ( r1Naive != 0 ) ) || ( ( r2Simd != 0 ) != ( r2Naive != 0 ) ) ) {
				Com_Printf( "R1 simd,naive: %d %d\n", r1Simd, r1Naive );
				Com_Printf( "R2 simd,naive: %d %d\n", r2Simd, r2Naive );
				abort();
			}

			if( !r1Simd ) {
				if( r2Simd ) {
					numPartiallyVisibleLeaves++;
				}
			}
		}
	}

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
		addDebugLine( v1, v2, COLOR_RGB( 192, 192, 96 ) );
	}
}

auto Frontend::collectVisibleOccluders( std::span<const unsigned> visibleLeaves ) -> std::span<const unsigned> {
	// TODO: Separate occluders/surfaces!!!!!
	m_visibleOccluderSurfacesBuffer.reserve( rsh.worldBrushModel->numModelSurfaces );
	unsigned *const __restrict visibleOccluderSurfaces = m_visibleOccluderSurfacesBuffer.data.get();
	const auto worldLeaves = rsh.worldBrushModel->visleafs;
	const auto worldSurfaces = rsh.worldBrushModel->surfaces;

	unsigned numVisibleOccluders = 0;
	for( const unsigned leafNum: visibleLeaves ) {
		const mleaf_t *const __restrict leaf = worldLeaves[leafNum];
		// TODO: Separate occluders/surfaces!!!!!
		for( unsigned i = 0; i < leaf->numOccluderSurfaces; ++i ) {
			// TODO: Cull against the primary frustum if the leaf visibility is partial
			visibleOccluderSurfaces[numVisibleOccluders++] = leaf->occluderSurfaces[i];
			msurface_s *surf = worldSurfaces + leaf->occluderSurfaces[i];
			showOccluderSurface( surf );
		}
	}

	return { visibleOccluderSurfaces, visibleOccluderSurfaces + numVisibleOccluders };
}

// TODO: Merge with buildFrustum and sort by a largest occluder fov?
auto Frontend::selectBestOccluders( std::span<const unsigned> visibleOccluders ) -> std::span<const msurface_t *> {
	const size_t numSelectedOccluders = std::min( visibleOccluders.size(), std::size( m_bestOccludersBuffer ) );
	const auto worldSurfaces = rsh.worldBrushModel->surfaces;
	for( size_t i = 0; i < numSelectedOccluders; ++i ) {
		m_bestOccludersBuffer[i] = worldSurfaces + visibleOccluders[i];
	}
	return { m_bestOccludersBuffer, m_bestOccludersBuffer + numSelectedOccluders };
}

auto Frontend::buildFrustaOfOccluders( std::span<const msurface_t *> bestOccluders ) -> std::span<const Frustum> {
	const float *const viewOrigin = m_state.viewOrigin;
	const unsigned numBestOccluders = bestOccluders.size();

	vec3_t pointInFrontOfView;
	VectorMA( viewOrigin, 8.0, &m_state.viewAxis[0], pointInFrontOfView );

	for( unsigned occluderNum = 0; occluderNum < numBestOccluders; ++occluderNum ) {
		const msurface_t *const __restrict surface = bestOccluders[occluderNum];
		const vec4_t *const __restrict allVertices = surface->mesh.xyzArray;
		const uint8_t *const __restrict polyIndices = surface->occluderPolyIndices;
		const unsigned numSurfVertices = surface->numOccluderPolyIndices;
		assert( numSurfVertices >= 4 && numSurfVertices <= 7 );
		Frustum *__restrict f = &m_occluderFrusta[occluderNum];

		vec3_t surfCenter;
		VectorSubtract( surface->maxs, surface->mins, surfCenter );
		VectorMA( surface->mins, 0.5f, surfCenter, surfCenter );
		for( unsigned vertIndex = 0; vertIndex < numSurfVertices; ++vertIndex ) {
			const float *const v1 = allVertices[polyIndices[vertIndex + 0]];
			const float *const v2 = allVertices[polyIndices[( vertIndex + 1 != numSurfVertices ) ? vertIndex + 1 : 0]];

			addDebugLine( v1, pointInFrontOfView );
			addDebugLine( v1, v2 );

			cplane_t plane;
			// TODO: Inline?
			PlaneFromPoints( v1, v2, viewOrigin, &plane );

			// Make the normal point inside the frustum
			// TODO: Build in a such order that we do not have to check this
			if( DotProduct( plane.normal, surfCenter ) - plane.dist < 0 ) {
				VectorNegate( plane.normal, plane.normal );
				plane.dist = -plane.dist;
			}

			const vec3_t midpointOfEdge {
				0.5f * (v1[0] + v2[0]), 0.5f * (v1[1] + v2[1]), 0.5f * (v1[2] + v2[2] )
			};
			vec3_t normalPoint;
			VectorMA( midpointOfEdge, 8.0f, plane.normal, normalPoint );
			addDebugLine( midpointOfEdge, normalPoint );

			f->setPlaneComponentsAtIndex( vertIndex, plane.normal, plane.dist );
		}

		vec4_t cappingPlane;
		Vector4Copy( surface->plane, cappingPlane );
		// TODO: Check correctness for the 8-plane setup
		if( DotProduct( cappingPlane, viewOrigin ) - cappingPlane[3] > 0 ) {
			Vector4Negate( cappingPlane, cappingPlane );
		}

		vec3_t cappingPlanePoint;
		VectorMA( surfCenter, 32.0f, cappingPlane, cappingPlanePoint );
		addDebugLine( surfCenter, cappingPlanePoint );

		f->setPlaneComponentsAtIndex( numSurfVertices, cappingPlane, cappingPlane[3] );
		f->fillComponentTails( numSurfVertices + 1 );
	}

	return { m_occluderFrusta, m_occluderFrusta + numBestOccluders };
}

void Frontend::cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfVisibleLeaves,
												   std::span<const Frustum> occluderFrusta ) {
	const msurface_t *const surfaces = rsh.worldBrushModel->surfaces;
	const auto leaves = rsh.worldBrushModel->visleafs;

	const unsigned numWorldSurfaces = rsh.worldBrushModel->numModelSurfaces;
	m_surfVisibilityTable.reserve( numWorldSurfaces );

	bool *const __restrict surfVisibilityTable = m_surfVisibilityTable.data.get();
	std::memset( surfVisibilityTable, 0, numWorldSurfaces * sizeof( bool ) );

	for( const unsigned leafIndex: indicesOfVisibleLeaves ) {
		const mleaf_t *const __restrict leaf = leaves[leafIndex];

		bool leafVisible = true;
		bool leafOverlapsWithFrustum = false;
		// TODO: Unroll and inline for a known size
		for( const Frustum &__restrict f: occluderFrusta ) {
			const auto [r1, r2] = f.computeTristateResultFor8Planes( leaf->mins, leaf->maxs );
			if( !r1 ) {
				// If completely inside
				if( !r2 ) {
					addDebugLine( leaf->mins, leaf->maxs, COLOR_RGB( 192, 0, 192 ) );
					leafVisible = false;
				} else {
					leafOverlapsWithFrustum = true;
				}
				break;
			}
		}

		if( leafVisible ) {
			const unsigned *leafSurfaceNums = leaf->visSurfaces;
			const unsigned numLeafSurfaces  = leaf->numVisSurfaces;
			if( !leafOverlapsWithFrustum ) {
				// TODO... AVX scatter (is it a thing?)
				// TODO... Check whether we can avoid filling this table
				for( unsigned surfIndex = 0; surfIndex < numLeafSurfaces; ++surfIndex ) {
					const unsigned surfNum = leafSurfaceNums[surfIndex];
					surfVisibilityTable[surfNum] = true;
				}
			} else {
				for( unsigned surfIndex = 0; surfIndex < numLeafSurfaces; ++surfIndex ) {
					const unsigned surfNum = leafSurfaceNums[surfIndex];
					const msurface_t *__restrict surf = surfaces + surfNum;
					bool surfVisible = true;
					for( const Frustum &__restrict f: occluderFrusta ) {
						const auto [r1, r2] = f.computeTristateResultFor8Planes( surf->mins, surf->maxs );
						if( !r1 && !r2 ) {
							surfVisible = false;
							addDebugLine( surf->mins, surf->maxs, COLOR_RGB( 192, 0, 0 ) );
							break;
						}
					}
					surfVisibilityTable[surfNum] = surfVisible;
					// TODO: Write to the list of surfaces as well
				}
			}
		}
	}
}

}