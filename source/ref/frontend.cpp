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

void R_SetupFrustum( const refdef_t *rd, float farClip, cplane_t *frustum ) {
	// 0 - left
	// 1 - right
	// 2 - down
	// 3 - up
	// 4 - farclip

	vec3_t forward, left, up;
	VectorCopy( &rd->viewaxis[AXIS_FORWARD], forward );
	VectorCopy( &rd->viewaxis[AXIS_RIGHT], left );
	VectorCopy( &rd->viewaxis[AXIS_UP], up );

	if( rd->rdflags & RDF_USEORTHO ) {
		VectorNegate( left, frustum[0].normal );
		VectorCopy( left, frustum[1].normal );
		VectorNegate( up, frustum[2].normal );
		VectorCopy( up, frustum[3].normal );

		for( unsigned i = 0; i < 4; i++ ) {
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

		for( unsigned i = 0; i < 4; i++ ) {
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

static void R_EndGL() {
	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, 0 );
	}

	if( rn.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}
}

static void R_BindRefInstFBO() {
	const int fbo = rn.renderTarget;
	R_BindFrameBufferObject( fbo );
}

void R_ClearRefInstStack() {
	riStackSize = 0;
}

bool R_PushRefInst() {
	if( riStackSize == REFINST_STACK_SIZE ) {
		riStack[riStackSize++] = rn;
		R_EndGL();
		return true;
	}
	return false;
}

void R_PopRefInst() {
	if( riStackSize ) {
		rn = riStack[--riStackSize];
		R_BindRefInstFBO();
		R_SetupGL();
	}
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

static int R_PackDistKey( int renderFx, const shader_t *shader, float dist, unsigned order ) {
	int shaderSort = shader->sort;

	if( renderFx & RF_WEAPONMODEL ) {
		const bool depthWrite = ( shader->flags & SHADER_DEPTHWRITE ) != 0;

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

static unsigned R_PackOpaqueOrder( const mfog_t *fog, const shader_t *shader, int numLightmaps, bool dlight ) {
	unsigned order = 0;

	// shader order
	if( shader != NULL ) {
		order = R_PackShaderOrder( shader );
	}
	// group by dlight
	if( dlight ) {
		order |= 0x40;
	}
	// group by dlight
	if( fog != NULL ) {
		order |= 0x80;
	}
	// group by lightmaps
	order |= ( (MAX_LIGHTMAPS - numLightmaps) << 10 );

	return order;
}

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

namespace wsw::ref {

auto Frontend::tryAddingPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) -> portalSurface_t * {
	if( shader ) {
		if( rn.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
			const bool depthPortal = !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) );
			if( !R_FASTSKY() || !depthPortal ) {
				portalSurface_t *portalSurface = &rn.portalSurfaces[rn.numPortalSurfaces++];
				memset( portalSurface, 0, sizeof( portalSurface_t ) );
				portalSurface->entity = ent;
				portalSurface->shader = shader;
				portalSurface->skyPortal = nullptr;
				ClearBounds( portalSurface->mins, portalSurface->maxs );
				memset( portalSurface->texures, 0, sizeof( portalSurface->texures ) );

				if( depthPortal ) {
					rn.numDepthPortalSurfaces++;
				}

				return portalSurface;
			}
		}
	}

	return nullptr;
}

void Frontend::updatePortalSurface( portalSurface_t *portalSurface, const mesh_t *mesh, const float *mins,
									const float *maxs, const shader_t *shader, void *drawSurf ) {
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
	VectorScale( portalSurface->centre, 0.5f, portalSurface->centre );
}

auto Frontend::tryUpdatingPortalSurfaceAndDistance( drawSurfaceBSP_t *drawSurf, const msurface_t *surf,
													const float *origin ) -> std::optional<float> {
	const shader_t *shader = drawSurf->shader;
	if( shader->flags & SHADER_PORTAL ) {
		const sortedDrawSurf_t *const sds = (sortedDrawSurf_t *)drawSurf->listSurf;

		unsigned shaderNum, entNum;
		int portalNum, fogNum;
		R_UnpackSortKey( sds->sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

		if( portalNum >= 0 ) {
			portalSurface_t *const portalSurface = rn.portalSurfaces + portalNum;
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
			updatePortalSurface( portalSurface, &surf->mesh, surf->mins, surf->maxs, shader, drawSurf );
			return dist;
		}
	}
	return std::nullopt;
}

static const drawSurf_cb r_drawSurfCb[ST_MAX_TYPES] =
	{
	/* ST_NONE */
	nullptr,
	/* ST_BSP */
	( drawSurf_cb ) &R_SubmitBSPSurfToBackend,
	/* ST_ALIAS */
	( drawSurf_cb ) &R_SubmitAliasSurfToBackend,
	/* ST_SKELETAL */
	( drawSurf_cb ) &R_SubmitSkeletalSurfToBackend,
	/* ST_SPRITE */
	nullptr,
	/* ST_POLY */
	nullptr,
	/* ST_CORONA */
	nullptr,
	/* ST_nullptrMODEL */
	( drawSurf_cb ) & R_SubmitNullSurfToBackend,
	};

static const batchDrawSurf_cb r_batchDrawSurfCb[ST_MAX_TYPES] =
	{
	/* ST_NONE */
	nullptr,
	/* ST_BSP */
	nullptr,
	/* ST_ALIAS */
	nullptr,
	/* ST_SKELETAL */
	nullptr,
	/* ST_SPRITE */
	( batchDrawSurf_cb ) & R_SubmitSpriteSurfToBackend,
	/* ST_POLY */
	( batchDrawSurf_cb ) & R_SubmitPolySurfToBackend,
	/* ST_CORONA */
	( batchDrawSurf_cb ) & R_SubmitCoronaSurfToBackend,
	/* ST_nullptrMODEL */
	nullptr,
	};

static drawSurfaceType_t spriteDrawSurf = ST_SPRITE;

bool Frontend::addSpriteToSortList( const entity_t *e ) {
	// TODO: This condition should be eliminated from this path
	if( e->flags & RF_NOSHADOW ) {
		if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
			return false;
		}
	}

	if( e->radius <= 0 || e->customShader == nullptr || e->scale <= 0 ) {
		return false;
	}

	vec3_t eyeToSprite;
	VectorSubtract( e->origin, rn.refdef.vieworg, eyeToSprite );
	const float dist = DotProduct( eyeToSprite, &rn.viewAxis[0] );
	if( dist <= 0 ) {
		return false;
	}

	return addEntryToSortList( rn.meshlist, e, R_FogForSphere( e->origin, e->radius ),
							 e->customShader, dist, 0, nullptr, &spriteDrawSurf );
}

static void R_ReserveVBOSlices( drawList_t *list, unsigned drawSurfIndex ) {
	unsigned minSlices = drawSurfIndex + 1;
	if( rsh.worldBrushModel ) {
		minSlices = std::max( rsh.worldBrushModel->numDrawSurfaces, minSlices );
	}

	const unsigned oldSize = list->maxVboSlices;
	const unsigned newSize = std::max( minSlices, oldSize * 2 );

	vboSlice_t *slices = list->vboSlices;
	vboSlice_t *newSlices = (vboSlice_t *)Q_malloc( newSize * sizeof( vboSlice_t ) );
	if( slices ) {
		memcpy( newSlices, slices, oldSize * sizeof( vboSlice_t ) );
		Q_free( slices );
	}

	list->vboSlices = newSlices;
	list->maxVboSlices = newSize;
}

bool Frontend::addBspSurfToSortList( const entity_t *e, drawSurfaceBSP_t *drawSurf, const float *maybeOrigin ) {
	assert( ( drawSurf->visFrame != rf.frameCount ) && "Should not be duplicated for now" );

	portalSurface_t *portalSurface = nullptr;
	const shader_t *shader = drawSurf->shader;
	if( shader->flags & SHADER_PORTAL ) {
		portalSurface = tryAddingPortalSurface( e, shader, drawSurf );
	}

	const mfog_t *fog = drawSurf->fog;
	const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, drawSurf->numLightmaps, false );

	drawSurf->dlightBits = 0;
	drawSurf->visFrame = rf.frameCount;
	drawSurf->listSurf = addEntryToSortList( rn.meshlist, e, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface,
											 drawSurf );
	if( !drawSurf->listSurf ) {
		return false;
	}

	if( portalSurface && !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) ) {
		addEntryToSortList( rn.portalmasklist, e, nullptr, nullptr, 0, 0, nullptr, drawSurf );
	}

	unsigned dlightFrame = 0;
	unsigned curDlightBits = 0;

	msurface_t *firstVisSurf = nullptr;
	msurface_t *lastVisSurf  = nullptr;

	float resultDist = 0;

	msurface_t *const modelSurfaces = rsh.worldBrushModel->surfaces + drawSurf->firstWorldSurface;
	const bool special = ( drawSurf->shader->flags & ( SHADER_PORTAL ) ) != 0;
	for( unsigned i = 0; i < drawSurf->numWorldSurfaces; i++ ) {
		msurface_t *const surf = &modelSurfaces[i];
		if( special ) {
			if( const auto maybePortalDistance = tryUpdatingPortalSurfaceAndDistance( drawSurf, surf, maybeOrigin ) ) {
				resultDist = std::max( resultDist, *maybePortalDistance );
			}
		}

		// TODO: ...
		unsigned dlightBits = 0;
		unsigned checkDlightBits = dlightBits & ~curDlightBits;

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
		if( !firstVisSurf ) {
			firstVisSurf = surf;
		}
		lastVisSurf = surf;
	}

	if( dlightFrame == rsc.frameCount ) {
		drawSurf->dlightBits = curDlightBits;
		drawSurf->dlightFrame = dlightFrame;
	}

	// prepare the slice
	if( firstVisSurf ) {
		const bool dlight = dlightFrame == rsc.frameCount;

		const auto drawSurfIndex = drawSurf - rsh.worldBrushModel->drawSurfaces;
		// TODO: Reserve once per frame
		if( drawSurfIndex >= rn.meshlist->maxVboSlices ) [[unlikely]] {
			R_ReserveVBOSlices( rn.meshlist, drawSurfIndex );
		}

		vboSlice_t *slice = &rn.meshlist->vboSlices[drawSurfIndex];
		assert( !slice->numVerts && !slice->numElems && !slice->firstVert && !slice->firstElem );

		slice->firstVert = firstVisSurf->firstDrawSurfVert;
		slice->firstElem = firstVisSurf->firstDrawSurfElem;
		slice->numVerts = lastVisSurf->mesh.numVerts + lastVisSurf->firstDrawSurfVert - firstVisSurf->firstDrawSurfVert;
		slice->numElems = lastVisSurf->mesh.numElems + lastVisSurf->firstDrawSurfElem - firstVisSurf->firstDrawSurfElem;

		// update the distance sorting key if it's a portal surface or a normal dlit surface
		if( resultDist != 0 || dlight ) {
			const unsigned order = R_PackOpaqueOrder( drawSurf->fog, drawSurf->shader, drawSurf->numLightmaps, dlight );
			if( resultDist == 0 ) {
				resultDist = WORLDSURF_DIST;
			}
			sortedDrawSurf_t *const sds = (sortedDrawSurf_t *)drawSurf->listSurf;
			sds->distKey = R_PackDistKey( 0, drawSurf->shader, resultDist, order );
		}
	}

	return true;
}

bool Frontend::addAliasModelToSortList( const entity_t *e ) {
	// never render weapon models or non-occluders into shadowmaps
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		if( e->renderfx & RF_WEAPONMODEL ) {
			return true;
		}
	}

	const model_t *mod = R_AliasModelLOD( e );
	const maliasmodel_t *aliasmodel;
	if( !( aliasmodel = ( ( const maliasmodel_t * )mod->extradata ) ) || !aliasmodel->nummeshes ) {
		return false;
	}

	vec3_t mins, maxs;
	const float radius = R_AliasModelLerpBBox( e, mod, mins, maxs );

	float distance;
	// make sure weapon model is always closest to the viewer
	if( e->renderfx & RF_WEAPONMODEL ) {
		distance = 0;
	} else {
		distance = Distance( e->origin, rn.viewOrigin ) + 1;
	}

	const mfog_t *fog = R_FogForSphere( e->origin, radius );

	for( int i = 0; i < aliasmodel->nummeshes; i++ ) {
		const maliasmesh_t *mesh = &aliasmodel->meshes[i];
		const shader_t *shader = nullptr;

		if( e->customSkin ) {
			shader = R_FindShaderForSkinFile( e->customSkin, mesh->name );
		} else if( e->customShader ) {
			shader = e->customShader;
		} else if( mesh->numskins ) {
			for( int j = 0; j < mesh->numskins; j++ ) {
				shader = mesh->skins[j].shader;
				if( shader ) {
					int drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
					addEntryToSortList( rn.meshlist, e, fog, shader, distance, drawOrder, nullptr,
										aliasmodel->drawSurfs + i );
				}
			}
			continue;
		}

		if( shader ) {
			int drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
			addEntryToSortList( rn.meshlist, e, fog, shader, distance, drawOrder, nullptr, aliasmodel->drawSurfs + i );
		}
	}

	return true;
}

