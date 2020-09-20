/*
Copyright (C) 2013 Victor Luchits

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

// r_surf.c: surface-related refresh code

#include "local.h"
#include "../qcommon/qcommon.h"
#include <algorithm>

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

static vec3_t modelOrg;                         // relative to view point

//==================================================================================

/*
* R_SurfPotentiallyVisible
*/
bool R_SurfPotentiallyVisible( const msurface_t *surf ) {
	const shader_t *shader = surf->shader;
	if( surf->flags & SURF_NODRAW ) {
		return false;
	}
	if( !surf->mesh.numVerts ) {
		return false;
	}
	if( !shader ) {
		return false;
	}
	return true;
}

/*
* R_SurfPotentiallyShadowed
*/
bool R_SurfPotentiallyShadowed( const msurface_t *surf ) {
	if( surf->flags & ( SURF_SKY | SURF_NODLIGHT | SURF_NODRAW ) ) {
		return false;
	}
	if( ( surf->shader->sort >= SHADER_SORT_OPAQUE ) && ( surf->shader->sort <= SHADER_SORT_ALPHATEST ) ) {
		return true;
	}
	return false;
}

/*
* R_SurfPotentiallyLit
*/
bool R_SurfPotentiallyLit( const msurface_t *surf ) {
	const shader_t *shader;

	if( surf->flags & ( SURF_SKY | SURF_NODLIGHT | SURF_NODRAW ) ) {
		return false;
	}
	shader = surf->shader;
	if( !shader->numpasses ) {
		return false;
	}
	return ( surf->mesh.numVerts != 0 /* && (surf->facetype != FACETYPE_TRISURF)*/ );
}

/*
* R_CullSurface
*/
bool R_CullSurface( const entity_t *e, const msurface_t *surf, unsigned int clipflags ) {
	return ( clipflags && R_CullBox( surf->mins, surf->maxs, clipflags ) );
}

/*
* R_SurfaceDlightBits
*/
static unsigned int R_SurfaceDlightBits( const msurface_t *surf, unsigned int checkDlightBits ) {
	float dist;
	unsigned int surfDlightBits = 0;

	if( !R_SurfPotentiallyLit( surf ) ) {
		return 0;
	}

	// TODO: This is totally wrong! Lights should mark affected surfaces in their radius on their own!

	const auto *scene = Scene::Instance();
	const Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	// Caution: bits correspond to indices in range now!
	uint32_t mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		// TODO: This condition seems to be pointless
		if( checkDlightBits & mask ) {
			const auto *__restrict lt = scene->ProgramLightForNum( *iter );
			// TODO: Avoid doing this and keep separate lists of planar and other surfaces
			switch( surf->facetype ) {
				case FACETYPE_PLANAR:
					dist = DotProduct( lt->center, surf->plane ) - surf->plane[3];
					if( dist > -lt->radius && dist < lt->radius ) {
						surfDlightBits |= mask;
					}
					break;
				case FACETYPE_PATCH:
				case FACETYPE_TRISURF:
				case FACETYPE_FOLIAGE:
					if( BoundsAndSphereIntersect( surf->mins, surf->maxs, lt->center, lt->radius ) ) {
						surfDlightBits |= mask;
					}
					break;
			}
			checkDlightBits &= ~mask;
			if( !checkDlightBits ) {
				break;
			}
		}
	}

	return surfDlightBits;
}

