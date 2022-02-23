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

#include "transienteffectssystem.h"
#include "../cgame/cg_local.h"
#include "../client/client.h"
#include "../qcommon/links.h"

#include <cstdlib>
#include <cstring>

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

	[[nodiscard]]
	auto getAllIcospheresForLevels() -> std::span<const IcosphereData, kMaxSubdivLevel + 1> {
		return std::span<const IcosphereData, kMaxSubdivLevel + 1> { m_icospheresForLevels.data(), kMaxSubdivLevel + 1 };
	}
private:
	struct alignas( 4 ) Vertex { float data[4]; };
	static_assert( sizeof( Vertex ) == sizeof( vec4_t ) );
	struct alignas( 2 ) Face { uint16_t data[3]; };
	static_assert( sizeof( Face ) == sizeof( uint16_t[3] ) );
	struct alignas( 2 ) Neighbours { uint16_t data[5]; };
	static_assert( sizeof( Neighbours ) == sizeof( uint16_t[5] ) );

	using MidpointMap = wsw::TreeMap<unsigned, uint16_t>;

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

	void buildNeighbours( wsw::StaticVector<Entry, kMaxSubdivLevel + 1> &icosphereEntries ) {
		constexpr uint8_t kNumVertexNeighbours = 5;
		alignas( 16 )uint8_t knownNeighboursCount[3072];

		// Note that neighbours of the same vertex differ for different subdivision level
		// so we have to recompute all neighbours for each entry (each subdivision level).
		for( Entry &entry: icosphereEntries ) {
			entry.firstNeighboursElement = m_neighbours.size();
			m_neighbours.resize( m_neighbours.size() + entry.numVertices );

			Neighbours *const indicesOfNeighboursForVertex = m_neighbours.data() + entry.firstNeighboursElement;
			// Fill by ~0 to spot non-feasible indices fast
			std::memset( indicesOfNeighboursForVertex, 255, entry.numVertices * sizeof( Neighbours ) );

			assert( entry.numVertices < std::size( knownNeighboursCount ) );
			std::memset( knownNeighboursCount, 0, entry.numVertices );

			unsigned numVerticesWithCompleteNeighbours = 0;
			const unsigned faceOffset = entry.firstIndex / 3;
			const unsigned facesCount = entry.numIndices / 3;
			for( unsigned faceIndex = faceOffset; faceIndex < faceOffset + facesCount; ++faceIndex ) {
				const Face &face = m_faces[faceIndex];
				bool isDoneWithThisEntry = false;
				for( unsigned i = 0; i < 3; ++i ) {
					const auto indexOfVertex = face.data[i];
					assert( indexOfVertex < entry.numVertices );
					assert( knownNeighboursCount[indexOfVertex] <= kNumVertexNeighbours );
					// If neighbours are not complete yet
					if( knownNeighboursCount[indexOfVertex] != kNumVertexNeighbours ) {
						uint16_t *const indicesOfVertex = indicesOfNeighboursForVertex[indexOfVertex].data;
						// For each other vertex try adding it to neighbours
						for( unsigned j = 0; j < 3; ++j ) {
							if( i != j ) {
								const auto otherIndex = face.data[j];
								assert( otherIndex != indexOfVertex );
								uint16_t *indicesEnd = indicesOfVertex + knownNeighboursCount[indexOfVertex];
								if( std::find( indicesOfVertex, indicesEnd, otherIndex ) == indicesEnd ) {
									knownNeighboursCount[indexOfVertex]++;
									*indicesEnd = otherIndex;
								}
							}
						}
						assert( knownNeighboursCount[indexOfVertex] <= kNumVertexNeighbours );
						// If neighbours become complete for this entry
						if( knownNeighboursCount[indexOfVertex] == kNumVertexNeighbours ) {
							numVerticesWithCompleteNeighbours++;
							if( numVerticesWithCompleteNeighbours == entry.numVertices ) {
								isDoneWithThisEntry = true;
								break;
							}
						}
					}
				}
				// Would like to use labeled-breaks if they were a thing
				if( isDoneWithThisEntry ) {
					break;
				}
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

TransientEffectsSystem::TransientEffectsSystem() {
	// TODO: Take care of exception-safety
	while( !m_freeShapeLists.full() ) {
		if( auto *shapeList = CM_AllocShapeList( cl.cms ) ) [[likely]] {
			m_freeShapeLists.push_back( shapeList );
		} else {
			throw std::bad_alloc();
		}
	}
	// TODO: Take care of exception-safety
	if( !( m_tmpShapeList = CM_AllocShapeList( cl.cms ) ) ) [[unlikely]] {
		throw std::bad_alloc();
	}
}

TransientEffectsSystem::~TransientEffectsSystem() {
	for( EntityEffect *effect = m_entityEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFreeEntityEffect( effect );
	}
	for( LightEffect *effect = m_lightEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFreeLightEffect( effect );
	}
	for( FireHull *hull = m_fireHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		unlinkAndFreeFireHull( hull );
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

static const byte_vec4_t kFireCoreReplacementPalette[] {
	{ 255, 170, 144, 144 },
	{ 255, 170, 128, 172 },
	{ 255, 144, 108, 128 },
	{ 255, 144, 108, 72 }
};

static const byte_vec4_t kFireReplacementPalette[] {
	{ 255, 108, 0, 24 },
	{ 255, 72, 0, 24 },
	{ 255, 255, 255, 24 },
	{ 255, 128, 0, 16 },
	{ 255, 144, 0, 12 },
};

static const byte_vec4_t kFireReplacementPalette2[] {
	{ 255, 108, 0, 24 },
	{ 255, 72, 0, 24 },
	{ 255, 128, 0, 16 },
	{ 0, 0, 0, 15 },
	{ 0, 0, 0, 15 },
	{ 0, 0, 0, 15 },
};

static const byte_vec4_t kSmokeReplacementPalette[] {
	{ 255, 255, 255, 12 },
	{ 255, 255, 255, 24 },
	{ 255, 255, 255, 16 },
};

const TransientEffectsSystem::HullLayerParams TransientEffectsSystem::kFireHullLayerParams[5] {
	{
		.speed = 22.5f, .finalOffset = 8.0f,
		.speedSpikeChance = 0.09f, .minSpeedSpike = 10.0f, .maxSpeedSpike = 15.0f,
		.biasAlongChosenDir = 20.0f,
		.initialColor = { 1.0f, 0.9f, 0.7f, 1.0f },
		.regularColorProps = {
			.replacementPalette = kFireCoreReplacementPalette, .dropChance = 0.008f, .replacementChance = 0.035f
		},
		.decayColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.25f, .replacementChance = 0.25f
		}
	},
	{
		.speed = 35.0f, .finalOffset = 6.0f,
		.speedSpikeChance = 0.04f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
		.biasAlongChosenDir = 20.0f,
		.initialColor = { 1.0f, 0.7f, 0.5f, 0.7f },
		.regularColorProps = {
			.replacementPalette = kFireReplacementPalette, .dropChance = 0.008f, .replacementChance = 0.025f
		},
		.decayColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.25f, .replacementChance = 0.25f
		}
	},
	{
		.speed = 45.0f, .finalOffset = 4.0f,
		.speedSpikeChance = 0.04f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
		.biasAlongChosenDir = 20.0f,
		.initialColor = { 1.0f, 0.7f, 0.5f, 0.7f },
		.regularColorProps = {
			.replacementPalette = kFireReplacementPalette, .dropChance = 0.025f, .replacementChance = 0.045f,
		},
		.decayColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.25f, .replacementChance = 0.50f,
		}
	},
	{
		.speed = 52.5f, .finalOffset = 2.0f,
		.speedSpikeChance = 0.08f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
		.biasAlongChosenDir = 15.0f,
		.initialColor = { 1.0f, 0.7f, 0.5f, 0.7f },
		.regularColorProps = {
			.replacementPalette = kFireReplacementPalette, .dropChance = 0.035f, .replacementChance = 0.065f,
		},
		.decayColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.25f, .replacementChance = 0.75f,
		}
	},
	{
		.speed = 60.0f, .finalOffset = 0.0f,
		.speedSpikeChance = 0.10f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
		.biasAlongChosenDir = 15.0f,
		.initialColor = { 1.0f, 0.7f, 0.5f, 0.3f },
		.regularColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.045f, .replacementChance = 0.085f
		},
		.decayColorProps = {
			.replacementPalette = kFireReplacementPalette2, .dropChance = 0.25f, .replacementChance = 0.50f
		}
	},
};

