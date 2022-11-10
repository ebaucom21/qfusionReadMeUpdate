#include "simulatedhullssystem.h"

#include "../qcommon/links.h"
#include "../client/client.h"
#include "../qcommon/wswvector.h"
#include "../qcommon/memspecbuilder.h"

#include <memory>
#include <unordered_map>

struct IcosphereData {
	std::span<const vec4_t> vertices;
	std::span<const uint16_t> indices;
	std::span<const uint16_t[5]> vertexNeighbours;
};

class BasicHullsHolder {
	struct Entry { unsigned firstIndex, numIndices, numVertices, firstNeighboursElement; };
public:
	static constexpr unsigned kMaxSubdivLevel = 4;

	BasicHullsHolder() {
		constexpr const uint8_t icosahedronFaces[20][3] {
			{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
			{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
			{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
			{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
		};

		constexpr const float a = 0.525731f, b = 0.850651f;
		const vec3_t icosahedronVertices[12] {
			{ -a, +b, +0 }, { +a, +b, +0 }, { -a, -b, +0 }, { +a, -b, +0 },
			{ +0, -a, +b }, { +0, +a, +b }, { +0, -a, -b }, { +0, +a, -b },
			{ +b, +0, -a }, { +b, +0, +a }, { -b, +0, -a }, { -b, +0, +a },
		};

		for( const auto &v : icosahedronVertices ) {
			m_vertices.emplace_back( Vertex { { v[0], v[1], v[2], 1.0f } } );
		}
		for( const auto &f: icosahedronFaces ) {
			m_faces.emplace_back( Face { { f[0], f[1], f[2] } } );
		}

		wsw::StaticVector<Entry, kMaxSubdivLevel + 1> icosphereEntries;
		icosphereEntries.emplace_back( Entry { 0, 3 * (unsigned)m_faces.size(), (unsigned)m_vertices.size() } );

		MidpointMap midpointCache;
		unsigned oldFacesSize = 0, facesSize = m_faces.size();
		while( !icosphereEntries.full() ) {
			for( unsigned i = oldFacesSize; i < facesSize; ++i ) {
				const Face &face = m_faces[i];
				const uint16_t v1 = face.data[0], v2 = face.data[1], v3 = face.data[2];
				assert( v1 != v2 && v2 != v3 && v1 != v3 );
				const uint16_t p = getMidPoint( v1, v2, &midpointCache );
				const uint16_t q = getMidPoint( v2, v3, &midpointCache );
				const uint16_t r = getMidPoint( v3, v1, &midpointCache );
				m_faces.emplace_back( Face { { v1, p, r } } );
				m_faces.emplace_back( Face { { v2, q, p } } );
				m_faces.emplace_back( Face { { v3, r, q } } );
				m_faces.emplace_back( Face { { p, q, r } } );
			}
			oldFacesSize = facesSize;
			facesSize = m_faces.size();
			const unsigned firstIndex = 3 * oldFacesSize;
			const unsigned numIndices = 3 * ( facesSize - oldFacesSize );
			icosphereEntries.emplace_back( Entry { firstIndex, numIndices, (unsigned)m_vertices.size() } );
		}

		// TODO: Use fixed vectors
		m_vertices.shrink_to_fit();
		m_faces.shrink_to_fit();

		// TODO can we do that during faces construction.
		buildNeighbours( icosphereEntries );

		// Build data of the public interface
		for( unsigned level = 0; level < kMaxSubdivLevel + 1; ++level ) {
			using OpaqueNeighbours     = uint16_t[5];
			const auto *vertexData     = (const vec4_t *)m_vertices.data();
			const auto *indexData      = (const uint16_t *)m_faces.data();
			const auto *neighboursData = (const OpaqueNeighbours *)m_neighbours.data();
			const Entry &entry         = icosphereEntries[level];
			std::span<const vec4_t> verticesSpan { vertexData, entry.numVertices };
			std::span<const uint16_t> indicesSpan { indexData + entry.firstIndex, entry.numIndices };
			std::span<const uint16_t[5]> neighboursSpan { neighboursData + entry.firstNeighboursElement, entry.numVertices };
			m_icospheresForLevels.emplace_back( IcosphereData { verticesSpan, indicesSpan, neighboursSpan } );
		}
	}

	[[nodiscard]]
	auto getIcosphereForLevel( unsigned level ) -> IcosphereData {
		return m_icospheresForLevels[level];
	}
private:
	struct alignas( 4 ) Vertex { float data[4]; };
	static_assert( sizeof( Vertex ) == sizeof( vec4_t ) );
	struct alignas( 2 ) Face { uint16_t data[3]; };
	static_assert( sizeof( Face ) == sizeof( uint16_t[3] ) );
	struct alignas( 2 ) Neighbours { uint16_t data[5]; };
	static_assert( sizeof( Neighbours ) == sizeof( uint16_t[5] ) );

	using MidpointMap = std::unordered_map<unsigned, uint16_t>;

	[[nodiscard]]
	auto getMidPoint( uint16_t i1, uint16_t i2, MidpointMap *midpointCache ) -> uint16_t {
		const unsigned smallest = ( i1 < i2 ) ? i1 : i2;
		const unsigned largest = ( i1 < i2 ) ? i2 : i1;
		const unsigned key = ( smallest << 16 ) | largest;
		if( const auto it = midpointCache->find( key ); it != midpointCache->end() ) {
			return it->second;
		}

		Vertex midpoint {};
		const float *v1 = m_vertices[i1].data, *v2 = m_vertices[i2].data;
		VectorAvg( v1, v2, midpoint.data );
		VectorNormalize( midpoint.data );
		midpoint.data[3] = 1.0f;

		const auto index = (uint16_t)m_vertices.size();
		m_vertices.push_back( midpoint );
		midpointCache->insert( std::make_pair( key, index ) );
		return index;
	}

	static constexpr unsigned kNumVertexNeighbours = 5;
	static constexpr unsigned kNumFaceNeighbours = 5;

	struct NeighbourFaces { uint16_t data[kNumVertexNeighbours]; };

	// Returns the number of vertices that got their neighbour faces list completed during this call
	[[nodiscard]]
	auto addFaceToNeighbours( uint16_t faceNum, NeighbourFaces *neighbourFaces,
							  uint8_t *knownNeighboursCounts ) -> unsigned {
		unsigned result = 0;

		for( unsigned i = 0; i < 3; ++i ) {
			const uint16_t vertexNum      = m_faces[faceNum].data[i];
			uint8_t *const countForVertex = &knownNeighboursCounts[vertexNum];
			assert( *countForVertex <= kNumFaceNeighbours );
			if( *countForVertex < kNumFaceNeighbours ) {
				uint16_t *const indicesBegin = neighbourFaces[vertexNum].data;
				uint16_t *const indicesEnd   = indicesBegin + *countForVertex;
				// TODO: Is this search really needed?
				if( std::find( indicesBegin, indicesEnd, faceNum ) == indicesEnd ) {
					( *countForVertex )++;
					*indicesEnd = (uint16_t)faceNum;
					// The neighbours list becomes complete
					if( *countForVertex == kNumFaceNeighbours ) {
						result++;
					}
				}
			}
		}

		return result;
	}

	[[nodiscard]]
	auto findFaceWithVertex( const NeighbourFaces *neighbourFaces, unsigned vertexNum, unsigned pickedFacesMask )
		-> std::optional<std::pair<unsigned, unsigned>> {

		// Assume that face #0 is always picked
		for( unsigned faceIndex = 1; faceIndex < kNumFaceNeighbours; ++faceIndex ) {
			if( !( pickedFacesMask & ( 1u << faceIndex ) ) ) {
				const Face &face = m_faces[neighbourFaces->data[faceIndex]];
				for( unsigned indexInFace = 0; indexInFace < 3; ++indexInFace ) {
					if( face.data[indexInFace] == vertexNum ) {
						return std::pair<unsigned, unsigned> { faceIndex, indexInFace };
					}
				}
			}
		}

		return std::nullopt;
	}

	void buildNeighboursForVertexUsingNeighbourFaces( unsigned vertexNum, Neighbours *neighbourVertices,
													  const NeighbourFaces *neighbourFaces ) {
		wsw::StaticVector<uint16_t, kNumVertexNeighbours> vertexNums;

		// Pick the first face unconditionally
		const Face &firstFace    = m_faces[neighbourFaces->data[0]];
		unsigned pickedFacesMask = 0x1;

		// Add 2 vertices of the first face
		for( unsigned i = 0; i < 3; ++i ) {
			if( firstFace.data[i] == vertexNum ) {
				vertexNums.push_back( firstFace.data[( i + 1 ) % 3] );
				vertexNums.push_back( firstFace.data[( i + 2 ) % 3] );
				break;
			}
		}

		assert( vertexNums.size() == 2 );
		while( !vertexNums.full() ) {
			bool prepend = false;
			// Try finding a face that contacts with the last added face forming a triangle fan
			auto maybeIndices = findFaceWithVertex( neighbourFaces, vertexNums.back(), pickedFacesMask );
			if( !maybeIndices ) {
				// Find a face that contacts with the first added face forming a triangle fan
				maybeIndices = findFaceWithVertex( neighbourFaces, vertexNums.front(), pickedFacesMask );
				prepend = true;
			}

			const auto [faceIndex, indexInFace] = maybeIndices.value();
			const Face &face = m_faces[neighbourFaces->data[faceIndex]];
			const unsigned otherIndex1 = ( indexInFace + 1 ) % 3;
			const unsigned otherIndex2 = ( indexInFace + 2 ) % 3;

			// Make sure the face contains the original vertex.
			assert( face.data[otherIndex1] == vertexNum || face.data[otherIndex2] == vertexNum );

			// Add the remaining one to the neighbour vertices list.
			unsigned indexOfVertexToAdd = otherIndex1;
			if( face.data[otherIndex1] == vertexNum ) {
				indexOfVertexToAdd = otherIndex2;
			}

			const uint16_t vertexNumToAdd = face.data[indexOfVertexToAdd];
			if( prepend ) {
				vertexNums.insert( vertexNums.begin(), vertexNumToAdd );
			} else {
				vertexNums.push_back( vertexNumToAdd );
			}

			pickedFacesMask |= ( 1u << faceIndex );
		}

		// Now check the normal direction with regard to direction to vertex

		const float *const v1 = m_vertices[vertexNum].data;
		const float *const v2 = m_vertices[vertexNums[0]].data;
		const float *const v3 = m_vertices[vertexNums[1]].data;

		vec3_t v1To2, v1To3, cross;
		VectorSubtract( v2, v1, v1To2 );
		VectorSubtract( v3, v1, v1To3 );
		CrossProduct( v1To2, v1To3, cross );
		// The vertex is a direction from (0, 0, 0) to itself
		if( DotProduct( v1, cross ) < 0 ) {
			std::reverse( vertexNums.begin(), vertexNums.end() );
		}

		std::copy( vertexNums.begin(), vertexNums.end(), neighbourVertices->data );
	}

	void buildNeighbours( wsw::StaticVector<Entry, kMaxSubdivLevel + 1> &icosphereEntries ) {
		NeighbourFaces neighbourFacesForVertex[3072];
		uint8_t knownNeighbourFacesCounts[3072];

		// Note that neighbours of the same vertex differ for different subdivision level
		// so we have to recompute all neighbours for each entry (each subdivision level).
		for( Entry &entry: icosphereEntries ) {
			entry.firstNeighboursElement = m_neighbours.size();
			m_neighbours.resize( m_neighbours.size() + entry.numVertices );

			Neighbours *const neighbourVerticesForVertex = m_neighbours.data() + entry.firstNeighboursElement;

			// Fill by ~0 to spot non-feasible indices fast
			std::memset( neighbourVerticesForVertex, 255, entry.numVertices * sizeof( Neighbours ) );
			std::memset( neighbourFacesForVertex, 255, entry.numVertices * sizeof( NeighbourFaces ) );

			assert( entry.numVertices < std::size( knownNeighbourFacesCounts ) );
			std::memset( knownNeighbourFacesCounts, 0, sizeof( uint8_t ) * entry.numVertices );

			const unsigned faceOffset = entry.firstIndex / 3;
			const unsigned facesCount = entry.numIndices / 3;

			// Build lists of neighbour face indices for every vertex

			unsigned numVerticesWithCompleteNeighbourFaces = 0;
			for( unsigned faceIndex = faceOffset; faceIndex < faceOffset + facesCount; ++faceIndex ) {
				numVerticesWithCompleteNeighbourFaces += addFaceToNeighbours( faceIndex,
																			  neighbourFacesForVertex,
																			  knownNeighbourFacesCounts );
				if( numVerticesWithCompleteNeighbourFaces == entry.numVertices ) {
					break;
				}
			}

			// Build lists of neighbour vertex indices for every vertex
			for( unsigned vertexIndex = 0; vertexIndex < entry.numVertices; ++vertexIndex ) {
				assert( knownNeighbourFacesCounts[vertexIndex] == kNumFaceNeighbours );

				buildNeighboursForVertexUsingNeighbourFaces( vertexIndex,
															 &neighbourVerticesForVertex[vertexIndex],
															 &neighbourFacesForVertex[vertexIndex] );
			}
		}

		// TODO: Use a fixed vector
		m_neighbours.shrink_to_fit();
	}

	wsw::Vector<Vertex> m_vertices;
	wsw::Vector<Face> m_faces;
	wsw::Vector<Neighbours> m_neighbours;

	wsw::StaticVector<IcosphereData, kMaxSubdivLevel + 1> m_icospheresForLevels;
};

static BasicHullsHolder basicHullsHolder;

SimulatedHullsSystem::SimulatedHullsSystem() {
	// TODO: Take care of exception-safety
	while( !m_freeShapeLists.full() ) {
		if( auto *shapeList = CM_AllocShapeList( cl.cms ) ) [[likely]] {
			m_freeShapeLists.push_back( shapeList );
		} else {
			wsw::failWithBadAlloc();
		}
	}
	// TODO: Take care of exception-safety
	if( !( m_tmpShapeList = CM_AllocShapeList( cl.cms ) ) ) [[unlikely]] {
		wsw::failWithBadAlloc();
	}
}

SimulatedHullsSystem::~SimulatedHullsSystem() {
	for( FireHull *hull = m_fireHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		unlinkAndFreeFireHull( hull );
	}
	for( FireClusterHull *hull = m_fireClusterHullsHead, *next = nullptr; hull; hull = next ) { next = hull->next;
		unlinkAndFreeFireClusterHull( hull );
	}
	for( BlastHull *hull = m_blastHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		unlinkAndFreeBlastHull( hull );
	}
	for( SmokeHull *hull = m_smokeHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		unlinkAndFreeSmokeHull( hull );
	}
	for( WaveHull *hull = m_waveHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		unlinkAndFreeWaveHull( hull );
	}
	for( CMShapeList *shapeList: m_freeShapeLists ) {
		CM_FreeShapeList( cl.cms, shapeList );
	}
	CM_FreeShapeList( cl.cms, m_tmpShapeList );
}

void SimulatedHullsSystem::unlinkAndFreeSmokeHull( SmokeHull *hull ) {
	wsw::unlink( hull, &m_smokeHullsHead );
	m_freeShapeLists.push_back( hull->shapeList );
	hull->~SmokeHull();
	m_smokeHullsAllocator.free( hull );
}

void SimulatedHullsSystem::unlinkAndFreeWaveHull( WaveHull *hull ) {
	wsw::unlink( hull, &m_waveHullsHead );
	m_freeShapeLists.push_back( hull->shapeList );
	hull->~WaveHull();
	m_waveHullsAllocator.free( hull );
}

void SimulatedHullsSystem::unlinkAndFreeFireHull( FireHull *hull ) {
	wsw::unlink( hull, &m_fireHullsHead );
	hull->~FireHull();
	m_fireHullsAllocator.free( hull );
}

void SimulatedHullsSystem::unlinkAndFreeFireClusterHull( FireClusterHull *hull ) {
	wsw::unlink( hull, &m_fireClusterHullsHead );
	hull->~FireClusterHull();
	m_fireClusterHullsAllocator.free( hull );
}

void SimulatedHullsSystem::unlinkAndFreeBlastHull( BlastHull *hull ) {
	wsw::unlink( hull, &m_blastHullsHead );
	hull->~BlastHull();
	m_blastHullsAllocator.free( hull );
}

[[nodiscard]]
auto SimulatedHullsSystem::allocFireHull( int64_t currTime, unsigned lifetime ) -> FireHull * {
	return allocHull<FireHull, false>( &m_fireHullsHead, &m_fireHullsAllocator, currTime, lifetime );
}

[[nodiscard]]
auto SimulatedHullsSystem::allocFireClusterHull( int64_t currTime, unsigned lifetime ) -> FireClusterHull * {
	return allocHull<FireClusterHull, false>( &m_fireClusterHullsHead, &m_fireClusterHullsAllocator, currTime, lifetime );
}

[[nodiscard]]
auto SimulatedHullsSystem::allocBlastHull( int64_t currTime, unsigned int lifetime ) -> BlastHull * {
	return allocHull<BlastHull, false>( &m_blastHullsHead, &m_blastHullsAllocator, currTime, lifetime );
}

[[nodiscard]]
auto SimulatedHullsSystem::allocSmokeHull( int64_t currTime, unsigned lifetime ) -> SmokeHull * {
	return allocHull<SmokeHull, true>( &m_smokeHullsHead, &m_smokeHullsAllocator, currTime, lifetime );
}

[[nodiscard]]
auto SimulatedHullsSystem::allocWaveHull( int64_t currTime, unsigned lifetime ) -> WaveHull * {
	return allocHull<WaveHull, true>( &m_waveHullsHead, &m_waveHullsAllocator, currTime, lifetime );
}

// TODO: Turn on "Favor small code" for this template

template <typename Hull, bool HasShapeList>
auto SimulatedHullsSystem::allocHull( Hull **head, wsw::FreelistAllocator *allocator,
									  int64_t currTime, unsigned lifetime ) -> Hull * {
	void *mem = allocator->allocOrNull();
	CMShapeList *hullShapeList = nullptr;
	if( mem ) [[likely]] {
		if constexpr( HasShapeList ) {
			hullShapeList = m_freeShapeLists.back();
			m_freeShapeLists.pop_back();
		}
	} else {
		Hull *oldestHull = nullptr;
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( Hull *hull = *head; hull; hull = hull->next ) {
			if( oldestSpawnTime > hull->spawnTime ) {
				oldestSpawnTime = hull->spawnTime;
				oldestHull = hull;
			}
		}
		assert( oldestHull );
		wsw::unlink( oldestHull, head );
		if constexpr( HasShapeList ) {
			hullShapeList = oldestHull->shapeList;
		}
		oldestHull->~Hull();
		mem = oldestHull;
	}

	auto *const hull = new( mem )Hull;
	hull->spawnTime  = currTime;
	hull->lifetime   = lifetime;
	if constexpr( HasShapeList ) {
		hull->shapeList = hullShapeList;
	}

	wsw::link( hull, head );
	return hull;
}

void SimulatedHullsSystem::setupHullVertices( BaseRegularSimulatedHull *hull, const float *origin,
											  const float *color, float speed, float speedSpead ) {
	const byte_vec4_t initialColor {
		(uint8_t)( color[0] * 255 ),
		(uint8_t)( color[1] * 255 ),
		(uint8_t)( color[2] * 255 ),
		(uint8_t)( color[3] * 255 )
	};

	const float minSpeed = wsw::max( 0.0f, speed - 0.5f * speedSpead );
	const float maxSpeed = speed + 0.5f * speedSpead;
	wsw::RandomGenerator *__restrict rng = &m_rng;

	const float originX = origin[0], originY = origin[1], originZ = origin[2];
	const auto [verticesSpan, indicesSpan, _] = ::basicHullsHolder.getIcosphereForLevel( hull->subdivLevel );
	const auto *__restrict vertices = verticesSpan.data();

	std::memset( hull->vertexForceVelocities, 0, verticesSpan.size() * sizeof( hull->vertexForceVelocities[0] ) );

	vec4_t *const __restrict positions       = hull->vertexPositions[0];
	vec4_t *const __restrict vertexNormals   = hull->vertexNormals;
	vec3_t *const __restrict burstVelocities = hull->vertexBurstVelocities;
	vec4_t *const __restrict altPositions    = hull->vertexPositions[1];
	byte_vec4_t *const __restrict colors     = hull->vertexColors;

	size_t i = 0;
	do {
		// Vertex positions are absolute to simplify simulation
		Vector4Set( positions[i], originX, originY, originZ, 1.0f );
		const float vertexSpeed = rng->nextFloat( minSpeed, maxSpeed );
		// Unit vertices define directions
		VectorScale( vertices[i], vertexSpeed, burstVelocities[i] );
		// Set the 4th component to 1.0 for alternating positions once as well
		altPositions[i][3] = 1.0f;
		Vector4Copy( initialColor, colors[i] );
		if( vertexNormals ) {
			// Supply vertex directions as normals for the first simulation frame
			VectorCopy( vertices[i], vertexNormals[i] );
			vertexNormals[i][3] = 0.0f;
		}
	} while( ++i < verticesSpan.size() );

	hull->minZLastFrame = originZ - 1.0f;
	hull->maxZLastFrame = originZ + 1.0f;

	VectorCopy( origin, hull->origin );
}

void SimulatedHullsSystem::setupHullVertices( BaseConcentricSimulatedHull *hull, const float *origin,
											  float scale, std::span<const HullLayerParams> layerParams ) {
	assert( layerParams.size() == hull->numLayers );

	const float originX = origin[0], originY = origin[1], originZ = origin[2];
	const auto [verticesSpan, indicesSpan, neighboursSpan] = ::basicHullsHolder.getIcosphereForLevel( hull->subdivLevel );
	const vec4_t *__restrict vertices = verticesSpan.data();

	// Calculate move limits in each direction

	float maxVertexSpeed = layerParams[0].speed + layerParams[0].maxSpeedSpike + layerParams[0].biasAlongChosenDir ;
	for( unsigned i = 1; i < layerParams.size(); ++i ) {
		const auto &params = layerParams[i];
		maxVertexSpeed = wsw::max( maxVertexSpeed, params.speed + params.maxSpeedSpike + params.biasAlongChosenDir );
	}

	// To prevent noticeable z-fighting in case if hulls of different layers start matching (e.g due to bias)
	constexpr float maxSmallRandomOffset = 1.5f;

	maxVertexSpeed += maxSmallRandomOffset;
	// Scale by the fine-tuning scale multiplier
	maxVertexSpeed *= 1.5f * scale;

	wsw::RandomGenerator *const __restrict rng = &m_rng;

	const float radius = 0.5f * maxVertexSpeed * ( 1e-3f * (float)hull->lifetime );
	const vec3_t growthMins { originX - radius, originY - radius, originZ - radius };
	const vec3_t growthMaxs { originX + radius, originY + radius, originZ + radius };

	// TODO: Add a fused call
	CM_BuildShapeList( cl.cms, m_tmpShapeList, growthMins, growthMaxs, MASK_SOLID );
	CM_ClipShapeList( cl.cms, m_tmpShapeList, m_tmpShapeList, growthMins, growthMaxs );

	trace_t trace;
	for( size_t i = 0; i < verticesSpan.size(); ++i ) {
		// Vertices of the unit hull define directions
		const float *dir = verticesSpan[i];

		vec3_t limitPoint;
		VectorMA( origin, radius, dir, limitPoint );

		CM_ClipToShapeList( cl.cms, m_tmpShapeList, &trace, origin, limitPoint, vec3_origin, vec3_origin, MASK_SOLID );
		hull->limitsAtDirections[i] = trace.fraction * radius;
	}

	auto *const __restrict spikeSpeedBoost = (float *)alloca( sizeof( float ) * verticesSpan.size() );

	const float *const __restrict globalBiasDir = verticesSpan[rng->nextBounded( verticesSpan.size() ) ];
	assert( std::fabs( VectorLengthFast( globalBiasDir ) - 1.0f ) < 0.001f );

	// Compute a hull-global bias for every vertex once
	auto *const __restrict globalVertexDotBias = (float *)alloca( sizeof( float ) * verticesSpan.size() );
	for( unsigned i = 0; i < verticesSpan.size(); ++i ) {
		const float *__restrict vertex = verticesSpan[i];
		// We have decided that permitting a negative bias yields better results.
		globalVertexDotBias[i] = DotProduct( vertex, globalBiasDir );
	}

	// Setup layers data
	assert( hull->numLayers >= 1 && hull->numLayers < 8 );
	for( unsigned layerNum = 0; layerNum < hull->numLayers; ++layerNum ) {
		BaseConcentricSimulatedHull::Layer *layer   = &hull->layers[layerNum];
		const HullLayerParams *__restrict params    = &layerParams[layerNum];
		vec4_t *const __restrict positions          = layer->vertexPositions;
		byte_vec4_t *const __restrict colors        = layer->vertexColors;
		vec2_t *const __restrict speedsAndDistances = layer->vertexSpeedsAndDistances;
		const float *const __restrict layerBiasDir  = verticesSpan[rng->nextBounded( verticesSpan.size() )];

		assert( params->speed >= 0.0f && params->minSpeedSpike >= 0.0f && params->biasAlongChosenDir >= 0.0f );
		assert( params->minSpeedSpike < params->maxSpeedSpike );
		assert( std::fabs( VectorLengthFast( layerBiasDir ) - 1.0f ) < 0.001f );

		layer->finalOffset         = params->finalOffset;
		layer->colorChangeTimeline = params->colorChangeTimeline;

		std::fill( spikeSpeedBoost, spikeSpeedBoost + verticesSpan.size(), 0.0f );

		const float *const __restrict baseColor  = params->baseInitialColor;
		const float *const __restrict bulgeColor = params->bulgeInitialColor;

		for( size_t i = 0; i < verticesSpan.size(); ++i ) {
			// Position XYZ is computed prior to submission in stateless fashion
			positions[i][3] = 1.0f;

			const float *vertexDir   = vertices[i];
			const float layerDotBias = DotProduct( vertexDir, layerBiasDir );
			const float layerSqrBias = std::copysign( layerDotBias * layerDotBias, layerDotBias );
			const float vertexBias   = wsw::max( layerSqrBias, globalVertexDotBias[i] );

			speedsAndDistances[i][0] = params->speed + vertexBias * params->biasAlongChosenDir;

			if( rng->tryWithChance( params->speedSpikeChance ) ) [[unlikely]] {
				const float boost = rng->nextFloat( params->minSpeedSpike, params->maxSpeedSpike );
				spikeSpeedBoost[i] += boost;
				const auto &indicesOfNeighbours = neighboursSpan[i];
				for( const unsigned neighbourIndex: indicesOfNeighbours ) {
					spikeSpeedBoost[neighbourIndex] += rng->nextFloat( 0.50f, 0.75f ) * boost;
				}
			} else {
				speedsAndDistances[i][0] += rng->nextFloat( 0.0f, maxSmallRandomOffset );
			}

			speedsAndDistances[i][1] = 0.0f;

			const float colorLerpFrac  = vertexBias * vertexBias;
			const float complementFrac = 1.0f - colorLerpFrac;
			colors[i][0] = (uint8_t)( 255.0f * ( baseColor[0] * colorLerpFrac + bulgeColor[0] * complementFrac ) );
			colors[i][1] = (uint8_t)( 255.0f * ( baseColor[1] * colorLerpFrac + bulgeColor[1] * complementFrac ) );
			colors[i][2] = (uint8_t)( 255.0f * ( baseColor[2] * colorLerpFrac + bulgeColor[2] * complementFrac ) );
			colors[i][3] = (uint8_t)( 255.0f * ( baseColor[3] * colorLerpFrac + bulgeColor[3] * complementFrac ) );
		}

		for( size_t i = 0; i < verticesSpan.size(); ++i ) {
			speedsAndDistances[i][0] += wsw::min( spikeSpeedBoost[i], maxVertexSpeed );
			// Scale by the fine-tuning scale multiplier
			speedsAndDistances[i][0] *= scale;
		}
	}

	VectorCopy( origin, hull->origin );
	hull->vertexMoveDirections = vertices;
}

void SimulatedHullsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *drawSceneRequest ) {
	// Limit the time step
	const float timeDeltaSeconds = 1e-3f * (float)wsw::min<int64_t>( 33, currTime - m_lastTime );

	constexpr unsigned numRegularHulls = kMaxSmokeHulls + kMaxWaveHulls;
	wsw::StaticVector<BaseRegularSimulatedHull *, numRegularHulls> activeRegularHulls;

	constexpr unsigned numConcentricHulls = kMaxFireHulls + kMaxFireClusterHulls + kMaxBlastHulls;
	wsw::StaticVector<BaseConcentricSimulatedHull *, numConcentricHulls> activeConcentricHulls;

	for( FireHull *hull = m_fireHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeConcentricHulls.push_back( hull );
		} else {
			unlinkAndFreeFireHull( hull );
		}
	}
	for( FireClusterHull *hull = m_fireClusterHullsHead, *next = nullptr; hull; hull = next ) { next = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeConcentricHulls.push_back( hull );
		} else {
			unlinkAndFreeFireClusterHull( hull );
		}
	}
	for( BlastHull *hull = m_blastHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeConcentricHulls.push_back( hull );
		} else {
			unlinkAndFreeBlastHull( hull );
		}
	}
	for( SmokeHull *hull = m_smokeHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeRegularHulls.push_back( hull );
		} else {
			unlinkAndFreeSmokeHull( hull );
		}
	}
	for( WaveHull *hull = m_waveHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeRegularHulls.push_back( hull );
		} else {
			unlinkAndFreeWaveHull( hull );
		}
	}

	for( BaseRegularSimulatedHull *__restrict hull: activeRegularHulls ) {
		HullDynamicMesh *__restrict mesh = &hull->submittedMesh;
		assert( hull->subdivLevel );

		Vector4Copy( hull->mins, mesh->cullMins );
		Vector4Copy( hull->maxs, mesh->cullMaxs );

		mesh->applyVertexDynLight    = hull->applyVertexDynLight;
		mesh->material               = nullptr;

		mesh->m_simulatedPositions   = hull->vertexPositions[hull->positionsFrame];
		mesh->m_simulatedNormals     = hull->vertexNormals;
		mesh->m_simulatedColors      = hull->vertexColors;
		mesh->m_minZLastFrame        = hull->minZLastFrame;
		mesh->m_maxZLastFrame        = hull->maxZLastFrame;
		mesh->m_viewDotFade          = hull->vertexViewDotFade;
		mesh->m_zFade                = hull->vertexZFade;
		mesh->m_simulatedSubdivLevel = hull->subdivLevel;
		mesh->m_tesselateClosestLod  = hull->tesselateClosestLod;
		mesh->m_lerpNextLevelColors  = hull->leprNextLevelColors;

		drawSceneRequest->addDynamicMesh( mesh );
	}

	for( const BaseConcentricSimulatedHull *__restrict hull: activeConcentricHulls ) {
		assert( hull->numLayers );

		std::optional<uint8_t> drawOnTopPartIndex;
		for( unsigned i = 0; i < hull->numLayers; ++i ) {
			BaseConcentricSimulatedHull::Layer *__restrict layer = &hull->layers[i];
			HullDynamicMesh *__restrict mesh = hull->layers[i].submittedMesh;

			Vector4Copy( layer->mins, mesh->cullMins );
			Vector4Copy( layer->maxs, mesh->cullMaxs );

			mesh->m_simulatedPositions   = layer->vertexPositions;
			mesh->m_simulatedNormals     = nullptr;
			mesh->m_simulatedColors      = layer->vertexColors;
			mesh->material               = nullptr;
			mesh->applyVertexDynLight    = hull->applyVertexDynLight;
			mesh->m_minZLastFrame        = 0.0f;
			mesh->m_maxZLastFrame        = 0.0f;
			mesh->m_viewDotFade          = layer->overrideHullFade ? *layer->overrideHullFade : hull->vertexViewDotFade;
			mesh->m_zFade                = ZFade::NoFade;
			mesh->m_simulatedSubdivLevel = hull->subdivLevel;
			mesh->m_tesselateClosestLod  = true;
			mesh->m_lerpNextLevelColors  = true;

			if( layer->useDrawOnTopHack ) [[unlikely]] {
				assert( drawOnTopPartIndex == std::nullopt );
				drawOnTopPartIndex = (uint8_t)i;
			}
		}

		drawSceneRequest->addCompoundDynamicMesh( hull->mins, hull->maxs,
												  hull->firstSubmittedMesh, hull->numLayers, drawOnTopPartIndex );
	}

	m_lastTime = currTime;
}