/*
* R_DrawBSPSurf
*/
void R_DrawBSPSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int entShadowBits, drawSurfaceBSP_t *drawSurf ) {
	const vboSlice_t *slice;
	const vboSlice_t *shadowSlice;
	static const vboSlice_t nullSlice = { 0 };
	int firstVert, firstElem;
	int numVerts, numElems;
	unsigned dlightBits;

	slice = R_GetDrawListVBOSlice( rn.meshlist, drawSurf - rsh.worldBrushModel->drawSurfaces );
	shadowSlice = R_GetDrawListVBOSlice( rn.meshlist, rsh.worldBrushModel->numDrawSurfaces + ( drawSurf - rsh.worldBrushModel->drawSurfaces ) );
	if( !shadowSlice ) {
		shadowSlice = &nullSlice;
	}

	assert( slice != NULL );
	if( !slice ) {
		return;
	}

	// shadowBits are shared for all rendering instances (normal view, portals, etc)
	dlightBits = drawSurf->dlightBits;

	// if either shadow slice is empty or shadowBits is 0, then we must pass the surface unshadowed

	numVerts = slice->numVerts;
	numElems = slice->numElems;
	firstVert = drawSurf->firstVboVert + slice->firstVert;
	firstElem = drawSurf->firstVboElem + slice->firstElem;

	if( !numVerts ) {
		return;
	}

	RB_BindVBO( drawSurf->vbo->index, GL_TRIANGLES );

	RB_SetDlightBits( dlightBits );

	RB_SetLightstyle( drawSurf->superLightStyle );

	if( drawSurf->numInstances ) {
		RB_DrawElementsInstanced( firstVert, numVerts, firstElem, numElems,
								  drawSurf->numInstances, drawSurf->instances );
	} else {
		RB_DrawElements( firstVert, numVerts, firstElem, numElems );
	}
}

/*
* R_AddSurfaceVBOSlice
*/
static void R_AddSurfaceVBOSlice( drawList_t *list, drawSurfaceBSP_t *drawSurf, const msurface_t *surf, int offset ) {
	R_AddDrawListVBOSlice( list, offset + drawSurf - rsh.worldBrushModel->drawSurfaces,
				   surf->mesh.numVerts, surf->mesh.numElems,
				   surf->firstDrawSurfVert, surf->firstDrawSurfElem );
}

/*
* R_AddSurfaceToDrawList
*/
static bool R_AddSurfaceToDrawList( const entity_t *e, drawSurfaceBSP_t *drawSurf ) {
	const shader_t *shader = drawSurf->shader;
	const mfog_t *fog = drawSurf->fog;
	portalSurface_t *portalSurface = NULL;
	unsigned drawOrder = 0;
	unsigned sliceIndex = drawSurf - rsh.worldBrushModel->drawSurfaces;

	if( drawSurf->visFrame == rf.frameCount ) {
		return true;
	}

	const bool portal = ( shader->flags & SHADER_PORTAL ) != 0;
	if( portal ) {
		portalSurface = R_AddPortalSurface( e, shader, drawSurf );
	}

	drawOrder = R_PackOpaqueOrder( fog, shader, drawSurf->numLightmaps, false );

	drawSurf->dlightBits = 0;
	drawSurf->visFrame = rf.frameCount;
	drawSurf->listSurf = R_AddSurfToDrawList( rn.meshlist, e, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface, drawSurf );
	if( !drawSurf->listSurf ) {
		return false;
	}

	if( portalSurface && !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) ) {
		R_AddSurfToDrawList( rn.portalmasklist, e, NULL, NULL, 0, 0, NULL, drawSurf );
	}

	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex, 0, 0, 0, 0 );
	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex + rsh.worldBrushModel->numDrawSurfaces, 0, 0, 0, 0 );

	rf.stats.c_world_draw_surfs++;
	return true;
}

/*
* R_ClipSpecialWorldSurf
*/
static bool R_ClipSpecialWorldSurf( drawSurfaceBSP_t *drawSurf, const msurface_t *surf, const vec3_t origin, float *pdist ) {
	portalSurface_t *portalSurface = NULL;
	const shader_t *shader = drawSurf->shader;

	const bool portal = ( shader->flags & SHADER_PORTAL ) != 0;
	if( portal ) {
		portalSurface = R_GetDrawListSurfPortal( drawSurf->listSurf );
	}

	if( portalSurface != NULL ) {
		vec3_t centre;
		float dist = 0;

		if( origin ) {
			VectorCopy( origin, centre );
		} else {
			VectorAdd( surf->mins, surf->maxs, centre );
			VectorScale( centre, 0.5, centre );
		}
		dist = Distance( rn.refdef.vieworg, centre );

		// draw portals in front-to-back order
		dist = 1024 - dist / 100.0f;
		if( dist < 1 ) {
			dist = 1;
		}

		R_UpdatePortalSurface( portalSurface, &surf->mesh, surf->mins, surf->maxs, shader, drawSurf );

		*pdist = dist;
	}

	return true;
}