void TransientEffectsSystem::spawnExplosion( const float *origin, float radius ) {
	LightEffect *const lightEffect = allocLightEffect( m_lastTime, 700, 100, 300 );
	VectorCopy( origin, lightEffect->origin );
	VectorCopy( kFireHullLayerParams[0].initialColor, lightEffect->color );
	// 250 for radius of 64
	// TODO: Make radius affect hulls
	constexpr float lightRadiusScale = 1.0f / 64.0f;
	lightEffect->radius = 250.0f * radius * lightRadiusScale;

	if( cg_volumetricExplosions->integer ) {
		if( FireHull *const hull = allocFireHull( m_lastTime, 800 ) ) {
			setupHullVertices( hull, origin, 0.9f, kFireHullLayerParams );
			hull->decayStartAt = m_lastTime + 550;
		}

		if( cg_volumetricExplosionsWave->integer ) {
			const vec4_t waveColor { 1.0f, 1.0f, 1.0f, 0.06f };
			if( WaveHull *const hull = allocWaveHull( m_lastTime, 250 ) ) {
				setupHullVertices( hull, origin, waveColor, 500.0f, 10.0f );
				hull->regularColorProps.interval = std::numeric_limits<unsigned>::max();
			}
		}

		// TODO: It would look better if smoke hulls are coupled together/allocated at once

		if( cg_volumetricExplosionsSmoke->integer ) {
			const vec4_t smokeColor { 1.0f, 0.9f, 0.9f, 0.03f };

			if( SmokeHull *const hull = allocSmokeHull( m_lastTime, 2500 ) ) {
				hull->archimedesBottomAccel   = +45.0f;
				hull->archimedesTopAccel      = +150.0f;
				hull->xyExpansionTopAccel     = +50.0f;
				hull->xyExpansionBottomAccel  = -30.0f;

				hull->regularColorProps.replacementPalette = kSmokeReplacementPalette;
				hull->regularColorProps.interval           = 15;
				hull->regularColorProps.dropChance         = 0.002f;
				hull->regularColorProps.replacementChance  = 0.020f;

				hull->expansionStartAt = m_lastTime + 500;

				hull->lodCurrLevelTangentRatio = 0.15f;
				hull->tesselateClosestLod      = true;
				hull->leprNextLevelColors      = true;

				setupHullVertices( hull, origin, smokeColor, 100.0f, 10.0f );
			}

			if( SmokeHull *const hull = allocSmokeHull( m_lastTime, 2500 ) ) {
				hull->archimedesBottomAccel   = +35.0f;
				hull->archimedesTopAccel      = +150.0f;
				hull->xyExpansionTopAccel     = +65.0f;
				hull->xyExpansionBottomAccel  = -25.0f;

				hull->regularColorProps.replacementPalette = kSmokeReplacementPalette;
				hull->regularColorProps.interval           = 15;
				hull->regularColorProps.replacementChance  = 0.035f;
				hull->regularColorProps.dropChance         = 0.003f;

				hull->expansionStartAt = m_lastTime + 500;

				hull->lodCurrLevelTangentRatio = 0.15f;
				hull->tesselateClosestLod      = true;
				hull->leprNextLevelColors      = true;

				setupHullVertices( hull, origin, smokeColor, 120.0f, 10.0f );
			}
		}
	} else {
		const float spriteRadius = 40.0f * radius * lightRadiusScale;
		EntityEffect *effect = addSpriteEffect( cgs.media.shaderRocketExplosion, origin, spriteRadius, 800u );
		// TODO: Unify this with hulls (quake random dirs seem to be vertices of the 2-nd tess level icosphere)
		const auto *randomDir       = kPredefinedDirs[m_rng.nextBounded( std::size( kPredefinedDirs ) )];
		const float randomMagnitude = m_rng.nextFloat( -5.0f, 5.0f );
		VectorScale( randomDir, randomMagnitude, effect->velocity );
	}
}

