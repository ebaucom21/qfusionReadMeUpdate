/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2007 Victor Luchits

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

// r_model.c -- model loading and caching

#include "local.h"
#include "iqm.h"
#include "../qcommon/qcommon.h"

#include <algorithm>
#include <numeric>
#include <span>

typedef struct {
	unsigned number;
	msurface_t *surf;
} msortedSurface_t;

void Mod_LoadAliasMD3Model( model_t *mod, model_t *parent, void *buffer, bspFormatDesc_t *unused );
void Mod_LoadSkeletalModel( model_t *mod, model_t *parent, void *buffer, bspFormatDesc_t *unused );
void Mod_LoadQ3BrushModel( model_t *mod, model_t *parent, void *buffer, bspFormatDesc_t *format );

void Mod_DestroyAliasMD3Model( maliasmodel_t *model );
void Mod_DestroySkeletalModel( mskmodel_t *model );
void Mod_DestroyQ3BrushModel( mbrushmodel_t *model );

static void R_InitMapConfig( const char *model );
static void R_FinishMapConfig( const model_t *mod );

static uint8_t mod_novis[MAX_MAP_LEAFS / 8];

#define MAX_MOD_KNOWN   512 * MOD_MAX_LODS
static model_t mod_known[MAX_MOD_KNOWN];
static int mod_numknown;
static bool mod_isworldmodel;
model_t *r_prevworldmodel;
static mapconfig_t *mod_mapConfigs;

static const modelFormatDescr_t mod_supportedformats[] =
{
	// Quake III Arena .md3 models
	{ IDMD3HEADER, 4, nullptr, MOD_MAX_LODS, ( const modelLoader_t )Mod_LoadAliasMD3Model },

	// Skeletal models
	{ IQM_MAGIC, sizeof( IQM_MAGIC ), nullptr, MOD_MAX_LODS, ( const modelLoader_t )Mod_LoadSkeletalModel },

	// Q3-alike .bsp models
	{ "*", 4, q3BSPFormats, 0, ( const modelLoader_t )Mod_LoadQ3BrushModel },

	// trailing nullptr
	{ nullptr, 0, nullptr, 0, nullptr }
};

mleaf_t *Mod_PointInLeaf( vec3_t p, model_t *model ) {
	if( !model ) {
		Com_Error( ERR_DROP, "Mod_PointInLeaf: bad model" );
	}

	auto *const bmodel = ( mbrushmodel_t * )model->extradata;
	if( !bmodel ) {
		Com_Error( ERR_DROP, "Mod_PointInLeaf: bad model" );
	}

	if( !bmodel->nodes ) {
		Com_Error( ERR_DROP, "Mod_PointInLeaf: bad model" );
	}

	const auto *__restrict nodes = bmodel->nodes;

	int nodeNum = 0;
	do {
		const auto *__restrict node = nodes + nodeNum;
		const auto *__restrict plane = &node->plane;
		nodeNum = node->children[PlaneDiff( p, plane ) < 0];
	} while( nodeNum >= 0 );

	return bmodel->leafs + ( -1 - nodeNum );
}

static inline uint8_t *Mod_ClusterVS( int cluster, dvis_t *vis ) {
	if( cluster < 0 || !vis ) {
		return mod_novis;
	}
	return ( (uint8_t *)vis->data + cluster * vis->rowsize );
}

uint8_t *Mod_ClusterPVS( int cluster, model_t *model ) {
	return Mod_ClusterVS( cluster, ( ( mbrushmodel_t * )model->extradata )->pvs );
}

class IndexPatternsCache {
public:
	// We use types of different size for every N to avoid wasting memory on unused indices
	template <unsigned N>
	struct Combination { uint8_t indices[N]; };

	template <unsigned N>
	struct Permutation { uint8_t indices[N]; };

	template <unsigned N>
	[[nodiscard]]
	auto getCombinationsForBound( unsigned bound ) const -> std::span<const Combination<N>> {
		if constexpr( N == 4 ) {
			return { m_4ElemCombinations.data(), m_max4ElemCombinationsForBound[bound] };
		} else if constexpr( N == 5 ) {
			return { m_5ElemCombinations.data(), m_max5ElemCombinationsForBound[bound] };
		} else if constexpr( N == 6 ) {
			return { m_6ElemCombinations.data(), m_max6ElemCombinationsForBound[bound] };
		} else if constexpr( N == 7 ) {
			return { m_7ElemCombinations.data(), m_max7ElemCombinationsForBound[bound] };
		} else {
			return {};
		}
	}

	template <unsigned N>
	[[nodiscard]]
	auto getPermutations() const -> std::span<const Permutation<N>> {
		if constexpr( N == 4 ) {
			return m_4ElemPermutations;
		} else if constexpr( N == 5 ) {
			return m_5ElemPermutations;
		} else if constexpr( N == 6 ) {
			return m_6ElemPermutations;
		} else if constexpr( N == 7 ) {
			return m_7ElemPermutations;
		} else {
			return {};
		}
	}

	IndexPatternsCache();
private:
	wsw::Vector<Combination<4>> m_4ElemCombinations;
	wsw::Vector<Combination<5>> m_5ElemCombinations;
	wsw::Vector<Combination<6>> m_6ElemCombinations;
	wsw::Vector<Combination<7>> m_7ElemCombinations;

	wsw::StaticVector<unsigned, 24> m_max4ElemCombinationsForBound;
	wsw::StaticVector<unsigned, 24> m_max5ElemCombinationsForBound;
	wsw::StaticVector<unsigned, 24> m_max6ElemCombinationsForBound;
	wsw::StaticVector<unsigned, 24> m_max7ElemCombinationsForBound;

	wsw::Vector<Permutation<4>> m_4ElemPermutations;
	wsw::Vector<Permutation<5>> m_5ElemPermutations;
	wsw::Vector<Permutation<6>> m_6ElemPermutations;
	wsw::Vector<Permutation<7>> m_7ElemPermutations;

	template <unsigned N>
	void addToCombinations( wsw::Vector<Combination<N>> &v, uint32_t n ) {
		Combination<N> combination {};
		unsigned numIndices = 0;
		for( unsigned i = 0; i < 32; ++i ) {
			if( n & ( 1 << i ) ) {
				combination.indices[numIndices] = i;
				numIndices++;
				if( numIndices == N ) {
					break;
				}
			}
		}
		v.push_back( combination );
	}

	template <unsigned N>
	void initPermutations( wsw::Vector<Permutation<N>> &v ) {
		Permutation<N> permutation {};
		std::iota( permutation.indices, permutation.indices + N, 0 );
		do {
			// This makes a deep copy
			v.push_back( permutation );
		} while( std::next_permutation( permutation.indices, permutation.indices + N ) );
		v.shrink_to_fit();
	}
};

#ifdef _MSC_VER
#define __builtin_popcount __popcnt
#endif