/*
* R_UpdateSurfaceInDrawList
*
* Walk the list of visible world surfaces and prepare the final VBO slice and draw order bits.
* For sky surfaces, skybox clipping is also performed.
*/
static void R_UpdateSurfaceInDrawList( drawSurfaceBSP_t *drawSurf, unsigned int dlightBits, const vec3_t origin ) {
	unsigned i, end;
	float dist = 0;
	bool special;
	msurface_t *surf;
	unsigned dlightFrame;
	unsigned curDlightBits;
	msurface_t *firstVisSurf, *lastVisSurf;

	if( !drawSurf->listSurf ) {
		return;
	}

	firstVisSurf = lastVisSurf = NULL;

	curDlightBits = dlightFrame = 0;

	end = drawSurf->firstWorldSurface + drawSurf->numWorldSurfaces;
	surf = rsh.worldBrushModel->surfaces + drawSurf->firstWorldSurface;

	special = ( drawSurf->shader->flags & (SHADER_PORTAL) ) != 0;

	for( i = drawSurf->firstWorldSurface; i < end; i++ ) {
		if( rf.worldSurfVis[i] ) {
			float sdist = 0;
			unsigned int checkDlightBits = dlightBits & ~curDlightBits;

			if( special && !R_ClipSpecialWorldSurf( drawSurf, surf, origin, &sdist ) ) {
				// clipped away
				continue;
			}

			if( sdist > sdist )
				dist = sdist;

			if( checkDlightBits )
				checkDlightBits = R_SurfaceDlightBits( surf, checkDlightBits );

			// dynamic lights that affect the surface
			if( checkDlightBits ) {
				// ignore dlights that have already been marked as affectors
				if( dlightFrame == rsc.frameCount ) {
					curDlightBits |= checkDlightBits;
				} else {
					dlightFrame = rsc.frameCount;
					curDlightBits = checkDlightBits;
				}
			}

			// surfaces are sorted by their firstDrawVert index so to cut the final slice
			// we only need to note the first and the last surface
			if( firstVisSurf == NULL )
				firstVisSurf = surf;
			lastVisSurf = surf;
		}
		surf++;
	}

	if( dlightFrame == rsc.frameCount ) {
		drawSurf->dlightBits = curDlightBits;
		drawSurf->dlightFrame = dlightFrame;
	}

	// prepare the slice
	if( firstVisSurf ) {
		bool dlight = dlightFrame == rsc.frameCount;

		R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, firstVisSurf, 0 );

		if( lastVisSurf != firstVisSurf )
			R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, lastVisSurf, 0 );

		// update the distance sorting key if it's a portal surface or a normal dlit surface
		if( dist != 0 || dlight ) {
			int drawOrder = R_PackOpaqueOrder( drawSurf->fog, drawSurf->shader, drawSurf->numLightmaps, dlight );
			if( dist == 0 )
				dist = WORLDSURF_DIST;
			R_UpdateDrawSurfDistKey( drawSurf->listSurf, 0, drawSurf->shader, dist, drawOrder );
		}
	}
}

/*
=============================================================

BRUSH MODELS

=============================================================
*/

/*
* R_BrushModelBBox
*/
float R_BrushModelBBox( const entity_t *e, vec3_t mins, vec3_t maxs, bool *rotated ) {
	int i;
	const model_t   *model = e->model;

	if( !Matrix3_Compare( e->axis, axis_identity ) ) {
		if( rotated ) {
			*rotated = true;
		}
		for( i = 0; i < 3; i++ ) {
			mins[i] = e->origin[i] - model->radius * e->scale;
			maxs[i] = e->origin[i] + model->radius * e->scale;
		}
		return model->radius * e->scale;
	} else {
		if( rotated ) {
			*rotated = false;
		}
		VectorMA( e->origin, e->scale, model->mins, mins );
		VectorMA( e->origin, e->scale, model->maxs, maxs );
		return RadiusFromBounds( mins, maxs );
	}
}