void TransientEffectsSystem::spawnCartoonHitEffect( const float *origin, const float *dir, int damage ) {
	if( cg_cartoonHitEffect->integer ) {
		float radius = 0.0f;
		shader_s *material = nullptr;
		if( damage > 64 ) {
			// OUCH!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit3, 24.0f );
		} else if( damage > 50 ) {
			// POW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit, 19.0f );
		} else if( damage > 38 ) {
			// SPLITZOW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit2, 15.0f );
		}

		if( material ) {
			// TODO:
			vec3_t localDir;
			if( !VectorLength( dir ) ) {
				VectorNegate( &cg.view.axis[AXIS_FORWARD], localDir );
			} else {
				VectorNormalize2( dir, localDir );
			}

			vec3_t spriteOrigin;
			// Move effect a bit up from player
			VectorCopy( origin, spriteOrigin );
			spriteOrigin[2] += ( playerbox_stand_maxs[2] - playerbox_stand_mins[2] ) + 1.0f;

			EntityEffect *effect = addSpriteEffect( material, spriteOrigin, radius, 700u );
			effect->entity.rotation = 0.0f;
			// TODO: Add a correct sampling of random sphere points as a random generator method
			for( unsigned i = 0; i < 3; ++i ) {
				effect->velocity[i] = m_rng.nextFloat( -10.0f, +10.0f );
			}
		}
	}
}

void TransientEffectsSystem::spawnElectroboltHitEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modElectroBoltWallHit, origin, dir, 600 );

	// TODO: Use a real time instead of last time as this can have a noticeable impact on fading in
	LightEffect *const lightEffect = allocLightEffect( m_lastTime, 500, 33, 200 );
	VectorMA( origin, 4.0f, dir, lightEffect->origin );
	Vector4Copy( colorWhite, lightEffect->color );
	lightEffect->radius = 144.0f;
}

void TransientEffectsSystem::spawnInstagunHitEffect( const float *origin, const float *dir, const float *color ) {
	(void)addModelEffect( cgs.media.modInstagunWallHit, origin, dir, 600u );

	LightEffect *const lightEffect = allocLightEffect( m_lastTime, 500, 33, 200 );
	VectorMA( origin, 4.0f, dir, lightEffect->origin );
	VectorCopy( colorMagenta, lightEffect->color );
	lightEffect->radius = 144.0f;
}

void TransientEffectsSystem::spawnPlasmaImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *const entityEffect = addModelEffect( cgs.media.modPlasmaExplosion, origin, dir, 400u );
	entityEffect->fadedInScale = entityEffect->fadedOutScale = 5.0f;

	LightEffect *const lightEffect = allocLightEffect( m_lastTime, 350, 33, 150 );
	VectorMA( origin, 4.0f, dir, lightEffect->origin );
	VectorCopy( colorGreen, lightEffect->color );
	lightEffect->radius = 108.0f;
}

void TransientEffectsSystem::spawnGunbladeBlastImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *const entityEffect = addModelEffect( cgs.media.modBladeWallExplo, origin, dir, 600u );
	entityEffect->fadedInScale = entityEffect->fadedOutScale = 5.0f;

	LightEffect *const lightEffect = allocLightEffect( m_lastTime, 500u, 50u, 200u );
	VectorMA( origin, 8.0f, dir, lightEffect->origin );
	VectorCopy( colorYellow, lightEffect->color );
	lightEffect->radius = 200.0f;
}

void TransientEffectsSystem::spawnGunbladeBladeImpactEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modBladeWallHit, origin, dir, 300u );
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnBulletLikeImpactEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modBulletExplode, origin, dir, 300u );
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnDustImpactEffect( const float *origin, const float *dir, float radius ) {
	vec3_t axis1, axis2;
	PerpendicularVector( axis2, dir );
	CrossProduct( dir, axis2, axis1 );

	VectorNormalize( axis1 ), VectorNormalize( axis2 );

	float angle = 0.0f;
	constexpr const int count = 12;
	const float speed = 0.67f * radius;
	const float angleStep = (float)M_TWOPI * Q_Rcp( (float)count );
	for( int i = 0; i < count; ++i ) {
		const float scale1 = std::sin( angle ), scale2 = std::cos( angle );
		angle += angleStep;

		vec3_t velocity { 0.0f, 0.0f, 0.0f };
		VectorMA( velocity, speed * scale1, axis1, velocity );
		VectorMA( velocity, speed * scale2, axis2, velocity );

		EntityEffect *effect = addSpriteEffect( cgs.media.shaderSmokePuff2, origin, 10.0f, 700u );
		effect->fadedInScale = 0.33f;
		effect->fadedOutScale = 0.0f;
		effect->initialAlpha = 0.25f;
		effect->fadedOutAlpha = 0.0f;
		VectorCopy( velocity, effect->velocity );
	}
}