void SimulatedHullsSystem::BaseRegularSimulatedHull::simulate( int64_t currTime, float timeDeltaSeconds,
															   wsw::RandomGenerator *__restrict rng ) {
	const vec4_t *const __restrict oldPositions = this->vertexPositions[positionsFrame];
	// Switch old/new positions buffer
	positionsFrame = ( positionsFrame + 1 ) % 2;
	vec4_t *const __restrict newPositions = this->vertexPositions[positionsFrame];

	vec3_t *const __restrict forceVelocities = this->vertexForceVelocities;
	vec3_t *const __restrict burstVelocities = this->vertexBurstVelocities;

	const auto &icosphereData  = ::basicHullsHolder.getIcosphereForLevel( subdivLevel );
	const unsigned numVertices = icosphereData.vertices.size();
	auto *const __restrict combinedVelocities = (vec3_t *)alloca( sizeof( vec3_t ) * numVertices );

	BoundsBuilder boundsBuilder;
	assert( timeDeltaSeconds < 0.1f );

	// Compute ideal positions (as if there were no obstacles)

	const float burstSpeedDecayMultiplier = 1.0f - 1.5f * timeDeltaSeconds;
	if( expansionStartAt > currTime ) {
		for( unsigned i = 0; i < numVertices; ++i ) {
			VectorScale( burstVelocities[i], burstSpeedDecayMultiplier, burstVelocities[i] );
			VectorAdd( burstVelocities[i], forceVelocities[i], combinedVelocities[i] );
			VectorMA( oldPositions[i], timeDeltaSeconds, combinedVelocities[i], newPositions[i] );
			// TODO: We should be able to supply vec4
			boundsBuilder.addPoint( newPositions[i] );
		}
	} else {
		// Having vertex normals buffer is now mandatory for hulls expansion
		const vec4_t *const __restrict normals = vertexNormals;
		assert( normals );

		const float rcpDeltaZ = ( maxZLastFrame - minZLastFrame ) > 0.1f ? Q_Rcp( maxZLastFrame - minZLastFrame ) : 1.0f;
		for( unsigned i = 0; i < numVertices; ++i ) {
			const float zFrac               = ( oldPositions[i][2] - minZLastFrame ) * rcpDeltaZ;
			const float archimedesAccel     = std::lerp( archimedesBottomAccel, archimedesTopAccel, Q_Sqrt( zFrac ) );
			const float expansionAccel      = std::lerp( xyExpansionBottomAccel, xyExpansionTopAccel, zFrac );
			const float expansionMultiplier = expansionAccel * timeDeltaSeconds;
			forceVelocities[i][0] += expansionMultiplier * normals[i][0];
			forceVelocities[i][1] += expansionMultiplier * normals[i][1];
			forceVelocities[i][2] += archimedesAccel * timeDeltaSeconds;

			VectorScale( burstVelocities[i], burstSpeedDecayMultiplier, burstVelocities[i] );
			VectorAdd( burstVelocities[i], forceVelocities[i], combinedVelocities[i] );

			VectorMA( oldPositions[i], timeDeltaSeconds, combinedVelocities[i], newPositions[i] );
			// TODO: We should be able to supply vec4
			boundsBuilder.addPoint( newPositions[i] );
		}
	}

	vec3_t verticesMins, verticesMaxs;
	boundsBuilder.storeToWithAddedEpsilon( verticesMins, verticesMaxs );
	// TODO: Allow bounds builder to store 4-vectors
	VectorCopy( verticesMins, mins );
	VectorCopy( verticesMaxs, maxs );
	mins[3] = 0.0f, maxs[3] = 1.0f;

	// TODO: Add a fused call
	CM_BuildShapeList( cl.cms, shapeList, verticesMins, verticesMaxs, MASK_SOLID | MASK_WATER );
	CM_ClipShapeList( cl.cms, shapeList, shapeList, verticesMins, verticesMaxs );

	minZLastFrame = std::numeric_limits<float>::max();
	maxZLastFrame = std::numeric_limits<float>::lowest();

	// int type is used to mitigate possible branching while performing bool->float conversions
	auto *const __restrict isVertexNonContacting          = (int *)alloca( sizeof( int ) * numVertices );
	auto *const __restrict indicesOfNonContactingVertices = (unsigned *)alloca( sizeof( unsigned ) * numVertices );

	trace_t clipTrace, slideTrace;
	unsigned numNonContactingVertices = 0;
	for( unsigned i = 0; i < numVertices; ++i ) {
		CM_ClipToShapeList( cl.cms, shapeList, &clipTrace, oldPositions[i], newPositions[i],
							vec3_origin, vec3_origin, MASK_SOLID );
		if( clipTrace.fraction == 1.0f ) [[likely]] {
			isVertexNonContacting[i] = 1;
			indicesOfNonContactingVertices[numNonContactingVertices++] = i;
		} else {
			isVertexNonContacting[i] = 0;
			bool putVertexAtTheContactPosition = true;
			if( const float squareSpeed = VectorLengthSquared( combinedVelocities[i] ); squareSpeed > 10.0f * 10.0f ) {
				vec3_t velocityDir;
				const float rcpSpeed = Q_RSqrt( squareSpeed );
				VectorScale( combinedVelocities[i], rcpSpeed, velocityDir );
				if( const float dot = std::fabs( DotProduct( velocityDir, clipTrace.plane.normal ) ); dot < 0.95f ) {
					const float speed               = Q_Rcp( rcpSpeed );
					const float idealMoveThisFrame  = timeDeltaSeconds * speed;
					const float distanceToObstacle  = idealMoveThisFrame * clipTrace.fraction;
					const float distanceAlongNormal = dot * distanceToObstacle;

					//   a'     c'
					//      ^ <--- ^    b' + c' = a'    | a' = lengthAlongNormal * surface normal'
					//      |     /                     | b' = -distanceToObstacle * velocity dir'
					//      |    /                      | c' = slide vec
					//      |   / b'                    | P  = trace endpos
					//      |  /
					//      | /
					// ____ |/__________
					//      P

					// c = a - b;

					vec3_t normalVec;
					VectorScale( clipTrace.plane.normal, distanceAlongNormal, normalVec );
					vec3_t vecToObstacle;
					VectorScale( velocityDir, -distanceToObstacle, vecToObstacle );
					vec3_t slideVec;
					VectorSubtract( normalVec, vecToObstacle, slideVec );

					// If the slide distance is sufficient for checks
					if( VectorLengthSquared( slideVec ) > 1.0f * 1.0f ) {
						vec3_t slideStartPoint, slideEndPoint;
						// Add an offset from the surface while testing sliding
						VectorAdd( clipTrace.endpos, clipTrace.plane.normal, slideStartPoint );
						VectorAdd( slideStartPoint, slideVec, slideEndPoint );

						CM_ClipToShapeList( cl.cms, shapeList, &slideTrace, slideStartPoint, slideEndPoint,
											vec3_origin, vec3_origin, MASK_SOLID );
						if( slideTrace.fraction == 1.0f ) {
							VectorCopy( slideEndPoint, newPositions[i] );
							putVertexAtTheContactPosition = false;
							// TODO: Modify velocity as well?
						}
					}
				}
			}

			if( putVertexAtTheContactPosition ) {
				VectorAdd( clipTrace.endpos, clipTrace.plane.normal, newPositions[i] );
			}

			// The final position for contacting vertices is considered to be known.
			const float *__restrict position = newPositions[i];
			minZLastFrame = wsw::min( minZLastFrame, position[2] );
			maxZLastFrame = wsw::max( maxZLastFrame, position[2] );

			// Make the contacting vertex transparent
			vertexColors[i][3] = 0;
		}
	}

	if( numNonContactingVertices ) {
		// Update positions of non-contacting vertices
		unsigned i;
		if( numNonContactingVertices != numVertices ) {
			const IcosphereData &icosphereData           = ::basicHullsHolder.getIcosphereForLevel( subdivLevel );
			auto *const __restrict neighboursForVertices = icosphereData.vertexNeighbours.data();
			auto *const __restrict neighboursFreeMoveSum = (int *)alloca( sizeof( int ) * numNonContactingVertices );

			std::memset( neighboursFreeMoveSum, 0, sizeof( float ) * numNonContactingVertices );

			i = 0;
			do {
				const unsigned vertexIndex = indicesOfNonContactingVertices[i];
				assert( vertexIndex < numVertices );
				for( const unsigned neighbourIndex: neighboursForVertices[vertexIndex] ) {
					// Add to the integer accumulator
					neighboursFreeMoveSum[i] += isVertexNonContacting[neighbourIndex];
				}
			} while( ++i < numNonContactingVertices );

			assert( std::size( neighboursForVertices[0] ) == 5 );
			constexpr float rcpNumVertexNeighbours = 0.2f;

			i = 0;
			do {
				const unsigned vertexIndex = indicesOfNonContactingVertices[i];
				const float neighboursFrac = (float)neighboursFreeMoveSum[i] * rcpNumVertexNeighbours;
				assert( neighboursFrac >= 0.0f && neighboursFrac <= 1.0f );
				const float lerpFrac = ( 1.0f / 3.0f ) + ( 2.0f / 3.0f ) * neighboursFrac;

				float *const __restrict position = newPositions[vertexIndex];
				VectorLerp( oldPositions[vertexIndex], lerpFrac, position, position );

				minZLastFrame = wsw::min( minZLastFrame, position[2] );
				maxZLastFrame = wsw::max( maxZLastFrame, position[2] );
			} while( ++i < numNonContactingVertices );
		} else {
			// Just update positions.
			// This is worth the separate branch as its quite common for smoke hulls.
			i = 0;
			do {
				const unsigned vertexIndex       = indicesOfNonContactingVertices[i];
				const float *__restrict position = newPositions[vertexIndex];
				minZLastFrame = wsw::min( minZLastFrame, position[2] );
				maxZLastFrame = wsw::max( maxZLastFrame, position[2] );
			} while( ++i < numNonContactingVertices );
		}
	}

	// Once positions are defined, recalculate vertex normals
	if( vertexNormals ) {
		const auto neighboursOfVertices = icosphereData.vertexNeighbours.data();
		unsigned vertexNum = 0;
		do {
			const uint16_t *const neighboursOfVertex = neighboursOfVertices[vertexNum];
			const float *const __restrict currVertex = newPositions[vertexNum];
			float *const __restrict normal           = vertexNormals[vertexNum];

			unsigned neighbourIndex = 0;
			bool hasAddedDirs = false;
			Vector4Clear( normal );
			do {
				const float *__restrict v2 = newPositions[neighboursOfVertex[neighbourIndex]];
				const float *__restrict v3 = newPositions[neighboursOfVertex[( neighbourIndex + 1 ) % 5]];
				vec3_t currTo2, currTo3, cross;
				VectorSubtract( v2, currVertex, currTo2 );
				VectorSubtract( v3, currVertex, currTo3 );
				CrossProduct( currTo2, currTo3, cross );
				if( const float squaredLength = VectorLengthSquared( cross ); squaredLength > 1.0f ) [[likely]] {
					const float rcpLength = Q_RSqrt( squaredLength );
					VectorMA( normal, rcpLength, cross, normal );
					hasAddedDirs = true;
				}
			} while( ++neighbourIndex < 5 );

			if( hasAddedDirs ) [[likely]] {
				VectorNormalizeFast( normal );
			} else {
				// Copy the unit vector of the original position as a normal
				VectorCopy( icosphereData.vertices[vertexNum], normal );
			}
		} while( ++vertexNum < numVertices );
	}

	const bool hasChangedColors = processColorChange( currTime, spawnTime, lifetime, colorChangeTimeline,
													  { vertexColors, numVertices }, &colorChangeState, rng );
	if( hasChangedColors && noColorChangeVertexColor && !noColorChangeIndices.empty() ) {
		for( const auto index: noColorChangeIndices ) {
			Vector4Copy( noColorChangeVertexColor, vertexColors[index] );
		}
	}
}

