/*
Copyright (C) 2007 Victor Luchits

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
#include "scene.h"
#include "materiallocal.h"
#include "../qcommon/singletonholder.h"

/*
=============================================================

FRUSTUM AND PVS CULLING

=============================================================
*/

/*
* R_SetupFrustum
*/
void R_SetupFrustum( const refdef_t *rd, float farClip, cplane_t *frustum ) {
	int i;
	vec3_t forward, left, up;

	// 0 - left
	// 1 - right
	// 2 - down
	// 3 - up
	// 4 - farclip

	VectorCopy( &rd->viewaxis[AXIS_FORWARD], forward );
	VectorCopy( &rd->viewaxis[AXIS_RIGHT], left );
	VectorCopy( &rd->viewaxis[AXIS_UP], up );

	if( rd->rdflags & RDF_USEORTHO ) {
		VectorNegate( left, frustum[0].normal );
		VectorCopy( left, frustum[1].normal );
		VectorNegate( up, frustum[2].normal );
		VectorCopy( up, frustum[3].normal );

		for( i = 0; i < 4; i++ ) {
			frustum[i].type = PLANE_NONAXIAL;
			frustum[i].dist = DotProduct( rd->vieworg, frustum[i].normal );
			frustum[i].signbits = SignbitsForPlane( &frustum[i] );
		}

		frustum[0].dist -= rd->ortho_x;
		frustum[1].dist -= rd->ortho_x;
		frustum[2].dist -= rd->ortho_y;
		frustum[3].dist -= rd->ortho_y;
	} else {
		vec3_t right;

		VectorNegate( left, right );
		// rotate rn.vpn right by FOV_X/2 degrees
		RotatePointAroundVector( frustum[0].normal, up, forward, -( 90 - rd->fov_x / 2 ) );
		// rotate rn.vpn left by FOV_X/2 degrees
		RotatePointAroundVector( frustum[1].normal, up, forward, 90 - rd->fov_x / 2 );
		// rotate rn.vpn up by FOV_X/2 degrees
		RotatePointAroundVector( frustum[2].normal, right, forward, 90 - rd->fov_y / 2 );
		// rotate rn.vpn down by FOV_X/2 degrees
		RotatePointAroundVector( frustum[3].normal, right, forward, -( 90 - rd->fov_y / 2 ) );

		for( i = 0; i < 4; i++ ) {
			frustum[i].type = PLANE_NONAXIAL;
			frustum[i].dist = DotProduct( rd->vieworg, frustum[i].normal );
			frustum[i].signbits = SignbitsForPlane( &frustum[i] );
		}
	}

	// farclip
	VectorNegate( forward, frustum[4].normal );
	frustum[4].type = PLANE_NONAXIAL;
	frustum[4].dist = DotProduct( rd->vieworg, frustum[4].normal ) - farClip;
	frustum[4].signbits = SignbitsForPlane( &frustum[4] );
}

/*
* R_CullBox
*
* Returns true if the box is completely outside the frustum
*/
bool R_CullBox( const vec3_t mins, const vec3_t maxs, const unsigned int clipflags ) {
	unsigned int i, bit;
	const cplane_t *p;

	if( r_nocull->integer ) {
		return false;
	}

	for( i = sizeof( rn.frustum ) / sizeof( rn.frustum[0] ), bit = 1, p = rn.frustum; i > 0; i--, bit <<= 1, p++ ) {
		if( !( clipflags & bit ) ) {
			continue;
		}

		switch( p->signbits & 7 ) {
			case 0:
				if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist ) {
					return true;
				}
				break;
			case 1:
				if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist ) {
					return true;
				}
				break;
			case 2:
				if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist ) {
					return true;
				}
				break;
			case 3:
				if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist ) {
					return true;
				}
				break;
			case 4:
				if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist ) {
					return true;
				}
				break;
			case 5:
				if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist ) {
					return true;
				}
				break;
			case 6:
				if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist ) {
					return true;
				}
				break;
			case 7:
				if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist ) {
					return true;
				}
				break;
			default:
				break;
		}
	}

	return false;
}

