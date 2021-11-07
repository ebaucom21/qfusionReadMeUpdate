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
#include "frontend.h"
#include "program.h"
#include "materiallocal.h"
#include "../qcommon/singletonholder.h"

#define REFINST_STACK_SIZE  64
static refinst_t riStack[REFINST_STACK_SIZE];
static unsigned riStackSize;

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

static void R_AddSurfaceVBOSlice( drawList_t *list, drawSurfaceBSP_t *drawSurf, const msurface_t *surf, int offset ) {
	R_AddDrawListVBOSlice( list, offset + drawSurf - rsh.worldBrushModel->drawSurfaces,
						   surf->mesh.numVerts, surf->mesh.numElems,
						   surf->firstDrawSurfVert, surf->firstDrawSurfElem );
}

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

static bool R_ClipSpecialWorldSurf( drawSurfaceBSP_t *drawSurf, const msurface_t *surf, const vec3_t origin, float *pdist ) {
	const shader_t *shader = drawSurf->shader;
	if( shader->flags & SHADER_PORTAL ) {
		portalSurface_t *portalSurface = R_GetDrawListSurfPortal( drawSurf->listSurf );
		if( portalSurface != NULL ) {
			vec3_t center;
			if( origin ) {
				VectorCopy( origin, center );
			} else {
				VectorAdd( surf->mins, surf->maxs, center );
				VectorScale( center, 0.5, center );
			}
			float dist = Distance( rn.refdef.vieworg, center );
			// draw portals in front-to-back order
			dist = 1024 - dist / 100.0f;
			if( dist < 1 ) {
				dist = 1;
			}
			R_UpdatePortalSurface( portalSurface, &surf->mesh, surf->mins, surf->maxs, shader, drawSurf );
			*pdist = dist;
		}
	}
	return true;
}

bool R_SurfPotentiallyLit( const msurface_t *surf ) {
	if( surf->flags & ( SURF_SKY | SURF_NODLIGHT | SURF_NODRAW ) ) {
		return false;
	}
	const shader_t *shader = surf->shader;
	if( !shader->numpasses ) {
		return false;
	}
	return ( surf->mesh.numVerts != 0 /* && (surf->facetype != FACETYPE_TRISURF)*/ );
}

static unsigned R_SurfaceDlightBits( const msurface_t *surf, unsigned checkDlightBits ) {
	if( !R_SurfPotentiallyLit( surf ) ) {
		return 0;
	}

	// TODO: This is totally wrong! Lights should mark affected surfaces in their radius on their own!

	unsigned surfDlightBits = 0;

	const auto *scene = wsw::ref::Scene::Instance();
	const wsw::ref::Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	// Caution: bits correspond to indices in range now!
	uint32_t mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		// TODO: This condition seems to be pointless
		if( checkDlightBits & mask ) {
			const auto *__restrict lt = scene->ProgramLightForNum( *iter );
			// TODO: Avoid doing this and keep separate lists of planar and other surfaces
			switch( surf->facetype ) {
				case FACETYPE_PLANAR: {
					const float dist = DotProduct( lt->center, surf->plane ) - surf->plane[3];
					if( dist > -lt->radius && dist < lt->radius ) {
						surfDlightBits |= mask;
					}
					break;
				}
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
* R_UpdateSurfaceInDrawList
*
* Walk the list of visible world surfaces and prepare the final VBO slice and draw order bits.
* For sky surfaces, skybox clipping is also performed.
*/
static void R_UpdateSurfaceInDrawList( drawSurfaceBSP_t *drawSurf, unsigned dlightBits, const vec3_t origin ) {
	if( !drawSurf->listSurf ) {
		return;
	}

	unsigned dlightFrame = 0;
	unsigned curDlightBits = 0;

	msurface_t *firstVisSurf = nullptr;
	msurface_t *lastVisSurf  = nullptr;

	float dist = 0;

	msurface_t *surf = rsh.worldBrushModel->surfaces + drawSurf->firstWorldSurface;
	const unsigned end = drawSurf->firstWorldSurface + drawSurf->numWorldSurfaces;
	const bool special = ( drawSurf->shader->flags & (SHADER_PORTAL) ) != 0;
	for( unsigned i = drawSurf->firstWorldSurface; i < end; i++ ) {
		float sdist = 0;
		if( special && !R_ClipSpecialWorldSurf( drawSurf, surf, origin, &sdist ) ) {
			// clipped away
			continue;
		}

		if( sdist > sdist )
			dist = sdist;

		unsigned checkDlightBits = dlightBits & ~curDlightBits;
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
		if( firstVisSurf == NULL ) {
			firstVisSurf = surf;
		}
		lastVisSurf = surf;
		surf++;
	}

	if( dlightFrame == rsc.frameCount ) {
		drawSurf->dlightBits = curDlightBits;
		drawSurf->dlightFrame = dlightFrame;
	}

	// prepare the slice
	if( firstVisSurf ) {
		const bool dlight = dlightFrame == rsc.frameCount;

		R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, firstVisSurf, 0 );

		if( lastVisSurf != firstVisSurf )
			R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, lastVisSurf, 0 );

		// update the distance sorting key if it's a portal surface or a normal dlit surface
		if( dist != 0 || dlight ) {
			int drawOrder = R_PackOpaqueOrder( drawSurf->fog, drawSurf->shader, drawSurf->numLightmaps, dlight );
			if( dist == 0 ) {
				dist = WORLDSURF_DIST;
			}
			R_UpdateDrawSurfDistKey( drawSurf->listSurf, 0, drawSurf->shader, dist, drawOrder );
		}
	}
}


static const drawSurf_cb r_drawSurfCb[ST_MAX_TYPES] =
	{
	/* ST_NONE */
	NULL,
	/* ST_BSP */
	( drawSurf_cb ) & R_DrawBSPSurf,
	/* ST_ALIAS */
	( drawSurf_cb ) & R_DrawAliasSurf,
	/* ST_SKELETAL */
	( drawSurf_cb ) & R_DrawSkeletalSurf,
	/* ST_SPRITE */
	NULL,
	/* ST_POLY */
	NULL,
	/* ST_CORONA */
	NULL,
	/* ST_NULLMODEL */
	( drawSurf_cb ) & R_DrawNullSurf,
	};

static const batchDrawSurf_cb r_batchDrawSurfCb[ST_MAX_TYPES] =
	{
	/* ST_NONE */
	NULL,
	/* ST_BSP */
	NULL,
	/* ST_ALIAS */
	NULL,
	/* ST_SKELETAL */
	NULL,
	/* ST_SPRITE */
	( batchDrawSurf_cb ) & R_BatchSpriteSurf,
	/* ST_POLY */
	( batchDrawSurf_cb ) & R_BatchPolySurf,
	/* ST_CORONA */
	( batchDrawSurf_cb ) & R_BatchCoronaSurf,
	/* ST_NULLMODEL */
	NULL,
	};

static drawSurfaceType_t spriteDrawSurf = ST_SPRITE;

static bool R_AddSpriteToDrawList( const entity_t *e ) {
	if( e->flags & RF_NOSHADOW ) {
		if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
			return false;
		}
	}

	if( e->radius <= 0 || e->customShader == NULL || e->scale <= 0 ) {
		return false;
	}

	const float dist =
		( e->origin[0] - rn.refdef.vieworg[0] ) * rn.viewAxis[AXIS_FORWARD + 0] +
		( e->origin[1] - rn.refdef.vieworg[1] ) * rn.viewAxis[AXIS_FORWARD + 1] +
		( e->origin[2] - rn.refdef.vieworg[2] ) * rn.viewAxis[AXIS_FORWARD + 2];
	if( dist <= 0 ) {
		return false; // cull it because we don't want to sort unneeded things

	}
	if( !R_AddSurfToDrawList( rn.meshlist, e, R_FogForSphere( e->origin, e->radius ),
							  e->customShader, dist, 0, NULL, &spriteDrawSurf ) ) {
		return false;
	}

	return true;
}