#define R_TransformPointToModelSpace( e,rotate,in,out ) \
	VectorSubtract( in, ( e )->origin, out ); \
	if( rotated ) { \
		vec3_t temp; \
		VectorCopy( out, temp ); \
		Matrix3_TransformVector( ( e )->axis, temp, out ); \
	}

/*
* R_AddBrushModelToDrawList
*/
bool R_AddBrushModelToDrawList( const entity_t *e ) {
	unsigned int i;
	vec3_t origin;
	vec3_t bmins, bmaxs;
	bool rotated;
	model_t *model = e->model;
	mbrushmodel_t *bmodel = ( mbrushmodel_t * )model->extradata;
	mfog_t *fog;
	float radius;
	unsigned dlightBits;

	if( bmodel->numModelDrawSurfaces == 0 ) {
		return false;
	}

	radius = R_BrushModelBBox( e, bmins, bmaxs, &rotated );

	if( R_CullModelEntity( e, bmins, bmaxs, radius, rotated, false ) ) {
		return false;
	}

	VectorAdd( e->model->mins, e->model->maxs, origin );
	VectorMA( e->origin, 0.5, origin, origin );

	// TODO: Unused?
	fog = R_FogForBounds( bmins, bmaxs );

	R_TransformPointToModelSpace( e, rotated, rn.refdef.vieworg, modelOrg );

	const auto *scene = Scene::Instance();
	// TODO: Lights should mark models they affect on their own!

	// check dynamic lights that matter in the instance against the model
	dlightBits = 0;

	const Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	unsigned mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		const auto *light = scene->ProgramLightForNum( *iter );
		if( !BoundsAndSphereIntersect( bmins, bmaxs, light->center, light->radius ) ) {
			continue;
		}
		dlightBits |= mask;
	}

	dlightBits &= rn.dlightBits;

	for( i = 0; i < bmodel->numModelSurfaces; i++ ) {
		unsigned s = bmodel->firstModelSurface + i;
		msurface_t *surf = rsh.worldBrushModel->surfaces + s;

		if( !surf->drawSurf ) {
			continue;
		}
		if( R_CullSurface( e, surf, 0 ) ) {
			continue;
		}

		rf.worldSurfVis[s] = 1;
		rf.worldDrawSurfVis[surf->drawSurf - 1] = 1;
	}

	for( i = 0; i < bmodel->numModelDrawSurfaces; i++ ) {
		unsigned s = bmodel->firstModelDrawSurface + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + s;

		if( rf.worldDrawSurfVis[s] ) {
			R_AddSurfaceToDrawList( e, drawSurf );

			R_UpdateSurfaceInDrawList( drawSurf, dlightBits, origin );
		}
	}

	return true;
}

/*
=============================================================

WORLD MODEL

=============================================================
*/

/*
* R_PostCullVisLeaves
*/
static void R_PostCullVisLeaves( void ) {
	unsigned i, j;
	mleaf_t *leaf;
	
	for( i = 0; i < rsh.worldBrushModel->numvisleafs; i++ ) {
		if( !rf.worldLeafVis[i] ) {
			continue;
		}

		leaf = rsh.worldBrushModel->visleafs[i];

		// add leaf bounds to view bounds
		for( j = 0; j < 3; j++ ) {
			rn.visMins[j] = std::min( rn.visMins[j], leaf->mins[j] );
			rn.visMaxs[j] = std::max( rn.visMaxs[j], leaf->maxs[j] );
		}

		rf.stats.c_world_leafs++;
	}
}

/*
* R_CullVisLeaves
*/
static void R_CullVisLeaves( unsigned firstLeaf, unsigned numLeaves, unsigned clipFlags ) {
	unsigned i, j;
	mleaf_t *leaf;
	uint8_t *pvs;
	uint8_t *areabits;
	int arearowbytes, areabytes;
	bool novis;

	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return;
	}

	novis = rn.renderFlags & RF_NOVIS || rf.viewcluster == -1 || !rsh.worldBrushModel->pvs;
	arearowbytes = ( ( rsh.worldBrushModel->numareas + 7 ) / 8 );
	areabytes = arearowbytes;