bool Frontend::addSkeletalModelToSortList( const entity_t *e ) {
	const model_t *mod = R_SkeletalModelLOD( e );
	const mskmodel_t *skmodel;
	if( !( skmodel = ( ( mskmodel_t * )mod->extradata ) ) || !skmodel->nummeshes ) {
		return false;
	}

	vec3_t mins, maxs;
	const float radius = R_SkeletalModelLerpBBox( e, mod, mins, maxs );

	// never render weapon models or non-occluders into shadowmaps
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		if( e->renderfx & RF_WEAPONMODEL ) {
			return true;
		}
	}

	float distance;
	// make sure weapon model is always closest to the viewer
	if( e->renderfx & RF_WEAPONMODEL ) {
		distance = 0;
	} else {
		distance = Distance( e->origin, rn.viewOrigin ) + 1;
	}

	const mfog_t *fog = R_FogForSphere( e->origin, radius );

	// run quaternions lerping job in the background
	R_AddSkeletalModelCache( e, mod );

	for( unsigned i = 0; i < skmodel->nummeshes; i++ ) {
		const mskmesh_t *mesh = &skmodel->meshes[i];
		shader_t *shader = nullptr;

		if( e->customSkin ) {
			shader = R_FindShaderForSkinFile( e->customSkin, mesh->name );
		} else if( e->customShader ) {
			shader = e->customShader;
		} else {
			shader = mesh->skin.shader;
		}

		if( shader ) {
			const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
			addEntryToSortList( rn.meshlist, e, fog, shader, distance, drawOrder, nullptr, skmodel->drawSurfs + i );
		}
	}

	return true;
}

