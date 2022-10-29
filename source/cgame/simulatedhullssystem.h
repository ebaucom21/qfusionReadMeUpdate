#ifndef WSW_25e6c863_70a4_4d36_9597_133f93b31f91_H
#define WSW_25e6c863_70a4_4d36_9597_133f93b31f91_H

#include <span>

#include "../ref/ref.h"
#include "../qcommon/randomgenerator.h"
#include "../qcommon/freelistallocator.h"

struct CMShapeList;

class SimulatedHullsSystem {
	friend class TransientEffectsSystem;
	friend class MeshTesselationHelper;
public:
	enum class ViewDotFade : uint8_t { NoFade, FadeOutContour, FadeOutCenter };

	struct ColorChangeTimelineNode {
		// Specifying it as a fraction is more flexible than absolute offsets
		float activateAtLifetimeFraction { 0.0f };
		// Colors get chosen randomly during replacement from this span
		std::span<const byte_vec4_t> replacementPalette;
		// 1.0f does not guarantee a full replacement
		float sumOfDropChanceForThisSegment { 0.0f };
		// 1.0f does not guarantee a full replacement
		float sumOfReplacementChanceForThisSegment { 0.0f };
		// True if vertex colors may be replaced by more opaque colors
		bool allowIncreasingOpacity { false };
	};

	struct HullLayerParams {
		float speed;
		float finalOffset;
		float speedSpikeChance;
		float minSpeedSpike, maxSpeedSpike;
		float biasAlongChosenDir;
		float baseInitialColor[4];
		float bulgeInitialColor[4];

		std::span<const ColorChangeTimelineNode> colorChangeTimeline;
	};