IndexPatternsCache::IndexPatternsCache() {
	for( unsigned bits = 0; bits < 4; ++bits ) {
		m_max4ElemCombinationsForBound.push_back( 0 );
		m_max5ElemCombinationsForBound.push_back( 0 );
		m_max6ElemCombinationsForBound.push_back( 0 );
		m_max7ElemCombinationsForBound.push_back( 0 );
	}

	for( unsigned bits = 4; bits < 24; ++bits ) {
		// 1000 ... 1111, 10000 ... 11111, 100'000 ... 111'111, ...
		const unsigned minWord = 1 << ( bits - 1 );
		const unsigned maxWord = ( 1 << bits ) - 1;
		for( unsigned word = minWord; word <= maxWord; ++word ) {
			switch( __builtin_popcount( word ) ) {
				case 4: addToCombinations( m_4ElemCombinations, word ); break;
				case 5: addToCombinations( m_5ElemCombinations, word ); break;
				case 6: addToCombinations( m_6ElemCombinations, word ); break;
				case 7: addToCombinations( m_7ElemCombinations, word ); break;
				default: break;
			}
		}
		m_max4ElemCombinationsForBound.push_back( (unsigned)m_4ElemCombinations.size() );
		m_max5ElemCombinationsForBound.push_back( (unsigned)m_5ElemCombinations.size() );
		m_max6ElemCombinationsForBound.push_back( (unsigned)m_6ElemCombinations.size() );
		m_max7ElemCombinationsForBound.push_back( (unsigned)m_7ElemCombinations.size() );
	}

	m_4ElemCombinations.shrink_to_fit();
	m_5ElemCombinations.shrink_to_fit();
	m_6ElemCombinations.shrink_to_fit();
	m_7ElemCombinations.shrink_to_fit();

	initPermutations( m_4ElemPermutations );
	initPermutations( m_5ElemPermutations );
	initPermutations( m_6ElemPermutations );
	initPermutations( m_7ElemPermutations );
}

static const IndexPatternsCache indexPatternsCache;

[[nodiscard]]
static auto getAreaOfArrangementDefinedPoly( const msurface_t *surf, std::span<const uint8_t> arrangement ) -> double {
	const vec4_t *const vertices = surf->mesh.xyzArray;
	const uint8_t *const indices = arrangement.data();
	const size_t numIndices      = arrangement.size();

	const float *__restrict firstPt = vertices[indices[0]];

	double result = 0.0;
	for( unsigned i = 1; i + 1 < numIndices; ++i ) {
		const float *const __restrict pt1 = vertices[indices[i + 0]];
		const float *const __restrict pt2 = vertices[indices[i + 1]];
		double to1[3], to2[3], cross[3];
		VectorSubtract( pt1, firstPt, to1 );
		VectorSubtract( pt2, firstPt, to2 );
		CrossProduct( to1, to2, cross );
		result += std::sqrt( VectorLengthSquared( cross ) );
	}

	return 0.5 * result;
}

struct HullVertex {
	float values[2];
	unsigned originalIndex;
	[[nodiscard]]
	auto operator[]( size_t index ) const -> float { return values[index]; }
	[[nodiscard]]
	bool operator==( const HullVertex &that ) const {
		return values[0] == that.values[0] && values[1] == that.values[1];
	}
	[[nodiscard]]
	bool operator<( const HullVertex &that ) const {
		return values[0] < that.values[0] || ( values[0] == that.values[0] && values[1] < that.values[1] );
	}
	[[nodiscard]]
	auto squareDistanceTo( const HullVertex &that ) const -> float {
		const float dx = that[0] - values[0];
		const float dy = that[1] - values[1];
		return dx * dx + dy * dy;
	}
};

template <typename V>
static inline auto cross2D( const V &p, const V &q, const V &r )  {
	return ( q[1] - p[1] ) * ( r[0] - q[0] ) - ( q[0] - p[0] ) * ( r[1] - q[1] );
}

[[nodiscard]]
static auto buildConvexHull( HullVertex *vertices, unsigned numVertices ) -> std::optional<unsigned> {
	// Minimal safety checks
	for( unsigned i = 0; i < numVertices; ++i ) {
		for( unsigned j = i + 1; j < numVertices; ++j ) {
			if( vertices[i].squareDistanceTo( vertices[j] ) < 4 * 4 ) {
				return false;
			}
		}
	}

	wsw::StaticVector<HullVertex, 24> result;

	// A crude gift-wrapping
	// TODO: Replace by the monotonic chain algorithm

	unsigned leftMostIndex = 0;
	for( unsigned i = 1; i < numVertices; ++i ) {
		const HullVertex &currVertex = vertices[i];
		const HullVertex &leftMostVertex = vertices[leftMostIndex];
		if( currVertex[0] < leftMostVertex[0] ) {
			leftMostIndex = i;
		} else if( currVertex[0] == leftMostVertex[0] ) {
			if( currVertex[1] < leftMostVertex[1] ) {
				leftMostIndex = i;
			}
		}
	}

	unsigned currIndex = leftMostIndex;
	do {
		unsigned nextIndex = (uint8_t)( currIndex + 1 ) % (uint8_t)numVertices;
		for( unsigned index = 0; index < numVertices; ++index ) {
			if( cross2D( vertices[currIndex], vertices[index], vertices[nextIndex] ) < 0 ) {
				nextIndex = index;
			}
		}
		result.push_back( vertices[nextIndex] );
		currIndex = nextIndex;
	} while( currIndex != leftMostIndex );

	if( result.size() == numVertices ) {
		std::memcpy( vertices, result.data(), sizeof( HullVertex ) * numVertices );
		return numVertices;
	}

	return std::nullopt;
}

template <unsigned N>
[[nodiscard]]
static auto getBestPolyAreaForArrangements( msurface_t *surf, double bestAreaSoFar, uint8_t *indicesToSave,
											unsigned *numIndicesToSave ) -> double {
	if( const unsigned numVerts = surf->mesh.numVerts; numVerts >= N ) {
		vec3_t extent { surf->maxs[0] - surf->mins[0], surf->maxs[1] - surf->mins[1], surf->maxs[2] - surf->mins[2] };
		VectorSubtract( surf->maxs, surf->mins, extent );
		// The poly is flat, we can use a viable conversion to 2D by throwing away the least-extent coord
		const auto indexOfMin = (unsigned)( std::min_element( extent, extent + 3 ) - extent );
		const unsigned coord1 = ( ( indexOfMin + 1 ) % 3 );
		const unsigned coord2 = ( ( indexOfMin + 2 ) % 3 );

		// TODO: We can save a lot by sorting all vertices once here
		// and use the monotone chain algorithm for all combinations
		const vec4_t *const surfVertices = surf->mesh.xyzArray;
		HullVertex hullVertices[24];
		for( unsigned i = 0; i < numVerts; ++i ) {
			hullVertices[i].values[0] = surfVertices[i][coord1];
			hullVertices[i].values[1] = surfVertices[i][coord2];
			hullVertices[i].originalIndex = i;
		}

		// Caution! We require the original mesh to form a convex hull
		// so these simple validation checks are sufficient
		// and also the gift-wrapping algorithm does not break on collinear input for combinations.
		// (We assume that there's no 3 collinear points in the original mesh).
		// This is a serious limitation and must be gone in the future.
		const std::optional<unsigned> maybeMeshHullSize = buildConvexHull( hullVertices, numVerts );
		if( maybeMeshHullSize != std::optional( numVerts ) ) {
			return bestAreaSoFar;
		}

		// For each combination (N-group of numVerts) indices try making an N-vertices convex hull and check it
		for( const IndexPatternsCache::Combination<N> &c: indexPatternsCache.getCombinationsForBound<N>( numVerts ) ) {
			// Make an input for hull building algorithm
			unsigned indicesMask = 0;
			for( unsigned i = 0; i < N; ++i ) {
				const unsigned index = c.indices[i];
				assert( index < numVerts );
				hullVertices[i].values[0] = surfVertices[index][coord1];
				hullVertices[i].values[1] = surfVertices[index][coord2];
				hullVertices[i].originalIndex = index;
				indicesMask |= ( 1u << index );
			}

			if( const auto maybeHullSize = buildConvexHull( hullVertices, N ) ) {
				const unsigned hullSize = *maybeHullSize;
				if( hullSize >= 4 ) {
					// Currently, there's no hull validity checks.
					// We assume that any combination of vertices of a convex hull mesh that is a convex hull is valid.
					// (makes a valid conservative approximation of a mesh).
					// In general case, we have to compute an intersection of the mesh and the hull poly
					// and require that the hull poly exactly matches this intersection.
					assert( hullSize <= 7 );
					uint8_t indices[7];
					for( unsigned i = 0; i < hullSize; ++i ) {
						indices[i] = hullVertices[i].originalIndex;
					}
					const double area = getAreaOfArrangementDefinedPoly( surf, { indices, indices + hullSize } );
					if( area > bestAreaSoFar ) {
						bestAreaSoFar = area;
						memcpy( indicesToSave, indices, hullSize );
						*numIndicesToSave = hullSize;
					}
				}
			}
		}
	}
	return bestAreaSoFar;
}