void SimulatedHullsSystem::BaseConcentricSimulatedHull::simulate( int64_t currTime, float timeDeltaSeconds,
																  wsw::RandomGenerator *__restrict rng ) {
	// Just move all vertices along directions clipping by limits

	BoundsBuilder hullBoundsBuilder;

	const float *__restrict growthOrigin = this->origin;
	const unsigned numVertices = basicHullsHolder.getIcosphereForLevel( subdivLevel ).vertices.size();

	const float speedMultiplier = 1.0f - 1.5f * timeDeltaSeconds;

	// Sanity check
	assert( numLayers >= 1 && numLayers < 8 );
	for( unsigned layerNum = 0; layerNum < numLayers; ++layerNum ) {
		BoundsBuilder layerBoundsBuilder;

		BaseConcentricSimulatedHull::Layer *layer   = &layers[layerNum];
		vec4_t *const __restrict positions          = layer->vertexPositions;
		vec2_t *const __restrict speedsAndDistances = layer->vertexSpeedsAndDistances;

		const float finalOffset = layer->finalOffset;
		for( unsigned i = 0; i < numVertices; ++i ) {
			float speed = speedsAndDistances[i][0];
			float distanceSoFar = speedsAndDistances[i][1];

			speed *= speedMultiplier;
			distanceSoFar += speed * timeDeltaSeconds;

			// Limit growth by the precomputed obstacle distance
			const float limit = wsw::max( 0.0f, limitsAtDirections[i] - finalOffset );
			distanceSoFar = wsw::min( distanceSoFar, limit );

			VectorMA( growthOrigin, distanceSoFar, vertexMoveDirections[i], positions[i] );

			// TODO: Allow supplying 4-component in-memory vectors directly
			layerBoundsBuilder.addPoint( positions[i] );

			// Write back to memory
			speedsAndDistances[i][0] = speed;
			speedsAndDistances[i][1] = distanceSoFar;
		}

		// TODO: Allow storing 4-component float vectors to memory directly
		layerBoundsBuilder.storeTo( layer->mins, layer->maxs );
		layer->mins[3] = 0.0f, layer->maxs[3] = 1.0f;

		// Don't relying on what hull is external is more robust

		// TODO: Allow adding other builder directly
		hullBoundsBuilder.addPoint( layer->mins );
		hullBoundsBuilder.addPoint( layer->maxs );
	}

	// TODO: Allow storing 4-component float vectors to memory directly
	hullBoundsBuilder.storeTo( this->mins, this->maxs );
	this->mins[3] = 0.0f, this->maxs[3] = 1.0f;

	for( unsigned i = 0; i < numLayers; ++i ) {
		Layer *const layer = &layers[i];
		processColorChange( currTime, spawnTime, lifetime, layer->colorChangeTimeline,
							{ layer->vertexColors, numVertices }, &layer->colorChangeState, rng );
	}
}