/*
* R_CullSphere
*
* Returns true if the sphere is completely outside the frustum
*/
bool R_CullSphere( const vec3_t centre, const float radius, const unsigned int clipflags ) {
	unsigned int i;
	unsigned int bit;
	const cplane_t *p;

	if( r_nocull->integer ) {
		return false;
	}

	for( i = sizeof( rn.frustum ) / sizeof( rn.frustum[0] ), bit = 1, p = rn.frustum; i > 0; i--, bit <<= 1, p++ ) {
		if( !( clipflags & bit ) ) {
			continue;
		}
		if( DotProduct( centre, p->normal ) - p->dist <= -radius ) {
			return true;
		}
	}

	return false;
}

/*
* R_VisCullBox
*/
bool R_VisCullBox( const vec3_t mins, const vec3_t maxs ) {
	int s, stackdepth = 0;
	vec3_t extmins, extmaxs;
	int localstack[2048];

	if( !rsh.worldModel || ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return false;
	}
	if( rn.renderFlags & RF_NOVIS ) {
		return false;
	}

	for( s = 0; s < 3; s++ ) {
		extmins[s] = mins[s] - 4;
		extmaxs[s] = maxs[s] + 4;
	}

	const auto *__restrict nodes = rsh.worldBrushModel->nodes;

	int nodeNum = 0;
	for(;; ) {
		if( nodeNum < 0 ) {
			// TODO: Implement/reorder cull/vis cull calls
			/*if( !rf.worldLeafVis[(mleaf_t *)node - rsh.worldBrushModel->leafs] )
			{
			    if( !stackdepth )
			        return true;
			    node = localstack[--stackdepth];
			    continue;
			}*/
			return false;
		}

		const auto *__restrict node = nodes + nodeNum;

		s = BOX_ON_PLANE_SIDE( extmins, extmaxs, &node->plane ) - 1;
		if( s < 2 ) {
			nodeNum = node->children[s];
			continue;
		}

		// go down both sides
		if( stackdepth < sizeof( localstack ) / sizeof( mnode_t * ) ) {
			localstack[stackdepth++] = node->children[0];
		}
		nodeNum = node->children[1];
	}
}

/*
* R_VisCullSphere
*/
bool R_VisCullSphere( const vec3_t origin, float radius ) {
	float dist;
	int stackdepth = 0;
	int localstack[2048];

	if( !rsh.worldModel || ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return false;
	}
	if( rn.renderFlags & RF_NOVIS ) {
		return false;
	}

	radius += 4;

	const auto *__restrict nodes = rsh.worldBrushModel->nodes;

	int nodeNum = 0;
	for(;; ) {
		if( nodeNum < 0 ) {
			// TODO: Implement/reorder cull/vis cull calls
			/*if( !rf.worldLeafVis[(mleaf_t *)node - rsh.worldBrushModel->leafs] )
			{
			    if( !stackdepth )
			        return true;
			    node = localstack[--stackdepth];
			    continue;
			}*/
			return false;
		}

		const auto *__restrict node = nodes + nodeNum;

		dist = PlaneDiff( origin, &node->plane );
		if( dist > radius ) {
			nodeNum = node->children[0];
			continue;
		} else if( dist < -radius ) {
			nodeNum = node->children[1];
			continue;
		}

		// go down both sides
		if( stackdepth < sizeof( localstack ) / sizeof( mnode_t * ) ) {
			localstack[stackdepth++] = node->children[0];
		} else {
			assert( 0 );
		}
		nodeNum = node->children[1];
	}
}