static bool R_AddSurfaceToDrawList( const entity_t *e, drawSurfaceBSP_t *drawSurf ) {
	if( drawSurf->visFrame == rf.frameCount ) {
		return true;
	}

	portalSurface_t *portalSurface = nullptr;
	const shader_t *shader = drawSurf->shader;
	if( shader->flags & SHADER_PORTAL ) {
		portalSurface = R_AddPortalSurface( e, shader, drawSurf );
	}

	const mfog_t *fog = drawSurf->fog;
	const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, drawSurf->numLightmaps, false );

	drawSurf->dlightBits = 0;
	drawSurf->visFrame = rf.frameCount;
	drawSurf->listSurf = R_AddSurfToDrawList( rn.meshlist, e, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface, drawSurf );
	if( !drawSurf->listSurf ) {
		return false;
	}

	if( portalSurface && !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) ) {
		R_AddSurfToDrawList( rn.portalmasklist, e, NULL, NULL, 0, 0, NULL, drawSurf );
	}

	const unsigned sliceIndex = drawSurf - rsh.worldBrushModel->drawSurfaces;
	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex, 0, 0, 0, 0 );
	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex + rsh.worldBrushModel->numDrawSurfaces, 0, 0, 0, 0 );

	return true;
}

static void R_ReserveDrawSurfaces( drawList_t *list, int minMeshes ) {
	int oldSize, newSize;
	sortedDrawSurf_t *newDs;
	sortedDrawSurf_t *ds = list->drawSurfs;
	int maxMeshes = list->maxDrawSurfs;

	oldSize = maxMeshes;
	newSize = std::max( minMeshes, oldSize * 2 );

	newDs = (sortedDrawSurf_t *)Q_malloc( newSize * sizeof( sortedDrawSurf_t ) );
	if( ds ) {
		memcpy( newDs, ds, oldSize * sizeof( sortedDrawSurf_t ) );
		Q_free( ds );
	}

	list->drawSurfs = newDs;
	list->maxDrawSurfs = newSize;
}

static int R_PackDistKey( int renderFx, const shader_t *shader, float dist, unsigned order ) {
	int shaderSort = shader->sort;

	if( renderFx & RF_WEAPONMODEL ) {
		bool depthWrite = ( shader->flags & SHADER_DEPTHWRITE ) ? true : false;

		if( renderFx & RF_NOCOLORWRITE ) {
			// depth-pass for alpha-blended weapon:
			// write to depth but do not write to color
			if( !depthWrite ) {
				return 0;
			}
			// reorder the mesh to be drawn after everything else
			// but before the blend-pass for the weapon
			shaderSort = SHADER_SORT_WEAPON;
		} else if( renderFx & RF_ALPHAHACK ) {
			// blend-pass for the weapon:
			// meshes that do not write to depth, are rendered as additives,
			// meshes that were previously added as SHADER_SORT_WEAPON (see above)
			// are now added to the very end of the list
			shaderSort = depthWrite ? SHADER_SORT_WEAPON2 : SHADER_SORT_ADDITIVE;
		}
	} else if( renderFx & RF_ALPHAHACK ) {
		// force shader sort to additive
		shaderSort = SHADER_SORT_ADDITIVE;
	}

	return ( shaderSort << 26 ) | ( std::max( 0x400 - (int)dist, 0 ) << 15 ) | ( order & 0x7FFF );
}

void R_UpdateDrawSurfDistKey( void *psds, int renderFx, const shader_t *shader, float dist, unsigned order ) {
	sortedDrawSurf_t *const sds = (sortedDrawSurf_t *)psds;
	sds->distKey = R_PackDistKey( renderFx, shader, dist, order );
}

static unsigned R_PackSortKey( unsigned shaderNum, int fogNum,
							   int portalNum, unsigned entNum ) {
	return ( shaderNum & 0x7FF ) << 21 | ( entNum & 0x7FF ) << 10 |
		( ( ( portalNum + 1 ) & 0x1F ) << 5 ) | ( (unsigned)( fogNum + 1 ) & 0x1F );
}

static void R_UnpackSortKey( unsigned sortKey, unsigned *shaderNum, int *fogNum,
							 int *portalNum, unsigned *entNum ) {
	*shaderNum = ( sortKey >> 21 ) & 0x7FF;
	*entNum = ( sortKey >> 10 ) & 0x7FF;
	*portalNum = (signed int)( ( sortKey >> 5 ) & 0x1F ) - 1;
	*fogNum = (signed int)( sortKey & 0x1F ) - 1;
}

void *R_AddSurfToDrawList( drawList_t *list, const entity_t *e, const mfog_t *fog, const shader_t *shader,
						   float dist, unsigned order, const portalSurface_t *portalSurf, void *drawSurf ) {

	if( !list || !shader ) {
		return NULL;
	}
	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) && Shader_ReadDepth( shader ) ) {
		return NULL;
	}
	if( !rsh.worldBrushModel ) {
		fog = NULL;
	}

	int distKey = R_PackDistKey( e->renderfx, shader, dist, order );
	if( !distKey ) {
		return NULL;
	}

	// reallocate if numDrawSurfs
	if( list->numDrawSurfs >= list->maxDrawSurfs ) {
		int minMeshes = MIN_RENDER_MESHES;
		if( rsh.worldBrushModel ) {
			minMeshes += rsh.worldBrushModel->numDrawSurfaces;
		}
		R_ReserveDrawSurfaces( list, minMeshes );
	}

	sortedDrawSurf_t *const sds = &list->drawSurfs[list->numDrawSurfs++];
	sds->drawSurf = ( drawSurfaceType_t * )drawSurf;
	sds->sortKey = R_PackSortKey( shader->id, fog ? fog - rsh.worldBrushModel->fogs : -1,
								  portalSurf ? portalSurf - rn.portalSurfaces : -1, R_ENT2NUM( e ) );
	sds->distKey = distKey;

	return sds;
}

static void R_AddVisSurfaces( unsigned dlightBits, unsigned shadowBits ) {
	for( unsigned i = 0; i < rsh.worldBrushModel->numModelDrawSurfaces; i++ ) {
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + i;
		R_AddSurfaceToDrawList( rsc.worldent, drawSurf );
	}
}

static void R_AddWorldDrawSurfaces( unsigned firstDrawSurf, unsigned numDrawSurfs ) {
	for( unsigned i = 0; i < numDrawSurfs; i++ ) {
		unsigned s = firstDrawSurf + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + s;
		R_UpdateSurfaceInDrawList( drawSurf, rn.dlightBits, NULL );
	}
}