auto SimulatedHullsSystem::computeCurrTimelineNodeIndex( unsigned startFromIndex, int64_t currTime,
														 int64_t spawnTime, unsigned effectDuration,
														 std::span<const ColorChangeTimelineNode> timeline )
	-> unsigned {
	// Sanity checks
	assert( effectDuration && effectDuration < std::numeric_limits<uint16_t>::max() );
	assert( currTime - spawnTime > 0 && currTime - spawnTime < std::numeric_limits<uint16_t>::max() );

	assert( startFromIndex < timeline.size() );

	const auto currLifetimeFraction = (float)( currTime - spawnTime ) * Q_Rcp( (float)effectDuration );
	assert( currLifetimeFraction >= 0.0f && currLifetimeFraction <= 1.001f );

	// Assume that startFromIndex is "good" even if the conditions in the loop won't be held for it
	unsigned currIndex = startFromIndex, lastGoodIndex = startFromIndex;
	for(;; ) {
		if( currIndex == timeline.size() ) {
			break;
		}
		if( timeline[currIndex].activateAtLifetimeFraction > currLifetimeFraction ) {
			break;
		}
		lastGoodIndex = currIndex;
		currIndex++;
	}

	return lastGoodIndex;
}

enum ColorChangeFlags : unsigned { MayDrop = 0x1, MayReplace = 0x2, MayIncreaseOpacity = 0x4, AssumePow2Palette = 0x8 };