void TransientEffectsSystem::spawnDashEffect( const float *origin, const float *dir ) {
	// Model orientation/streching hacks
	vec3_t angles;
	VecToAngles( dir, angles );
	angles[1] += 270.0f;
	EntityEffect *effect = addModelEffect( cgs.media.modDash, origin, dir, 700u );
	AnglesToAxis( angles, effect->entity.axis );
	// Scale Z
	effect->entity.axis[2 * 3 + 2] *= 2.0f;
	// Size hacks
	effect->fadedInScale = effect->fadedOutScale = 0.15f;
}

auto TransientEffectsSystem::addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect * {
	EntityEffect *const effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_MODEL;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.model = model;
	effect->entity.customShader = nullptr;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	NormalVectorToAxis( dir, &effect->entity.axis[0] );
	VectorCopy( origin, effect->entity.origin );

	return effect;
}

auto TransientEffectsSystem::addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect * {
	EntityEffect *effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_SPRITE;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.radius = radius;
	effect->entity.customShader = material;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	Matrix3_Identity( effect->entity.axis );
	VectorCopy( origin, effect->entity.origin );

	return effect;
}

auto TransientEffectsSystem::allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect * {
	void *mem = m_entityEffectsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		EntityEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( EntityEffect *effect = m_entityEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_entityEffectsHead );
		oldestEffect->~EntityEffect();
		mem = oldestEffect;
	}

	auto *effect = new( mem )EntityEffect;

	assert( duration <= std::numeric_limits<uint16_t>::max() );
	// Try forcing 16-bit division if a compiler fails to optimize division by constant
	unsigned fadeInDuration = (uint16_t)duration / (uint16_t)10;
	if( fadeInDuration > 33 ) [[likely]] {
		fadeInDuration = 33;
	} else if( fadeInDuration < 1 ) [[unlikely]] {
		fadeInDuration = 1;
	}

	unsigned fadeOutDuration;
	if( duration > fadeInDuration ) [[likely]] {
		fadeOutDuration = duration - fadeInDuration;
	} else {
		fadeOutDuration = fadeInDuration;
		duration = fadeInDuration + 1;
	}

	effect->duration = duration;
	effect->rcpDuration = Q_Rcp( (float)duration );
	effect->fadeInDuration = fadeInDuration;
	effect->rcpFadeInDuration = Q_Rcp( (float)fadeInDuration );
	effect->rcpFadeOutDuration = Q_Rcp( (float)fadeOutDuration );
	effect->spawnTime = currTime;

	wsw::link( effect, &m_entityEffectsHead );
	return effect;
}

auto TransientEffectsSystem::allocLightEffect( int64_t currTime, unsigned duration, unsigned fadeInDuration,
											   unsigned fadeOutOffset ) -> LightEffect * {
	void *mem = m_lightEffectsAllocator.allocOrNull();
	// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Generalize
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		LightEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( LightEffect *effect = m_lightEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_lightEffectsHead );
		oldestEffect->~LightEffect();
		mem = oldestEffect;
	}

	assert( fadeInDuration > 0 );
	assert( fadeInDuration < fadeOutOffset );
	assert( fadeOutOffset < duration );

	auto *effect           = new( mem )LightEffect;
	effect->duration       = duration;
	effect->spawnTime      = currTime;
	effect->fadeInDuration = fadeInDuration;
	effect->fadeOutOffset  = fadeOutOffset;

	wsw::link( effect, &m_lightEffectsHead );
	return effect;
}

// TODO: Turn on "Favor small code" for this template