static void R_DrawEntities( void ) {
	if( rn.renderFlags & RF_ENVVIEW ) {
		for( unsigned i = 0; i < rsc.numBmodelEntities; i++ ) {
			entity_t *e = rsc.bmodelEntities[i];
			if( !r_lerpmodels->integer ) {
				e->backlerp = 0;
			}
			e->outlineHeight = rsc.worldent->outlineHeight;
			Vector4Copy( rsc.worldent->outlineRGBA, e->outlineColor );
			R_AddBrushModelToDrawList( e );
		}
		return;
	}

	for( unsigned i = rsc.numLocalEntities; i < rsc.numEntities; i++ ) {
		entity_t *e = R_NUM2ENT( i );

		if( !r_lerpmodels->integer ) {
			e->backlerp = 0;
		}

		if( e->flags & RF_VIEWERMODEL ) {
			//if( !(rn.renderFlags & RF_NONVIEWERREF) )
			if( !( rn.renderFlags & ( RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
				continue;
			}
		}

		switch( e->rtype ) {
			case RT_MODEL:
				if( !e->model ) {
					R_AddNullSurfToDrawList( e );
					continue;
				}

				switch( e->model->type ) {
					case mod_alias:
						R_AddAliasModelToDrawList( e );
						break;
						case mod_skeletal:
							R_AddSkeletalModelToDrawList( e );
							break;
							case mod_brush:
								e->outlineHeight = rsc.worldent->outlineHeight;
								Vector4Copy( rsc.worldent->outlineRGBA, e->outlineColor );
								R_AddBrushModelToDrawList( e );
								default:
									break;
				}
				break;
				case RT_SPRITE:
					R_AddSpriteToDrawList( e );
					break;
				default:
					break;
		}
	}
}

void R_BatchSpriteSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned shadowBits, drawSurfaceType_t *drawSurf ) {
	vec3_t v_left, v_up;
	if( const float rotation = e->rotation ) {
		RotatePointAroundVector( v_left, &rn.viewAxis[AXIS_FORWARD], &rn.viewAxis[AXIS_RIGHT], rotation );
		CrossProduct( &rn.viewAxis[AXIS_FORWARD], v_left, v_up );
	} else {
		VectorCopy( &rn.viewAxis[AXIS_RIGHT], v_left );
		VectorCopy( &rn.viewAxis[AXIS_UP], v_up );
	}

	if( rn.renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
		VectorInverse( v_left );
	}

	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };

	vec3_t point;
	const float radius = e->radius * e->scale;
	VectorMA( e->origin, -radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[0] );
	VectorMA( point, -radius, v_left, xyz[3] );

	VectorMA( e->origin, radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[1] );
	VectorMA( point, -radius, v_left, xyz[2] );

	byte_vec4_t colors[4];
	for( unsigned i = 0; i < 4; i++ ) {
		VectorNegate( &rn.viewAxis[AXIS_FORWARD], normals[i] );
		Vector4Copy( e->color, colors[i] );
	}

	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };

	mesh_t mesh;
	mesh.elems = elems;
	mesh.numElems = 6;
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.lmstArray[0] = NULL;
	mesh.lmlayersArray[0] = NULL;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;
	mesh.colorsArray[1] = NULL;
	mesh.sVectorsArray = NULL;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

void R_BatchPolySurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned shadowBits, drawSurfacePoly_t *poly ) {
	mesh_t mesh;

	mesh.elems = poly->elems;
	mesh.numElems = poly->numElems;
	mesh.numVerts = poly->numVerts;
	mesh.xyzArray = poly->xyzArray;
	mesh.normalsArray = poly->normalsArray;
	mesh.lmstArray[0] = NULL;
	mesh.lmlayersArray[0] = NULL;
	mesh.stArray = poly->stArray;
	mesh.colorsArray[0] = poly->colorsArray;
	mesh.colorsArray[1] = NULL;
	mesh.sVectorsArray = NULL;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, shadowBits, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

void R_BatchCoronaSurf( const entity_t *e, const shader_t *shader,
						const mfog_t *fog, const portalSurface_t *portalSurface, unsigned shadowBits, drawSurfaceType_t *drawSurf ) {
	auto *const light = wsw::ref::Scene::Instance()->LightForCoronaSurf( drawSurf );

	float radius = light->radius;
	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };
	mesh_t mesh;

	vec3_t origin;
	VectorCopy( light->center, origin );

	vec3_t v_left, v_up;
	VectorCopy( &rn.viewAxis[AXIS_RIGHT], v_left );
	VectorCopy( &rn.viewAxis[AXIS_UP], v_up );

	if( rn.renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
		VectorInverse( v_left );
	}

	vec3_t point;
	VectorMA( origin, -radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[0] );
	VectorMA( point, -radius, v_left, xyz[3] );

	VectorMA( origin, radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[1] );
	VectorMA( point, -radius, v_left, xyz[2] );

	Vector4Set( colors[0],
				bound( 0, light->color[0] * 96, 255 ),
				bound( 0, light->color[1] * 96, 255 ),
				bound( 0, light->color[2] * 96, 255 ),
				255 );

	for( unsigned i = 1; i < 4; i++ ) {
		Vector4Copy( colors[0], colors[i] );
	}

	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numElems = 6;
	mesh.elems = elems;
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

static void R_SetupGL() {
	RB_Scissor( rn.scissor[0], rn.scissor[1], rn.scissor[2], rn.scissor[3] );
	RB_Viewport( rn.viewport[0], rn.viewport[1], rn.viewport[2], rn.viewport[3] );

	if( rn.renderFlags & RF_CLIPPLANE ) {
		cplane_t *p = &rn.clipPlane;
		Matrix4_ObliqueNearClipping( p->normal, -p->dist, rn.cameraMatrix, rn.projectionMatrix );
	}

	RB_SetZClip( Z_NEAR, rn.farClip );

	RB_SetCamera( rn.viewOrigin, rn.viewAxis );

	RB_SetLightParams( rn.refdef.minLight, ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) != 0, rn.hdrExposure );

	RB_SetRenderFlags( rn.renderFlags );

	RB_LoadProjectionMatrix( rn.projectionMatrix );

	RB_LoadCameraMatrix( rn.cameraMatrix );

	RB_LoadObjectMatrix( mat4x4_identity );

	if( rn.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}

	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, GLSTATE_NO_COLORWRITE );
	}
}

static void R_EndGL( void ) {
	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, 0 );
	}

	if( rn.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}
}

static void R_BindRefInstFBO( void ) {
	const int fbo = rn.renderTarget;
	R_BindFrameBufferObject( fbo );
}

void R_ClearRefInstStack( void ) {
	riStackSize = 0;
}

bool R_PushRefInst( void ) {
	if( riStackSize == REFINST_STACK_SIZE ) {
		riStack[riStackSize++] = rn;
		R_EndGL();
		return true;
	}
	return false;
}

void R_PopRefInst( void ) {
	if( riStackSize ) {
		rn = riStack[--riStackSize];
		R_BindRefInstFBO();
		R_SetupGL();
	}
}

static vec3_t modelOrg;                         // relative to view point

void R_DrawWorld() {
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

	const bool worldOutlines = mapConfig.forceWorldOutlines || ( rn.refdef.rdflags & RDF_WORLDOUTLINES );
	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		rsc.worldent->outlineHeight = std::max( 0.0f, r_outlines_world->value );
	} else {
		rsc.worldent->outlineHeight = 0;
	}
	Vector4Copy( mapConfig.outlineColor, rsc.worldent->outlineColor );

	int clipFlags = rn.clipFlags;
	const unsigned dlightBits = 0;
	const unsigned shadowBits = 0;

	if( r_nocull->integer ) {
		clipFlags = 0;
	}

	// cull dynamic lights
	rn.dlightBits = wsw::ref::Scene::Instance()->CullLights( clipFlags );

	R_AddVisSurfaces( dlightBits, shadowBits );

	R_AddWorldDrawSurfaces( 0, rsh.worldBrushModel->numModelDrawSurfaces );
}