template <unsigned Flags>
static void changeColors( std::span<byte_vec4_t> colorsSpan, wsw::RandomGenerator *__restrict rng,
						  [[maybe_unused]] std::span<const byte_vec4_t> replacementPalette,
						  [[maybe_unused]] float dropChance, [[maybe_unused]] float replacementChance ) {

	byte_vec4_t *const __restrict colors = colorsSpan.data();
	const unsigned numColors             = colorsSpan.size();

	[[maybe_unused]] unsigned paletteIndexMask = 0;
	if constexpr( Flags & AssumePow2Palette ) {
		paletteIndexMask = (unsigned)( replacementPalette.size() - 1 );
	}

	unsigned i = 0;
	do {
		// Don't process elements that became void
		if( colors[i][3] != 0 ) [[likely]] {
			[[maybe_unused]] bool dropped = false;
			if constexpr( Flags & MayDrop ) {
				if( rng->tryWithChance( dropChance ) ) [[unlikely]] {
					colors[i][3] = 0;
					dropped = true;
				}
			}
			if constexpr( Flags & MayReplace ) {
				if( !dropped ) {
					if( rng->tryWithChance( replacementChance ) ) [[unlikely]] {
						const uint8_t *chosenColor;
						if constexpr( Flags & AssumePow2Palette ) {
							chosenColor = replacementPalette[rng->next() & paletteIndexMask];
						} else {
							chosenColor = replacementPalette[rng->nextBounded( replacementPalette.size() ) ];
						}
						auto *const existingColor = colors[i];
						if constexpr( Flags & MayIncreaseOpacity ) {
							Vector4Copy( chosenColor, existingColor );
						} else {
							// Branching could be perfectly avoided in this case
							// (replacement is actually not that "unlikely" for some parts of hull lifetimes
							// so slightly optimizing it makes sense).
							const uint8_t *srcColorsToSelect[2] { existingColor, chosenColor };
							const uint8_t *selectedColor = srcColorsToSelect[chosenColor[3] <= existingColor[3]];
							Vector4Copy( selectedColor, existingColor );
						}
					}
				}
			}
		}
	} while( ++i < numColors );
}

static const struct ColorChangeFnTableHolder {
	using Fn = void (*)( std::span<byte_vec4_t>, wsw::RandomGenerator *, std::span<const byte_vec4_t>, float, float );
	Fn table[16] {};

#define ADD_FN_FOR_FLAGS_TO_TABLE( Flags ) do { table[Flags] = &::changeColors<Flags>; } while( 0 )
	ColorChangeFnTableHolder() noexcept {
		// Add a reference to a template instantiation for each feasible combination of flags.
		// (Don't force generating code for unfeasible ones
		// by a brute-force instantiation of templates for all 16 values).
		ADD_FN_FOR_FLAGS_TO_TABLE( MayDrop | MayReplace | MayIncreaseOpacity | AssumePow2Palette );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayDrop | MayReplace | MayIncreaseOpacity );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayDrop | MayReplace | AssumePow2Palette );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayDrop | MayReplace );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayReplace | MayIncreaseOpacity | AssumePow2Palette );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayReplace | MayIncreaseOpacity );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayReplace | AssumePow2Palette );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayReplace );
		ADD_FN_FOR_FLAGS_TO_TABLE( MayDrop );
	}
#undef ADD_FN_FOR_FLAGS_TO_TABLE
} colorChangeFnTableHolder;

bool SimulatedHullsSystem::processColorChange( int64_t currTime,
											   int64_t spawnTime,
											   unsigned effectDuration,
											   std::span<const ColorChangeTimelineNode> timeline,
											   std::span<byte_vec4_t> colorsSpan,
											   ColorChangeState *__restrict state,
											   wsw::RandomGenerator *__restrict rng ) {
	// This helps to handle non-color-changing hulls in a least-effort fashion
	if( state->lastNodeIndex >= timeline.size() ) [[unlikely]] {
		return false;
	}

	// Compute the current node in an immediate mode. This is inexpensive for a realistic input.
	state->lastNodeIndex = computeCurrTimelineNodeIndex( state->lastNodeIndex, currTime, spawnTime,
														 effectDuration, timeline );

	constexpr int64_t colorChangeIntervalMillis = 15;

	const ColorChangeTimelineNode &currNode = timeline[state->lastNodeIndex];
	if( state->lastColorChangeAt + colorChangeIntervalMillis > currTime ) [[likely]] {
		return false;
	}

	// Do nothing during the first frame
	if( state->lastColorChangeAt <= 0 ) [[unlikely]] {
		state->lastColorChangeAt = currTime;
		return false;
	}

	const auto timeDeltaSeconds = 1e-3f * (float)( currTime - state->lastColorChangeAt );
	assert( timeDeltaSeconds >= 1e-3f * (float)colorChangeIntervalMillis );
	state->lastColorChangeAt = currTime;

	float currSegmentLifetimePercentage;
	if( state->lastNodeIndex + 1 < timeline.size() ) {
		const ColorChangeTimelineNode &nextNode = timeline[state->lastNodeIndex + 1];
		currSegmentLifetimePercentage = nextNode.activateAtLifetimeFraction - currNode.activateAtLifetimeFraction;
	} else {
		currSegmentLifetimePercentage = 1.0f - currNode.activateAtLifetimeFraction;
	}

	assert( currSegmentLifetimePercentage >= 0.0f && currSegmentLifetimePercentage <= 1.0f );
	assert( currSegmentLifetimePercentage > 1e-3f );

	// Reconstruct duration of this timeline segment
	const float currSegmentDurationSeconds = currSegmentLifetimePercentage * ( 1e-3f * (float)effectDuration );

	float dropChance, replacementChance;
	// Protect from going out of value bounds (e.g. after a freeze due to external reasons)
	if( timeDeltaSeconds < currSegmentDurationSeconds ) [[likely]] {
		// Compute how much this color change frame takes of the segment
		// TODO: Don't convert to seconds prior to the division?
		const auto currFrameFractionOfTheSegment = timeDeltaSeconds * Q_Rcp( currSegmentDurationSeconds );
		assert( currFrameFractionOfTheSegment > 0.0f && currFrameFractionOfTheSegment <= 1.0f );
		// Convert accumulative chances for the entire segment to chances for this color change frame.
		dropChance        = currNode.sumOfDropChanceForThisSegment * currFrameFractionOfTheSegment;
		replacementChance = currNode.sumOfReplacementChanceForThisSegment * currFrameFractionOfTheSegment;
	} else {
		dropChance        = currNode.sumOfDropChanceForThisSegment;
		replacementChance = currNode.sumOfReplacementChanceForThisSegment;
	}

	// Don't let the chance drop to zero if the specified integral chance is non-zero.
	// Don't let it exceed 1.0 as well during rapid color changes.
	constexpr float minDropChance = 1e-3f, minReplacementChance = 1e-3f;
	if( currNode.sumOfDropChanceForThisSegment > 0.0f ) {
		dropChance = wsw::clamp( dropChance, minDropChance, 1.0f );
	}
	if( currNode.sumOfReplacementChanceForThisSegment > 0.0f ) {
		replacementChance = wsw::clamp( replacementChance, minReplacementChance, 1.0f );
	}

	const auto palette    = currNode.replacementPalette;
	const bool mayDrop    = dropChance > 0.0f;
	const bool mayReplace = replacementChance > 0.0f && !palette.empty();

	if( mayDrop | mayReplace ) {
		const bool isPalettePow2  = ( palette.size() & ( palette.size() - 1u ) ) == 0;
		const unsigned dropBit    = ( mayDrop ) ? MayDrop : 0;
		const unsigned replaceBit = ( mayReplace ) ? MayReplace : 0;
		const unsigned opacityBit = ( currNode.allowIncreasingOpacity ) ? MayIncreaseOpacity : 0;
		const unsigned pow2Bit    = ( mayReplace & isPalettePow2 ) ? AssumePow2Palette : 0;
		const unsigned fnIndex    = dropBit | replaceBit | opacityBit | pow2Bit;

		// Call the respective template specialization for flags
		::colorChangeFnTableHolder.table[fnIndex]( colorsSpan, rng, palette, dropChance, replacementChance );
	}

	return true;
}