#ifdef AREAPORTALS_MATRIX
	areabytes *= rsh.worldBrushModel->numareas;
#endif

	pvs = Mod_ClusterPVS( rf.viewcluster, rsh.worldModel );
	if( rf.viewarea > -1 && rn.refdef.areabits )
#ifdef AREAPORTALS_MATRIX
	{ areabits = rn.refdef.areabits + rf.viewarea * arearowbytes;}
#else
	{ areabits = rn.refdef.areabits;}
#endif
	else {
		areabits = NULL;
	}

	for( i = 0; i < numLeaves; i++ ) {
		int clipped;
		unsigned bit, testFlags;
		cplane_t *clipplane;
		unsigned l = firstLeaf + i;

		leaf = rsh.worldBrushModel->visleafs[l];
		if( !novis ) {
			// check for door connected areas
			if( areabits ) {
				if( leaf->area < 0 || !( areabits[leaf->area >> 3] & ( 1 << ( leaf->area & 7 ) ) ) ) {
					continue; // not visible
				}
			}

			if( !( pvs[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) ) {
				continue; // not visible
			}
		}

		// track leaves, which are entirely inside the frustum
		clipped = 0;
		testFlags = clipFlags;
		for( j = sizeof( rn.frustum ) / sizeof( rn.frustum[0] ), bit = 1, clipplane = rn.frustum; j > 0; j--, bit <<= 1, clipplane++ ) {
			if( testFlags & bit ) {
				clipped = BoxOnPlaneSide( leaf->mins, leaf->maxs, clipplane );
				if( clipped == 2 ) {
					break;
				} else if( clipped == 1 ) {
					testFlags &= ~bit; // node is entirely on screen
				}
			}
		}

		if( clipped == 2 ) {
			continue; // fully clipped
		}

		if( testFlags == 0 ) {
			// fully visible
			for( j = 0; j < leaf->numVisSurfaces; j++ ) {
				assert( leaf->visSurfaces[j] < rf.numWorldSurfVis );
				rf.worldSurfFullVis[leaf->visSurfaces[j]] = 1;
			}
		} else {
			// partly visible
			for( j = 0; j < leaf->numVisSurfaces; j++ ) {
				assert( leaf->visSurfaces[j] < rf.numWorldSurfVis );
				rf.worldSurfVis[leaf->visSurfaces[j]] = 1;
			}
		}

		rf.worldLeafVis[l] = 1;
	}
}

/*
* R_CullVisSurfaces
*/
static void R_CullVisSurfaces( unsigned firstSurf, unsigned numSurfs, unsigned clipFlags ) {
	unsigned i;
	unsigned end;
	msurface_t *surf;
	
	end = firstSurf + numSurfs;
	surf = rsh.worldBrushModel->surfaces + firstSurf;

	for( i = firstSurf; i < end; i++ ) {
		if( rf.worldSurfVis[i] ) {
			// the surface is at partly visible in at least one leaf, frustum cull it
			if( R_CullSurface( rsc.worldent, surf, clipFlags ) ) {
				rf.worldSurfVis[i] = 0;
			}
			rf.worldSurfFullVis[i] = 0;
		}
		else {
			if( rf.worldSurfFullVis[i] ) {
				// a fully visible surface, mark as visible
				rf.worldSurfVis[i] = 1;
			}
		}

		if( rf.worldSurfVis[i] ) {
			if( !surf->drawSurf )
				rf.worldSurfVis[i] = 0;
			else
				rf.worldDrawSurfVis[surf->drawSurf - 1] = 1;
		}

		surf++;
	}
}

/*
* R_AddVisSurfaces
*/
static void R_AddVisSurfaces( unsigned dlightBits, unsigned shadowBits ) {

	unsigned i;

	for( i = 0; i < rsh.worldBrushModel->numModelDrawSurfaces; i++ ) {
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + i;

		if( !rf.worldDrawSurfVis[i] ) {
			continue;
		}

		R_AddSurfaceToDrawList( rsc.worldent, drawSurf );
	}
}