portalSurface_t *R_GetDrawListSurfPortal( void *psds ) {
	const sortedDrawSurf_t *const sds = (sortedDrawSurf_t *)psds;

	unsigned shaderNum, entNum;
	int portalNum, fogNum;
	R_UnpackSortKey( sds->sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

	return portalNum >= 0 ? rn.portalSurfaces + portalNum : NULL;
}

static void _R_DrawSurfaces( drawList_t *list ) {
	if( !list->numDrawSurfs ) {
		return;
	}

	unsigned prevShaderNum = std::numeric_limits<unsigned>::max();
	unsigned prevEntNum = std::numeric_limits<unsigned>::max();
	int prevPortalNum = std::numeric_limits<int>::min();
	int prevFogNum = std::numeric_limits<int>::min();

	bool batchDrawSurf = false;
	bool prevBatchDrawSurf = false;
	float depthmin = 0.0f, depthmax = 0.0f;
	bool depthHack = false, cullHack = false;
	bool prevInfiniteProj = false;
	int prevEntityFX = -1;

	for( unsigned i = 0; i < list->numDrawSurfs; i++ ) {
		const sortedDrawSurf_t *sds = list->drawSurfs + i;
		const unsigned sortKey = sds->sortKey;
		const int drawSurfType = *(int *)sds->drawSurf;

		assert( drawSurfType > ST_NONE && drawSurfType < ST_MAX_TYPES );

		batchDrawSurf = ( r_batchDrawSurfCb[drawSurfType] ? true : false );

		unsigned shaderNum, entNum;
		int fogNum, portalNum;
		// decode draw surface properties
		R_UnpackSortKey( sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

		const shader_t *shader = MaterialCache::instance()->getMaterialById( shaderNum );
		const entity_t *entity = R_NUM2ENT( entNum );
		const mfog_t *fog = fogNum >= 0 ? rsh.worldBrushModel->fogs + fogNum : NULL;
		const portalSurface_t *portalSurface = portalNum >= 0 ? rn.portalSurfaces + portalNum : NULL;
		const int entityFX = entity->renderfx;
		const bool depthWrite = shader->flags & SHADER_DEPTHWRITE ? true : false;

		// see if we need to reset mesh properties in the backend

		bool reset = false;
		if( !prevBatchDrawSurf ) {
			reset = true;
		} else if( shaderNum != prevShaderNum ) {
			reset = true;
		} else if( fogNum != prevFogNum ) {
			reset = true;
		} else if( portalNum != prevPortalNum ) {
			reset = true;
		} else if( ( entNum != prevEntNum && !( shader->flags & SHADER_ENTITY_MERGABLE ) ) ) {
			reset = true;
		} else if( entityFX != prevEntityFX ) {
			reset = true;
		}

		if( reset ) {
			if( prevBatchDrawSurf && !batchDrawSurf ) {
				RB_FlushDynamicMeshes();
			}

			// hack the depth range to prevent view model from poking into walls
			if( entity->flags & RF_WEAPONMODEL ) {
				if( !depthHack ) {
					RB_FlushDynamicMeshes();
					depthHack = true;
					RB_GetDepthRange( &depthmin, &depthmax );
					RB_DepthRange( depthmin, depthmin + 0.3f * ( depthmax - depthmin ) );
				}
			} else {
				if( depthHack ) {
					RB_FlushDynamicMeshes();
					depthHack = false;
					RB_DepthRange( depthmin, depthmax );
				}
			}

			if( entNum != prevEntNum ) {
				// backface culling for left-handed weapons
				bool oldCullHack = cullHack;
				cullHack = ( ( entity->flags & RF_CULLHACK ) ? true : false );
				if( cullHack != oldCullHack ) {
					RB_FlushDynamicMeshes();
					RB_FlipFrontFace();
				}
			}

			// sky and things that don't use depth test use infinite projection matrix
			// to not pollute the farclip
			const bool infiniteProj = entity->renderfx & RF_NODEPTHTEST ? true : false;
			if( infiniteProj != prevInfiniteProj ) {
				RB_FlushDynamicMeshes();
				if( infiniteProj ) {
					mat4_t projectionMatrix;
					Matrix4_Copy( rn.projectionMatrix, projectionMatrix );
					Matrix4_PerspectiveProjectionToInfinity( Z_NEAR, projectionMatrix, glConfig.depthEpsilon );
					RB_LoadProjectionMatrix( projectionMatrix );
				} else {
					RB_LoadProjectionMatrix( rn.projectionMatrix );
				}
			}

			if( batchDrawSurf ) {
				// don't transform batched surfaces
				if( !prevBatchDrawSurf ) {
					RB_LoadObjectMatrix( mat4x4_identity );
				}
			} else {
				if( ( entNum != prevEntNum ) || prevBatchDrawSurf ) {
					if( shader->flags & SHADER_AUTOSPRITE ) {
						R_TranslateForEntity( entity );
					} else {
						R_TransformForEntity( entity );
					}
				}
			}

			if( !batchDrawSurf ) {
				assert( r_drawSurfCb[drawSurfType] );

				RB_BindShader( entity, shader, fog );
				RB_SetPortalSurface( portalSurface );

				r_drawSurfCb[drawSurfType]( entity, shader, fog, portalSurface, 0, sds->drawSurf );
			}

			prevShaderNum = shaderNum;
			prevEntNum = entNum;
			prevFogNum = fogNum;
			prevBatchDrawSurf = batchDrawSurf;
			prevPortalNum = portalNum;
			prevInfiniteProj = infiniteProj;
			prevEntityFX = entityFX;
		}

		if( batchDrawSurf ) {
			r_batchDrawSurfCb[drawSurfType]( entity, shader, fog, portalSurface, 0, sds->drawSurf );
		}
	}

	if( batchDrawSurf ) {
		RB_FlushDynamicMeshes();
	}
	if( depthHack ) {
		RB_DepthRange( depthmin, depthmax );
	}
	if( cullHack ) {
		RB_FlipFrontFace();
	}

	RB_BindFrameBufferObject();
}

void R_DrawSurfaces( drawList_t *list ) {
	const bool triOutlines = RB_EnableTriangleOutlines( false );
	if( !triOutlines ) {
		// do not recurse into normal mode when rendering triangle outlines
		_R_DrawSurfaces( list );
	}
	RB_EnableTriangleOutlines( triOutlines );
}

void R_DrawOutlinedSurfaces( drawList_t *list ) {
	if( !( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		// properly store and restore the state, as the
		// R_DrawOutlinedSurfaces calls can be nested
		const bool triOutlines = RB_EnableTriangleOutlines( true );
		_R_DrawSurfaces( list );
		RB_EnableTriangleOutlines( triOutlines );
	}
}

#define R_TransformPointToModelSpace( e,rotate,in,out ) \
VectorSubtract( in, ( e )->origin, out ); \
if( rotated ) { \
vec3_t temp; \
VectorCopy( out, temp ); \
Matrix3_TransformVector( ( e )->axis, temp, out ); \
}

bool R_AddBrushModelToDrawList( const entity_t *e ) {
	const model_t *const model = e->model;
	const mbrushmodel_t *const bmodel = ( mbrushmodel_t * )model->extradata;

	if( bmodel->numModelDrawSurfaces == 0 ) {
		return false;
	}

	bool rotated;
	vec3_t bmins, bmaxs;
	const float radius = R_BrushModelBBox( e, bmins, bmaxs, &rotated );

	vec3_t origin;
	VectorAdd( e->model->mins, e->model->maxs, origin );
	VectorMA( e->origin, 0.5, origin, origin );

	R_TransformPointToModelSpace( e, rotated, rn.refdef.vieworg, modelOrg );

	const auto *scene = wsw::ref::Scene::Instance();
	// TODO: Lights should mark models they affect on their own!

	// check dynamic lights that matter in the instance against the model
	unsigned dlightBits = 0;

	const wsw::ref::Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	unsigned mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		const auto *light = scene->ProgramLightForNum( *iter );
		if( BoundsAndSphereIntersect( bmins, bmaxs, light->center, light->radius ) ) {
			dlightBits |= mask;
		}
	}

	dlightBits &= rn.dlightBits;

	for( unsigned i = 0; i < bmodel->numModelDrawSurfaces; i++ ) {
		const unsigned surfNum = bmodel->firstModelDrawSurface + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + surfNum;
		R_AddSurfaceToDrawList( e, drawSurf );
		R_UpdateSurfaceInDrawList( drawSurf, dlightBits, origin );
	}

	return true;
}

static void R_SetupViewMatrices( void ) {
	refdef_t *rd = &rn.refdef;

	Matrix4_Modelview( rd->vieworg, rd->viewaxis, rn.cameraMatrix );

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthogonalProjection( -rd->ortho_x, rd->ortho_x, -rd->ortho_y, rd->ortho_y,
									  -rn.farClip, rn.farClip, rn.projectionMatrix );
	} else {
		Matrix4_PerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, rn.farClip, rn.projectionMatrix );
	}

	if( rd->rdflags & RDF_FLIPPED ) {
		rn.projectionMatrix[0] = -rn.projectionMatrix[0];
		rn.renderFlags |= RF_FLIPFRONTFACE;
	}

	Matrix4_Multiply( rn.projectionMatrix, rn.cameraMatrix, rn.cameraProjectionMatrix );
}