static int nullDrawSurf = ST_NULLMODEL;

bool Frontend::addNullSurfToSortList( const entity_t *e ) {
	return addEntryToSortList( rn.meshlist, e, nullptr, rsh.whiteShader, 0, 0, nullptr, &nullDrawSurf );
}

static void R_ReserveDrawSurfaces( drawList_t *list ) {
	int minMeshes = MIN_RENDER_MESHES;
	if( rsh.worldBrushModel ) {
		minMeshes += (int)rsh.worldBrushModel->numDrawSurfaces;
	}

	sortedDrawSurf_t *ds = list->drawSurfs;
	const int maxMeshes = list->maxDrawSurfs;

	const int oldSize = maxMeshes;
	const int newSize = std::max( minMeshes, oldSize * 2 );

	auto *newDs = (sortedDrawSurf_t *)Q_malloc( newSize * sizeof( sortedDrawSurf_t ) );
	if( ds ) {
		memcpy( newDs, ds, oldSize * sizeof( sortedDrawSurf_t ) );
		Q_free( ds );
	}

	list->drawSurfs = newDs;
	list->maxDrawSurfs = newSize;
}

void *Frontend::addEntryToSortList( drawList_t *list, const entity_t *e, const mfog_t *fog,
									const shader_t *shader, float dist, unsigned order,
									const portalSurface_t *portalSurf, void *drawSurf ) {
	assert( list );

	if( shader ) [[likely]] {
		// TODO: This should be moved to an outer loop
		if( !( rn.renderFlags & RF_SHADOWMAPVIEW ) || !Shader_ReadDepth( shader ) ) [[likely]] {
			// TODO: This should be moved to an outer loop
			if( !rsh.worldBrushModel ) [[unlikely]] {
				fog = nullptr;
			}

			if( const int distKey = R_PackDistKey( e->renderfx, shader, dist, order ) ) [[likely]] {
				// TODO: This should be moved to an outer loop
				if( list->numDrawSurfs >= list->maxDrawSurfs ) [[unlikely]] {
					R_ReserveDrawSurfaces( list );
				}

				const int fogNum = fog ? (int)( fog - rsh.worldBrushModel->fogs ) : -1;
				const int portalNum = portalSurf ? (int)( portalSurf - rn.portalSurfaces ) : -1;

				sortedDrawSurf_t *const sds = &list->drawSurfs[list->numDrawSurfs++];
				sds->drawSurf = ( drawSurfaceType_t * )drawSurf;
				sds->sortKey = R_PackSortKey( shader->id, fogNum, portalNum, R_ENT2NUM( e ) );
				sds->distKey = distKey;

				return sds;
			}
		}
	}

	return nullptr;
}