template <typename Hull, bool HasShapeList>
auto TransientEffectsSystem::allocHull( Hull **head, wsw::FreelistAllocator *allocator,
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

void TransientEffectsSystem::setupHullVertices( BaseRegularSimulatedHull *hull, const float *origin,
												const float *color, float speed, float speedSpead ) {
	const byte_vec4_t initialColor {
		(uint8_t)( color[0] * 255 ),
		(uint8_t)( color[1] * 255 ),
		(uint8_t)( color[2] * 255 ),
		(uint8_t)( color[3] * 255 )
	};

	const float minSpeed = std::max( 0.0f, speed - 0.5f * speedSpead );
	const float maxSpeed = speed + 0.5f * speedSpead;
	wsw::RandomGenerator *__restrict rng = &m_rng;

	const float originX = origin[0], originY = origin[1], originZ = origin[2];
	const auto [verticesSpan, indicesSpan, _] = ::basicHullsHolder.getIcosphereForLevel( hull->subdivLevel );
	const auto *__restrict vertices = verticesSpan.data();

	std::memset( hull->vertexForceVelocities, 0, verticesSpan.size() * sizeof( hull->vertexForceVelocities[0] ) );

	vec4_t *const __restrict positions       = hull->vertexPositions[0];
	vec3_t *const __restrict burstVelocities = hull->vertexBurstVelocities;
	vec4_t *const __restrict altPositions    = hull->vertexPositions[1];
	byte_vec4_t *const __restrict colors     = hull->vertexColors;

	for( size_t i = 0; i < verticesSpan.size(); ++i ) {
		// Vertex positions are absolute to simplify simulation
		Vector4Set( positions[i], originX, originY, originZ, 1.0f );
		const float vertexSpeed = rng->nextFloat( minSpeed, maxSpeed );
		// Unit vertices define directions
		VectorScale( vertices[i], vertexSpeed, burstVelocities[i] );
		// Set the 4th component to 1.0 for alternating positions once as well
		altPositions[i][3] = 1.0f;
		Vector4Copy( initialColor, colors[i] );
	}

	hull->minZLastFrame = originZ - 1.0f;
	hull->maxZLastFrame = originZ + 1.0f;

	VectorCopy( origin, hull->origin );
}

void TransientEffectsSystem::setupHullVertices( BaseConcentricSimulatedHull *hull, const float *origin,
												float scale, std::span<const HullLayerParams> layerParams ) {
	assert( layerParams.size() == hull->numLayers );

	const float originX = origin[0], originY = origin[1], originZ = origin[2];
	const auto [verticesSpan, indicesSpan, neighboursSpan] = ::basicHullsHolder.getIcosphereForLevel( hull->subdivLevel );
	const vec4_t *__restrict vertices = verticesSpan.data();

	// Calculate move limits in each direction

	float maxVertexSpeed = layerParams[0].speed + layerParams[0].maxSpeedSpike + layerParams[0].biasAlongChosenDir ;
	for( unsigned i = 1; i < layerParams.size(); ++i ) {
		const auto &params = layerParams[i];
		maxVertexSpeed = std::max( maxVertexSpeed, params.speed + params.maxSpeedSpike + params.biasAlongChosenDir );
	}

	// To prevent noticeable z-fighting in case if hulls of different layers start matching (e.g due to bias)
	constexpr float maxSmallRandomOffset = 1.5f;

	maxVertexSpeed += maxSmallRandomOffset;
	// Scale by the fine-tuning scale multiplier
	maxVertexSpeed *= scale;

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

	// Setup layers data
	assert( hull->numLayers >= 1 && hull->numLayers < 8 );
	for( unsigned layerNum = 0; layerNum < hull->numLayers; ++layerNum ) {
		BaseConcentricSimulatedHull::Layer *layer   = &hull->layers[layerNum];
		const HullLayerParams *__restrict params    = &layerParams[layerNum];
		vec4_t *const __restrict positions          = layer->vertexPositions;
		byte_vec4_t *const __restrict colors        = layer->vertexColors;
		vec2_t *const __restrict speedsAndDistances = layer->vertexSpeedsAndDistances;
		const float *const __restrict randomBiasDir = verticesSpan[rng->nextBounded( verticesSpan.size() )];

		assert( params->speed >= 0.0f && params->minSpeedSpike >= 0.0f && params->biasAlongChosenDir >= 0.0f );
		assert( params->minSpeedSpike < params->maxSpeedSpike );
		assert( std::fabs( VectorLengthFast( randomBiasDir ) - 1.0f ) < 0.01f );

		layer->finalOffset       = params->finalOffset;
		layer->regularColorProps = params->regularColorProps;
		layer->decayColorProps   = params->decayColorProps;

		std::fill( spikeSpeedBoost, spikeSpeedBoost + verticesSpan.size(), 0.0f );

		const byte_vec4_t initialColor {
			(uint8_t)( params->initialColor[0] * 255 ),
			(uint8_t)( params->initialColor[1] * 255 ),
			(uint8_t)( params->initialColor[2] * 255 ),
			(uint8_t)( params->initialColor[3] * 255 )
		};

		for( size_t i = 0; i < verticesSpan.size(); ++i ) {
			// Position XYZ is computed prior to submission in stateless fashion
			positions[i][3] = 1.0f;

			const float *vertexDir     = vertices[i];
			const float dotWithBiasDir = DotProduct( vertexDir, randomBiasDir );
			speedsAndDistances[i][0]   = params->speed + std::max( 0.0f, dotWithBiasDir ) * params->biasAlongChosenDir;

			if( rng->nextFloat() > params->speedSpikeChance ) [[likely]] {
				speedsAndDistances[i][0] += rng->nextFloat( 0.0f, maxSmallRandomOffset );
			} else {
				const float boost = rng->nextFloat( params->minSpeedSpike, params->maxSpeedSpike );
				spikeSpeedBoost[i] += boost;
				const auto &indicesOfNeighbours = neighboursSpan[i];
				for( const unsigned neighbourIndex: indicesOfNeighbours ) {
					spikeSpeedBoost[neighbourIndex] += rng->nextFloat( 0.50f, 0.75f ) * boost;
				}
			}

			speedsAndDistances[i][1] = 0.0f;

			Vector4Copy( initialColor, colors[i] );
		}

		for( size_t i = 0; i < verticesSpan.size(); ++i ) {
			speedsAndDistances[i][0] += std::min( spikeSpeedBoost[i], maxVertexSpeed );
			// Scale by the fine-tuning scale multiplier
			speedsAndDistances[i][0] *= scale;
		}
	}

	VectorCopy( origin, hull->origin );
	hull->vertexMoveDirections = vertices;
	hull->meshIndices          = indicesSpan.data();
}

void TransientEffectsSystem::unlinkAndFreeEntityEffect( EntityEffect *effect ) {
	wsw::unlink( effect, &m_entityEffectsHead );
	effect->~EntityEffect();
	m_entityEffectsAllocator.free( effect );
}

void TransientEffectsSystem::unlinkAndFreeLightEffect( LightEffect *effect ) {
	wsw::unlink( effect, &m_lightEffectsHead );
	effect->~LightEffect();
	m_lightEffectsAllocator.free( effect );
}

void TransientEffectsSystem::unlinkAndFreeSmokeHull( SmokeHull *hull ) {
	wsw::unlink( hull, &m_smokeHullsHead );
	m_freeShapeLists.push_back( hull->shapeList );
	hull->~SmokeHull();
	m_smokeHullsAllocator.free( hull );
}

void TransientEffectsSystem::unlinkAndFreeWaveHull( WaveHull *hull ) {
	wsw::unlink( hull, &m_waveHullsHead );
	m_freeShapeLists.push_back( hull->shapeList );
	hull->~WaveHull();
	m_waveHullsAllocator.free( hull );
}

void TransientEffectsSystem::unlinkAndFreeFireHull( FireHull *hull ) {
	wsw::unlink( hull, &m_fireHullsHead );
	hull->~FireHull();
	m_fireHullsAllocator.free( hull );
}

void TransientEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	// Limit the time step
	const float timeDeltaSeconds = 1e-3f * (float)std::min<int64_t>( 33, currTime - m_lastTime );

	simulateEntityEffectsAndSubmit( currTime, timeDeltaSeconds, request );
	simulateHullsAndSubmit( currTime, timeDeltaSeconds, request );
	simulateLightEffectsAndSubmit( currTime, timeDeltaSeconds, request );

	m_lastTime = currTime;
}