[[nodiscard]]
static bool setupOccluderContourPoly( msurface_t *surf ) {
	if( surf->facetype != FACETYPE_PLANAR ) {
		return false;
	}

	if( surf->mesh.numVerts >= 24 ) {
		return false;
	}

	if( !surf->shader ) {
		return false;
	}

	if( surf->shader->sort != SHADER_SORT_OPAQUE ) {
		return false;
	}

	if( surf->wasTestedToBeAnOccluder ) {
		if( surf->numOccluderPolyIndices ) {
			assert( surf->numOccluderPolyIndices >= 4 && surf->numOccluderPolyIndices <= 7 );
			return true;
		}
		return false;
	}

	surf->wasTestedToBeAnOccluder = true;
	surf->numOccluderPolyIndices = 0;

	uint8_t resultIndices[7] {};
	unsigned numResultIndices = 0;
	auto bestAreaSoFar = std::numeric_limits<double>::min();
	bestAreaSoFar = getBestPolyAreaForArrangements<4>( surf, bestAreaSoFar, resultIndices, &numResultIndices );
	bestAreaSoFar = getBestPolyAreaForArrangements<5>( surf, bestAreaSoFar, resultIndices, &numResultIndices );
	bestAreaSoFar = getBestPolyAreaForArrangements<6>( surf, bestAreaSoFar, resultIndices, &numResultIndices );
	bestAreaSoFar = getBestPolyAreaForArrangements<7>( surf, bestAreaSoFar, resultIndices, &numResultIndices );
	if( bestAreaSoFar < 96.0 * 96.0 ) {
		return false;
	}

	assert( numResultIndices >= 4 && numResultIndices <= 7 );
	surf->numOccluderPolyIndices = (uint8_t)numResultIndices;
	surf->sqrtOfOccluderPolyArea = (float)std::sqrt( bestAreaSoFar );
	std::memcpy( surf->occluderPolyIndices, resultIndices, sizeof( uint8_t ) * numResultIndices );

	// This is not a true centroid but should be fine for frusta setup
	double innerPoint[3] { 0.0, 0.0, 0.0 };

	BoundsBuilder boundsBuilder;
	for( unsigned i = 0; i < numResultIndices; ++i ) {
		const float *vertex = surf->mesh.xyzArray[resultIndices[i]];
		boundsBuilder.addPoint( vertex );
		VectorAdd( innerPoint, vertex, innerPoint );
	}

	// TODO: Allow doing scalar 4-component stores explicitly
	boundsBuilder.storeToWithAddedEpsilon( surf->occluderPolyMins, surf->occluderPolyMaxs );
	surf->occluderPolyMins[3] = 0.0f;
	surf->occluderPolyMaxs[3] = 1.0f;

	VectorScale( innerPoint, 1.0 / numResultIndices, surf->occluderPolyInnerPoint );

	return true;
}

static void Mod_CreateVisLeafs( model_t *mod ) {
	mbrushmodel_t *const loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	const unsigned count = loadbmodel->numleafs;
	loadbmodel->visleafs = (mleaf_t **)Q_malloc( ( count + 1 ) * sizeof( *loadbmodel->visleafs ) );
	memset( loadbmodel->visleafs, 0, ( count + 1 ) * sizeof( *loadbmodel->visleafs ) );

	constexpr float minsVal = std::numeric_limits<float>::max();
	constexpr float maxsVal = std::numeric_limits<float>::min();

	unsigned numVisLeafs = 0;
	for( unsigned i = 0; i < count; i++ ) {
		mleaf_t *const __restrict leaf = loadbmodel->leafs + i;
		Vector4Set( leaf->mins + 0, minsVal, minsVal, minsVal, minsVal );
		Vector4Set( leaf->mins + 4, minsVal, minsVal, minsVal, minsVal );
		Vector4Set( leaf->maxs + 0, maxsVal, maxsVal, maxsVal, maxsVal );
		Vector4Set( leaf->maxs + 4, maxsVal, maxsVal, maxsVal, maxsVal );

		if( leaf->cluster < 0 || !leaf->numVisSurfaces ) {
			leaf->visSurfaces = nullptr;
			leaf->numVisSurfaces = 0;
			leaf->fragmentSurfaces = nullptr;
			leaf->numFragmentSurfaces = 0;
			leaf->fragmentSurfaces = nullptr;
			leaf->numOccluderSurfaces = 0;
			leaf->occluderSurfaces = nullptr;
		} else {
			BoundingDopBuilder<14> boundingDopBuilder;
			unsigned numVisSurfaces = 0;
			unsigned numFragmentSurfaces = 0;
			unsigned numOccluderSurfaces = 0;
			for( unsigned j = 0; j < leaf->numVisSurfaces; j++ ) {
				const unsigned surfNum = leaf->visSurfaces[j];
				msurface_t *const surf = loadbmodel->surfaces + surfNum;
				if( R_SurfPotentiallyVisible( surf ) ) {
					boundingDopBuilder.addOtherDop( surf->mins, surf->maxs );
					leaf->visSurfaces[numVisSurfaces++] = surfNum;
					if( R_SurfPotentiallyFragmented( surf ) ) {
						leaf->fragmentSurfaces[numFragmentSurfaces++] = surfNum;
					}
					if( setupOccluderContourPoly( surf ) ) {
						leaf->occluderSurfaces[numOccluderSurfaces++] = surfNum;
					}
				}
			}
			leaf->numVisSurfaces      = numVisSurfaces;
			leaf->numFragmentSurfaces = numFragmentSurfaces;
			leaf->numOccluderSurfaces = numOccluderSurfaces;
			if( numVisSurfaces ) {
				boundingDopBuilder.storeTo( leaf->mins, leaf->maxs );
				loadbmodel->visleafs[numVisLeafs++] = leaf;
			}
		}
	}

	loadbmodel->visleafs[numVisLeafs] = nullptr;
	loadbmodel->numvisleafs = numVisLeafs;
}