class MeshTesselationHelper {
public:
	vec4_t *m_tmpTessPositions { nullptr };
	vec4_t *m_tmpTessFloatColors { nullptr };
	int8_t *m_tessNeighboursCount { nullptr };
	vec4_t *m_tessPositions { nullptr };
	byte_vec4_t *m_tessByteColors { nullptr };
	uint16_t *m_neighbourLessVertices { nullptr };
	bool *m_isVertexNeighbourLess { nullptr };

	std::unique_ptr<uint8_t[]> m_allocationBuffer;
	unsigned m_lastNumNextLevelVertices { 0 };
	unsigned m_lastAllocationSize { 0 };

	template <bool SmoothColors>
	void exec( const SimulatedHullsSystem::HullDynamicMesh *mesh, const byte_vec4_t *overrideColors, unsigned numSimulatedVertices,
			   unsigned numNextLevelVertices, const uint16_t nextLevelNeighbours[][5] );
private:
	static constexpr unsigned kAlignment = 16;

	void setupBuffers( unsigned numSimulatedVertices, unsigned numNextLevelVertices );

	template <bool SmoothColors>
	void runPassOverOriginalVertices( const SimulatedHullsSystem::HullDynamicMesh *mesh, const byte_vec4_t *overrideColors,
									  unsigned numSimulatedVertices, const uint16_t nextLevelNeighbours[][5] );
	template <bool SmoothColors>
	[[nodiscard]]
	auto collectNeighbourLessVertices( unsigned numSimulatedVertices, unsigned numNextLevelVertices ) -> unsigned;
	template <bool SmoothColors>
	void processNeighbourLessVertices( unsigned numNeighbourLessVertices, const uint16_t nextLevelNeighbours[][5] );
	template <bool SmoothColors>
	void runSmoothVerticesPass( unsigned numNextLevelVertices, const uint16_t nextLevelNeighbours[][5] );
};

template <bool SmoothColors>
void MeshTesselationHelper::exec( const SimulatedHullsSystem::HullDynamicMesh *mesh, const byte_vec4_t *overrideColors,
								  unsigned numSimulatedVertices, unsigned numNextLevelVertices,
								  const uint16_t nextLevelNeighbours[][5] ) {
	setupBuffers( numSimulatedVertices, numNextLevelVertices );

	runPassOverOriginalVertices<SmoothColors>( mesh, overrideColors, numSimulatedVertices, nextLevelNeighbours );
	unsigned numNeighbourLessVertices = collectNeighbourLessVertices<SmoothColors>( numSimulatedVertices, numNextLevelVertices );
	processNeighbourLessVertices<SmoothColors>( numNeighbourLessVertices, nextLevelNeighbours );
	runSmoothVerticesPass<SmoothColors>( numNextLevelVertices, nextLevelNeighbours );
}

void MeshTesselationHelper::setupBuffers( unsigned numSimulatedVertices, unsigned numNextLevelVertices ) {
	if( m_lastNumNextLevelVertices != numNextLevelVertices ) {
		// Compute offsets from the base ptr
		wsw::MemSpecBuilder memSpecBuilder( wsw::MemSpecBuilder::initiallyEmpty() );

		const auto tmpTessPositionsSpec       = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tmpTessFloatColorsSpec     = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tessPositionsSpec          = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tmpTessNeighboursCountSpec = memSpecBuilder.add<int8_t>( numNextLevelVertices );
		const auto tessByteColorsSpec         = memSpecBuilder.add<byte_vec4_t>( numNextLevelVertices );
		const auto neighbourLessVerticesSpec  = memSpecBuilder.add<uint16_t>( numNextLevelVertices );
		const auto isNextVertexNeighbourLess  = memSpecBuilder.add<bool>( numNextLevelVertices );

		if ( m_lastAllocationSize < memSpecBuilder.sizeSoFar() ) [[unlikely]] {
			m_lastAllocationSize = memSpecBuilder.sizeSoFar();
			m_allocationBuffer   = std::make_unique<uint8_t[]>( m_lastAllocationSize );
		}

		void *const basePtr     = m_allocationBuffer.get();
		m_tmpTessPositions      = tmpTessPositionsSpec.get( basePtr );
		m_tmpTessFloatColors    = tmpTessFloatColorsSpec.get( basePtr );
		m_tessNeighboursCount   = tmpTessNeighboursCountSpec.get( basePtr );
		m_tessPositions         = tessPositionsSpec.get( basePtr );
		m_tessByteColors        = tessByteColorsSpec.get( basePtr );
		m_neighbourLessVertices = neighbourLessVerticesSpec.get( basePtr );
		m_isVertexNeighbourLess = isNextVertexNeighbourLess.get( basePtr );

		m_lastNumNextLevelVertices = numNextLevelVertices;
	}

	assert( numSimulatedVertices && numNextLevelVertices && numSimulatedVertices < numNextLevelVertices );
	const unsigned numAddedVertices = numNextLevelVertices - numSimulatedVertices;

	std::memset( m_tessPositions, 0, sizeof( m_tessPositions[0] ) * numNextLevelVertices );
	std::memset( m_tessByteColors, 0, sizeof( m_tessByteColors[0] ) * numNextLevelVertices );
	std::memset( m_isVertexNeighbourLess, 0, sizeof( m_isVertexNeighbourLess[0] ) * numNextLevelVertices );

	std::memset( m_tmpTessPositions + numSimulatedVertices, 0, sizeof( m_tmpTessPositions[0] ) * numAddedVertices );
	std::memset( m_tmpTessFloatColors + numSimulatedVertices, 0, sizeof( m_tmpTessFloatColors[0] ) * numAddedVertices );
	std::memset( m_tessNeighboursCount + numSimulatedVertices, 0, sizeof( m_tessNeighboursCount[0] ) * numAddedVertices );
}

template <bool SmoothColors>
void MeshTesselationHelper::runPassOverOriginalVertices( const SimulatedHullsSystem::HullDynamicMesh *mesh,
														 const byte_vec4_t *overrideColors,
														 unsigned numSimulatedVertices,
														 const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessFloatColors  = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	vec4_t *const __restrict tmpTessPositions    = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	const byte_vec4_t *const __restrict colors   = overrideColors ? overrideColors : mesh->m_simulatedColors;
	byte_vec4_t *const __restrict tessByteColors = m_tessByteColors;
	int8_t *const __restrict tessNeighboursCount = m_tessNeighboursCount;

	// For each vertex in the original mesh
	for( unsigned vertexIndex = 0; vertexIndex < numSimulatedVertices; ++vertexIndex ) {
		const auto byteColor  = colors[vertexIndex];
		const float *position = mesh->m_simulatedPositions[vertexIndex];

		if constexpr( SmoothColors ) {
			// Write the color to the accum buffer for smoothing it later
			Vector4Copy( byteColor, tmpTessFloatColors[vertexIndex] );
		} else {
			// Write the color directly to the resulting color buffer
			Vector4Copy( byteColor, tessByteColors[vertexIndex] );
		}

		// Copy for the further smooth pass
		Vector4Copy( position, tmpTessPositions[vertexIndex] );

		// For each neighbour of this vertex in the tesselated mesh
		for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
			if( neighbourIndex >= numSimulatedVertices ) {
				VectorAdd( tmpTessPositions[neighbourIndex], position, tmpTessPositions[neighbourIndex] );
				// Add integer values as is to the float accumulation buffer
				Vector4Add( tmpTessFloatColors[neighbourIndex], byteColor, tmpTessFloatColors[neighbourIndex] );
				tessNeighboursCount[neighbourIndex]++;
			}
		}
	}
}

template <bool SmoothColors>
auto MeshTesselationHelper::collectNeighbourLessVertices( unsigned numSimulatedVertices,
														  unsigned numNextLevelVertices ) -> unsigned {
	assert( numSimulatedVertices && numNextLevelVertices && numSimulatedVertices < numNextLevelVertices );

	vec4_t *const __restrict tmpTessPositions        = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	vec4_t *const __restrict  tmpTessFloatColors     = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	int8_t *const __restrict tessNeighboursCount     = m_tessNeighboursCount;
	uint16_t *const __restrict neighbourLessVertices = m_neighbourLessVertices;
	bool *const __restrict isVertexNeighbourLess     = m_isVertexNeighbourLess;
	byte_vec4_t *const __restrict tessByteColors     = m_tessByteColors;

	unsigned numNeighbourLessVertices = 0;
	for( unsigned vertexIndex = numSimulatedVertices; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
		// Wtf? how do such vertices exist?
		if( !tessNeighboursCount[vertexIndex] ) [[unlikely]] {
			neighbourLessVertices[numNeighbourLessVertices++] = vertexIndex;
			isVertexNeighbourLess[vertexIndex] = true;
			continue;
		}

		const float scale = Q_Rcp( (float)tessNeighboursCount[vertexIndex] );
		VectorScale( tmpTessPositions[vertexIndex], scale, tmpTessPositions[vertexIndex] );
		tmpTessPositions[vertexIndex][3] = 1.0f;

		if constexpr( SmoothColors ) {
			// Just scale by the averaging multiplier
			Vector4Scale( tmpTessFloatColors[vertexIndex], scale, tmpTessFloatColors[vertexIndex] );
		} else {
			// Write the vertex color directly to the resulting color buffer
			Vector4Scale( tmpTessFloatColors[vertexIndex], scale, tessByteColors[vertexIndex] );
		}
	}

	return numNeighbourLessVertices;
}

template <bool SmoothColors>
void MeshTesselationHelper::processNeighbourLessVertices( unsigned numNeighbourLessVertices,
														  const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessFloatColors            = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	const uint16_t *const __restrict neighbourLessVertices = m_neighbourLessVertices;
	const bool *const __restrict isVertexNeighbourLess     = m_isVertexNeighbourLess;
	byte_vec4_t *const __restrict tessByteColors           = m_tessByteColors;

	// Hack for neighbour-less vertices: apply a gathering pass
	// (the opposite to what we do for each vertex in the original mesh)
	for( unsigned i = 0; i < numNeighbourLessVertices; ++i ) {
		const unsigned vertexIndex = neighbourLessVertices[i];
		alignas( 16 ) vec4_t accumulatedColor { 0.0f, 0.0f, 0.0f, 0.0f };
		unsigned numAccumulatedColors = 0;
		for( unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
			if( !isVertexNeighbourLess[neighbourIndex] ) [[likely]] {
				numAccumulatedColors++;
				if constexpr( SmoothColors ) {
					const float *const __restrict neighbourColor = tmpTessFloatColors[neighbourIndex];
					Vector4Add( neighbourColor, accumulatedColor, accumulatedColor );
				} else {
					const uint8_t *const __restrict neighbourColor = tessByteColors[neighbourIndex];
					Vector4Add( neighbourColor, accumulatedColor, accumulatedColor );
				}
			}
		}
		if( numAccumulatedColors ) [[likely]] {
			const float scale = Q_Rcp( (float)numAccumulatedColors );
			if constexpr( SmoothColors ) {
				Vector4Scale( accumulatedColor, scale, tmpTessFloatColors[vertexIndex] );
			} else {
				Vector4Scale( accumulatedColor, scale, tessByteColors[vertexIndex] );
			}
		}
	}
}

