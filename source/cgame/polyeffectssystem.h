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

#ifndef WSW_424d6447_3887_491d_8150_c80cf50fd066_H
#define WSW_424d6447_3887_491d_8150_c80cf50fd066_H

#include "../ref/ref.h"
#include "../qcommon/freelistallocator.h"

class DrawSceneRequest;

class PolyEffectsSystem {
public:
	// Opaque handles TODO optimize layout?
	struct CurvedBeam {};
	struct StraightBeam {};

	~PolyEffectsSystem();

	[[nodiscard]]
	auto createCurvedBeamEffect( shader_s *material ) -> CurvedBeam *;
	void updateCurvedBeamEffect( CurvedBeam *, const float *color, float width,
								 float tileLength, std::span<const vec3_t> points );
	void destroyCurvedBeamEffect( CurvedBeam * );

	[[nodiscard]]
	auto createStraightBeamEffect( shader_s *material ) -> StraightBeam *;
	void updateStraightBeamEffect( StraightBeam *, const float *color, float width,
								   float tileLength, const float *from, const float *to );
	void destroyStraightBeamEffect( StraightBeam * );

	struct TransientBeamParams {
		shader_s *material { nullptr };
		ColorLifespan beamColorLifespan;
		std::optional<std::pair<unsigned, LightLifespan>> lightProps;
		float width { 0.0f };
		float tileLength { 0.0f };
		unsigned timeout;
	};

	void spawnTransientBeamEffect( const float *from, const float *to, TransientBeamParams &&params );

	struct TracerParams {
		shader_s *material { nullptr };
		float prestep { 0.0f };
		float width { 0.0f };
		float length { 0.0f };
		float color[4] { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	void spawnTracerEffect( const float *from, const float *to, TracerParams &&params );

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request );
private:
	struct StraightBeamEffect : public StraightBeam {
		StraightBeamEffect *prev { nullptr }, *next { nullptr };
		QuadPoly poly;
	};

	static constexpr unsigned kMaxCurvedBeamSegments = 24;

	struct CurvedBeamEffect : public CurvedBeam {
		CurvedBeamEffect *prev { nullptr }, *next { nullptr };
		ComplexPoly poly;

		static constexpr unsigned kNumPlanes = 2;

		// TODO: We don't need that much for adjacent quads
		vec4_t storageOfPositions[kNumPlanes * kMaxCurvedBeamSegments * 4];
		vec2_t storageOfTexCoords[kNumPlanes * kMaxCurvedBeamSegments * 4];
		byte_vec4_t storageOfColors[kNumPlanes * kMaxCurvedBeamSegments * 4];
		uint16_t storageOfIndices[kNumPlanes * kMaxCurvedBeamSegments * 6];
	};

	struct TransientBeamEffect {
		TransientBeamEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime;
		ColorLifespan colorLifespan;
		unsigned timeout;
		// Contrary to tracked laser beams, the light position is computed in an "immediate" mode.
		// As these beams are multifunctional, the light appearance is more configurable.
		// This field is a-maybe pair of light timeout and light lifespan
		std::optional<std::pair<unsigned, LightLifespan>> lightProps;
		QuadPoly poly;
	};

	struct TracerEffect {
		TracerEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime;
		vec3_t from;
		vec3_t to;
		float speed { 0.0f };
		float totalDistance { 0.0f };
		float distanceSoFar { 0.0f };
		QuadPoly poly;
	};

	void destroyTransientBeamEffect( TransientBeamEffect *effect );

	void destroyTracerEffect( TracerEffect *effect );

	wsw::HeapBasedFreelistAllocator m_straightLaserBeamsAllocator { sizeof( StraightBeamEffect ), MAX_CLIENTS };
	wsw::HeapBasedFreelistAllocator m_curvedLaserBeamsAllocator { sizeof( CurvedBeamEffect ), MAX_CLIENTS };

	wsw::HeapBasedFreelistAllocator m_transientBeamsAllocator { sizeof( TransientBeamEffect ), MAX_CLIENTS * 2 };

	wsw::HeapBasedFreelistAllocator m_tracerEffectsAllocator { sizeof( TracerEffect ), MAX_CLIENTS * 4 };

	StraightBeamEffect *m_straightLaserBeamsHead { nullptr };
	CurvedBeamEffect *m_curvedLaserBeamsHead { nullptr };

	TransientBeamEffect *m_transientBeamsHead { nullptr };

	TracerEffect *m_tracerEffectsHead { nullptr };

	int64_t m_lastTime { 0 };
};

#endif