/*
* Mod_CalculateAutospriteBounds
*
* Make bounding box of an autosprite surf symmetric and enlarges it
* to account for rotation along the longest axis.
*/
static void Mod_CalculateAutospriteBounds( msurface_t *surf, vec3_t mins, vec3_t maxs ) {
	// find the longest axis
	int longestAxis = 2;
	vec3_t radius, center;
	float maxDist = std::numeric_limits<float>::min();
	for( int i = 0; i < 3; i++ ) {
		const float dist = maxs[i] - mins[i];
		if( dist > maxDist ) {
			longestAxis = i;
			maxDist = dist;
		}

		// make the bbox symmetrical
		radius[i] = 0.5f * dist;
		center[i] = 0.5f * ( maxs[i] + mins[i] );
		mins[i] = center[i] - radius[i];
		maxs[i] = center[i] + radius[i];
	}

	// shorter axis
	const int shorterAxis1 = ( longestAxis + 1 ) % 3;
	const int shorterAxis2 = ( longestAxis + 2 ) % 3;

	// enlarge the bounding box, accounting for rotation along the longest axis
	maxs[shorterAxis1] = std::max( maxs[shorterAxis1], center[shorterAxis1] + radius[shorterAxis2] );
	maxs[shorterAxis2] = std::max( maxs[shorterAxis2], center[shorterAxis2] + radius[shorterAxis1] );

	mins[shorterAxis1] = std::min( mins[shorterAxis1], center[shorterAxis1] - radius[shorterAxis2] );
	mins[shorterAxis2] = std::min( mins[shorterAxis2], center[shorterAxis2] - radius[shorterAxis1] );
}

static void Mod_FinishFaces( model_t *mod ) {
	mbrushmodel_t *const loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	for( unsigned i = 0; i < loadbmodel->numsurfaces; i++ ) {
		msurface_t *const surf = loadbmodel->surfaces + i;
		const mesh_t *const mesh = &surf->mesh;
		const shader_t *shader = surf->shader;

		if( !R_SurfPotentiallyVisible( surf ) ) {
			continue;
		}

		float *vert = mesh->xyzArray[0];
		// calculate bounding DOP of a surface
		BoundingDopBuilder<14> boundingDopBuilder( vert );
		vert += 4;

		for( unsigned j = 1; j < mesh->numVerts; j++ ) {
			boundingDopBuilder.addPoint( vert );
			vert += 4;
		}

		// foliage surfaces need special treatment for bounds
		if( surf->facetype == FACETYPE_FOLIAGE ) {
			vert = &surf->instances[0][4];
			for( unsigned j = 0; j < surf->numInstances; j++ ) {
				vec3_t temp;
				VectorMA( vert, vert[3], surf->mins, temp );
				boundingDopBuilder.addPoint( temp );

				VectorMA( vert, vert[3], surf->maxs, temp );
				boundingDopBuilder.addPoint( temp );

				vert += 8;
			}
		}

		boundingDopBuilder.storeTo( surf->mins, surf->maxs );

		// handle autosprites
		if( shader->flags & SHADER_AUTOSPRITE ) {
			// handle autosprites as trisurfs to avoid backface culling
			surf->facetype = FACETYPE_TRISURF;

			vec3_t inoutMins, inoutMaxs;
			VectorCopy( surf->mins, inoutMins );
			VectorCopy( surf->maxs, inoutMaxs );
			Mod_CalculateAutospriteBounds( surf, inoutMins, inoutMaxs );
			boundingDopBuilder.addPoint( inoutMins );
			boundingDopBuilder.addPoint( inoutMaxs );
			boundingDopBuilder.storeTo( surf->mins, surf->maxs );
		}
	}
}

static void Mod_SetupSubmodels( model_t *mod ) {
	mbrushmodel_t *const loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	// set up the submodels
	for( unsigned i = 0; i < loadbmodel->numsubmodels; i++ ) {
		mmodel_t *bm = &loadbmodel->submodels[i];
		model_t *starmod = &loadbmodel->inlines[i];
		mbrushmodel_t *bmodel = ( mbrushmodel_t * )starmod->extradata;

		memcpy( starmod, mod, sizeof( model_t ) );
		if( i ) {
			memcpy( bmodel, loadbmodel, sizeof( mbrushmodel_t ) );
		}

		bmodel->firstModelSurface = bm->firstModelSurface;
		bmodel->numModelSurfaces = bm->numModelSurfaces;

		bmodel->firstModelDrawSurface = bm->firstModelDrawSurface;
		bmodel->numModelDrawSurfaces = bm->numModelDrawSurfaces;

		starmod->extradata = bmodel;
		if( i == 0 ) {
			bmodel->visleafs = loadbmodel->visleafs;
			bmodel->numvisleafs = loadbmodel->numvisleafs;
		} else {
			bmodel->visleafs = nullptr;
			bmodel->numvisleafs = 0;
		}

		VectorCopy( bm->maxs, starmod->maxs );
		VectorCopy( bm->mins, starmod->mins );
		starmod->radius = bm->radius;

		if( i == 0 ) {
			*mod = *starmod;
		} else {
			bmodel->numsubmodels = 0;
		}
	}
}

static int R_CompareSurfacesByDrawSurf( const void *ps1, const void *ps2 ) {
	const auto *const s1 = (msortedSurface_t *)ps1;
	const auto *const s2 = (msortedSurface_t *)ps2;
	if( s1->surf->drawSurf > s2->surf->drawSurf ) {
		return 1;
	}
	if( s1->surf->drawSurf < s2->surf->drawSurf ) {
		return -1;
	}
	return (int)s1->surf->firstDrawSurfVert - (int)s2->surf->firstDrawSurfVert;
}