void TransientEffectsSystem::simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds,
															 DrawSceneRequest *request ) {
	const model_s *const dashModel = cgs.media.modDash;
	const float backlerp = 1.0f - cg.lerpfrac;

	EntityEffect *nextEffect = nullptr;
	for( EntityEffect *__restrict effect = m_entityEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;

		if( effect->spawnTime + effect->duration <= currTime ) [[unlikely]] {
			unlinkAndFreeEntityEffect( effect );
			continue;
		}

		const auto lifetimeMillis = (unsigned)( currTime - effect->spawnTime );
		assert( lifetimeMillis < effect->duration );

		if( lifetimeMillis >= effect->fadeInDuration ) [[likely]] {
			assert( effect->duration > effect->fadeInDuration );
			const float fadeOutFrac = (float)( lifetimeMillis - effect->fadeInDuration ) * effect->rcpFadeOutDuration;
			effect->entity.scale = std::lerp( effect->fadedInScale, effect->fadedOutScale, fadeOutFrac );
		} else {
			const float fadeInFrac = (float)lifetimeMillis * effect->rcpFadeInDuration;
			effect->entity.scale = effect->fadedInScale * fadeInFrac;
		}

		// Dash model hacks
		if( effect->entity.model == dashModel ) [[unlikely]] {
			float *const zScale = effect->entity.axis + ( 2 * 3 ) + 2;
			*zScale -= 4.0f * timeDeltaSeconds;
			if( *zScale < 0.01f ) {
				unlinkAndFreeEntityEffect( effect );
				continue;
			}
		}

		vec3_t moveVec;
		VectorScale( effect->velocity, timeDeltaSeconds, moveVec );
		VectorAdd( effect->entity.origin, moveVec, effect->entity.origin );

		const float lifetimeFrac = (float)lifetimeMillis * effect->rcpDuration;

		effect->entity.backlerp = backlerp;
		const float alpha = std::lerp( effect->initialAlpha, effect->fadedOutAlpha, lifetimeFrac );
		effect->entity.shaderRGBA[3] = (uint8_t)( 255 * alpha );

		request->addEntity( &effect->entity );
	}
}

void TransientEffectsSystem::simulateLightEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds,
															DrawSceneRequest *request ) {
	LightEffect *nextEffect = nullptr;
	for( LightEffect *__restrict effect = m_lightEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;

		if( effect->spawnTime + effect->duration <= currTime ) [[unlikely]] {
			unlinkAndFreeLightEffect( effect );
			continue;
		}

		assert( effect->fadeInDuration && effect->duration > effect->fadeOutOffset );
		const auto lifetimeMillis = (unsigned)( currTime - effect->spawnTime );
		assert( lifetimeMillis < effect->duration );

		float scaleFrac;
		if( lifetimeMillis < effect->fadeInDuration ) {
			scaleFrac = (float)lifetimeMillis * Q_Rcp( (float)effect->fadeInDuration );
		} else if( lifetimeMillis < effect->fadeOutOffset ) {
			scaleFrac = 1.0f;
		} else {
			// TODO: Precache?
			const float rcpFadeOutDuration = Q_Rcp( (float)( effect->duration - effect->fadeOutOffset ) );
			scaleFrac = 1.0f - (float)( lifetimeMillis - effect->fadeOutOffset ) * rcpFadeOutDuration;
		}

		if( const float currRadius = scaleFrac * effect->radius; currRadius >= 1.0f ) [[likely]] {
			request->addLight( effect->origin, currRadius, 0.0f, effect->color );
		}
	}

	// TODO: Add and use a bulk submission of lights
}