static void R_SetupFrame() {
	// build the transformation matrix for the given view angles
	VectorCopy( rn.refdef.vieworg, rn.viewOrigin );
	Matrix3_Copy( rn.refdef.viewaxis, rn.viewAxis );

	rn.lod_dist_scale_for_fov = std::tan( rn.refdef.fov_x * ( M_PI / 180 ) * 0.5f );

	// current viewcluster
	if( !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		mleaf_t *leaf = Mod_PointInLeaf( rn.pvsOrigin, rsh.worldModel );
		rf.viewcluster = leaf->cluster;
		rf.viewarea = leaf->area;

		if( rf.worldModelSequence != rsh.worldModelSequence ) {
			rf.frameCount = 0;
			rf.worldModelSequence = rsh.worldModelSequence;
		}
	} else {
		rf.viewcluster = -1;
		rf.viewarea = -1;
	}

	rf.frameCount++;
}

static void R_SetFarClip( void ) {
	rn.farClip = R_DefaultFarClip();
}

static void R_Clear( int bitMask ) {
	const bool rgbShadow = ( rn.renderFlags & ( RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB ) ) == ( RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB );
	const bool depthPortal = ( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW ) ) != 0 && ( rn.renderFlags & RF_PORTAL_CAPTURE ) == 0;

	bool clearColor = false;
	vec4_t envColor;
	if( rgbShadow ) {
		clearColor = true;
		Vector4Set( envColor, 1, 1, 1, 1 );
	} else if( rn.refdef.rdflags & RDF_NOWORLDMODEL ) {
		clearColor = rn.renderTarget != 0;
		Vector4Set( envColor, 1, 1, 1, 0 );
	} else {
		clearColor = !rn.numDepthPortalSurfaces || R_FASTSKY();
		if( rsh.worldBrushModel && rsh.worldBrushModel->globalfog && rsh.worldBrushModel->globalfog->shader ) {
			Vector4Scale( rsh.worldBrushModel->globalfog->shader->fog_color, 1.0 / 255.0, envColor );
		} else {
			Vector4Scale( mapConfig.environmentColor, 1.0 / 255.0, envColor );
		}
	}

	int bits = 0;
	if( !depthPortal ) {
		bits |= GL_DEPTH_BUFFER_BIT;
	}
	if( clearColor ) {
		bits |= GL_COLOR_BUFFER_BIT;
	}

	bits &= bitMask;

	RB_Clear( bits, envColor[0], envColor[1], envColor[2], envColor[3] );
}

void R_RenderView( const refdef_t *fd ) {
	const bool shadowMap = rn.renderFlags & RF_SHADOWMAPVIEW ? true : false;

	rn.refdef = *fd;

	// load view matrices with default far clip value
	R_SetupViewMatrices();

	rn.fog_eye = nullptr;
	rn.hdrExposure = 1;

	rn.dlightBits = 0;

	rn.numPortalSurfaces = 0;
	rn.numDepthPortalSurfaces = 0;
	rn.skyportalSurface = nullptr;

	if( r_novis->integer ) {
		rn.renderFlags |= RF_NOVIS;
	}

	if( r_lightmap->integer ) {
		rn.renderFlags |= RF_LIGHTMAP;
	}

	if( r_drawflat->integer ) {
		rn.renderFlags |= RF_DRAWFLAT;
	}

	R_ClearDrawList( rn.meshlist );

	R_ClearDrawList( rn.portalmasklist );

	if( !rsh.worldModel && !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}

	R_SetupFrame();

	R_SetupFrustum( &rn.refdef, rn.farClip, rn.frustum );

	// we know the initial farclip at this point after determining visible world leafs
	// R_DrawEntities can make adjustments as well

	if( !shadowMap ) {
		if( !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
			R_DrawWorld();

			rn.fog_eye = R_FogForSphere( rn.viewOrigin, 0.5 );
			rn.hdrExposure = 1.0f;
		}

		wsw::ref::Scene::Instance()->DrawCoronae();

		R_DrawPolys();
	}

	R_DrawEntities();

	if( !shadowMap ) {
		// now set  the real far clip value and reload view matrices
		R_SetFarClip();

		R_SetupViewMatrices();

		// render to depth textures, mark shadowed entities and surfaces
		// TODO
	}

	R_SortDrawList( rn.meshlist );

	R_BindRefInstFBO();

	R_SetupGL();

	R_DrawPortals();

	if( r_portalonly->integer && !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW ) ) ) {
		return;
	}

	R_Clear( ~0 );

	R_DrawSurfaces( rn.meshlist );

	if( r_showtris->integer ) {
		R_DrawOutlinedSurfaces( rn.meshlist );
	}

	R_TransformForWorld();

	R_EndGL();
}

portalSurface_t *R_AddPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) {
	if( !shader ) {
		return NULL;
	}

	const bool depthPortal = !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) );
	if( R_FASTSKY() && depthPortal ) {
		return NULL;
	}

	if( rn.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
		// not enough space
		return NULL;
	}

	portalSurface_t *portalSurface = &rn.portalSurfaces[rn.numPortalSurfaces++];
	memset( portalSurface, 0, sizeof( portalSurface_t ) );
	portalSurface->entity = ent;
	portalSurface->shader = shader;
	portalSurface->skyPortal = NULL;
	ClearBounds( portalSurface->mins, portalSurface->maxs );
	memset( portalSurface->texures, 0, sizeof( portalSurface->texures ) );

	if( depthPortal ) {
		rn.numDepthPortalSurfaces++;
	}

	return portalSurface;
}