static void Mod_SortModelSurfaces( model_t *mod, unsigned int modnum ) {
	mbrushmodel_s *const loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	assert( modnum >= 0 && modnum < loadbmodel->numsubmodels );
	mmodel_t *const bm = loadbmodel->submodels + modnum;

	// ignore empty models
	const unsigned numSurfaces = bm->numModelSurfaces;
	const unsigned firstSurface = bm->firstModelSurface;
	if( !numSurfaces ) {
		return;
	}

	auto *const map = ( unsigned * )Q_malloc( numSurfaces * sizeof( unsigned ) );
	auto *const sortedSurfaces = ( msortedSurface_t * )Q_malloc( numSurfaces * sizeof( msortedSurface_t ) );
	auto *const backupSurfaces = ( msurface_t * )Q_malloc( numSurfaces * sizeof( msurface_t ) );
	for( unsigned i = 0; i < numSurfaces; i++ ) {
		sortedSurfaces[i].number = i;
		sortedSurfaces[i].surf = loadbmodel->surfaces + firstSurface + i;
	}

	memcpy( backupSurfaces, loadbmodel->surfaces + firstSurface, numSurfaces * sizeof( msurface_t ) );
	qsort( sortedSurfaces, numSurfaces, sizeof( msortedSurface_t ), &R_CompareSurfacesByDrawSurf );

	for( unsigned i = 0; i < numSurfaces; i++ ) {
		map[sortedSurfaces[i].number] = i;
	}

	if( !modnum && loadbmodel->visleafs ) {
		mleaf_t *leaf, **pleaf;
		for( pleaf = loadbmodel->visleafs, leaf = *pleaf; leaf; leaf = *++pleaf ) {
			for( unsigned j = 0; j < leaf->numVisSurfaces; j++ ) {
				leaf->visSurfaces[j] = map[leaf->visSurfaces[j]];
				leaf->fragmentSurfaces[j] = map[leaf->fragmentSurfaces[j]];
				leaf->occluderSurfaces[j] = map[leaf->occluderSurfaces[j]];
			}
		}
	}

	for( unsigned i = 0; i < numSurfaces; i++ ) {
		*(loadbmodel->surfaces + firstSurface + i) = backupSurfaces[sortedSurfaces[i].number];
	}

	unsigned lastDrawSurf = loadbmodel->numDrawSurfaces + 1;
	for( unsigned i = 0; i < numSurfaces; i++ ) {
		msurface_t *const surf = loadbmodel->surfaces + firstSurface + i;
		if( surf->drawSurf ) {
			drawSurfaceBSP_t *const drawSurf = &loadbmodel->drawSurfaces[surf->drawSurf - 1];
			if( lastDrawSurf != surf->drawSurf ) {
				drawSurf->numWorldSurfaces = 0;
				drawSurf->firstWorldSurface = firstSurface + i;
				lastDrawSurf = surf->drawSurf;
			}
			drawSurf->numWorldSurfaces++;
		}
	}

	Q_free( map );
	Q_free( sortedSurfaces );
	Q_free( backupSurfaces );
}

