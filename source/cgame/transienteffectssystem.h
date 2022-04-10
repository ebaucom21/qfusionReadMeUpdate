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

#ifndef WSW_c583803c_d204_40e4_8883_36cdfe1ccf20_H
#define WSW_c583803c_d204_40e4_8883_36cdfe1ccf20_H

#include "../qcommon/freelistallocator.h"
#include "../qcommon/randomgenerator.h"
#include "../ref/ref.h"

struct CMShapeList;
class DrawSceneRequest;

/// Manages "fire-and-forget" effects that usually get spawned upon events.
class TransientEffectsSystem {
public:
	struct ColorChangeTimelineNode {
		std::span<const byte_vec4_t> replacementPalette;
		// Specifying it as a fraction is more flexible than absolute offsets
		float nodeActivationLifetimeFraction { 1.0f };
		unsigned colorChangeInterval { 15 };
		float dropChance { 0.0f };
		float replacementChance { 0.0f };
	};

	struct HullLayerParams {
		const float speed;
		const float finalOffset;
		const float speedSpikeChance;
		const float minSpeedSpike, maxSpeedSpike;
		const float biasAlongChosenDir;
		const float initialColor[4];

		std::span<const ColorChangeTimelineNode> colorChangeTimeline;
	};

	TransientEffectsSystem();
	~TransientEffectsSystem();

	void spawnExplosion( const float *origin, float radius = 72.0f );
	void spawnCartoonHitEffect( const float *origin, const float *dir, int damage );
	void spawnElectroboltHitEffect( const float *origin, const float *dir );
	void spawnInstagunHitEffect( const float *origin, const float *dir, const float *color );
	void spawnPlasmaImpactEffect( const float *origin, const float *dir );

	void spawnGunbladeBlastImpactEffect( const float *origin, const float *dir );
	void spawnGunbladeBladeImpactEffect( const float *origin, const float *dir );

	void spawnBulletLikeImpactEffect( const float *origin, const float *dir );

	void spawnDustImpactEffect( const float *origin, const float *dir, float radius );