void Frontend::collectVisiblePolys() {
	for( unsigned i = 0; i < rsc.numPolys; i++ ) {
		drawSurfacePoly_t *const p = rsc.polys + i;
		mfog_t *fog;
		// TODO: Use a single branch
		if( p->fogNum <= 0 || (unsigned)p->fogNum > rsh.worldBrushModel->numfogs ) {
			fog = nullptr;
		} else {
			fog = rsh.worldBrushModel->fogs + p->fogNum - 1;
		}

		if( !addEntryToSortList( rn.meshlist, rsc.polyent, fog, p->shader, 0, i, nullptr, p ) ) {
			continue;
		}
	}
}

void Frontend::collectVisibleEntities() {
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

		// TODO: Keep model of different types in different lists
		switch( e->rtype ) {
			case RT_MODEL:
				if( !e->model ) {
					addNullSurfToSortList( e );
					continue;
				}
				switch( e->model->type ) {
					case mod_alias:
						addAliasModelToSortList( e );
						break;
					case mod_skeletal:
						addSkeletalModelToSortList( e );
						break;
					case mod_brush:
						e->outlineHeight = rsc.worldent->outlineHeight;
						Vector4Copy( rsc.worldent->outlineRGBA, e->outlineColor );
						addBrushModelToSortList( e );
					default:
						break;
				}
				break;
				case RT_SPRITE:
					addSpriteToSortList( e );
					break;
				default:
					break;
		}
	}
}