template <bool SmoothColors>
void MeshTesselationHelper::runSmoothVerticesPass( unsigned numNextLevelVertices,
												   const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessPositions    = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	vec4_t *const __restrict tmpTessFloatColors  = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	vec4_t *const __restrict tessPositions       = std::assume_aligned<kAlignment>( m_tessPositions );
	byte_vec4_t *const __restrict tessByteColors = m_tessByteColors;

	// Each icosphere vertex has 5 neighbours (we don't make a distinction between old and added ones for this pass)
	constexpr float icoAvgScale = 1.0f / 5.0f;
	constexpr float smoothFrac  = 0.5f;

	// Apply the smooth pass
	if constexpr( SmoothColors ) {
		for( unsigned vertexIndex = 0; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
			alignas( 16 ) vec4_t sumOfNeighbourPositions { 0.0f, 0.0f, 0.0f, 0.0f };
			alignas( 16 ) vec4_t sumOfNeighbourColors { 0.0f, 0.0f, 0.0f, 0.0f };

			for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
				Vector4Add( tmpTessPositions[neighbourIndex], sumOfNeighbourPositions, sumOfNeighbourPositions );
				Vector4Add( tmpTessFloatColors[neighbourIndex], sumOfNeighbourColors, sumOfNeighbourColors );
			}

			Vector4Scale( sumOfNeighbourPositions, icoAvgScale, sumOfNeighbourPositions );

			// Write the average color
			Vector4Scale( sumOfNeighbourColors, icoAvgScale, tessByteColors[vertexIndex] );

			// Write combined positions
			Vector4Lerp( tmpTessPositions[vertexIndex], smoothFrac, sumOfNeighbourPositions, tessPositions[vertexIndex] );
		}
	} else {
		for( unsigned vertexIndex = 0; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
			alignas( 16 ) vec4_t sumOfNeighbourPositions { 0.0f, 0.0f, 0.0f, 0.0f };

			for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
				Vector4Add( tmpTessPositions[neighbourIndex], sumOfNeighbourPositions, sumOfNeighbourPositions );
			}

			Vector4Scale( sumOfNeighbourPositions, icoAvgScale, sumOfNeighbourPositions );

			// Write combined positions
			Vector4Lerp( tmpTessPositions[vertexIndex], smoothFrac, sumOfNeighbourPositions, tessPositions[vertexIndex] );
		}
	}
}

static MeshTesselationHelper meshTesselationHelper;

template <SimulatedHullsSystem::ViewDotFade Fade>
[[nodiscard]]
static inline wsw_forceinline auto calcAlphaFracForViewDirDotNormal( const float *__restrict viewDir,
																	 const float *__restrict normal ) -> float {
	assert( std::fabs( VectorLengthFast( viewDir ) - 1.0f ) < 0.001f );
	assert( std::fabs( VectorLengthFast( normal ) - 1.0f ) < 0.001f );

	const float absDot = std::fabs( DotProduct( viewDir, normal ) );
	if constexpr( Fade == SimulatedHullsSystem::ViewDotFade::FadeOutContour ) {
		return absDot;
	} else if constexpr( Fade == SimulatedHullsSystem::ViewDotFade::FadeOutCenter ) {
		// This looks best for the current purposes
		float alphaFrac = ( 1.0f - absDot );
		alphaFrac *= alphaFrac * alphaFrac;
		return alphaFrac;
	} else {
		return 1.0f;
	}
}

template <SimulatedHullsSystem::ZFade ZFade>
[[nodiscard]]
static inline wsw_forceinline auto calcAlphaFracForDeltaZ( float z, float minZ, float rcpDeltaZ ) -> float {
	if constexpr( ZFade == SimulatedHullsSystem::ZFade::FadeOutBottom ) {
		assert( z >= minZ );
		return ( z - minZ ) * rcpDeltaZ;
	} else {
		// Don't put an assertion here as it's often supposed to be called for default (zero) minZ values
		return 1.0f;
	}
}

using IcosphereVertexNeighbours = const uint16_t (*)[5];

template <SimulatedHullsSystem::ViewDotFade ViewDotFade, SimulatedHullsSystem::ZFade ZFade>
static void calcNormalsAndApplyAlphaFade( byte_vec4_t *const __restrict resultColors,
										  const vec4_t *const __restrict positions,
										  const IcosphereVertexNeighbours neighboursOfVertices,
										  const byte_vec4_t *const __restrict givenColors,
										  const float *const __restrict viewOrigin,
										  unsigned numVertices, float minZ, float maxZ ) {
	[[maybe_unused]] const float rcpDeltaZ = minZ < maxZ ? Q_Rcp( maxZ - minZ ) : 1.0f;

	unsigned vertexNum = 0;
	do {
		const uint16_t *const neighboursOfVertex = neighboursOfVertices[vertexNum];
		const float *const __restrict currVertex = positions[vertexNum];
		vec3_t accumDir { 0.0f, 0.0f, 0.0f };
		unsigned neighbourIndex = 0;
		bool hasAddedDirs = false;
		do {
			const float *__restrict v2 = positions[neighboursOfVertex[neighbourIndex]];
			const float *__restrict v3 = positions[neighboursOfVertex[( neighbourIndex + 1 ) % 5]];
			vec3_t currTo2, currTo3, cross;
			VectorSubtract( v2, currVertex, currTo2 );
			VectorSubtract( v3, currVertex, currTo3 );
			CrossProduct( currTo2, currTo3, cross );
			if( const float squaredLength = VectorLengthSquared( cross ); squaredLength > 1.0f ) [[likely]] {
				const float rcpLength = Q_RSqrt( squaredLength );
				VectorMA( accumDir, rcpLength, cross, accumDir );
				hasAddedDirs = true;
			}
		} while( ++neighbourIndex < 5 );

		VectorCopy( givenColors[vertexNum], resultColors[vertexNum] );
		if( hasAddedDirs ) [[likely]] {
			vec3_t viewDir;
			VectorSubtract( currVertex, viewOrigin, viewDir );
			const float squareDistanceToVertex = VectorLengthSquared( viewDir );
			if( squareDistanceToVertex > 1.0f ) [[likely]] {
				vec3_t normal;
				VectorCopy( accumDir, normal );
				VectorNormalizeFast( normal );
				const float rcpDistance = Q_RSqrt( squareDistanceToVertex );
				VectorScale( viewDir, rcpDistance, viewDir );
				const float givenAlpha        = givenColors[vertexNum][3];
				const float viewDirAlphaFrac  = calcAlphaFracForViewDirDotNormal<ViewDotFade>( viewDir, normal );
				const float deltaZAlphaFrac   = calcAlphaFracForDeltaZ<ZFade>( currVertex[2], minZ, rcpDeltaZ );
				const float combinedAlphaFrac = viewDirAlphaFrac * deltaZAlphaFrac;
				resultColors[vertexNum][3]    = (uint8_t)wsw::clamp( givenAlpha * combinedAlphaFrac, 0.0f, 255.0f );
			} else {
				resultColors[vertexNum][3] = 0;
			}
		} else {
			resultColors[vertexNum][3] = 0;
		}
	} while( ++vertexNum < numVertices );
}

template <SimulatedHullsSystem::ViewDotFade ViewDotFade, SimulatedHullsSystem::ZFade ZFade>
static void applyAlphaFade( byte_vec4_t *const __restrict resultColors,
							const vec4_t *const __restrict positions,
							const vec4_t *const __restrict normals,
							const byte_vec4_t *const __restrict givenColors,
							const float *const __restrict viewOrigin,
							unsigned numVertices, float minZ, float maxZ ) {
	[[maybe_unused]] const float rcpDeltaZ = minZ < maxZ ? Q_Rcp( maxZ - minZ ) : 1.0f;

	unsigned vertexNum = 0;
	do {
		VectorCopy( givenColors[vertexNum], resultColors[vertexNum] );
		vec3_t viewDir;
		VectorSubtract( positions[vertexNum], viewOrigin, viewDir );
		const float squareDistanceToVertex = VectorLengthSquared( viewDir );
		if( squareDistanceToVertex > 1.0f ) [[likely]] {
			const float rcpDistance = Q_RSqrt( squareDistanceToVertex );
			VectorScale( viewDir, rcpDistance, viewDir );
			const float givenAlpha        = givenColors[vertexNum][3];
			const float viewDirAlphaFrac  = calcAlphaFracForViewDirDotNormal<ViewDotFade>( viewDir, normals[vertexNum] );
			const float deltaZAlphaFrac   = calcAlphaFracForDeltaZ<ZFade>( positions[vertexNum][2], minZ, rcpDeltaZ );
			const float combinedAlphaFrac = viewDirAlphaFrac * deltaZAlphaFrac;
			assert( combinedAlphaFrac >= -0.01f && combinedAlphaFrac <= +1.01f );
			resultColors[vertexNum][3]    = (uint8_t)wsw::clamp( givenAlpha * combinedAlphaFrac, 0.0f, 255.0f );
		} else {
			resultColors[vertexNum][3] = 0;
		}
	} while( ++vertexNum < numVertices );
}

static void applyLightsToVertices( const vec4_t *__restrict givenPositions,
								   const byte_vec4_t *__restrict givenColors,
								   byte_vec4_t *__restrict destColors,
								   const byte_vec4_t *__restrict alphaSourceColors,
								   const Scene::DynamicLight *__restrict lights,
								   std::span<const uint16_t> affectingLightIndices,
								   unsigned numVertices ) {
	const auto numAffectingLights = (unsigned)affectingLightIndices.size();

	assert( numVertices );
	unsigned vertexIndex = 0;
	do {
		const float *__restrict vertexOrigin  = givenPositions[vertexIndex];
		auto *const __restrict givenColor     = givenColors[vertexIndex];
		auto *const __restrict resultingColor = destColors[vertexIndex];

		alignas( 16 ) vec4_t accumColor;
		VectorScale( givenColor, ( 1.0f / 255.0f ), accumColor );

		unsigned lightNum = 0;
		do {
			const Scene::DynamicLight *__restrict light = lights + affectingLightIndices[lightNum];
			const float squareLightToVertexDistance = DistanceSquared( light->origin, vertexOrigin );
			// May go outside [0.0, 1.0] as we test against the bounding box of the entire hull
			float impactStrength = 1.0f - Q_Sqrt( squareLightToVertexDistance ) * Q_Rcp( light->maxRadius );
			// Just clamp so the code stays branchless
			impactStrength = wsw::clamp( impactStrength, 0.0f, 1.0f );
			VectorMA( accumColor, impactStrength, light->color, accumColor );
		} while( ++lightNum < numAffectingLights );

		resultingColor[0] = (uint8_t)( 255.0f * wsw::clamp( accumColor[0], 0.0f, 1.0f ) );
		resultingColor[1] = (uint8_t)( 255.0f * wsw::clamp( accumColor[1], 0.0f, 1.0f ) );
		resultingColor[2] = (uint8_t)( 255.0f * wsw::clamp( accumColor[2], 0.0f, 1.0f ) );
		resultingColor[3] = alphaSourceColors[vertexIndex][3];
	} while( ++vertexIndex < numVertices );
}