void TransientEffectsSystem::simulateHullsAndSubmit( int64_t currTime, float timeDeltaSeconds,
													 DrawSceneRequest *drawSceneRequest ) {
	wsw::StaticVector<BaseRegularSimulatedHull *, kMaxSmokeHulls + kMaxWaveHulls> activeRegularHulls;
	wsw::StaticVector<BaseConcentricSimulatedHull *, kMaxFireHulls> activeConcentricHulls;
	for( FireHull *hull = m_fireHullsHead, *nextHull = nullptr; hull; hull = nextHull ) { nextHull = hull->next;
		if( hull->spawnTime + hull->lifetime > currTime ) [[likely]] {
			hull->simulate( currTime, timeDeltaSeconds, &m_rng );
			activeConcentricHulls.push_back( hull );
		} else {
			unlinkAndFreeFireHull( hull );
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
		assert( std::size( hull->meshSubmissionBuffer ) == 1 );
		assert( hull->subdivLevel );

		ExternalMesh *__restrict mesh = &hull->meshSubmissionBuffer[0];

		Vector4Copy( hull->mins, mesh->mins );
		Vector4Copy( hull->maxs, mesh->maxs );

		mesh->positions = hull->vertexPositions[hull->positionsFrame];
		mesh->colors    = hull->vertexColors;
		mesh->material  = nullptr;
		mesh->numLods   = setupLods( mesh->lods, LodSetupParams {
			.currSubdivLevel       = hull->subdivLevel,
			.minSubdivLevel        = hull->subdivLevel - 1u,
			.currLevelTangentRatio = hull->lodCurrLevelTangentRatio,
			.tesselateClosestLod   = hull->tesselateClosestLod,
			.lerpNextLevelColors   = hull->leprNextLevelColors
		});

		drawSceneRequest->addExternalMesh( hull->mins, hull->maxs, { hull->meshSubmissionBuffer, 1 } );
	}

	for( const BaseConcentricSimulatedHull *__restrict hull: activeConcentricHulls ) {
		assert( hull->numLayers );
		// Meshes should be placed in memory continuously, so we can supply a span
		assert( hull->layers[hull->numLayers - 1].submittedMesh - hull->layers[0].submittedMesh + 1 == hull->numLayers );

		// TODO: Use different lods for different layers?
		ExternalMesh::LodProps lods[ExternalMesh::kMaxLods];
		const unsigned numLods = setupLods( lods, LodSetupParams {
			.currSubdivLevel       = hull->subdivLevel,
			.minSubdivLevel        = 0u,
			.currLevelTangentRatio = 0.15f,
			.tesselateClosestLod   = true,
			.lerpNextLevelColors   = false
		});

		for( unsigned i = 0; i < hull->numLayers; ++i ) {
			BaseConcentricSimulatedHull::Layer *__restrict layer = &hull->layers[i];
			ExternalMesh *__restrict mesh = hull->layers[i].submittedMesh;

			Vector4Copy( layer->mins, mesh->mins );
			Vector4Copy( layer->maxs, mesh->maxs );

			mesh->positions = layer->vertexPositions;
			mesh->colors    = layer->vertexColors;
			mesh->material  = nullptr;
			mesh->numLods   = numLods;
			assert( numLods <= ExternalMesh::kMaxLods );
			std::copy( lods, lods + numLods, mesh->lods );
		}

		drawSceneRequest->addExternalMesh( hull->mins, hull->maxs, { hull->layers[0].submittedMesh, hull->numLayers } );
	}
}

auto TransientEffectsSystem::setupLods( ExternalMesh::LodProps *lods, LodSetupParams &&params ) -> unsigned {
	const auto &icospheresForLevels = ::basicHullsHolder.getAllIcospheresForLevels();
	assert( params.currSubdivLevel < icospheresForLevels.size() );
	assert( params.currSubdivLevel >= params.minSubdivLevel );

	unsigned numLods = 0;
	if( params.tesselateClosestLod && params.currSubdivLevel < BasicHullsHolder::kMaxSubdivLevel ) {
		const IcosphereData &dynTessData = icospheresForLevels[params.currSubdivLevel + 1];
		lods[numLods++] = {
			.indices                     = dynTessData.indices.data(),
			.neighbours                  = dynTessData.vertexNeighbours.data(),
			.maxRatioOfViewTangentsToUse = std::numeric_limits<float>::max(),
			.numIndices                  = (uint16_t)dynTessData.indices.size(),
			.numVertices                 = (uint16_t)dynTessData.vertices.size(),
			.lerpNextLevelColors         = params.lerpNextLevelColors
		};
	}

	float lodTangentsRatio = params.currLevelTangentRatio;
	for( int level = (signed)params.currSubdivLevel; level >= (signed)params.minSubdivLevel; --level ) {
		const IcosphereData &levelData = icospheresForLevels[level];
		lods[numLods++] = {
			.indices                     = levelData.indices.data(),
			.maxRatioOfViewTangentsToUse = lodTangentsRatio,
			.numIndices                  = (uint16_t)levelData.indices.size(),
			.numVertices                 = (uint16_t)levelData.vertices.size(),
		};
		lodTangentsRatio *= 0.5f;
	}

	return numLods;
}

void TransientEffectsSystem::BaseRegularSimulatedHull::simulate( int64_t currTime, float timeDeltaSeconds,
																 wsw::RandomGenerator *__restrict rng ) {
	const vec4_t *const __restrict oldPositions = this->vertexPositions[positionsFrame];
	// Switch old/new positions buffer
	positionsFrame = ( positionsFrame + 1 ) % 2;
	vec4_t *const __restrict newPositions = this->vertexPositions[positionsFrame];

	vec3_t *const __restrict forceVelocities = this->vertexForceVelocities;
	vec3_t *const __restrict burstVelocities = this->vertexBurstVelocities;

	const unsigned numVertices = ::basicHullsHolder.getIcosphereForLevel( subdivLevel ).vertices.size();
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
		const float rcpDeltaZ = ( maxZLastFrame - minZLastFrame ) > 0.1f ? Q_Rcp( maxZLastFrame - minZLastFrame ) : 1.0f;
		float maxSquareDistance2D = 0.0f;
		for ( unsigned i = 0; i < numVertices; ++i ) {
			const float dx = oldPositions[i][0] - avgXLastFrame;
			const float dy = oldPositions[i][1] - avgYLastFrame;
			maxSquareDistance2D = std::max( dx * dx + dy * dy, maxSquareDistance2D );
		}
		const float rcpMaxDistance2D = Q_RSqrt( maxSquareDistance2D );
		for( unsigned i = 0; i < numVertices; ++i ) {
			const float zFrac = Q_Sqrt( ( oldPositions[i][2] - minZLastFrame ) * rcpDeltaZ );
			const float archimedesAccel = std::lerp( archimedesBottomAccel, archimedesTopAccel, zFrac );
			forceVelocities[i][2] += archimedesAccel * timeDeltaSeconds;

			vec2_t toVertex2D { oldPositions[i][0] - avgXLastFrame, oldPositions[i][1] - avgYLastFrame };
			const float squareDistance2D = toVertex2D[0] * toVertex2D[0] + toVertex2D[1] * toVertex2D[1];
			if( squareDistance2D > 1.0f * 1.0f ) [[likely]] {
				const float rcpDistance2D  = Q_RSqrt( squareDistance2D );
				const float distance2D     = Q_Rcp( rcpDistance2D );
				const float distanceFrac   = distance2D * rcpMaxDistance2D;
				const float expansionAccel = std::lerp( xyExpansionBottomAccel, xyExpansionTopAccel, zFrac );
				const float multiplier     = ( expansionAccel * timeDeltaSeconds ) * ( rcpDistance2D * distanceFrac );
				forceVelocities[i][0] += multiplier * toVertex2D[0];
				forceVelocities[i][1] += multiplier * toVertex2D[1];
			}

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
	maxZLastFrame = std::numeric_limits<float>::min();
	avgXLastFrame = avgYLastFrame = 0.0f;

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
			minZLastFrame = std::min( minZLastFrame, position[2] );
			maxZLastFrame = std::max( maxZLastFrame, position[2] );
			avgXLastFrame += position[0];
			avgYLastFrame += position[1];

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

				minZLastFrame = std::min( minZLastFrame, position[2] );
				maxZLastFrame = std::max( maxZLastFrame, position[2] );
				avgXLastFrame += position[0];
				avgYLastFrame += position[1];
			} while( ++i < numNonContactingVertices );
		} else {
			// Just update positions.
			// This is worth the separate branch as its quite common for smoke hulls.
			i = 0;
			do {
				const unsigned vertexIndex       = indicesOfNonContactingVertices[i];
				const float *__restrict position = newPositions[vertexIndex];
				minZLastFrame = std::min( minZLastFrame, position[2] );
				maxZLastFrame = std::max( maxZLastFrame, position[2] );
				avgXLastFrame += position[0];
				avgYLastFrame += position[1];
			} while( ++i < numNonContactingVertices );
		}
	}

	const float rcpNumVertices = Q_Rcp( (float)numVertices );
	avgXLastFrame *= rcpNumVertices;
	avgYLastFrame *= rcpNumVertices;

	const ColorChangeProps &colorChangeProps = currTime < decayStartAt ? regularColorProps : decayColorProps;
	if( lastColorChangeTime + colorChangeProps.interval < currTime ) {
		processColorChange( vertexColors, numVertices, colorChangeProps, rng );
		lastColorChangeTime = currTime;
	}
}