void R_UpdatePortalSurface( portalSurface_t *portalSurface, const mesh_t *mesh,
							const vec3_t mins, const vec3_t maxs, const shader_t *shader, void *drawSurf ) {
	if( !mesh || !portalSurface ) {
		return;
	}

	vec3_t v[3];
	for( unsigned i = 0; i < 3; i++ ) {
		VectorCopy( mesh->xyzArray[mesh->elems[i]], v[i] );
	}

	const entity_t *ent = portalSurface->entity;

	cplane_t plane, untransformed_plane;
	PlaneFromPoints( v, &untransformed_plane );
	untransformed_plane.dist += DotProduct( ent->origin, untransformed_plane.normal );
	untransformed_plane.dist += 1; // nudge along the normal a bit
	CategorizePlane( &untransformed_plane );

	if( shader->flags & SHADER_AUTOSPRITE ) {
		// autosprites are quads, facing the viewer
		if( mesh->numVerts < 4 ) {
			return;
		}

		vec3_t centre;
		// compute centre as average of 4 vertices
		VectorCopy( mesh->xyzArray[mesh->elems[3]], centre );
		for( unsigned i = 0; i < 3; i++ ) {
			VectorAdd( centre, v[i], centre );
		}

		VectorMA( ent->origin, 0.25, centre, centre );

		VectorNegate( &rn.viewAxis[AXIS_FORWARD], plane.normal );
		plane.dist = DotProduct( plane.normal, centre );
		CategorizePlane( &plane );
	} else {
		// regular surfaces
		if( !Matrix3_Compare( ent->axis, axis_identity ) ) {
			mat3_t entity_rotation;
			Matrix3_Transpose( ent->axis, entity_rotation );

			vec3_t temp;
			for( unsigned i = 0; i < 3; i++ ) {
				VectorCopy( v[i], temp );
				Matrix3_TransformVector( entity_rotation, temp, v[i] );
				VectorMA( ent->origin, ent->scale, v[i], v[i] );
			}

			PlaneFromPoints( v, &plane );
			CategorizePlane( &plane );
		} else {
			plane = untransformed_plane;
		}
	}

	float dist;
	if( ( dist = PlaneDiff( rn.viewOrigin, &plane ) ) <= BACKFACE_EPSILON ) {
		// behind the portal plane
		if( !( shader->flags & SHADER_PORTAL_CAPTURE2 ) ) {
			return;
		}

		// we need to render the backplane view
	}

	// check if portal view is opaque due to alphagen portal
	if( shader->portalDistance && dist > shader->portalDistance ) {
		return;
	}

	portalSurface->plane = plane;
	portalSurface->untransformed_plane = untransformed_plane;

	AddPointToBounds( mins, portalSurface->mins, portalSurface->maxs );
	AddPointToBounds( maxs, portalSurface->mins, portalSurface->maxs );
	VectorAdd( portalSurface->mins, portalSurface->maxs, portalSurface->centre );
	VectorScale( portalSurface->centre, 0.5, portalSurface->centre );
}

