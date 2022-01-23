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

class CMShapeList;
class DrawSceneRequest;

/// Manages "fire-and-forget" effects that usually get spawned upon events.
class TransientEffectsSystem {
public:
	TransientEffectsSystem();
	~TransientEffectsSystem();

	void spawnExplosion( const float *origin, const float *color, float radius = 72.0f );
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
		float lightOrigin[3] { 0.0f, 0.0f, 0.0f };
		float lightColor[3] { 1.0f, 1.0f, 1.0f };
		float lightRadius { 0.0f };
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

	static constexpr unsigned kNumHullVertices = 162;

	struct SimulatedHull {
		SimulatedHull *prev { nullptr }, *next { nullptr };
		CMShapeList *const shapeList { nullptr };
		const uint16_t *meshIndices { nullptr };
		const int64_t spawnTime { 0 };
		const unsigned lifetime { 0 };
		unsigned numMeshIndices { 0 };
		vec4_t mins, maxs;
		vec3_t origin;

		// Old/current
		vec4_t vertexPositions[2][kNumHullVertices];
		vec3_t vertexVelocities[kNumHullVertices];
		// 0.0f or 1.0f, just to reduce branching during the vertices update
		float vertexMovability[kNumHullVertices];
		byte_vec4_t vertexColors[kNumHullVertices];

		unsigned positionsFrame { 0 };

		// The renderer assumes external lifetime of the submitted spans. Keep the buffer within the hull.
		ExternalMesh meshSubmissionBuffer[1];

		void simulate( int64_t currTime, float timeDeltaSeconds );
	};

	void unlinkAndFree( EntityEffect *effect );
	void unlinkAndFree( SimulatedHull *hull );

	[[nodiscard]]
	auto addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocSimulatedHull( int64_t currTime, unsigned lifetime ) -> SimulatedHull *;

	void setupHullVertices( SimulatedHull *hull, const float *origin, float speed, const float *color );

	void simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );
	void simulateHullsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );

	static constexpr unsigned kMaxSimulatedHulls = 64;

	wsw::StaticVector<CMShapeList *, kMaxSimulatedHulls> m_freeShapeLists;

	wsw::HeapBasedFreelistAllocator m_entityEffectsAllocator { sizeof( EntityEffect ), 256 };
	wsw::HeapBasedFreelistAllocator m_simulatedHullsAllocator { sizeof( SimulatedHull ), kMaxSimulatedHulls };

	EntityEffect *m_entityEffectsHead { nullptr };
	SimulatedHull *m_simulatedHullsHead { nullptr };

	wsw::RandomGenerator m_rng;
	int64_t m_lastTime { 0 };
};

#endif