void TransientEffectsSystem::BaseConcentricSimulatedHull::simulate( int64_t currTime, float timeDeltaSeconds,
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
			const float limit = std::max( 0.0f, limitsAtDirections[i] - finalOffset );
			distanceSoFar = std::min( distanceSoFar, limit );

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
		const ColorChangeProps &props = currTime < decayStartAt ? layer->regularColorProps : layer->decayColorProps;
		if( layer->lastColorChangeTime + props.interval < currTime ) {
			processColorChange( layer->vertexColors, numVertices, props, rng );
			layer->lastColorChangeTime = currTime;
		}
	}
}

void TransientEffectsSystem::processColorChange( byte_vec4_t *__restrict colors, unsigned numColors,
												 const ColorChangeProps &colorChangeProps,
												 wsw::RandomGenerator *__restrict rng ) {
	const auto replacementPalette = colorChangeProps.replacementPalette;
	const float dropChance = colorChangeProps.dropChance;

	assert( numColors );
	unsigned i = 0;
	if( replacementPalette.empty() ) {
		do {
			if( colors[i][3] != 0 ) {
				if( rng->nextFloat() < dropChance ) {
					colors[i][3] = 0;
				}
			}
		} while( ++i < numColors );
	} else {
		const float replacementChance = colorChangeProps.replacementChance;
		do {
			// Don't process elements that became void
			if( colors[i][3] != 0 ) {
				if( rng->nextFloat() < dropChance ) [[unlikely]] {
					colors[i][3] = 0;
				} else if( rng->nextFloat() < replacementChance ) [[unlikely]] {
					const auto *chosenColor = replacementPalette[rng->nextBounded( replacementPalette.size() )];
					auto *existingColor = colors[i];
					// In order to replace, the alpha must be less than the existing one
					if( chosenColor[3] < existingColor[3] ) {
						Vector4Copy( chosenColor, existingColor );
					}
				}
			}
		} while( ++i < numColors );
	}
}