auto SimulatedHullsSystem::HullDynamicMesh::getStorageRequirements( const float *viewOrigin,
																	const float *viewAxis,
																	float cameraFovTangent ) const
	-> std::pair<unsigned, unsigned> {
	// Get a suitable subdiv level and store it for further use during this frame
	m_chosenSubdivLevel = m_tesselateClosestLod ? m_simulatedSubdivLevel + 1 : m_simulatedSubdivLevel;

	assert( cameraFovTangent > 0.0f );
	assert( m_nextLodTangentRatio > 0.0f && m_nextLodTangentRatio < 1.0f );

	vec3_t center, extentVector;
	VectorAvg( this->cullMins, this->cullMaxs, center );
	VectorSubtract( this->cullMaxs, this->cullMins, extentVector );
	const float squareExtentValue = VectorLengthSquared( extentVector );
	if( squareExtentValue < wsw::square( 1.0f ) ) [[unlikely]] {
		// Skip drawing
		return { 0, 0 };
	}

	const float extentValue    = Q_Sqrt( squareExtentValue );
	const float squareDistance = DistanceSquared( center, viewOrigin );
	// Don't even try using lesser lods if the mesh is sufficiently close to the viewer
	if( squareDistance > wsw::square( 0.5f * extentValue + 64.0f ) ) {
		const float meshViewTangent  = extentValue * Q_RSqrt( squareDistance );
		const float meshTangentRatio = meshViewTangent * Q_Rcp( cameraFovTangent );

		// Diminish lod tangent ratio in the loop, drop subdiv level every step.
		float lodTangentRatio = m_nextLodTangentRatio;
		for(;; ) {
			if( meshTangentRatio > lodTangentRatio ) {
				break;
			}
			if( m_chosenSubdivLevel == 0 ) {
				break;
			}
			m_chosenSubdivLevel--;
			lodTangentRatio *= m_nextLodTangentRatio;
			// Sanity check
			if( lodTangentRatio < 1e-6 ) [[unlikely]] {
				break;
			}
		};
	}

	const IcosphereData &chosenLevelData = ::basicHullsHolder.getIcosphereForLevel( m_chosenSubdivLevel );
	return { (unsigned)chosenLevelData.vertices.size(), (unsigned)chosenLevelData.indices.size() };
}

static const struct FadeFnHolder {
	using CalcNormalsAndApplyAlphaFadeFn = void (*)( byte_vec4_t *,
													 const vec4_t *,
													 const IcosphereVertexNeighbours,
													 const byte_vec4_t *,
													 const float *,
													 unsigned, float, float );
	using ApplyAlphaFadeFn = void (*)( byte_vec4_t *,
									   const vec4_t *,
									   const vec4_t *,
									   const byte_vec4_t *,
									   const float *,
									   unsigned, float, float );

	CalcNormalsAndApplyAlphaFadeFn calcNormalsAndApplyFadeFn[6] {};
	ApplyAlphaFadeFn applyFadeFn[6] {};

#define ADD_FNS_FOR_FADE_TO_TABLES( ViewDotFade, ZFade ) \
	do { \
        calcNormalsAndApplyFadeFn[indexForFade( ViewDotFade, ZFade )] = &::calcNormalsAndApplyAlphaFade<ViewDotFade, ZFade>; \
		applyFadeFn[indexForFade( ViewDotFade, ZFade )]               = &::applyAlphaFade<ViewDotFade, ZFade>; \
	} while( 0 )

	FadeFnHolder() noexcept {
		// Add a reference to a template instantiation for each feasible combination.
		ADD_FNS_FOR_FADE_TO_TABLES( SimulatedHullsSystem::ViewDotFade::NoFade, SimulatedHullsSystem::ZFade::FadeOutBottom );
		ADD_FNS_FOR_FADE_TO_TABLES( SimulatedHullsSystem::ViewDotFade::FadeOutContour, SimulatedHullsSystem::ZFade::NoFade );
		ADD_FNS_FOR_FADE_TO_TABLES( SimulatedHullsSystem::ViewDotFade::FadeOutContour, SimulatedHullsSystem::ZFade::FadeOutBottom );
		ADD_FNS_FOR_FADE_TO_TABLES( SimulatedHullsSystem::ViewDotFade::FadeOutCenter, SimulatedHullsSystem::ZFade::NoFade );
		ADD_FNS_FOR_FADE_TO_TABLES( SimulatedHullsSystem::ViewDotFade::FadeOutCenter, SimulatedHullsSystem::ZFade::FadeOutBottom );
	}

	[[nodiscard]]
	static constexpr auto indexForFade( SimulatedHullsSystem::ViewDotFade viewDotFade,
										SimulatedHullsSystem::ZFade zFade ) -> unsigned {
		assert( (unsigned)viewDotFade < 3 && (unsigned)zFade < 2 );
		return 2 * (unsigned)viewDotFade + (unsigned)zFade;
	}

#undef ADD_FN_FOR_FLAGS_TO_TABLES
} fadeFnHolder;

auto SimulatedHullsSystem::HullDynamicMesh::fillMeshBuffers( const float *__restrict viewOrigin,
															 const float *__restrict viewAxis,
															 float cameraFovTangent,
															 const Scene::DynamicLight *lights,
															 std::span<const uint16_t> affectingLightIndices,
					  										 vec4_t *__restrict destPositions,
					  										 vec4_t *__restrict destNormals,
					  										 vec2_t *__restrict destTexCoords,
					  										 byte_vec4_t *__restrict destColors,
					                                         uint16_t *__restrict destIndices ) const
 	-> std::pair<unsigned, unsigned> {
	assert( m_chosenSubdivLevel <= m_simulatedSubdivLevel + 1 );
	assert( m_minZLastFrame <= m_maxZLastFrame );

	byte_vec4_t *overrideColors = nullptr;

	const bool actuallyDoTesselation    = m_tesselateClosestLod && m_chosenSubdivLevel > m_simulatedSubdivLevel;

	const bool shouldApplyViewDotFade   = m_viewDotFade != ViewDotFade::NoFade;
	const bool actuallyApplyViewDotFade = shouldApplyViewDotFade;
	const bool shouldApplyZFade         = m_zFade != ZFade::NoFade;
	const bool actuallyApplyZFade       = shouldApplyZFade && m_maxZLastFrame - m_minZLastFrame > 1.0f;
	const ZFade effectiveZFade          = actuallyApplyZFade ? m_zFade : ZFade::NoFade;

	if( actuallyApplyViewDotFade || actuallyApplyZFade ) {
		const unsigned dataLevelToUse     = actuallyDoTesselation ? m_simulatedSubdivLevel : m_chosenSubdivLevel;
		const IcosphereData &lodDataToUse = ::basicHullsHolder.getIcosphereForLevel( dataLevelToUse );

		// If tesselation is going to be performed, apply light to the base non-tesselated lod colors
		const auto numVertices = (unsigned)lodDataToUse.vertices.size();
		overrideColors         = (byte_vec4_t *)alloca( sizeof( byte_vec4_t ) * numVertices );

		// Either we have already calculated normals or do not need normals
		if( m_simulatedNormals || !actuallyApplyViewDotFade ) {
			// Call specialized implementations for each fade func

			const unsigned fnIndex = ::fadeFnHolder.indexForFade( m_viewDotFade, effectiveZFade );
			const auto applyFadeFn = ::fadeFnHolder.applyFadeFn[fnIndex];

			applyFadeFn( overrideColors, m_simulatedPositions, m_simulatedNormals, m_simulatedColors,
						 viewOrigin,  numVertices, m_minZLastFrame, m_maxZLastFrame );
		} else {
			// Call specialized implementations for each fade func

			const unsigned fnIndex = ::fadeFnHolder.indexForFade( m_viewDotFade, effectiveZFade );
			const auto applyFadeFn = ::fadeFnHolder.calcNormalsAndApplyFadeFn[fnIndex];

			applyFadeFn( overrideColors, m_simulatedPositions, lodDataToUse.vertexNeighbours.data(),
						 m_simulatedColors, viewOrigin, numVertices, m_minZLastFrame, m_maxZLastFrame );
		}
	}

	if( this->applyVertexDynLight && !affectingLightIndices.empty() ) {
		unsigned numVertices;
		// If tesselation is going to be performed, apply light to the base non-tesselated lod colors
		if( actuallyDoTesselation ) {
			numVertices = ::basicHullsHolder.getIcosphereForLevel( m_simulatedSubdivLevel ).vertices.size();
		} else {
			numVertices = ::basicHullsHolder.getIcosphereForLevel( m_chosenSubdivLevel ).vertices.size();
		}

		// Copy alpha from these colors
		const byte_vec4_t *alphaSourceColors;
		if( overrideColors ) {
			// We have applied view dot fade, use this view-dot-produced data
			alphaSourceColors = overrideColors;
		} else {
			overrideColors    = (byte_vec4_t *)alloca( sizeof( byte_vec4_t ) * numVertices );
			alphaSourceColors = m_simulatedColors;
		}

		applyLightsToVertices( m_simulatedPositions, m_simulatedColors, overrideColors, alphaSourceColors,
							   lights, affectingLightIndices, numVertices );
	}

	unsigned numResultVertices, numResultIndices;

	// HACK Perform an additional tesselation of some hulls.
	// CPU-side tesselation is the single option in the current codebase state.
	if( actuallyDoTesselation ) {
		assert( m_simulatedSubdivLevel + 1 == m_chosenSubdivLevel );
		const IcosphereData &nextLevelData = ::basicHullsHolder.getIcosphereForLevel( m_chosenSubdivLevel );
		const IcosphereData &simLevelData  = ::basicHullsHolder.getIcosphereForLevel( m_simulatedSubdivLevel );

		const IcosphereVertexNeighbours nextLevelNeighbours = nextLevelData.vertexNeighbours.data();

		const auto numSimulatedVertices = (unsigned)simLevelData.vertices.size();
		const auto numNextLevelVertices = (unsigned)nextLevelData.vertices.size();

		MeshTesselationHelper *const tesselationHelper = &::meshTesselationHelper;
		if( m_lerpNextLevelColors ) {
			tesselationHelper->exec<true>( this, overrideColors,
										   numSimulatedVertices, numNextLevelVertices, nextLevelNeighbours );
		} else {
			tesselationHelper->exec<false>( this, overrideColors,
											numSimulatedVertices, numNextLevelVertices, nextLevelNeighbours );
		}

		numResultVertices = numNextLevelVertices;
		numResultIndices  = (unsigned)nextLevelData.indices.size();

		// TODO: Eliminate this excessive copying
		std::memcpy( destPositions, tesselationHelper->m_tessPositions, sizeof( destPositions[0] ) * numResultVertices );
		std::memcpy( destColors, tesselationHelper->m_tessByteColors, sizeof( destColors[0] ) * numResultVertices );
		std::memcpy( destIndices, nextLevelData.indices.data(), sizeof( uint16_t ) * numResultIndices );
	} else {
		const IcosphereData &dataToUse = ::basicHullsHolder.getIcosphereForLevel( m_chosenSubdivLevel );
		const byte_vec4_t *colorsToUse = overrideColors ? overrideColors : m_simulatedColors;

		numResultVertices = (unsigned)dataToUse.vertices.size();
		numResultIndices  = (unsigned)dataToUse.indices.size();

		std::memcpy( destPositions, m_simulatedPositions, sizeof( m_simulatedPositions[0] ) * numResultVertices );
		std::memcpy( destColors, colorsToUse, sizeof( colorsToUse[0] ) * numResultVertices );
		std::memcpy( destIndices, dataToUse.indices.data(), sizeof( uint16_t ) * numResultIndices );
	}

	return { numResultVertices, numResultIndices };
};