/*
* R_DrawPortalSurface
*
* Renders the portal view and captures the results from framebuffer if
* we need to do a $portalmap stage. Note that for $portalmaps we must
* use a different viewport.
*/
static void R_DrawPortalSurface( portalSurface_t *portalSurface ) {
	vec3_t viewerOrigin;
	vec3_t origin;
	mat3_t axis;
	entity_t *ent, *best;
	cplane_t *portal_plane = &portalSurface->plane, *untransformed_plane = &portalSurface->untransformed_plane;
	const shader_t *shader = portalSurface->shader;
	vec_t *portal_centre = portalSurface->centre;
	bool mirror, refraction = false;
	Texture *captureTexture;
	int captureTextureId = -1;
	int prevRenderFlags = 0;
	bool prevFlipped;
	bool doReflection, doRefraction;
	Texture *portalTexures[2] = { NULL, NULL };

	doReflection = doRefraction = true;
	if( shader->flags & SHADER_PORTAL_CAPTURE ) {

		captureTexture = NULL;
		captureTextureId = 0;

		for( unsigned i = 0; i < shader->numpasses; i++ ) {
			shaderpass_t *pass = &shader->passes[i];
			if( pass->program_type == GLSL_PROGRAM_TYPE_DISTORTION ) {
				if( ( pass->alphagen.type == ALPHA_GEN_CONST && pass->alphagen.args[0] == 1 ) ) {
					doRefraction = false;
				} else if( ( pass->alphagen.type == ALPHA_GEN_CONST && pass->alphagen.args[0] == 0 ) ) {
					doReflection = false;
				}
				break;
			}
		}
	} else {
		captureTexture = NULL;
		captureTextureId = -1;
	}

	int x = 0, y = 0;
	int w = rn.refdef.width;
	int h = rn.refdef.height;

	float dist = PlaneDiff( rn.viewOrigin, portal_plane );
	if( dist <= BACKFACE_EPSILON || !doReflection ) {
		if( !( shader->flags & SHADER_PORTAL_CAPTURE2 ) || !doRefraction ) {
			return;
		}

		// even if we're behind the portal, we still need to capture
		// the second portal image for refraction
		refraction = true;
		captureTexture = NULL;
		captureTextureId = 1;
		if( dist < 0 ) {
			VectorInverse( portal_plane->normal );
			portal_plane->dist = -portal_plane->dist;
		}
	}

	mirror = true; // default to mirror view
	// it is stupid IMO that mirrors require a RT_PORTALSURFACE entity

	best = NULL;
	float best_d = std::numeric_limits<float>::max();
	for( unsigned i = rsc.numLocalEntities; i < rsc.numEntities; i++ ) {
		ent = R_NUM2ENT( i );
		if( ent->rtype != RT_PORTALSURFACE ) {
			continue;
		}

		float d = PlaneDiff( ent->origin, untransformed_plane );
		if( ( d >= -64 ) && ( d <= 64 ) ) {
			d = Distance( ent->origin, portal_centre );
			if( d < best_d ) {
				best = ent;
				best_d = d;
			}
		}
	}

	if( best == NULL ) {
		if( captureTextureId < 0 ) {
			// still do a push&pop because to ensure the clean state
			if( R_PushRefInst() ) {
				R_PopRefInst();
			}
			return;
		}
	} else {
		if( !VectorCompare( best->origin, best->origin2 ) ) { // portal
			mirror = false;
		}
		best->rtype = NUM_RTYPES;
	}

	prevRenderFlags = rn.renderFlags;
	prevFlipped = ( rn.refdef.rdflags & RDF_FLIPPED ) != 0;
	if( !R_PushRefInst() ) {
		return;
	}

	VectorCopy( rn.viewOrigin, viewerOrigin );
	if( prevFlipped ) {
		VectorInverse( &rn.viewAxis[AXIS_RIGHT] );
	}

	setup_and_render:

	if( refraction ) {
		VectorInverse( portal_plane->normal );
		portal_plane->dist = -portal_plane->dist;
		CategorizePlane( portal_plane );
		VectorCopy( rn.viewOrigin, origin );
		Matrix3_Copy( rn.refdef.viewaxis, axis );
		VectorCopy( viewerOrigin, rn.pvsOrigin );

		rn.renderFlags |= RF_PORTALVIEW;
		if( prevFlipped ) {
			rn.renderFlags |= RF_FLIPFRONTFACE;
		}
	} else if( mirror ) {
		VectorReflect( rn.viewOrigin, portal_plane->normal, portal_plane->dist, origin );

		VectorReflect( &rn.viewAxis[AXIS_FORWARD], portal_plane->normal, 0, &axis[AXIS_FORWARD] );
		VectorReflect( &rn.viewAxis[AXIS_RIGHT], portal_plane->normal, 0, &axis[AXIS_RIGHT] );
		VectorReflect( &rn.viewAxis[AXIS_UP], portal_plane->normal, 0, &axis[AXIS_UP] );

		Matrix3_Normalize( axis );

		VectorCopy( viewerOrigin, rn.pvsOrigin );

		rn.renderFlags = ( prevRenderFlags ^ RF_FLIPFRONTFACE ) | RF_MIRRORVIEW;
	} else {
		vec3_t tvec;
		mat3_t A, B, C, rot;

		// build world-to-portal rotation matrix
		VectorNegate( portal_plane->normal, tvec );
		NormalVectorToAxis( tvec, A );

		// build portal_dest-to-world rotation matrix
		ByteToDir( best->frame, tvec );
		NormalVectorToAxis( tvec, B );
		Matrix3_Transpose( B, C );

		// multiply to get world-to-world rotation matrix
		Matrix3_Multiply( C, A, rot );

		// translate view origin
		VectorSubtract( rn.viewOrigin, best->origin, tvec );
		Matrix3_TransformVector( rot, tvec, origin );
		VectorAdd( origin, best->origin2, origin );

		Matrix3_Transpose( A, B );
		Matrix3_Multiply( rn.viewAxis, B, rot );
		Matrix3_Multiply( best->axis, rot, B );
		Matrix3_Transpose( C, A );
		Matrix3_Multiply( B, A, axis );

		// set up portal_plane
		VectorCopy( &axis[AXIS_FORWARD], portal_plane->normal );
		portal_plane->dist = DotProduct( best->origin2, portal_plane->normal );
		CategorizePlane( portal_plane );

		// for portals, vis data is taken from portal origin, not
		// view origin, because the view point moves around and
		// might fly into (or behind) a wall
		VectorCopy( best->origin2, rn.pvsOrigin );
		VectorCopy( best->origin2, rn.lodOrigin );

		rn.renderFlags |= RF_PORTALVIEW;

		// ignore entities, if asked politely
		if( best->renderfx & RF_NOPORTALENTS ) {
			rn.renderFlags |= RF_ENVVIEW;
		}
		if( prevFlipped ) {
			rn.renderFlags |= RF_FLIPFRONTFACE;
		}
	}

	rn.refdef.rdflags &= ~( RDF_UNDERWATER | RDF_CROSSINGWATER | RDF_FLIPPED );

	rn.meshlist = &r_portallist;
	rn.portalmasklist = NULL;

	rn.renderFlags |= RF_CLIPPLANE;
	rn.renderFlags &= ~RF_SOFT_PARTICLES;
	rn.clipPlane = *portal_plane;

	rn.farClip = R_DefaultFarClip();

	rn.clipFlags |= ( 1 << 5 );
	rn.frustum[5] = *portal_plane;
	CategorizePlane( &rn.frustum[5] );

	// if we want to render to a texture, initialize texture
	// but do not try to render to it more than once
	if( captureTextureId >= 0 ) {
		//int texFlags = shader->flags & SHADER_NO_TEX_FILTERING ? IT_NOFILTERING : 0;

		// TODO:
		//captureTexture = R_GetPortalTexture( rsc.refdef.width, rsc.refdef.height, texFlags,
		//									 rsc.frameCount );
		portalTexures[captureTextureId] = captureTexture;

		if( !captureTexture ) {
			// couldn't register a slot for this plane
			goto done;
		}

		x = y = 0;
		w = captureTexture->width;
		h = captureTexture->height;
		rn.refdef.width = w;
		rn.refdef.height = h;
		rn.refdef.x = 0;
		rn.refdef.y = 0;
		// TODO.... rn.renderTarget = captureTexture->fbo;
		rn.renderFlags |= RF_PORTAL_CAPTURE;
		Vector4Set( rn.viewport, rn.refdef.x + x, rn.refdef.y + y, w, h );
		Vector4Set( rn.scissor, rn.refdef.x + x, rn.refdef.y + y, w, h );
	} else {
		rn.renderFlags &= ~RF_PORTAL_CAPTURE;
	}

	VectorCopy( origin, rn.refdef.vieworg );
	Matrix3_Copy( axis, rn.refdef.viewaxis );

	R_RenderView( &rn.refdef );

	if( doRefraction && !refraction && ( shader->flags & SHADER_PORTAL_CAPTURE2 ) ) {
		rn.renderFlags = prevRenderFlags;
		refraction = true;
		captureTexture = NULL;
		captureTextureId = 1;
		goto setup_and_render;
	}

	done:
	portalSurface->texures[0] = portalTexures[0];
	portalSurface->texures[1] = portalTexures[1];

	R_PopRefInst();
}

/*
* R_DrawPortalsDepthMask
*
* Renders portal or sky surfaces from the BSP tree to depth buffer. Each rendered pixel
* receives the depth value of 1.0, everything else is cleared to 0.0.
*
* The depth buffer is then preserved for portal render stage to minimize overdraw.
*/
static void R_DrawPortalsDepthMask( void ) {
	float depthmin, depthmax;

	if( !rn.portalmasklist || !rn.portalmasklist->numDrawSurfs ) {
		return;
	}

	RB_GetDepthRange( &depthmin, &depthmax );

	RB_ClearDepth( depthmin );
	RB_Clear( GL_DEPTH_BUFFER_BIT, 0, 0, 0, 0 );
	RB_SetShaderStateMask( ~0, GLSTATE_DEPTHWRITE | GLSTATE_DEPTHFUNC_GT | GLSTATE_NO_COLORWRITE );
	RB_DepthRange( depthmax, depthmax );

	R_DrawSurfaces( rn.portalmasklist );

	RB_DepthRange( depthmin, depthmax );
	RB_ClearDepth( depthmax );
	RB_SetShaderStateMask( ~0, 0 );
}



portalSurface_t *R_AddSkyportalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) {
	portalSurface_t *portalSurface;

	if( rn.skyportalSurface ) {
		portalSurface = rn.skyportalSurface;
	} else if( rn.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
		// not enough space
		return NULL;
	} else {
		portalSurface = &rn.portalSurfaces[rn.numPortalSurfaces++];
		memset( portalSurface, 0, sizeof( *portalSurface ) );
		rn.skyportalSurface = portalSurface;
		rn.numDepthPortalSurfaces++;
	}

	R_AddSurfToDrawList( rn.portalmasklist, ent, NULL, NULL, 0, 0, NULL, drawSurf );

	portalSurface->entity = ent;
	portalSurface->shader = shader;
	portalSurface->skyPortal = &rn.refdef.skyportal;
	return rn.skyportalSurface;
}