	SimulatedHullsSystem();
	~SimulatedHullsSystem();

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request );
private:
	static constexpr unsigned kNumVerticesForSubdivLevel[5] { 12, 42, 162, 642, 2562 };

	class HullDynamicMesh : public DynamicMesh {
		friend class SimulatedHullsSystem;
		friend class MeshTesselationHelper;

		const vec4_t *m_simulatedPositions { nullptr };
		const vec4_t *m_simulatedNormals { nullptr };
		const byte_vec4_t *m_simulatedColors { nullptr };
		unsigned m_simulatedSubdivLevel { 0 };
		// Gets set in getStorageRequirements()
		mutable unsigned m_chosenSubdivLevel { 0 };
		float m_nextLodTangentRatio { 0.18f };
		ViewDotFade m_viewDotFade { ViewDotFade::NoFade };
		bool m_tesselateClosestLod { false };
		bool m_lerpNextLevelColors { false };

		[[nodiscard]]
		auto getStorageRequirements( const float *viewOrigin, const float *viewAxis, float cameraViewTangent ) const
			-> std::pair<unsigned, unsigned> override;

		[[nodiscard]]
		auto fillMeshBuffers( const float *__restrict viewOrigin,
							  const float *__restrict viewAxis,
							  float cameraViewTangent,
							  const Scene::DynamicLight *lights,
							  std::span<const uint16_t> affectingLightIndices,
							  vec4_t *__restrict destPositions,
							  vec4_t *__restrict destNormals,
							  vec2_t *__restrict destTexCoords,
							  byte_vec4_t *__restrict destColors,
							  uint16_t *__restrict destIndices ) const -> std::pair<unsigned, unsigned> override;
	};

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

		vec4_t *vertexNormals;

		// Velocities of an initial burst, decelerate with time
		vec3_t *vertexBurstVelocities;
		// Velocities produced by external forces during simulation
		vec3_t *vertexForceVelocities;

		byte_vec4_t *vertexColors;

		std::span<const ColorChangeTimelineNode> colorChangeTimeline;
		ColorChangeState colorChangeState;

		// It's actually cheaper to process these vertices as regular ones
		// and overwrite possible changes after processColorChange() calls.
		// We can't just set the alpha to zero like it used to be,
		// as zero-alpha vertices still may be overwritten with new color replacement rules.
		std::span<const uint16_t> noColorChangeIndices;
		const uint8_t *noColorChangeVertexColor { nullptr };

		float archimedesTopAccel { 0.0f }, archimedesBottomAccel { 0.0f };
		float xyExpansionTopAccel { 0.0f }, xyExpansionBottomAccel { 0.0f };
		float minZLastFrame { std::numeric_limits<float>::max() };
		float maxZLastFrame { std::numeric_limits<float>::lowest() };

		// The renderer assumes external lifetime of the submitted spans. Keep it within the hull.
		HullDynamicMesh submittedMesh;

		bool tesselateClosestLod { false };
		bool leprNextLevelColors { false };
		bool applyVertexDynLight { false };
		ViewDotFade vertexViewDotFade { ViewDotFade::NoFade };

		uint8_t positionsFrame { 0 };
		uint8_t subdivLevel { 0 };

		void simulate( int64_t currTime, float timeDeltaSeconds, wsw::RandomGenerator *__restrict rng );
	};

	template <unsigned SubdivLevel, bool CalcNormals = false>
	struct RegularSimulatedHull : public BaseRegularSimulatedHull {
		static constexpr auto kNumVertices = kNumVerticesForSubdivLevel[SubdivLevel];

		RegularSimulatedHull<SubdivLevel, CalcNormals> *prev { nullptr }, *next { nullptr };

		vec4_t storageOfPositions[2][kNumVertices];
		vec3_t storageOfBurstVelocities[kNumVertices];
		vec3_t storageOfForceVelocities[kNumVertices];
		byte_vec4_t storageOfColors[kNumVertices];
		vec4_t storageOfNormals[CalcNormals ? kNumVertices : 0];

		RegularSimulatedHull() {
			this->vertexPositions[0]    = storageOfPositions[0];
			this->vertexPositions[1]    = storageOfPositions[1];
			this->vertexNormals         = CalcNormals ? storageOfNormals : nullptr;
			this->vertexBurstVelocities = storageOfBurstVelocities;
			this->vertexForceVelocities = storageOfForceVelocities;
			this->vertexColors          = storageOfColors;
			this->subdivLevel           = SubdivLevel;
		}
	};

	struct BaseConcentricSimulatedHull {
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
			HullDynamicMesh *submittedMesh;

			// Subtracted from limitsAtDirections for this layer, must be non-negative.
			// This offset is supposed to prevent hulls from ending at the same distance in the end position.
			float finalOffset { 0 };

			std::span<const ColorChangeTimelineNode> colorChangeTimeline;
			ColorChangeState colorChangeState;

			bool useDrawOnTopHack { false };
			std::optional<ViewDotFade> overrideHullFade;
		};

		Layer *layers { nullptr };
		const DynamicMesh **firstSubmittedMesh;

		vec4_t mins, maxs;
		vec3_t origin;

		unsigned numLayers { 0 };
		unsigned lifetime { 0 };

		uint8_t subdivLevel { 0 };
		bool applyVertexDynLight { false };
		ViewDotFade vertexViewDotFade { ViewDotFade::NoFade };

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
		HullDynamicMesh storageOfMeshes[NumLayers];
		const DynamicMesh *storageOfMeshPointers[NumLayers];

		ConcentricSimulatedHull() {
			this->numLayers = NumLayers;
			this->subdivLevel = SubdivLevel;
			this->layers = &storageOfLayers[0];
			this->limitsAtDirections = &storageOfLimits[0];
			this->firstSubmittedMesh = &storageOfMeshPointers[0];
			for( unsigned i = 0; i < NumLayers; ++i ) {
				Layer *const layer              = &layers[i];
				layer->vertexPositions          = &storageOfPositions[i * kNumVertices];
				layer->vertexSpeedsAndDistances = &storageOfSpeedsAndDistances[i * kNumVertices];
				layer->vertexColors             = &storageOfColors[i * kNumVertices];
				layer->submittedMesh            = &storageOfMeshes[i];
				this->firstSubmittedMesh[i]     = layer->submittedMesh;
			}
		}
	};

	using FireHull        = ConcentricSimulatedHull<3, 5>;
	using FireClusterHull = ConcentricSimulatedHull<1, 2>;
	using BlastHull       = ConcentricSimulatedHull<3, 3>;
	using SmokeHull       = RegularSimulatedHull<2, true>;
	using WaveHull        = RegularSimulatedHull<2>;

	void unlinkAndFreeFireHull( FireHull *hull );
	void unlinkAndFreeFireClusterHull( FireClusterHull *hull );
	void unlinkAndFreeBlastHull( BlastHull *hull );
	void unlinkAndFreeSmokeHull( SmokeHull *hull );
	void unlinkAndFreeWaveHull( WaveHull *hull );

	template <typename Hull, bool HasShapeLists>
	[[nodiscard]]
	auto allocHull( Hull **head, wsw::FreelistAllocator *allocator, int64_t currTime, unsigned lifetime ) -> Hull *;

	// TODO: Having these specialized methods while the actual setup is performed by the caller feels wrong...

	[[nodiscard]]
	auto allocFireHull( int64_t currTime, unsigned lifetime ) -> FireHull *;
	[[nodiscard]]
	auto allocFireClusterHull( int64_t currTime, unsigned lifetime ) -> FireClusterHull *;
	[[nodiscard]]
	auto allocBlastHull( int64_t currTime, unsigned lifetime ) -> BlastHull *;
	[[nodiscard]]
	auto allocSmokeHull( int64_t currTime, unsigned lifetime ) -> SmokeHull *;
	[[nodiscard]]
	auto allocWaveHull( int64_t currTime, unsigned lifetime ) -> WaveHull *;

	void setupHullVertices( BaseRegularSimulatedHull *hull, const float *origin, const float *color,
							float speed, float speedSpread );

	void setupHullVertices( BaseConcentricSimulatedHull *hull, const float *origin,
							float scale, std::span<const HullLayerParams> paramsOfLayers );

	[[maybe_unused]]
	static bool processColorChange( int64_t currTime, int64_t spawnTime, unsigned effectDuration,
									std::span<const ColorChangeTimelineNode> timeline,
									std::span<byte_vec4_t> colors,
									ColorChangeState *__restrict state,
									wsw::RandomGenerator *__restrict rng );

	[[nodiscard]]
	static auto computeCurrTimelineNodeIndex( unsigned startFromIndex, int64_t currTime,
											  int64_t spawnTime, unsigned effectDuration,
											  std::span<const ColorChangeTimelineNode> timeline ) -> unsigned;

	FireHull *m_fireHullsHead { nullptr };
	FireClusterHull *m_fireClusterHullsHead { nullptr };
	BlastHull *m_blastHullsHead { nullptr };
	SmokeHull *m_smokeHullsHead { nullptr };
	WaveHull *m_waveHullsHead { nullptr };

	static constexpr unsigned kMaxFireHulls  = 32;
	static constexpr unsigned kMaxFireClusterHulls = kMaxFireHulls * 2;
	static constexpr unsigned kMaxBlastHulls = 32;
	static constexpr unsigned kMaxSmokeHulls = kMaxFireHulls * 2;
	static constexpr unsigned kMaxWaveHulls  = kMaxFireHulls;

	wsw::StaticVector<CMShapeList *, kMaxSmokeHulls + kMaxWaveHulls> m_freeShapeLists;
	CMShapeList *m_tmpShapeList { nullptr };

	wsw::HeapBasedFreelistAllocator m_fireHullsAllocator { sizeof( FireHull ), kMaxFireHulls };
	wsw::HeapBasedFreelistAllocator m_fireClusterHullsAllocator { sizeof( FireClusterHull ), kMaxFireHulls };
	wsw::HeapBasedFreelistAllocator m_blastHullsAllocator { sizeof( BlastHull ), kMaxBlastHulls };
	wsw::HeapBasedFreelistAllocator m_smokeHullsAllocator { sizeof( SmokeHull ), kMaxSmokeHulls };
	wsw::HeapBasedFreelistAllocator m_waveHullsAllocator { sizeof( WaveHull ), kMaxWaveHulls };

	wsw::RandomGenerator m_rng;
	int64_t m_lastTime { 0 };
};

#endif