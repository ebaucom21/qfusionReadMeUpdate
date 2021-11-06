/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2013 Victor Luchits

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

#ifndef WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H
#define WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H

void R_SetupFrustum( const refdef_t *rd, float farClip, cplane_t *frustum );

namespace wsw::ref {

/// \brief This is supposed to be a (queryable) spatial database that performs culling, finds light interactions, etc
class Scene {
	shader_t *coronaShader { nullptr };
public:
	// TODO: Declare it private, no time to check for compiler quicks for now
	// (there're problems with singleton holder friend/forward declarations across various compilers)
	Scene() {
		for( int i = 0; i < MAX_CORONA_LIGHTS; ++i ) {
			coronaSurfs[i] = ST_CORONA;
		}
	}

	using LightNumType = uint8_t;

	class Light {
		friend class Scene;
	public:
		vec3_t color;
		float radius;
		vec4_t center;
	private:
		Light() {}

		Light( const float *center_, const float *color_, float radius_ ) {
			VectorCopy( color_, this->color );
			this->radius = radius_;
			VectorCopy( center_, this->center );
			this->center[3] = 0;
		}
	};

private:
	enum { MAX_CORONA_LIGHTS = 255 };
	enum { MAX_PROGRAM_LIGHTS = 255 };

	Light coronaLights[MAX_CORONA_LIGHTS];
	Light programLights[MAX_PROGRAM_LIGHTS];

	int numCoronaLights { 0 };
	int numProgramLights { 0 };

	LightNumType drawnCoronaLightNums[MAX_CORONA_LIGHTS];
	LightNumType drawnProgramLightNums[MAX_PROGRAM_LIGHTS];

	int numDrawnCoronaLights { 0 };
	int numDrawnProgramLights { 0 };

	drawSurfaceType_t coronaSurfs[MAX_CORONA_LIGHTS];

	uint32_t BitsForNumberOfLights( int numLights ) {
		assert( numLights <= 32 );
		return (uint32_t)( ( 1ull << (uint64_t)( numLights ) ) - 1 );
	}
public:
	static void Init();
	static void Shutdown();
	static Scene *Instance();

	void Clear() {
		numCoronaLights = 0;
		numProgramLights = 0;

		numDrawnCoronaLights = 0;
		numDrawnProgramLights = 0;
	}

	void InitVolatileAssets();

	void DestroyVolatileAssets();

	void AddLight( const vec3_t origin, float programIntensity, float coronaIntensity, float r, float g, float b );

	void DynLightDirForOrigin( const vec3_t origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal );

	const Light *LightForCoronaSurf( const drawSurfaceType_t *surf ) const {
		return &coronaLights[surf - coronaSurfs];
	}

	// CAUTION: The meaning of dlight bits has been changed:
	// a bit correspond to an index of a light num in drawnProgramLightNums
	uint32_t CullLights( unsigned clipFlags );

	void DrawCoronae();

	const Light *ProgramLightForNum( LightNumType num ) const {
		assert( (unsigned)num < (unsigned)MAX_PROGRAM_LIGHTS );
		return &programLights[num];
	}

	void GetDrawnProgramLightNums( const LightNumType **rangeBegin, const LightNumType **rangeEnd ) const {
		*rangeBegin = drawnProgramLightNums;
		*rangeEnd = drawnProgramLightNums + numDrawnProgramLights;
	}
};

}

#endif