static int Mod_CreateSubmodelBufferObjects( model_t *mod, unsigned modnum ) {
	assert( mod );

	mbrushmodel_t *const loadbmodel = ( ( mbrushmodel_t * )mod->extradata );
	assert( loadbmodel );

	assert( modnum >= 0 && modnum < loadbmodel->numsubmodels );
	mmodel_t *const bm = loadbmodel->submodels + modnum;

	// ignore empty models
	if( !bm->numModelSurfaces ) {
		return 0;
	}

	auto *const surfmap = ( msurface_t ** )Q_malloc( bm->numModelSurfaces * sizeof( void * ) );
	auto *const surfaces = ( msurface_t ** )Q_malloc( bm->numModelSurfaces * sizeof( void * ) );

	unsigned maxTempVBOs = 1024;

	auto *tempVBOs = ( mesh_vbo_t * )Q_malloc( maxTempVBOs * sizeof( mesh_vbo_t ) );
	const unsigned startDrawSurface = loadbmodel->numDrawSurfaces;

	bm->numModelDrawSurfaces = 0;
	bm->firstModelDrawSurface = startDrawSurface;

	unsigned rowbytes, rowlongs;
	int areabytes;

	unsigned numSurfaces = 0;
	uint8_t *visdata = nullptr;
	uint8_t *areadata = nullptr;
	if( !modnum && loadbmodel->pvs ) {
		rowbytes = loadbmodel->pvs->rowsize;
		rowlongs = ( rowbytes + 3 ) / 4;
		areabytes = ( loadbmodel->numareas + 7 ) / 8;

		if( !rowbytes ) {
			return 0;
		}

		// build visibility data for each face, based on what leafs
		// this face belongs to (visible from)
		visdata = ( uint8_t * )Q_malloc( rowlongs * 4 * loadbmodel->numsurfaces );
		areadata = ( uint8_t * )Q_malloc( areabytes * loadbmodel->numsurfaces );

		for( unsigned visLeafIndex = 0; visLeafIndex < loadbmodel->numvisleafs; ++visLeafIndex ) {
			mleaf_t *const leaf = loadbmodel->visleafs[visLeafIndex];
			for( unsigned surfIndex = 0; surfIndex < leaf->numVisSurfaces; surfIndex++ ) {
				const unsigned surfnum = leaf->visSurfaces[surfIndex];
				msurface_t *const surf = loadbmodel->surfaces + surfnum;

				// some buggy maps such as aeroq2 contain visleafs that address faces from submodels...
				if( surfnum < bm->numModelSurfaces && !surfmap[surfnum] ) {
					surfmap[surfnum] = surf;
					surfaces[numSurfaces] = surf;

					auto *const longrow  = ( int * )( visdata + numSurfaces * rowbytes );
					auto *const longrow2 = ( int * )( Mod_ClusterPVS( leaf->cluster, mod ) );

					// merge parent leaf cluster visibility into face visibility set
					// we could probably check for duplicates here because face can be
					// shared among multiple leafs
					for( unsigned j = 0; j < rowlongs; j++ ) {
						longrow[j] |= longrow2[j];
					}

					if( leaf->area >= 0 ) {
						uint8_t *const arearow = areadata + numSurfaces * areabytes;
						arearow[leaf->area >> 3] |= ( 1 << ( leaf->area & 7 ) );
					}

					numSurfaces++;
				}
			}
		}

		memset( surfmap, 0, bm->numModelSurfaces * sizeof( *surfmap ) );
	} else {
		// either a submodel or an unvised map
		rowbytes = 0;
		rowlongs = 0;
		visdata = nullptr;
		areabytes = 0;
		areadata = nullptr;

		msurface_t *surf = loadbmodel->surfaces + bm->firstModelSurface;
		for( unsigned i = 0; i < bm->numModelSurfaces; i++ ) {
			if( R_SurfPotentiallyVisible( surf ) ) {
				surfaces[numSurfaces] = surf;
				numSurfaces++;
			}
			surf++;
		}
	}

	// now linearly scan all faces for this submodel, merging them into
	// vertex buffer objects if they share shader, lightmap texture and we can render
	// them in hardware (some Q3A shaders require GLSL for that)

	// don't use half-floats for XYZ due to precision issues
	vattribmask_t floatVattribs = VATTRIB_POSITION_BIT;
	if( mapConfig.maxLightmapSize > 1024 ) {
		// don't use half-floats for lightmaps if there's not enough precision (half mantissa is 10 bits)
		floatVattribs |= VATTRIB_LMCOORDS_BITS;
	}

	unsigned numTempVBOs = 0;
	unsigned numResultVBOs = 0;
	unsigned numUnmappedSurfaces = numSurfaces;
	for( unsigned i = 0; i < numSurfaces; i++ ) {
		unsigned lastMerged = i;

		// done
		if( numUnmappedSurfaces == 0 ) {
			break;
		}

		// ignore faces already merged
		if( surfmap[i] ) {
			continue;
		}

		msurface_t *const surf = surfaces[i];
		shader_t *const shader = surf->shader;
		auto *const longrow    = ( int * )( visdata + i * rowbytes );
		uint8_t *const arearow = areadata + i * areabytes;

		int numFaces = 1;
		int numVerts = surf->mesh.numVerts;
		int numElems = surf->mesh.numElems;

		// portal or foliage surfaces can not be batched
		if( !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) && !surf->numInstances ) {
			// scan remaining face checking whether we merge them with the current one
			for( unsigned j = i + 1; j < numSurfaces; j++ ) {
				msurface_t *const surf2 = surfaces[j];

				// already merged
				if( surf2->drawSurf ) {
					continue;
				}

				// the following checks ensure the two faces are compatible can can be merged
				// into a single vertex buffer object
				if( surf2->shader != surf->shader || surf2->superLightStyle != surf->superLightStyle ) {
					continue;
				}
				if( surf2->fog != surf->fog ) {
					continue;
				}
				if( numVerts + surf2->mesh.numVerts >= USHRT_MAX ) {
					continue;
				}
				if( surf2->numInstances != 0 ) {
					continue;
				}

				unsigned k = 0;
				int *longrow2 = nullptr;
				// unvised maps and submodels submodel can simply skip PVS checks
				if( !visdata ) {
					goto merge;
				}

				// only merge faces that reside in same map areas
				if( areabytes > 0 ) {
					// if areabits aren't equal, faces have different area visibility
					if( memcmp( arearow, areadata + j * areabytes, areabytes ) ) {
						continue;
					}
				}

				// if two faces potentially see same things, we can merge them
				longrow2 = ( int * )( visdata + j * rowbytes );

				while( k < rowlongs && !( longrow[k] & longrow2[k] ) ) {
					++k;
				}

				if( k != rowlongs ) {
					// merge visibility sets
					for( k = 0; k < rowlongs; k++ ) {
						longrow[k] |= longrow2[k];
					}
merge:
					numFaces++;
					numVerts += surf2->mesh.numVerts;
					numElems += surf2->mesh.numElems;
					surfmap[j] = surf;
					lastMerged = j;
				}
			}
		}

		// create vertex buffer object for this face then upload data
		vattribmask_t vattribs = shader->vattribs | surf->superLightStyle->vattribs | VATTRIB_NORMAL_BIT;
		if( surf->numInstances ) {
			vattribs |= VATTRIB_INSTANCES_BITS;
		}

		// create temp VBO to hold pre-batched info
		if( numTempVBOs == maxTempVBOs ) {
			maxTempVBOs += 1024;
			tempVBOs = (mesh_vbo_s *)Q_realloc( tempVBOs, maxTempVBOs * sizeof( *tempVBOs ) );
		}

		mesh_vbo_t *vbo = &tempVBOs[numTempVBOs++];
		vbo->numVerts = numVerts;
		vbo->numElems = numElems;
		vbo->vertexAttribs = vattribs;
		if( numFaces == 1 ) {
			// non-mergable
			vbo->index = numTempVBOs;
		}

		// allocate a drawsurf
		drawSurfaceBSP_t *const drawSurf = &loadbmodel->drawSurfaces[loadbmodel->numDrawSurfaces++];
		drawSurf->type = ST_BSP;
		drawSurf->superLightStyle = surf->superLightStyle;
		drawSurf->instances = surf->instances;
		drawSurf->numInstances = surf->numInstances;
		drawSurf->fog = surf->fog;
		drawSurf->shader = surf->shader;
		drawSurf->numLightmaps = 0;

		// upload vertex and elements data for face itself
		surf->drawSurf = loadbmodel->numDrawSurfaces;
		surf->firstDrawSurfVert = 0;
		surf->firstDrawSurfElem = 0;

		numVerts = surf->mesh.numVerts;
		numElems = surf->mesh.numElems;
		numUnmappedSurfaces--;

		// count lightmaps
		for( int lightmapStyle: surf->superLightStyle->lightmapStyles ) {
			if( lightmapStyle == 255 ) {
				break;
			}
			drawSurf->numLightmaps++;
		}

		// now if there are any merged faces upload them to the same VBO
		if( numFaces > 1 ) {
			for( unsigned j = i + 1; j <= lastMerged; j++ ) {
				if( surfmap[j] == surf ) {
					msurface_t *const surf2 = surfaces[j];
					surf2->drawSurf = loadbmodel->numDrawSurfaces;
					surf2->firstDrawSurfVert = numVerts;
					surf2->firstDrawSurfElem = numElems;
					numVerts += surf2->mesh.numVerts;
					numElems += surf2->mesh.numElems;
					numUnmappedSurfaces--;
				}
			}
		}

		drawSurf->numVerts = numVerts;
		drawSurf->numElems = numElems;
	}

	assert( numUnmappedSurfaces == 0 );

	// merge vertex buffer objects with identical vertex attribs
	unsigned numUnmergedVBOs = numTempVBOs;
	for( unsigned i = 0; i < numTempVBOs; i++ ) {
		if( !numUnmergedVBOs ) {
			break;
		}

		mesh_vbo_t *const vbo = &tempVBOs[i];
		if( vbo->index == 0 ) {
			for( unsigned j = i + 1; j < numTempVBOs; j++ ) {
				mesh_vbo_t *const vbo2 = &tempVBOs[j];
				// If unmerged
				if( vbo2->index == 0 ) {
					if( vbo2->vertexAttribs == vbo->vertexAttribs ) {
						if( vbo->numVerts + vbo2->numVerts < USHRT_MAX ) {
							drawSurfaceBSP_t *const drawSurf = &loadbmodel->drawSurfaces[startDrawSurface + j];
							drawSurf->firstVboVert = vbo->numVerts;
							drawSurf->firstVboElem = vbo->numElems;

							vbo->numVerts += vbo2->numVerts;
							vbo->numElems += vbo2->numElems;

							vbo2->index = i + 1;
							numUnmergedVBOs--;
						}
					}
				}
			}

			vbo->index = i + 1;
		}

		if( vbo->index == i + 1 ) {
			numUnmergedVBOs--;
		}
	}

	assert( numUnmergedVBOs == 0 );

	// create real VBOs and assign owner pointers
	numUnmergedVBOs = numTempVBOs;
	for( unsigned i = 0; i < numTempVBOs; i++ ) {
		mesh_vbo_t *const vbo = &tempVBOs[i];

		if( !numUnmergedVBOs ) {
			break;
		}

		if( vbo->owner != nullptr ) {
			// already assigned to a real VBO
			continue;
		}
		if( vbo->index != i + 1 ) {
			// not owning self, meaning it's been merged to another VBO
			continue;
		}

		drawSurfaceBSP_t *drawSurf = &loadbmodel->drawSurfaces[startDrawSurface + i];

		// don't use half-floats for XYZ due to precision issues
		vbo->owner = R_CreateMeshVBO( drawSurf, vbo->numVerts, vbo->numElems, drawSurf->numInstances,
									  vbo->vertexAttribs, VBO_TAG_WORLD, vbo->vertexAttribs & ~floatVattribs );
		drawSurf->vbo = (mesh_vbo_s *)vbo->owner;

		if( drawSurf->numInstances == 0 ) {
			for( unsigned j = i + 1; j < numTempVBOs; j++ ) {
				mesh_vbo_t *vbo2 = &tempVBOs[j];
				if( vbo2->index == i + 1 ) {
					vbo2->owner = vbo->owner;
					drawSurf = &loadbmodel->drawSurfaces[startDrawSurface + j];
					drawSurf->vbo = (mesh_vbo_s *)vbo->owner;
					numUnmergedVBOs--;
				}
			}
		}

		numResultVBOs++;
		numUnmergedVBOs--;
	}

	assert( numUnmergedVBOs == 0 );

	// upload data to merged VBO's and assign offsets to drawSurfs
	for( unsigned i = 0; i < numSurfaces; i++ ) {
		msurface_t *const surf = surfaces[i];
		if( surf->drawSurf ) {
			drawSurfaceBSP_t *const drawSurf = &loadbmodel->drawSurfaces[surf->drawSurf - 1];
			const mesh_t *mesh = &surf->mesh;
			mesh_vbo_t *const vbo = drawSurf->vbo;

			const int vertsOffset = drawSurf->firstVboVert + surf->firstDrawSurfVert;
			const int elemsOffset = drawSurf->firstVboElem + surf->firstDrawSurfElem;

			R_UploadVBOVertexData( vbo, vertsOffset, vbo->vertexAttribs, mesh );
			R_UploadVBOElemData( vbo, vertsOffset, elemsOffset, mesh );
			R_UploadVBOInstancesData( vbo, 0, surf->numInstances, surf->instances );
		}
	}

	bm->numModelDrawSurfaces = loadbmodel->numDrawSurfaces - bm->firstModelDrawSurface;

	Q_free( tempVBOs );
	Q_free( surfmap );
	Q_free( surfaces );

	if( visdata ) {
		Q_free( visdata );
	}
	if( areadata ) {
		Q_free( areadata );
	}

	return (int)numResultVBOs;
}