void Frontend::collectVisibleWorldBrushes() {
	const bool worldOutlines = mapConfig.forceWorldOutlines || ( rn.refdef.rdflags & RDF_WORLDOUTLINES );
	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		rsc.worldent->outlineHeight = std::max( 0.0f, r_outlines_world->value );
	} else {
		rsc.worldent->outlineHeight = 0;
	}

	Vector4Copy( mapConfig.outlineColor, rsc.worldent->outlineColor );

	for( unsigned i = 0; i < rsh.worldBrushModel->numModelDrawSurfaces; i++ ) {
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + i;
		addBspSurfToSortList( rsc.worldent, drawSurf, nullptr );
	}
}

void Frontend::submitSortedSurfacesToBackend( drawList_t *list ) {
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
		const mfog_t *fog = fogNum >= 0 ? rsh.worldBrushModel->fogs + fogNum : nullptr;
		const portalSurface_t *portalSurface = portalNum >= 0 ? rn.portalSurfaces + portalNum : nullptr;
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

bool Frontend::addBrushModelToSortList( const entity_t *e ) {
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

	// TODO: Lift addBspSurfToSortList condition here
	for( unsigned i = 0; i < bmodel->numModelDrawSurfaces; i++ ) {
		const unsigned surfNum = bmodel->firstModelDrawSurface + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + surfNum;
		addBspSurfToSortList( e, drawSurf, origin );
	}

	return true;
}

void Frontend::setupViewMatrices() {
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

void Frontend::clearActiveFrameBuffer() {
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

	RB_Clear( bits, envColor[0], envColor[1], envColor[2], envColor[3] );
}

void Frontend::renderViewFromThisCamera( const refdef_t *fd ) {
	const bool shadowMap = rn.renderFlags & RF_SHADOWMAPVIEW ? true : false;

	rn.refdef = *fd;

	// load view matrices with default far clip value
	setupViewMatrices();

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

	R_SetupFrustum( &rn.refdef, rn.farClip, rn.frustum );

	// we know the initial farclip at this point after determining visible world leafs
	// R_DrawEntities can make adjustments as well

	if( !shadowMap ) {
		if( !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
			if( r_drawworld->integer && rsh.worldModel ) {
				collectVisibleWorldBrushes();
			}
		}

		rn.fog_eye = R_FogForSphere( rn.viewOrigin, 0.5 );
		rn.hdrExposure = 1.0f;

		collectVisiblePolys();
	}

	if( r_drawentities->integer ) {
		collectVisibleEntities();
	}

	if( !shadowMap ) {
		// now set  the real far clip value and reload view matrices
		rn.farClip = R_DefaultFarClip();

		setupViewMatrices();

		// render to depth textures, mark shadowed entities and surfaces
		// TODO
	}

	if( !r_draworder->integer ) {
		const auto cmp = []( const sortedDrawSurf_t &lhs, const sortedDrawSurf_t &rhs ) {
			// TODO: Avoid runtime coposition of keys
			const auto lhsKey = ( (uint64_t)lhs.distKey << 32 ) | (uint64_t)lhs.sortKey;
			const auto rhsKey = ( (uint64_t)rhs.distKey << 32 ) | (uint64_t)rhs.sortKey;
			return lhsKey < rhsKey;
		};
		std::sort( rn.meshlist->drawSurfs, rn.meshlist->drawSurfs + rn.meshlist->numDrawSurfs, cmp );
	}

	R_BindRefInstFBO();

	R_SetupGL();

	drawPortals();

	if( r_portalonly->integer && !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW ) ) ) {
		return;
	}

	clearActiveFrameBuffer();

	submitSortedSurfacesToBackend( rn.meshlist );

	if( r_showtris->integer && !( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_EnableWireframe( true );
		submitSortedSurfacesToBackend( rn.meshlist );
		RB_EnableWireframe( false );
	}

	R_TransformForWorld();

	R_EndGL();
}

/*
* R_DrawPortalSurface
*
* Renders the portal view and captures the results from framebuffer if
* we need to do a $portalmap stage. Note that for $portalmaps we must
* use a different viewport.
*/
void Frontend::drawPortalSurface( portalSurface_t *portalSurface ) {
	int captureTextureId = -1;
	Texture *captureTexture = nullptr;
	bool doReflection = true, doRefraction = true;
	const shader_t *const shader = portalSurface->shader;
	if( shader->flags & SHADER_PORTAL_CAPTURE ) {
		captureTexture = nullptr;
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
	}

	int x = 0, y = 0;
	int w = rn.refdef.width;
	int h = rn.refdef.height;

	bool refraction = false;
	cplane_t *const portal_plane = &portalSurface->plane;
	const float dist = PlaneDiff( rn.viewOrigin, portal_plane );
	if( dist <= BACKFACE_EPSILON || !doReflection ) {
		if( !( shader->flags & SHADER_PORTAL_CAPTURE2 ) || !doRefraction ) {
			return;
		}

		// even if we're behind the portal, we still need to capture
		// the second portal image for refraction
		refraction = true;
		captureTexture = nullptr;
		captureTextureId = 1;
		if( dist < 0 ) {
			VectorInverse( portal_plane->normal );
			portal_plane->dist = -portal_plane->dist;
		}
	}

	bool mirror = true; // default to mirror view
	// it is stupid IMO that mirrors require a RT_PORTALSURFACE entity

	// TODO: Shouldn't it be performed upon loading?

	entity_t *best = nullptr;
	float best_d = std::numeric_limits<float>::max();
	const float *const portal_centre = portalSurface->centre;
	const cplane_t *const untransformed_plane = &portalSurface->untransformed_plane;
	for( unsigned i = rsc.numLocalEntities; i < rsc.numEntities; i++ ) {
		entity_t *ent = R_NUM2ENT( i );
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

	if( !best ) {
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

	const int prevRenderFlags = rn.renderFlags;
	const bool prevFlipped = ( rn.refdef.rdflags & RDF_FLIPPED ) != 0;
	if( !R_PushRefInst() ) {
		return;
	}

	vec3_t viewerOrigin;
	VectorCopy( rn.viewOrigin, viewerOrigin );
	if( prevFlipped ) {
		VectorInverse( &rn.viewAxis[AXIS_RIGHT] );
	}

	Texture *portalTexures[2] { nullptr, nullptr };

setup_and_render:

	vec3_t origin;
	mat3_t axis;

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
	rn.portalmasklist = nullptr;

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

	renderViewFromThisCamera( &rn.refdef );

	if( doRefraction && !refraction && ( shader->flags & SHADER_PORTAL_CAPTURE2 ) ) {
		rn.renderFlags = prevRenderFlags;
		refraction = true;
		captureTexture = nullptr;
		captureTextureId = 1;
		goto setup_and_render;
	}

done:
	portalSurface->texures[0] = portalTexures[0];
	portalSurface->texures[1] = portalTexures[1];

	R_PopRefInst();
}

void Frontend::drawPortalsDepthMask() {
	// TODO: This should be lifted to the caller for clarity
	if( !rn.portalmasklist || !rn.portalmasklist->numDrawSurfs ) {
		return;
	}

	float depthmin, depthmax;
	RB_GetDepthRange( &depthmin, &depthmax );

	RB_ClearDepth( depthmin );
	RB_Clear( GL_DEPTH_BUFFER_BIT, 0, 0, 0, 0 );
	RB_SetShaderStateMask( ~0, GLSTATE_DEPTHWRITE | GLSTATE_DEPTHFUNC_GT | GLSTATE_NO_COLORWRITE );
	RB_DepthRange( depthmax, depthmax );

	submitSortedSurfacesToBackend( rn.portalmasklist );

	RB_DepthRange( depthmin, depthmax );
	RB_ClearDepth( depthmax );
	RB_SetShaderStateMask( ~0, 0 );
}

void Frontend::drawSkyPortal( const entity_t *e, skyportal_t *skyportal ) {
	if( !R_PushRefInst() ) {
		return;
	}

	rn.renderFlags = ( rn.renderFlags | RF_PORTALVIEW );
	//rn.renderFlags &= ~RF_SOFT_PARTICLES;
	VectorCopy( skyportal->vieworg, rn.pvsOrigin );

	rn.farClip = R_DefaultFarClip();

	rn.clipFlags = 15;
	rn.meshlist = &r_skyportallist;
	rn.portalmasklist = nullptr;
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

	renderViewFromThisCamera( &rn.refdef );

	// restore modelview and projection matrices, scissoring, etc for the main view
	R_PopRefInst();
}

void Frontend::drawPortals() {
	// TODO: These conditions should be lifted to the caller for clarity
	if( rf.viewcluster != -1 ) {
		if( !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW | RF_SHADOWMAPVIEW ) ) ) {
			drawPortalsDepthMask();

			// render skyportal
			if( rn.skyportalSurface ) {
				const portalSurface_t *ps = rn.skyportalSurface;
				drawSkyPortal( ps->entity, ps->skyPortal );
			}

			// render regular portals
			for( unsigned i = 0; i < rn.numPortalSurfaces; i++ ) {
				portalSurface_t ps = rn.portalSurfaces[i];
				if( !ps.skyPortal ) {
					drawPortalSurface( &ps );
					rn.portalSurfaces[i] = ps;
				}
			}
		}
	}
}

void Frontend::clearScene() {
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
	rsc.polyent->model = nullptr;
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
}

void Frontend::addEntityToScene( const entity_t *ent ) {
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
			de->model = nullptr;
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

void Frontend::addPolyToScene( const poly_t *poly ) {
	assert( sizeof( *poly->elems ) == sizeof( elem_t ) );

	if( ( rsc.numPolys < MAX_POLYS ) && poly && poly->numverts ) {
		drawSurfacePoly_t *dp = &rsc.polys[rsc.numPolys];

		assert( poly->shader );
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

void Frontend::addLightStyleToScene( int style, float r, float g, float b ) {
	if( style < 0 || style >= MAX_LIGHTSTYLES ) {
		Com_Error( ERR_DROP, "R_AddLightStyleToScene: bad light style %i", style );
		return;
	}

	lightstyle_t *const ls = &rsc.lightStyles[style];
	ls->rgb[0] = std::max( 0.0f, r );
	ls->rgb[1] = std::max( 0.0f, g );
	ls->rgb[2] = std::max( 0.0f, b );
}

static SingletonHolder<Frontend> sceneInstanceHolder;

void Frontend::init() {
	sceneInstanceHolder.Init();
}

void Frontend::shutdown() {
	sceneInstanceHolder.Shutdown();
}

Frontend *Frontend::instance() {
	return sceneInstanceHolder.Instance();
}

void Frontend::initVolatileAssets() {
	//m_coronaShader = MaterialCache::instance()->loadDefaultMaterial( "$corona"_asView, SHADER_TYPE_CORONA );
}

void Frontend::destroyVolatileAssets() {
	//m_coronaShader = nullptr;
}

void Frontend::addLight( const vec3_t origin, float programIntensity, float coronaIntensity, float r, float g, float b ) {
}

void Frontend::renderScene( const refdef_s *fd ) {
	R_Set2DMode( false );

	RB_SetTime( fd->time );

	if( !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		rsc.refdef = *fd;
	}

	rn.refdef = *fd;
	if( !rn.refdef.minLight ) {
		rn.refdef.minLight = 0.1f;
	}

	fd = &rn.refdef;

	rn.renderFlags = RF_NONE;

	rn.farClip = R_DefaultFarClip();
	rn.clipFlags = 15;
	if( rsh.worldModel && !( fd->rdflags & RDF_NOWORLDMODEL ) && rsh.worldBrushModel->globalfog ) {
		rn.clipFlags |= 16;
	}

	rn.meshlist = &r_worldlist;
	rn.portalmasklist = &r_portalmasklist;
	rn.dlightBits = 0;

	rn.renderTarget = 0;
	rn.multisampleDepthResolved = false;

	// clip new scissor region to the one currently set
	Vector4Set( rn.scissor, fd->scissor_x, fd->scissor_y, fd->scissor_width, fd->scissor_height );
	Vector4Set( rn.viewport, fd->x, fd->y, fd->width, fd->height );
	VectorCopy( fd->vieworg, rn.pvsOrigin );
	VectorCopy( fd->vieworg, rn.lodOrigin );

	R_BindFrameBufferObject( 0 );

	renderViewFromThisCamera( fd );

	R_RenderDebugSurface( fd );

	R_BindFrameBufferObject( 0 );

	R_Set2DMode( true );
}

void Frontend::dynLightDirForOrigin( const vec_t *origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal ) {
}

}