static void R_DrawSkyportal( const entity_t *e, skyportal_t *skyportal ) {
	if( !R_PushRefInst() ) {
		return;
	}

	rn.renderFlags = ( rn.renderFlags | RF_PORTALVIEW );
	//rn.renderFlags &= ~RF_SOFT_PARTICLES;
	VectorCopy( skyportal->vieworg, rn.pvsOrigin );

	rn.farClip = R_DefaultFarClip();

	rn.clipFlags = 15;
	rn.meshlist = &r_skyportallist;
	rn.portalmasklist = NULL;
	//Vector4Set( rn.scissor, rn.refdef.x + x, rn.refdef.y + y, w, h );

	if( skyportal->noEnts ) {
		rn.renderFlags |= RF_ENVVIEW;
	}

	if( skyportal->scale ) {
		vec3_t centre, diff;

		VectorAdd( rsh.worldModel->mins, rsh.worldModel->maxs, centre );
		VectorScale( centre, 0.5f, centre );
		VectorSubtract( centre, rn.viewOrigin, diff );
		VectorMA( skyportal->vieworg, -skyportal->scale, diff, rn.refdef.vieworg );
	} else {
		VectorCopy( skyportal->vieworg, rn.refdef.vieworg );
	}

	// FIXME
	if( !VectorCompare( skyportal->viewanglesOffset, vec3_origin ) ) {
		vec3_t angles;
		mat3_t axis;

		Matrix3_Copy( rn.refdef.viewaxis, axis );
		VectorInverse( &axis[AXIS_RIGHT] );
		Matrix3_ToAngles( axis, angles );

		VectorAdd( angles, skyportal->viewanglesOffset, angles );
		AnglesToAxis( angles, axis );
		Matrix3_Copy( axis, rn.refdef.viewaxis );
	}

	rn.refdef.rdflags &= ~( RDF_UNDERWATER | RDF_CROSSINGWATER | RDF_SKYPORTALINVIEW );
	if( skyportal->fov ) {
		rn.refdef.fov_x = skyportal->fov;
		rn.refdef.fov_y = CalcFov( rn.refdef.fov_x, rn.refdef.width, rn.refdef.height );
		AdjustFov( &rn.refdef.fov_x, &rn.refdef.fov_y, glConfig.width, glConfig.height, false );
	}

	R_RenderView( &rn.refdef );

	// restore modelview and projection matrices, scissoring, etc for the main view
	R_PopRefInst();
}

void R_DrawPortals( void ) {
	if( rf.viewcluster != -1 ) {
		if( !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW | RF_SHADOWMAPVIEW ) ) ) {
			R_DrawPortalsDepthMask();

			// render skyportal
			if( rn.skyportalSurface ) {
				const portalSurface_t *ps = rn.skyportalSurface;
				R_DrawSkyportal( ps->entity, ps->skyPortal );
			}

			// render regular portals
			for( unsigned i = 0; i < rn.numPortalSurfaces; i++ ) {
				portalSurface_t ps = rn.portalSurfaces[i];
				if( !ps.skyPortal ) {
					R_DrawPortalSurface( &ps );
					rn.portalSurfaces[i] = ps;
				}
			}
		}
	}
}

void R_ClearScene( void ) {
	rsc.numLocalEntities = 0;
	rsc.numPolys = 0;

	rsc.worldent = R_NUM2ENT( rsc.numLocalEntities );
	rsc.worldent->scale = 1.0f;
	rsc.worldent->model = rsh.worldModel;
	rsc.worldent->rtype = RT_MODEL;
	Matrix3_Identity( rsc.worldent->axis );
	rsc.numLocalEntities++;

	rsc.polyent = R_NUM2ENT( rsc.numLocalEntities );
	rsc.polyent->scale = 1.0f;
	rsc.polyent->model = NULL;
	rsc.polyent->rtype = RT_MODEL;
	Matrix3_Identity( rsc.polyent->axis );
	rsc.numLocalEntities++;

	rsc.skyent = R_NUM2ENT( rsc.numLocalEntities );
	*rsc.skyent = *rsc.worldent;
	rsc.numLocalEntities++;

	rsc.numEntities = rsc.numLocalEntities;

	rsc.numBmodelEntities = 0;

	rsc.frameCount++;

	R_ClearSkeletalCache();

	wsw::ref::Scene::Instance()->Clear();
}

void R_AddEntityToScene( const entity_t *ent ) {
	if( !r_drawentities->integer ) {
		return;
	}

	if( ( ( rsc.numEntities - rsc.numLocalEntities ) < MAX_ENTITIES ) && ent ) {
		const int eNum = rsc.numEntities;
		entity_t *de = R_NUM2ENT( eNum );

		*de = *ent;
		if( r_outlines_scale->value <= 0 ) {
			de->outlineHeight = 0;
		}

		if( de->rtype == RT_MODEL ) {
			if( de->model && de->model->type == mod_brush ) {
				rsc.bmodelEntities[rsc.numBmodelEntities++] = de;
			}
			if( !( de->renderfx & RF_NOSHADOW ) ) {
				// TODO
			}
		} else if( de->rtype == RT_SPRITE ) {
			// simplifies further checks
			de->model = NULL;
		}

		if( de->renderfx & RF_ALPHAHACK ) {
			if( de->shaderRGBA[3] == 255 ) {
				de->renderfx &= ~RF_ALPHAHACK;
			}
		}

		rsc.numEntities++;

		// add invisible fake entity for depth write
		if( ( de->renderfx & ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) == ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) {
			entity_t tent = *ent;
			tent.renderfx &= ~RF_ALPHAHACK;
			tent.renderfx |= RF_NOCOLORWRITE | RF_NOSHADOW;
			R_AddEntityToScene( &tent );
		}
	}
}

void R_AddPolyToScene( const poly_t *poly ) {
	assert( sizeof( *poly->elems ) == sizeof( elem_t ) );

	if( ( rsc.numPolys < MAX_POLYS ) && poly && poly->numverts ) {
		drawSurfacePoly_t *dp = &rsc.polys[rsc.numPolys];

		assert( poly->shader != NULL );
		if( !poly->shader ) {
			return;
		}

		dp->type = ST_POLY;
		dp->shader = poly->shader;
		dp->numVerts = std::min( poly->numverts, MAX_POLY_VERTS );
		dp->xyzArray = poly->verts;
		dp->normalsArray = poly->normals;
		dp->stArray = poly->stcoords;
		dp->colorsArray = poly->colors;
		dp->numElems = poly->numelems;
		dp->elems = ( elem_t * )poly->elems;
		dp->fogNum = poly->fognum;

		// if fogNum is unset, we need to find the volume for polygon bounds
		if( !dp->fogNum ) {
			vec3_t dpmins, dpmaxs;
			ClearBounds( dpmins, dpmaxs );

			for( int i = 0; i < dp->numVerts; i++ ) {
				AddPointToBounds( dp->xyzArray[i], dpmins, dpmaxs );
			}

			mfog_t *const fog = R_FogForBounds( dpmins, dpmaxs );
			dp->fogNum = fog ? ( fog - rsh.worldBrushModel->fogs + 1 ) : -1;
		}

		rsc.numPolys++;
	}
}

void R_AddLightStyleToScene( int style, float r, float g, float b ) {
	if( style < 0 || style >= MAX_LIGHTSTYLES ) {
		Com_Error( ERR_DROP, "R_AddLightStyleToScene: bad light style %i", style );
		return;
	}

	lightstyle_t *const ls = &rsc.lightStyles[style];
	ls->rgb[0] = std::max( 0.0f, r );
	ls->rgb[1] = std::max( 0.0f, g );
	ls->rgb[2] = std::max( 0.0f, b );
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
			drawnProgramLightNums[numDrawnProgramLights++] = (LightNumType)i;
		}
		return BitsForNumberOfLights( numDrawnProgramLights );
	}

	int numCulledLights = 0;
	for( int i = 0; i < numProgramLights; ++i ) {
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