void Mod_CreateVertexBufferObjects( model_t *mod ) {
	mbrushmodel_t *loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	// free all VBO's allocated for previous world map so
	// we won't end up with both maps residing in video memory
	// until R_FreeUnusedVBOs call
	if( r_prevworldmodel && r_prevworldmodel->registrationSequence != rsh.registrationSequence ) {
		R_FreeVBOsByTag( VBO_TAG_WORLD );
	}

	// allocate memory for drawsurfs
	loadbmodel->numDrawSurfaces = 0;
	loadbmodel->drawSurfaces = (drawSurfaceBSP_t *)Q_malloc( sizeof( *loadbmodel->drawSurfaces ) * loadbmodel->numsurfaces );

	for( unsigned i = 0; i < loadbmodel->numsubmodels; i++ ) {
		Mod_CreateSubmodelBufferObjects( mod, i );
	}

	for( unsigned i = 0; i < loadbmodel->numsubmodels; i++ ) {
		Mod_SortModelSurfaces( mod, i );
	}
}

static void Mod_FinalizeBrushModel( model_t *model ) {
	Mod_FinishFaces( model );

	Mod_CreateVisLeafs( model );

	Mod_CreateVertexBufferObjects( model );

	Mod_SetupSubmodels( model );
}

static void Mod_TouchBrushModel( model_t *model ) {
	auto *loadbmodel = ( ( mbrushmodel_t * )model->extradata );

	for( unsigned modnum = 0; modnum < loadbmodel->numsubmodels; modnum++ ) {
		loadbmodel->inlines[modnum].registrationSequence = rsh.registrationSequence;
	}

	// touch all shaders and vertex buffer objects for this bmodel

	for( unsigned i = 0; i < loadbmodel->numDrawSurfaces; i++ ) {
		drawSurfaceBSP_t *drawSurf = &loadbmodel->drawSurfaces[i];
		R_TouchShader( drawSurf->shader );
		R_TouchMeshVBO( drawSurf->vbo );
	}

	for( unsigned i = 0; i < loadbmodel->numfogs; i++ ) {
		if( loadbmodel->fogs[i].shader ) {
			R_TouchShader( loadbmodel->fogs[i].shader );
		}
	}

	R_TouchLightmapImages( model );
}

void R_InitModels() {
	memset( mod_novis, 0xff, sizeof( mod_novis ) );
	mod_isworldmodel = false;
	r_prevworldmodel = nullptr;
	mod_mapConfigs = (decltype( mod_mapConfigs ))Q_malloc( sizeof( *mod_mapConfigs ) * MAX_MOD_KNOWN );
}

static void Mod_Free( model_t *model ) {
	if( model->type == mod_alias ) {
		Mod_DestroyAliasMD3Model( (maliasmodel_s *)model->extradata );
	} else if( model->type == mod_skeletal ) {
		Mod_DestroySkeletalModel( (mskmodel_s *)model->extradata );
	} else if( model->type == mod_brush ) {
		Mod_DestroyQ3BrushModel( (mbrushmodel_s *)model->extradata );
	}
	Q_free( model->name );
	memset( model, 0, sizeof( *model ) );
	model->type = mod_free;
}

void R_FreeUnusedModels() {
	for( int i = 0; i < mod_numknown; ++i ) {
		model_s *const mod = &mod_known[i];
		if( mod->name ) {
			if( mod->registrationSequence != rsh.registrationSequence ) {
				Mod_Free( mod );
			}
		}
	}

	// check whether the world model has been freed
	if( rsh.worldModel && rsh.worldModel->type == mod_free ) {
		rsh.worldModel = nullptr;
		rsh.worldBrushModel = nullptr;
	}
}

void R_ShutdownModels() {
	for( int i = 0; i < mod_numknown; i++ ) {
		if( mod_known[i].name ) {
			Mod_Free( &mod_known[i] );
		}
	}

	rsh.worldModel = nullptr;
	rsh.worldBrushModel = nullptr;

	mod_numknown = 0;
	memset( mod_known, 0, sizeof( mod_known ) );
}

void Mod_StripLODSuffix( char *name ) {
	if( const size_t len = strlen( name ); len > 2 ) {
		if( name[len - 2] == '_' ) {
			const char digit = name[len - 1];
			if( digit >= '0' && digit <= '0' + MOD_MAX_LODS ) {
				name[len - 2] = 0;
			}
		}
	}
}

static model_t *Mod_FindSlot( const char *name ) {
	model_t *best = nullptr;
	for( int i = 0; i < mod_numknown; i++ ) {
		model_t *const mod = &mod_known[i];
		if( mod->type == mod_free ) {
			if( !best ) {
				best = mod;
			}
		} else {
			if( !Q_stricmp( mod->name, name ) ) {
				return mod;
			}
		}
	}

	if( best ) {
		return best;
	}

	//
	// find a free model slot spot
	//
	if( mod_numknown == MAX_MOD_KNOWN ) {
		Com_Error( ERR_DROP, "mod_numknown == MAX_MOD_KNOWN" );
	}

	return &mod_known[mod_numknown++];
}