/*
* R_AddWorldDrawSurfaces
*/
static void R_AddWorldDrawSurfaces( unsigned firstDrawSurf, unsigned numDrawSurfs ) {
	unsigned i;

	for( i = 0; i < numDrawSurfs; i++ ) {
		unsigned s = firstDrawSurf + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + s;

		if( !rf.worldDrawSurfVis[s] ) {
			continue;
		}

		R_UpdateSurfaceInDrawList( drawSurf, rn.dlightBits, NULL );
	}
}

/*
* R_DrawWorld
*/
void R_DrawWorld( void ) {
	unsigned int i;
	int clipFlags;
	int64_t msec = 0, msec2 = 0;
	unsigned int dlightBits;
	unsigned int shadowBits;
	bool worldOutlines;
	bool speeds = r_speeds->integer != 0;

	assert( rf.numWorldSurfVis >= rsh.worldBrushModel->numsurfaces );
	assert( rf.numWorldLeafVis >= rsh.worldBrushModel->numvisleafs );

	if( !r_drawworld->integer ) {
		return;
	}
	if( !rsh.worldModel ) {
		return;
	}
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return;
	}

	VectorCopy( rn.refdef.vieworg, modelOrg );

	worldOutlines = mapConfig.forceWorldOutlines || ( rn.refdef.rdflags & RDF_WORLDOUTLINES );

	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		rsc.worldent->outlineHeight = std::max( 0.0f, r_outlines_world->value );
	} else {
		rsc.worldent->outlineHeight = 0;
	}
	Vector4Copy( mapConfig.outlineColor, rsc.worldent->outlineColor );

	clipFlags = rn.clipFlags;
	dlightBits = 0;
	shadowBits = 0;

	if( r_nocull->integer ) {
		clipFlags = 0;
	}

	// cull dynamic lights
	rn.dlightBits = Scene::Instance()->CullLights( clipFlags );

	// BEGIN t_world_node
	if( speeds ) {
		msec = Sys_Milliseconds();
	}

	if( rsh.worldBrushModel->numvisleafs > rsh.worldBrushModel->numsurfaces ) {
		memset( (void *)rf.worldSurfVis, 1, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldSurfFullVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldLeafVis, 1, rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
		memset( (void *)rf.worldDrawSurfVis, 0, rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );
	} else {
		memset( (void *)rf.worldSurfVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldSurfFullVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldLeafVis, 0, rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
		memset( (void *)rf.worldDrawSurfVis, 0, rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );

		if( r_speeds->integer ) {
			msec2 = Sys_Milliseconds();
		}

		//
		// cull leafs
		//
		R_CullVisLeaves( 0, rsh.worldBrushModel->numvisleafs, clipFlags );

		if( speeds ) {
			rf.stats.t_cull_world_nodes += Sys_Milliseconds() - msec2;
		}
	}

	//
	// cull surfaces and do some background work on computed vis leafs
	// 
	if( speeds ) {
		msec2 = Sys_Milliseconds();
	}

	R_CullVisSurfaces( 0, rsh.worldBrushModel->numModelSurfaces, clipFlags );

	R_PostCullVisLeaves();

	if( speeds ) {
		rf.stats.t_cull_world_surfs += Sys_Milliseconds() - msec2;
	}

	//
	// add visible surfaces to draw list
	//
	if( speeds ) {
		msec2 = Sys_Milliseconds();
	}
	R_AddVisSurfaces( dlightBits, shadowBits );

	R_AddWorldDrawSurfaces( 0, rsh.worldBrushModel->numModelDrawSurfaces );

	if( speeds ) {
		for( i = 0; i < rsh.worldBrushModel->numsurfaces; i++ ) {
			if( rf.worldSurfVis[i] ) {
				rf.stats.c_brush_polys++;
			}
		}
	}

	// END t_world_node
	if( speeds ) {
		rf.stats.t_world_node += Sys_Milliseconds() - msec;
	}
}