/*
* R_CullModelEntity
*/
int R_CullModelEntity( const entity_t *e, vec3_t mins, vec3_t maxs, float radius, bool sphereCull, bool pvsCull ) {
	if( e->flags & RF_NOSHADOW ) {
		if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
			return 3;
		}
	}

	if( e->flags & RF_WEAPONMODEL ) {
		if( rn.renderFlags & RF_NONVIEWERREF ) {
			return 1;
		}
		return 0;
	}

	if( e->flags & RF_VIEWERMODEL ) {
		//if( !(rn.renderFlags & RF_NONVIEWERREF) )
		if( !( rn.renderFlags & ( RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
			return 1;
		}
	}

	if( e->flags & RF_NODEPTHTEST ) {
		return 0;
	}

	// account for possible outlines
	if( e->outlineHeight ) {
		radius += e->outlineHeight * r_outlines_scale->value * 1.73 /*sqrt(3)*/;
	}

	if( sphereCull ) {
		if( R_CullSphere( e->origin, radius, rn.clipFlags ) ) {
			return 1;
		}
	} else {
		if( R_CullBox( mins, maxs, rn.clipFlags ) ) {
			return 1;
		}
	}

	if( pvsCull ) {
		if( sphereCull ) {
			if( R_VisCullSphere( e->origin, radius ) ) {
				return 2;
			}
		} else {
			if( R_VisCullBox( mins, maxs ) ) {
				return 2;
			}
		}
	}

	return 0;
}

namespace wsw::ref {

static SingletonHolder<Scene> sceneInstanceHolder;

void Scene::Init() {
	sceneInstanceHolder.Init();
}

void Scene::Shutdown() {
	sceneInstanceHolder.Shutdown();
}

Scene *Scene::Instance() {
	return sceneInstanceHolder.Instance();
}

void Scene::InitVolatileAssets() {
	coronaShader = MaterialCache::instance()->loadDefaultMaterial( "$corona"_asView, SHADER_TYPE_CORONA );
}

void Scene::DestroyVolatileAssets() {
	coronaShader = nullptr;
}

void Scene::AddLight( const vec3_t org, float programIntensity, float coronaIntensity, float r, float g, float b ) {
	assert( r || g || b );
	assert( programIntensity || coronaIntensity );
	assert( coronaIntensity >= 0 );
	assert( programIntensity >= 0 );

	vec3_t color { r, g, b };
	if( r_lighting_grayscale->integer ) {
		float grey = ColorGrayscale( color );
		color[0] = color[1] = color[2] = bound( 0, grey, 1 );
	}

	// TODO: We can share culling information for program lights and coronae even if radii do not match

	const int cvarValue = r_dynamiclight->integer;
	if( ( cvarValue & ~1 ) && coronaIntensity && numCoronaLights < MAX_CORONA_LIGHTS ) {
		new( &coronaLights[numCoronaLights++] )Light( org, color, coronaIntensity );
	}

	if( ( cvarValue & 1 ) && programIntensity && numProgramLights < MAX_PROGRAM_LIGHTS ) {
		new( &programLights[numProgramLights++] )Light( org, color, programIntensity );
	}
}

void Scene::DynLightDirForOrigin( const vec_t *origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal ) {
	if( !( r_dynamiclight->integer & 1 ) ) {
		return;
	}

	vec3_t direction;
	bool anyDlights = false;

	// TODO: We can avoid doing a loop over all lights
	// if there's a spatial hierarchy for most entities that receive dlights

	const auto *__restrict lights = programLights;
	const LightNumType *__restrict nums = drawnProgramLightNums;
	for( int i = 0; i < numDrawnProgramLights; i++ ) {
		const auto *__restrict dl = lights + nums[i];
		const float squareDist = DistanceSquared( dl->center, origin );
		const float threshold = dl->radius + radius;
		if( squareDist > threshold * threshold ) {
			continue;
		}

		// Start computing invThreshold so hopefully the result is ready to the moment of its usage
		const float invThreshold = 1.0f / threshold;

		// TODO: Mark as "unlikely"
		if( squareDist < 0.001f ) {
			continue;
		}

		VectorSubtract( dl->center, origin, direction );
		const float invDist = Q_RSqrt( squareDist );
		VectorScale( direction, invDist, direction );

		if( !anyDlights ) {
			VectorNormalizeFast( dir );
			anyDlights = true;
		}

		const float dist = Q_Rcp( invDist );
		const float add = 1.0f - ( dist * invThreshold );
		const float dist2 = add * 0.5f / dist;
		for( int j = 0; j < 3; j++ ) {
			const float dot = dl->color[j] * add;
			diffuseLocal[j] += dot;
			ambientLocal[j] += dot * 0.05f;
			dir[j] += direction[j] * dist2;
		}
	}
}

uint32_t Scene::CullLights( unsigned clipFlags ) {
	if( rn.renderFlags & RF_ENVVIEW ) {
		return 0;
	}

	if( r_fullbright->integer ) {
		return 0;
	}

	const int cvarValue = r_dynamiclight->integer;
	if( !cvarValue ) {
		return 0;
	}

	if( cvarValue & ~1 ) {
		for( int i = 0; i < numCoronaLights; ++i ) {
			const auto &light = coronaLights[i];
			if( R_CullSphere( light.center, light.radius, clipFlags ) ) {
				continue;
			}
			drawnCoronaLightNums[numDrawnCoronaLights++] = (LightNumType)i;
		}
	}

	if( !( cvarValue & 1 ) ) {
		return 0;
	}

	// TODO: Use PVS as well..
	// TODO: Mark surfaces that the light has an impact on during PVS BSP traversal
	// TODO: Cull world nodes / surfaces prior to this so we do not have to test light impact on culled surfaces

	if( numProgramLights <= MAX_DLIGHTS ) {
		for( int i = 0; i < numProgramLights; ++i ) {
			const auto &light = programLights[i];
			if( R_CullSphere( light.center, light.radius, clipFlags ) ) {
				continue;
			}
			drawnProgramLightNums[numDrawnProgramLights++] = (LightNumType)i;
		}
		return BitsForNumberOfLights( numDrawnProgramLights );
	}

	int numCulledLights = 0;
	for( int i = 0; i < numProgramLights; ++i ) {
		const auto &light = programLights[i];
		if( R_CullSphere( light.center, light.radius, clipFlags ) ) {
			continue;
		}
		drawnProgramLightNums[numCulledLights++] = (LightNumType)i;
	}

	if( numCulledLights <= MAX_DLIGHTS ) {
		numDrawnProgramLights = numCulledLights;
		return BitsForNumberOfLights( numDrawnProgramLights );
	}

	// TODO: We can reuse computed distances for further surface sorting...

	struct LightAndScore {
		int num;
		float score;

		LightAndScore() = default;
		LightAndScore( int num_, float score_ ): num( num_ ), score( score_ ) {}
		bool operator<( const LightAndScore &that ) const { return score < that.score; }
	};

	// TODO: Use a proper component layout and SIMD distance (dot) computations here

	int numSortedLights = 0;
	LightAndScore sortedLights[MAX_PROGRAM_LIGHTS];
	for( int i = 0; i < numCulledLights; ++i ) {
		const int num = drawnProgramLightNums[i];
		const Light *light = &programLights[num];
		float score = Q_RSqrt( DistanceSquared( light->center, rn.viewOrigin ) ) * light->radius;
		new( &sortedLights[numSortedLights++] )LightAndScore( num, score );
		std::push_heap( sortedLights, sortedLights + numSortedLights );
	}

	numDrawnProgramLights = 0;
	while( numDrawnProgramLights < MAX_DLIGHTS ) {
		std::pop_heap( sortedLights, sortedLights + numSortedLights );
		assert( numSortedLights > 0 );
		numSortedLights--;
		int num = sortedLights[numSortedLights].num;
		drawnProgramLightNums[numDrawnProgramLights++] = num;
	}

	assert( numDrawnProgramLights == MAX_DLIGHTS );
	return BitsForNumberOfLights( MAX_DLIGHTS );
}

void Scene::DrawCoronae() {
	if( !( r_dynamiclight->integer & ~1 ) ) {
		return;
	}

	const auto *__restrict nums = drawnCoronaLightNums;
	const auto *__restrict lights = coronaLights;
	const float *__restrict viewOrigin = rn.viewOrigin;
	auto *__restrict meshList = rn.meshlist;
	auto *__restrict polyEnt = rsc.polyent;

	bool hasManyFogs = false;
	mfog_t *fog = nullptr;
	if( !( rn.renderFlags & RF_SHADOWMAPVIEW ) &&  !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		if( rsh.worldModel && rsh.worldBrushModel->numfogs ) {
			if( auto *globalFog = rsh.worldBrushModel->globalfog ) {
				fog = globalFog;
			} else {
				hasManyFogs = true;
			}
		}
	}

	const auto numLights = numDrawnCoronaLights;
	if( !hasManyFogs ) {
		for( int i = 0; i < numLights; ++i ) {
			const auto *light = &lights[nums[i]];
			const float distance = Q_Rcp( Q_RSqrt( DistanceSquared( viewOrigin, light->center ) ) );
			// TODO: All this stuff below should use restrict qualifiers
			R_AddSurfToDrawList( meshList, polyEnt, fog, coronaShader, distance, 0, nullptr, &coronaSurfs[i] );
		}
		return;
	}

	for( int i = 0; i < numLights; i++ ) {
		const auto *light = &lights[nums[i]];
		const float distance = Q_Rcp( Q_RSqrt( DistanceSquared( viewOrigin, light->center ) ) );
		// TODO: We can skip some tests even in this case
		fog = R_FogForSphere( light->center, 1 );
		R_AddSurfToDrawList( meshList, polyEnt, fog, coronaShader, distance, 0, nullptr, &coronaSurfs[i] );
	}
}

}