model_t *Mod_ForName( const char *name, bool crash ) {

	if( !name[0] ) {
		Com_Error( ERR_DROP, "Mod_ForName: empty name" );
	}

	//
	// inline models are grabbed only from worldmodel
	//
	if( name[0] == '*' ) {
		const int modnum = atoi( name + 1 );
		if( modnum < 1 || !rsh.worldModel || (unsigned)modnum >= rsh.worldBrushModel->numsubmodels ) {
			Com_Error( ERR_DROP, "bad inline model number" );
		}
		return &rsh.worldBrushModel->inlines[modnum];
	}

	char shortname[MAX_QPATH];
	Q_strncpyz( shortname, name, sizeof( shortname ) );
	COM_StripExtension( shortname );
	const char *extension = &name[strlen( shortname ) + 1];

	model_t *mod = Mod_FindSlot( name );
	if( mod->type == mod_bad ) {
		return nullptr;
	}
	if( mod->type != mod_free ) {
		return mod;
	}

	//
	// load the file
	//
	unsigned *buf;
	(void)R_LoadFile( name, (void **)&buf );
	if( !buf && crash ) {
		Com_Error( ERR_DROP, "Mod_NumForName: %s not found", name );
	}

	mod->type = mod_bad;
	mod->name = (char *)Q_malloc( strlen( name ) + 1 );
	strcpy( mod->name, name );

	// return the nullptr model
	if( !buf ) {
		return nullptr;
	}

	// call the apropriate loader
	bspFormatDesc_t *bspFormat = nullptr;
	const auto *descr = Q_FindFormatDescriptor( mod_supportedformats, ( const uint8_t * )buf, (const bspFormatDesc_t **)&bspFormat );
	if( !descr ) {
		Com_DPrintf( S_COLOR_YELLOW "Mod_NumForName: unknown fileid for %s", mod->name );
		return nullptr;
	}

	if( mod_isworldmodel ) {
		// we only init map config when loading the map from disk
		R_InitMapConfig( name );
	}

	descr->loader( mod, nullptr, buf, bspFormat );
	R_FreeFile( buf );

	if( mod->type == mod_bad ) {
		return nullptr;
	}

	if( mod_isworldmodel ) {
		// we only init map config when loading the map from disk
		R_FinishMapConfig( mod );
	}

	// do some common things
	if( mod->type == mod_brush ) {
		Mod_FinalizeBrushModel( mod );
		mod->touch = &Mod_TouchBrushModel;
	}

	if( !descr->maxLods ) {
		return mod;
	}

	//
	// load level-of-detail models
	//
	mod->lodnum = 0;
	mod->numlods = 0;
	for( int i = 0; i < descr->maxLods; i++ ) {
		char lodname[MAX_QPATH];
		Q_snprintfz( lodname, sizeof( lodname ), "%s_%i.%s", shortname, i + 1, extension );
		R_LoadFile( lodname, (void **)&buf );
		if( !buf || strncmp( (const char *)buf, descr->header, descr->headerLen ) ) {
			break;
		}

		model_t *lod = mod->lods[i] = Mod_FindSlot( lodname );
		if( lod->name && !strcmp( lod->name, lodname ) ) {
			continue;
		}

		lod->type = mod_bad;
		lod->lodnum = i + 1;
		lod->name = (char *)Q_malloc( strlen( lodname ) + 1 );
		strcpy( lod->name, lodname );

		mod_numknown++;

		descr->loader( lod, mod, buf, bspFormat );
		R_FreeFile( buf );

		mod->numlods++;
	}

	return mod;
}

static void R_TouchModel( model_t *mod ) {
	if( mod->registrationSequence == rsh.registrationSequence ) {
		return;
	}

	// touching a model precaches all images and possibly other assets
	mod->registrationSequence = rsh.registrationSequence;
	if( mod->touch ) {
		mod->touch( mod );
	}

	// handle Level Of Details
	for( int i = 0; i < mod->numlods; i++ ) {
		model_t *const lod = mod->lods[i];
		lod->registrationSequence = rsh.registrationSequence;
		if( lod->touch ) {
			lod->touch( lod );
		}
	}
}

//=============================================================================

/*
* R_InitMapConfig
*
* Clears map config before loading the map from disk. NOT called when the map
* is reloaded from model cache.
*/
static void R_InitMapConfig( const char *model ) {
	memset( &mapConfig, 0, sizeof( mapConfig ) );

	mapConfig.lightmapsPacking = false;
	mapConfig.lightmapArrays = false;
	mapConfig.maxLightmapSize = 0;
	mapConfig.deluxeMaps = false;
	mapConfig.deluxeMappingEnabled = false;
	mapConfig.forceClear = false;
	mapConfig.forceWorldOutlines = false;
	mapConfig.averageLightingIntensity = 1;

	VectorClear( mapConfig.ambient );
	VectorClear( mapConfig.outlineColor );

	if( r_lighting_packlightmaps->integer ) {
		char lightmapsPath[MAX_QPATH], *p;

		mapConfig.lightmapsPacking = true;

		Q_strncpyz( lightmapsPath, model, sizeof( lightmapsPath ) );
		p = strrchr( lightmapsPath, '.' );
		if( p ) {
			*p = 0;
			Q_strncatz( lightmapsPath, "/lm_0000.tga", sizeof( lightmapsPath ) );
			if( FS_FOpenFile( lightmapsPath, nullptr, FS_READ ) != -1 ) {
				Com_DPrintf( S_COLOR_YELLOW "External lightmap stage: lightmaps packing is disabled\n" );
				mapConfig.lightmapsPacking = false;
			}
		}
	}
}

/*
* R_FinishMapConfig
*
* Called after loading the map from disk.
*/
static void R_FinishMapConfig( const model_t *mod ) {
	// ambient lighting
	if( r_fullbright->integer ) {
		VectorSet( mapConfig.ambient, 1, 1, 1 );
		mapConfig.averageLightingIntensity = 1;
	} else {
		ColorNormalize( mapConfig.ambient,  mapConfig.ambient );
	}

	// A dirty hack for fixing a crooked sky appearance
	if( const char *name = mod->name ) {
		for( const char *map : { "maps/wdm1.bsp", "maps/wdm15.bsp" } ) {
			if( !Q_stricmp( name, map ) ) {
				mapConfig.skipSky = true;
				break;
			}
		}
	}

	mod_mapConfigs[mod - mod_known] = mapConfig;
}

void R_RegisterWorldModel( const char *model ) {
	r_prevworldmodel = rsh.worldModel;
	rsh.worldModel = nullptr;
	rsh.worldBrushModel = nullptr;
	rsh.worldModelSequence++;

	mod_isworldmodel = true;

	rsh.worldModel = Mod_ForName( model, true );

	mod_isworldmodel = false;

	if( !rsh.worldModel ) {
		return;
	}

	// FIXME: this is ugly...
	mapConfig = mod_mapConfigs[rsh.worldModel - mod_known];

	R_TouchModel( rsh.worldModel );
	rsh.worldBrushModel = ( mbrushmodel_t * )rsh.worldModel->extradata;
}

struct model_s *R_RegisterModel( const char *name ) {
	model_t *const mod = Mod_ForName( name, false );
	if( mod ) {
		R_TouchModel( mod );
	}
	return mod;
}

void R_ModelBounds( const model_t *model, vec3_t mins, vec3_t maxs ) {
	if( model ) {
		VectorCopy( model->mins, mins );
		VectorCopy( model->maxs, maxs );
	} else if( rsh.worldModel ) {
		VectorCopy( rsh.worldModel->mins, mins );
		VectorCopy( rsh.worldModel->maxs, maxs );
	}
}

void R_ModelFrameBounds( const struct model_s *model, int frame, vec3_t mins, vec3_t maxs ) {
	if( model ) {
		switch( model->type ) {
			case mod_alias:
				R_AliasModelFrameBounds( model, frame, mins, maxs );
				break;
			case mod_skeletal:
				R_SkeletalModelFrameBounds( model, frame, mins, maxs );
				break;
			default:
				break;
		}
	}
}