	void spawnDashEffect( const float *origin, const float *dir );

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request );
private:

	struct EntityEffect {
		EntityEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };
		unsigned duration { 0 };
		float rcpDuration { 0.0f };
		unsigned fadeInDuration { 0 };
		float rcpFadeInDuration { 0.0f };
		float rcpFadeOutDuration { 0.0f };
		float velocity[3] { 0.0f, 0.0f, 0.0f };
		// The entity scale once it fades in
		float fadedInScale { 1.0f };
		// The entity scale once it fades out (no shrinking by default)
		float fadedOutScale { 1.0f };
		// The entity alpha upon spawn
		float initialAlpha { 1.0f };
		// The entity alpha once it fades out (alpha fade out is on by default)
		float fadedOutAlpha { 0.0f };
		entity_t entity;
	};

	struct LightEffect {
		LightEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };
		unsigned duration { 0 };
		unsigned fadeInDuration { 0 };
		unsigned fadeOutOffset { 0 };

		float origin[3] { 0.0f, 0.0f, 0.0f };
		float color[3] { 0.0f, 0.0f, 0.0f };
		float radius { 0.0f };
	};

	static constexpr unsigned kNumVerticesForSubdivLevel[5] { 12, 42, 162, 642, 2562 };

	struct ColorChangeState {
		int64_t lastColorChangeAt { 0 };
		unsigned lastNodeIndex { 0 };
	};

	struct BaseRegularSimulatedHull {
		CMShapeList *shapeList { nullptr };
		int64_t spawnTime { 0 };

		vec4_t mins, maxs;
		vec3_t origin;

		unsigned lifetime { 0 };
		// Archimedes/xy expansion activation offset
		int64_t expansionStartAt { std::numeric_limits<int64_t>::max() };

		// Old/current
		vec4_t *vertexPositions[2];

		// Velocities of an initial burst, decelerate with time
		vec3_t *vertexBurstVelocities;
		// Velocities produced by external forces during simulation
		vec3_t *vertexForceVelocities;

		byte_vec4_t *vertexColors;

		std::span<const ColorChangeTimelineNode> colorChangeTimeline;
		ColorChangeState colorChangeState;

		float archimedesTopAccel { 0.0f }, archimedesBottomAccel { 0.0f };
		float xyExpansionTopAccel { 0.0f }, xyExpansionBottomAccel { 0.0f };
		float minZLastFrame { std::numeric_limits<float>::max() }, maxZLastFrame { std::numeric_limits<float>::min() };
		float avgXLastFrame { 0.0f }, avgYLastFrame { 0.0f };

		// The renderer assumes external lifetime of the submitted spans. Keep the buffer within the hull.
		ExternalMesh meshSubmissionBuffer[1];

		float lodCurrLevelTangentRatio { 0.25f };
		bool tesselateClosestLod { false };
		bool leprNextLevelColors { false };

		uint8_t positionsFrame { 0 };
		uint8_t subdivLevel { 0 };

		void simulate( int64_t currTime, float timeDeltaSeconds, wsw::RandomGenerator *__restrict rng );
	};

	template <unsigned SubdivLevel>
	struct RegularSimulatedHull : public BaseRegularSimulatedHull {
		static constexpr auto kNumVertices = kNumVerticesForSubdivLevel[SubdivLevel];

		RegularSimulatedHull<SubdivLevel> *prev { nullptr }, *next {nullptr };

		vec4_t storageOfPositions[2][kNumVertices];
		vec3_t storageOfBurstVelocities[kNumVertices];
		vec3_t storageOfForceVelocities[kNumVertices];
		byte_vec4_t storageOfColors[kNumVertices];

		RegularSimulatedHull() {
			this->vertexPositions[0]    = storageOfPositions[0];
			this->vertexPositions[1]    = storageOfPositions[1];
			this->vertexBurstVelocities = storageOfBurstVelocities;
			this->vertexForceVelocities = storageOfForceVelocities;
			this->vertexColors          = storageOfColors;
			this->subdivLevel           = SubdivLevel;
		}
	};

	struct BaseConcentricSimulatedHull {
		const uint16_t *meshIndices { nullptr };
		// Externally managed, should point to the unit mesh data
		const vec4_t *vertexMoveDirections;
		// Distances to the nearest obstacle (or the maximum growth radius in case of no obstacles)
		float *limitsAtDirections;
		int64_t spawnTime { 0 };

		struct Layer {
			vec4_t mins, maxs;
			vec4_t *vertexPositions;
			// Contains pairs (speed, distance from origin along the direction)
			vec2_t *vertexSpeedsAndDistances;
			byte_vec4_t *vertexColors;
			ExternalMesh *submittedMesh;

			// Subtracted from limitsAtDirections for this layer, must be non-negative.
			// This offset is supposed to prevent hulls from ending at the same distance in the end position.
			float finalOffset { 0 };

			std::span<const ColorChangeTimelineNode> colorChangeTimeline;
			ColorChangeState colorChangeState;
		};

		Layer *layers { nullptr };

		vec4_t mins, maxs;
		vec3_t origin;

		unsigned numLayers { 0 };
		unsigned lifetime { 0 };

		uint8_t subdivLevel { 0 };

		void simulate( int64_t currTime, float timeDeltaSeconds, wsw::RandomGenerator *__restrict rng );
	};

	template <unsigned SubdivLevel, unsigned NumLayers>
	struct ConcentricSimulatedHull : public BaseConcentricSimulatedHull {
		static constexpr auto kNumVertices = kNumVerticesForSubdivLevel[SubdivLevel];

		ConcentricSimulatedHull<SubdivLevel, NumLayers> *prev { nullptr }, *next { nullptr };

		Layer storageOfLayers[NumLayers];
		float storageOfLimits[kNumVertices];
		vec4_t storageOfPositions[kNumVertices * NumLayers];
		vec2_t storageOfSpeedsAndDistances[kNumVertices * NumLayers];
		byte_vec4_t storageOfColors[kNumVertices * NumLayers];
		ExternalMesh storageOfMeshes[NumLayers];

		ConcentricSimulatedHull() {
			this->numLayers = NumLayers;
			this->subdivLevel = SubdivLevel;
			this->layers = &storageOfLayers[0];
			this->limitsAtDirections = &storageOfLimits[0];
			for( unsigned i = 0; i < NumLayers; ++i ) {
				Layer *const layer              = &layers[i];
				layer->vertexPositions          = &storageOfPositions[i * kNumVertices];
				layer->vertexSpeedsAndDistances = &storageOfSpeedsAndDistances[i * kNumVertices];
				layer->vertexColors             = &storageOfColors[i * kNumVertices];
				layer->submittedMesh            = &storageOfMeshes[i];
			}
		}
	};

	using FireHull  = ConcentricSimulatedHull<3, 5>;
	using SmokeHull = RegularSimulatedHull<2>;
	using WaveHull  = RegularSimulatedHull<2>;

	void unlinkAndFreeEntityEffect( EntityEffect *effect );
	void unlinkAndFreeLightEffect( LightEffect *effect );

	void unlinkAndFreeFireHull( FireHull *hull );
	void unlinkAndFreeSmokeHull( SmokeHull *hull );
	void unlinkAndFreeWaveHull( WaveHull *hull );

	[[nodiscard]]
	auto addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocLightEffect( int64_t currTime, unsigned duration, unsigned fadeInDuration,
						   unsigned fadeOutOffset ) -> LightEffect *;

	template <typename Hull, bool HasShapeLists>
	[[nodiscard]]
	auto allocHull( Hull **head, wsw::FreelistAllocator *allocator, int64_t currTime, unsigned lifetime ) -> Hull *;

	[[nodiscard]]
	auto allocFireHull( int64_t currTime, unsigned lifetime ) -> FireHull * {
		return allocHull<FireHull, false>( &m_fireHullsHead, &m_fireHullsAllocator, currTime, lifetime );
	}

	[[nodiscard]]
	auto allocSmokeHull( int64_t currTime, unsigned lifetime ) -> SmokeHull * {
		return allocHull<SmokeHull, true>( &m_smokeHullsHead, &m_smokeHullsAllocator, currTime, lifetime );
	}

	[[nodiscard]]
	auto allocWaveHull( int64_t currTime, unsigned lifetime ) -> WaveHull * {
		return allocHull<WaveHull, true>( &m_waveHullsHead, &m_waveHullsAllocator, currTime, lifetime );
	}

	void setupHullVertices( BaseRegularSimulatedHull *hull, const float *origin, const float *color,
							float speed, float speedSpread );



	void setupHullVertices( BaseConcentricSimulatedHull *hull, const float *origin,
							float scale, std::span<const HullLayerParams> paramsOfLayers );

	struct LodSetupParams {
		unsigned currSubdivLevel;
		unsigned minSubdivLevel;
		float currLevelTangentRatio;
		bool tesselateClosestLod;
		bool lerpNextLevelColors;
	};

	[[nodiscard]]
	static auto setupLods( ExternalMesh::LodProps *lods, LodSetupParams &&params ) -> unsigned;

	void simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );
	void simulateHullsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );
	void simulateLightEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );

	static void processColorChange( int64_t currTime, int64_t spawnTime, unsigned effectDuration,
									std::span<const ColorChangeTimelineNode> timeline,
		    						std::span<byte_vec4_t> colors,
									ColorChangeState *__restrict state,
									wsw::RandomGenerator *__restrict rng );

	[[nodiscard]]
	static auto computeCurrTimelineNodeIndex( unsigned startFromIndex, int64_t currTime,
											  int64_t spawnTime, unsigned effectDuration,
											  std::span<const ColorChangeTimelineNode> timeline ) -> unsigned;

	static constexpr unsigned kMaxFireHulls  = 32;
	static constexpr unsigned kMaxSmokeHulls = kMaxFireHulls * 2;
	static constexpr unsigned kMaxWaveHulls  = kMaxFireHulls;

	wsw::StaticVector<CMShapeList *, kMaxSmokeHulls + kMaxWaveHulls> m_freeShapeLists;
	CMShapeList *m_tmpShapeList { nullptr };

	wsw::HeapBasedFreelistAllocator m_entityEffectsAllocator { sizeof( EntityEffect ), 256 };
	wsw::HeapBasedFreelistAllocator m_lightEffectsAllocator { sizeof( LightEffect ), 72 };
	wsw::HeapBasedFreelistAllocator m_fireHullsAllocator { sizeof( FireHull ), kMaxFireHulls };
	wsw::HeapBasedFreelistAllocator m_smokeHullsAllocator { sizeof( SmokeHull ), kMaxSmokeHulls };
	wsw::HeapBasedFreelistAllocator m_waveHullsAllocator { sizeof( WaveHull ), kMaxWaveHulls };

	EntityEffect *m_entityEffectsHead { nullptr };
	LightEffect *m_lightEffectsHead { nullptr };
	FireHull *m_fireHullsHead { nullptr };
	SmokeHull *m_smokeHullsHead { nullptr };
	WaveHull *m_waveHullsHead { nullptr };

	wsw::RandomGenerator m_rng;
	int64_t m_lastTime { 0 };
};

#endif
