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

static void R_TransformForWorld( void ) {
	RB_LoadObjectMatrix( mat4x4_identity );
}

static void R_TranslateForEntity( const entity_t *e ) {
	mat4_t objectMatrix;

	Matrix4_Identity( objectMatrix );

	objectMatrix[0] = e->scale;
	objectMatrix[5] = e->scale;
	objectMatrix[10] = e->scale;
	objectMatrix[12] = e->origin[0];
	objectMatrix[13] = e->origin[1];
	objectMatrix[14] = e->origin[2];

	RB_LoadObjectMatrix( objectMatrix );
}

static void R_TransformForEntity( const entity_t *e ) {
	assert( e->rtype == RT_MODEL && e->number != kWorldEntNumber );

	mat4_t objectMatrix;

	if( e->scale != 1.0f ) {
		objectMatrix[0] = e->axis[0] * e->scale;
		objectMatrix[1] = e->axis[1] * e->scale;
		objectMatrix[2] = e->axis[2] * e->scale;
		objectMatrix[4] = e->axis[3] * e->scale;
		objectMatrix[5] = e->axis[4] * e->scale;
		objectMatrix[6] = e->axis[5] * e->scale;
		objectMatrix[8] = e->axis[6] * e->scale;
		objectMatrix[9] = e->axis[7] * e->scale;
		objectMatrix[10] = e->axis[8] * e->scale;
	} else {
		objectMatrix[0] = e->axis[0];
		objectMatrix[1] = e->axis[1];
		objectMatrix[2] = e->axis[2];
		objectMatrix[4] = e->axis[3];
		objectMatrix[5] = e->axis[4];
		objectMatrix[6] = e->axis[5];
		objectMatrix[8] = e->axis[6];
		objectMatrix[9] = e->axis[7];
		objectMatrix[10] = e->axis[8];
	}

	objectMatrix[3] = 0;
	objectMatrix[7] = 0;
	objectMatrix[11] = 0;
	objectMatrix[12] = e->origin[0];
	objectMatrix[13] = e->origin[1];
	objectMatrix[14] = e->origin[2];
	objectMatrix[15] = 1.0;

	RB_LoadObjectMatrix( objectMatrix );
}

/*
* R_Set2DMode
*
* Note that this sets the viewport to size of the active framebuffer.
*/
void R_Set2DMode( bool enable ) {
	wsw::ref::Frontend::instance()->set2DMode( enable );
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
	if( shader != nullptr ) {
		order = R_PackShaderOrder( shader );
	}
	// group by dlight
	if( dlight ) {
		order |= 0x40;
	}
	if( fog != nullptr ) {
		order |= 0x80;
	}
	// group by lightmaps
	order |= ( (MAX_LIGHTMAPS - numLightmaps) << 10 );

	return order;
}

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

namespace wsw::ref {

auto Frontend::getDefaultFarClip() const -> float {
	float dist;

	if( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) {
		dist = 1024;
	} else if( rsh.worldModel && rsh.worldBrushModel->globalfog ) {
		dist = rsh.worldBrushModel->globalfog->shader->fog_dist;
	} else {
		// TODO: Restore computations of world bounds
		dist = (float)( 1 << 16 );
	}

	return std::max( Z_NEAR, dist ) + Z_BIAS;
}

auto Frontend::getFogForBounds( const float *mins, const float *maxs ) -> mfog_t * {
	if( !rsh.worldModel || ( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) || !rsh.worldBrushModel->numfogs ) {
		return nullptr;
	}
	if( m_state.renderFlags & RF_SHADOWMAPVIEW ) {
		return nullptr;
	}
	if( rsh.worldBrushModel->globalfog ) {
		return rsh.worldBrushModel->globalfog;
	}

	for( unsigned i = 0; i < rsh.worldBrushModel->numfogs; i++ ) {
		mfog_t *const fog = rsh.worldBrushModel->fogs;
		if( fog->shader ) {
			if( BoundsIntersect( mins, maxs, fog->mins, fog->maxs ) ) {
				return fog;
			}
		}
	}

	return nullptr;
}

auto Frontend::getFogForSphere( const vec3_t centre, const float radius ) -> mfog_t * {
	vec3_t mins, maxs;
	for( unsigned i = 0; i < 3; i++ ) {
		mins[i] = centre[i] - radius;
		maxs[i] = centre[i] + radius;
	}
	return getFogForBounds( mins, maxs );
}

bool Frontend::isPointCompletelyFogged( const mfog_t *fog, const float *origin, float radius ) {
	// note that fog->distanceToEye < 0 is always true if
	// globalfog is not nullptr and we're inside the world boundaries
	if( fog && fog->shader && fog == m_state.fog_eye ) {
		float vpnDist = (( m_state.viewOrigin[0] - origin[0] ) * m_state.viewAxis[AXIS_FORWARD + 0] +
						 ( m_state.viewOrigin[1] - origin[1] ) * m_state.viewAxis[AXIS_FORWARD + 1] +
						 ( m_state.viewOrigin[2] - origin[2] ) * m_state.viewAxis[AXIS_FORWARD + 2] );
		return ( ( vpnDist + radius ) / fog->shader->fog_dist ) < -1;
	}

	return false;
}

void Frontend::bindFrameBuffer( int ) {
	const int width = glConfig.width;
	const int height = glConfig.height;

	rf.frameBufferWidth = width;
	rf.frameBufferHeight = height;

	RB_BindFrameBufferObject();

	RB_Viewport( m_state.viewport[0], m_state.viewport[1], m_state.viewport[2], m_state.viewport[3] );
	RB_Scissor( m_state.scissor[0], m_state.scissor[1], m_state.scissor[2], m_state.scissor[3] );
}

void Frontend::set2DMode( bool enable ) {
	const int width = rf.frameBufferWidth;
	const int height = rf.frameBufferHeight;

	if( rf.in2D == true && enable == true && width == rf.width2D && height == rf.height2D ) {
		return;
	} else if( rf.in2D == false && enable == false ) {
		return;
	}

	rf.in2D = enable;

	if( enable ) {
		rf.width2D = width;
		rf.height2D = height;

		Matrix4_OrthogonalProjection( 0, width, height, 0, -99999, 99999, m_state.projectionMatrix );
		Matrix4_Copy( m_state.projectionMatrix, m_state.cameraProjectionMatrix );

		// set 2D virtual screen size
		RB_Scissor( 0, 0, width, height );
		RB_Viewport( 0, 0, width, height );

		RB_LoadProjectionMatrix( m_state.projectionMatrix );
		RB_LoadCameraMatrix( mat4x4_identity );
		RB_LoadObjectMatrix( mat4x4_identity );

		RB_SetShaderStateMask( ~0, GLSTATE_NO_DEPTH_TEST );

		RB_SetRenderFlags( 0 );
	} else {
		// render previously batched 2D geometry, if any
		RB_FlushDynamicMeshes();

		RB_SetShaderStateMask( ~0, 0 );
	}
}

auto Frontend::tryAddingPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) -> portalSurface_t * {
	if( shader ) {
		if( m_state.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
			const bool depthPortal = !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) );
			if( !R_FASTSKY() || !depthPortal ) {
				portalSurface_t *portalSurface = &m_state.portalSurfaces[m_state.numPortalSurfaces++];
				memset( portalSurface, 0, sizeof( portalSurface_t ) );
				portalSurface->entity = ent;
				portalSurface->shader = shader;
				portalSurface->skyPortal = nullptr;
				ClearBounds( portalSurface->mins, portalSurface->maxs );
				memset( portalSurface->texures, 0, sizeof( portalSurface->texures ) );

				if( depthPortal ) {
					m_state.numDepthPortalSurfaces++;
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

		VectorNegate( &m_state.viewAxis[AXIS_FORWARD], plane.normal );
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
	if(( dist = PlaneDiff( m_state.viewOrigin, &plane ) ) <= BACKFACE_EPSILON ) {
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
			portalSurface_t *const portalSurface = m_state.portalSurfaces + portalNum;
			vec3_t center;
			if( origin ) {
				VectorCopy( origin, center );
			} else {
				VectorAdd( surf->mins, surf->maxs, center );
				VectorScale( center, 0.5, center );
			}
			float dist = Distance( m_state.refdef.vieworg, center );
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
	/* ST_EXTERNAL_MESH */
	( drawSurf_cb ) &R_SubmitExternalMeshToBackend,
	/* ST_SPRITE */
	nullptr,
	/* ST_POLY */
	nullptr,
	/* ST_PARTICLE */
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
	/* ST_EXTERNAL_MESH */
	nullptr,
	/* ST_SPRITE */
	( batchDrawSurf_cb ) & R_SubmitSpriteSurfToBackend,
	/* ST_POLY */
	( batchDrawSurf_cb ) & R_SubmitPolySurfToBackend,
	/* ST_PARTICLE */
	( batchDrawSurf_cb ) & R_SubmitParticleSurfToBackend,
	/* ST_CORONA */
	( batchDrawSurf_cb ) & R_SubmitCoronaSurfToBackend,
	/* ST_nullptrMODEL */
	nullptr,
	};

static drawSurfaceType_t spriteDrawSurf = ST_SPRITE;

void Frontend::addAliasModelEntitiesToSortList( const entity_t *aliasModelEntities,
												std::span<VisTestedModel> visibleModels ) {
	for( const VisTestedModel &visTestedModel: visibleModels ) {
		const entity_t *const __restrict entity = aliasModelEntities + visTestedModel.indexInEntitiesGroup;

		float distance;
		// make sure weapon model is always closest to the viewer
		if( entity->renderfx & RF_WEAPONMODEL ) {
			distance = 0;
		} else {
			distance = Distance( entity->origin, m_state.viewOrigin ) + 1;
		}

		const mfog_t *const fog = getFogForBounds( visTestedModel.absMins, visTestedModel.absMaxs );

		const auto *const aliasmodel = (const maliasmodel_t *)visTestedModel.selectedLod->extradata;
		for( int meshNum = 0; meshNum < aliasmodel->nummeshes; meshNum++ ) {
			const maliasmesh_t *mesh = &aliasmodel->meshes[meshNum];
			const shader_t *shader = nullptr;

			if( entity->customSkin ) {
				shader = R_FindShaderForSkinFile( entity->customSkin, mesh->name );
			} else if( entity->customShader ) {
				shader = entity->customShader;
			} else if( mesh->numskins ) {
				for( int skinNum = 0; skinNum < mesh->numskins; skinNum++ ) {
					shader = mesh->skins[skinNum].shader;
					if( shader ) {
						void *drawSurf = aliasmodel->drawSurfs + meshNum;
						const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
						addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, drawSurf );
					}
				}
				continue;
			}

			if( shader ) {
				const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
				addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, aliasmodel->drawSurfs + meshNum );
			}
		}
	}
}

void Frontend::addSkeletalModelEntitiesToSortList( const entity_t *skeletalModelEntities,
												   std::span<VisTestedModel> visibleModels ) {
	for( const VisTestedModel &visTestedModel: visibleModels ) {
		const entity_t *const __restrict entity = skeletalModelEntities + visTestedModel.indexInEntitiesGroup;

		float distance;
		// make sure weapon model is always closest to the viewer
		if( entity->renderfx & RF_WEAPONMODEL ) {
			distance = 0;
		} else {
			distance = Distance( entity->origin, m_state.viewOrigin ) + 1;
		}

		const mfog_t *const fog = getFogForBounds( visTestedModel.absMins, visTestedModel.absMaxs );

		const model_t *const mod = visTestedModel.selectedLod;
		R_AddSkeletalModelCache( entity, mod );

		const auto *const skmodel = ( const mskmodel_t * )mod->extradata;
		for( unsigned meshNum = 0; meshNum < skmodel->nummeshes; meshNum++ ) {
			const mskmesh_t *const mesh = &skmodel->meshes[meshNum];
			shader_t *shader = nullptr;

			if( entity->customSkin ) {
				shader = R_FindShaderForSkinFile( entity->customSkin, mesh->name );
			} else if( entity->customShader ) {
				shader = entity->customShader;
			} else {
				shader = mesh->skin.shader;
			}

			if( shader ) {
				void *drawSurf = skmodel->drawSurfs + meshNum;
				const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
				addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, drawSurf );
			}
		}
	}
}

static int nullDrawSurf = ST_NULLMODEL;

void Frontend::addNullModelEntitiesToSortList( const entity_t *nullModelEntities, std::span<const uint16_t> indices ) {
	for( const auto index: indices ) {
		(void)addEntryToSortList( nullModelEntities + index, nullptr, rsh.whiteShader, 0, 0, nullptr, &nullDrawSurf );
	}
}

void Frontend::addBrushModelEntitiesToSortList( const entity_t *brushModelEntities, std::span<const uint16_t> indices,
												std::span<const Scene::DynamicLight> lights ) {
	drawSurfaceBSP_t *const mergedSurfaces = rsh.worldBrushModel->drawSurfaces;
	msurface_s *const surfaces = rsh.worldBrushModel->surfaces;

	for( const auto index: indices ) {
		const entity_t *const __restrict entity = brushModelEntities + index;
		const model_t *const model = entity->model;
		const auto *const brushModel = ( mbrushmodel_t * )model->extradata;
		assert( brushModel->numModelDrawSurfaces );

		vec3_t origin;
		VectorAdd( entity->model->mins, entity->model->maxs, origin );
		VectorMA( entity->origin, 0.5, origin, origin );

		for( unsigned i = 0; i < brushModel->numModelDrawSurfaces; i++ ) {
			const unsigned surfNum = brushModel->firstModelDrawSurface + i;
			drawSurfaceBSP_t *const mergedSurface = mergedSurfaces + surfNum;
			msurface_t *const firstVisSurface = surfaces + mergedSurface->firstWorldSurface;
			msurface_t *const lastVisSurface = firstVisSurface + mergedSurface->numWorldSurfaces - 1;
			addMergedBspSurfToSortList( entity, mergedSurface, firstVisSurface, lastVisSurface, origin, lights );
		}
	}
}

void Frontend::addSpriteEntitiesToSortList( const entity_t *spriteEntities, std::span<const uint16_t> indices ) {
	for( const unsigned index: indices ) {
		const entity_t *const __restrict entity = spriteEntities + index;

		vec3_t eyeToSprite;
		VectorSubtract( entity->origin, m_state.refdef.vieworg, eyeToSprite );
		if( const float dist = DotProduct( eyeToSprite, &m_state.viewAxis[0] ); dist > 0 ) [[likely]] {
			const mfog_t *const fog = getFogForSphere( entity->origin, entity->radius );
			addEntryToSortList( entity, fog, entity->customShader, dist, 0, nullptr, &spriteDrawSurf );
		}
	}
}

[[nodiscard]]
static inline bool doOverlapTestFor14Dops( const float *mins1, const float *maxs1, const float *mins2, const float *maxs2 ) {
	// TODO: Inline into call sites/reduce redundant loads

	const __m128 xmmMins1_03 = _mm_loadu_ps( mins1 + 0 ), xmmMaxs1_03 = _mm_loadu_ps( maxs1 + 0 );
	const __m128 xmmMins2_03 = _mm_loadu_ps( mins2 + 0 ), xmmMaxs2_03 = _mm_loadu_ps( maxs2 + 0 );
	const __m128 xmmMins1_47 = _mm_loadu_ps( mins1 + 4 ), xmmMaxs1_47 = _mm_loadu_ps( maxs1 + 4 );
	const __m128 xmmMins2_47 = _mm_loadu_ps( mins2 + 4 ), xmmMaxs2_47 = _mm_loadu_ps( maxs2 + 4 );

	// Mins1 [0..3] > Maxs2[0..3]
	__m128 cmp1 = _mm_cmpgt_ps( xmmMins1_03, xmmMaxs2_03 );
	// Mins2 [0..3] > Maxs1[0..3]
	__m128 cmp2 = _mm_cmpgt_ps( xmmMins2_03, xmmMaxs1_03 );
	// Mins1 [4..7] > Maxs2[4..7]
	__m128 cmp3 = _mm_cmpgt_ps( xmmMins1_47, xmmMaxs2_47 );
	// Mins2 [4..7] > Maxs1[4..7]
	__m128 cmp4 = _mm_cmpgt_ps( xmmMins2_47, xmmMaxs1_47 );
	// Zero if no comparison was successful
	return _mm_movemask_ps( _mm_or_ps( _mm_or_ps( cmp1, cmp2 ), _mm_or_ps( cmp3, cmp4 ) ) ) == 0;
}

void Frontend::addMergedBspSurfToSortList( const entity_t *entity,
										   drawSurfaceBSP_t *drawSurf,
										   msurface_t *firstVisSurf, msurface_t *lastVisSurf,
										   const float *maybeOrigin,
										   std::span<const Scene::DynamicLight> lightsSpan ) {
	assert( ( drawSurf->visFrame != rf.frameCount ) && "Should not be duplicated for now" );

	portalSurface_t *portalSurface = nullptr;
	const shader_t *shader = drawSurf->shader;
	if( shader->flags & SHADER_PORTAL ) {
		portalSurface = tryAddingPortalSurface( entity, shader, drawSurf );
	}

	const mfog_t *fog = drawSurf->fog;
	const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, drawSurf->numLightmaps, false );

	drawSurf->dlightBits = 0;
	drawSurf->visFrame = rf.frameCount;
	drawSurf->listSurf = addEntryToSortList( entity, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface, drawSurf );
	if( !drawSurf->listSurf ) {
		return;
	}

	if( portalSurface && !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) ) {
		//addEntryToSortList( m_state.portalmasklist, e, nullptr, nullptr, 0, 0, nullptr, drawSurf );
	}

	float resultDist = 0;
	if( drawSurf->shader->flags & ( SHADER_PORTAL ) ) [[unlikely]] {
		for( msurface_s *surf = firstVisSurf; surf <= lastVisSurf; ++surf ) {
			if( const auto maybeDistance = tryUpdatingPortalSurfaceAndDistance( drawSurf, surf, maybeOrigin ) ) {
				resultDist = std::max( resultDist, *maybeDistance );
			}
		}
	}

	unsigned dlightBits = 0;
	if( m_numVisibleProgramLights ) {
		const Scene::DynamicLight *const lights = lightsSpan.data();
		const unsigned *const surfaceDlightBits = m_leafLightBitsOfSurfacesHolder.data.get();
		const msurface_t *const worldSurfaces = rsh.worldBrushModel->surfaces;
		const unsigned numLights = lightsSpan.size();

		msurface_t *surf = firstVisSurf;
		do {
			// Coarse bits are combined light bits of leaves.
			// Check against individual surfaces if a coarse bit is set.
			// TODO: Iterate over all bits in the outer loop?
			// TODO: Think of using SIMD-friendly indices + left-packing instead of integer bits
			if( const unsigned coarseBits = surfaceDlightBits[surf - worldSurfaces] ) {
				assert( numLights );
				unsigned lightNum = 0;
				unsigned testedBits = 0;
				do {
					const unsigned lightBit = 1u << lightNum;
					testedBits |= lightBit;
					if( coarseBits & lightBit ) [[likely]] {
						const auto *lightDop = &m_lightBoundingDops[lightNum];
						if( doOverlapTestFor14Dops( lightDop->mins, lightDop->maxs, surf->mins, surf->maxs ) ) {
							dlightBits |= lightBit;
						}
					}
				} while( ++lightNum < numLights && testedBits != coarseBits );
			}
			// TODO: Exclude culled/occluded surfaces in range [firstVisSurf, lastVisSurf] from light bits computation
			// TODO: Compute an accumulative table of light bits of visible surfaces for merged surfaces
		} while( ++surf < lastVisSurf + 1 );
	}

	drawSurf->firstSpanVert = firstVisSurf->firstDrawSurfVert;
	drawSurf->firstSpanElem = firstVisSurf->firstDrawSurfElem;
	drawSurf->numSpanVerts = lastVisSurf->mesh.numVerts + lastVisSurf->firstDrawSurfVert - firstVisSurf->firstDrawSurfVert;
	drawSurf->numSpanElems = lastVisSurf->mesh.numElems + lastVisSurf->firstDrawSurfElem - firstVisSurf->firstDrawSurfElem;

	// update the distance sorting key if it's a portal surface or a normal dlit surface
	if( resultDist != 0 || dlightBits != 0 ) {
		drawSurf->dlightBits = dlightBits;
		const unsigned order = R_PackOpaqueOrder( drawSurf->fog, drawSurf->shader, drawSurf->numLightmaps, false );
		if( resultDist == 0 ) {
			resultDist = WORLDSURF_DIST;
		}
		sortedDrawSurf_t *const sds = (sortedDrawSurf_t *)drawSurf->listSurf;
		sds->distKey = R_PackDistKey( 0, drawSurf->shader, resultDist, order );
	}
}

void Frontend::addParticlesToSortList( const entity_t *particleEntity, const Scene::ParticlesAggregate *particles,
									   std::span<const uint16_t> aggregateIndices ) {
	const float *const __restrict forwardAxis = m_state.viewAxis;
	const float *const __restrict viewOrigin  = m_state.viewOrigin;

	unsigned numParticleDrawSurfaces = 0;
	ParticleDrawSurface *const particleDrawSurfaces  = m_particleDrawSurfaces.get();
	for( const unsigned aggregateIndex: aggregateIndices ) {
		const Scene::ParticlesAggregate *const __restrict pa = particles + aggregateIndex;
		for( unsigned particleIndex = 0; particleIndex < pa->numParticles; ++particleIndex ) {
			const BaseParticle *__restrict particle = pa->particles + particleIndex;

			vec3_t toParticle;
			VectorSubtract( particle->origin, viewOrigin, toParticle );
			const float distanceLike = DotProduct( toParticle, forwardAxis );

			// TODO: Account for fogs
			const mfog_t *fog = nullptr;

			auto *const drawSurf     = &particleDrawSurfaces[numParticleDrawSurfaces++];
			drawSurf->surfType       = ST_PARTICLE;
			drawSurf->aggregateIndex = aggregateIndex;
			drawSurf->particleIndex  = particleIndex;

			// TODO: Inline/add some kind of bulk insertion
			addEntryToSortList( particleEntity, fog, m_particleShader, distanceLike, 0, nullptr, drawSurf );
		}
	}
}

void Frontend::addExternalMeshesToSortList( const entity_t *meshEntity,
											const Scene::ExternalCompoundMesh *meshes,
											std::span<const uint16_t> indicesOfMeshes ) {
	const float *const __restrict viewOrigin  = m_state.viewOrigin;

	unsigned numMeshDrawSurfaces = 0;
	ExternalMeshDrawSurface *const meshDrawSurfaces = m_externalMeshDrawSurfaces;
	for( const unsigned compoundMeshIndex: indicesOfMeshes ) {
		const Scene::ExternalCompoundMesh *const __restrict compoundMesh = meshes + compoundMeshIndex;
		for( size_t partIndex = 0; partIndex < compoundMesh->parts.size(); ++partIndex ) {
			const ExternalMesh &__restrict mesh = compoundMesh->parts[partIndex];

			vec3_t meshCenter;
			VectorAvg( mesh.mins, mesh.maxs, meshCenter );
			// We guess nothing better could be done
			const float distance = DistanceFast( meshCenter, viewOrigin );

			// TODO: Account for fogs
			const mfog_t *fog = nullptr;

			auto *const drawSurf        = &meshDrawSurfaces[numMeshDrawSurfaces++];
			drawSurf->surfType          = ST_EXTERNAL_MESH;
			drawSurf->compoundMeshIndex = compoundMeshIndex;
			drawSurf->partIndex         = partIndex;

			const shader_s *material = mesh.material ? mesh.material : rsh.whiteShader;
			addEntryToSortList( meshEntity, fog, material, distance, 0, nullptr, drawSurf );
		}
	}
}

void Frontend::addCoronaLightsToSortList( const entity_t *polyEntity, const Scene::DynamicLight *lights,
										  std::span<const uint16_t> indices ) {
	const float *const __restrict forwardAxis = m_state.viewAxis;
	const float *const __restrict viewOrigin  = m_state.viewOrigin;

	for( const unsigned index: indices ) {
		const Scene::DynamicLight *light = &lights[index];

		vec3_t toLight;
		VectorSubtract( light->origin, viewOrigin, toLight );
		const float distanceLike = DotProduct( toLight, forwardAxis );

		// TODO: Account for fogs
		const mfog_t *fog = nullptr;
		void *drawSurf = m_coronaDrawSurfaces + index;
		addEntryToSortList( polyEntity, fog, m_coronaShader, distanceLike, 0, nullptr, drawSurf );
	}
}

void *Frontend::addEntryToSortList( const entity_t *e, const mfog_t *fog,
									const shader_t *shader, float dist, unsigned order,
									const portalSurface_t *portalSurf, void *drawSurf ) {
	if( shader ) [[likely]] {
		// TODO: This should be moved to an outer loop
		if( !( m_state.renderFlags & RF_SHADOWMAPVIEW ) || !Shader_ReadDepth( shader ) ) [[likely]] {
			// TODO: This should be moved to an outer loop
			if( !rsh.worldBrushModel ) [[unlikely]] {
				fog = nullptr;
			}

			if( const unsigned distKey = R_PackDistKey( e->renderfx, shader, dist, order ) ) [[likely]] {

				const int fogNum = fog ? (int)( fog - rsh.worldBrushModel->fogs ) : -1;
				const int portalNum = portalSurf ? (int)( portalSurf - m_state.portalSurfaces ) : -1;

				m_state.list->emplace_back( sortedDrawSurf_t {
					.drawSurf = (drawSurfaceType_t *)drawSurf,
					.distKey  = distKey,
					.sortKey  = R_PackSortKey( shader->id, fogNum, portalNum, e->number ),
				});

				return std::addressof( m_state.list->back() );
			}
		}
	}

	return nullptr;
}

void Frontend::collectVisiblePolys( Scene *scene ) {
	auto *const polys = scene->m_polys.data();
	const unsigned numPolys = scene->m_polys.size();
	const auto *polyEntity  = scene->m_polyent;

	for( unsigned i = 0; i < numPolys; i++ ) {
		auto *const p = polys + i;
		mfog_t *fog;
		// TODO: Use a single branch
		if( p->fogNum <= 0 || (unsigned)p->fogNum > rsh.worldBrushModel->numfogs ) {
			fog = nullptr;
		} else {
			fog = rsh.worldBrushModel->fogs + p->fogNum - 1;
		}

		(void)addEntryToSortList( polyEntity, fog, p->shader, 0, i, nullptr, p );
	}
}

void Frontend::collectVisibleEntities( Scene *scene, std::span<const Frustum> frusta ) {
	uint16_t indices[MAX_ENTITIES];
	m_visTestedModelsBuffer.reserve( MAX_ENTITIES );
	VisTestedModel *const visModels = m_visTestedModelsBuffer.data.get();

	const std::span<const entity_t> nullModelEntities = scene->m_nullModelEntities;
	const auto visibleNullModelEntityIndices = cullNullModelEntities( nullModelEntities, &m_frustum, frusta, indices );
	addNullModelEntitiesToSortList( nullModelEntities.data(), visibleNullModelEntityIndices );

	const std::span<const entity_t> aliasModelEntities = scene->m_aliasModelEntities;
	const auto visibleAliasModels = cullAliasModelEntities( aliasModelEntities, &m_frustum, frusta, visModels );
	addAliasModelEntitiesToSortList( aliasModelEntities.data(), visibleAliasModels );

	const std::span<const entity_t> skeletalModelEntities = scene->m_skeletalModelEntities;
	const auto visibleSkeletalModels = cullSkeletalModelEntities( skeletalModelEntities, &m_frustum, frusta, visModels );
	addSkeletalModelEntitiesToSortList( skeletalModelEntities.data(), visibleSkeletalModels );

	const std::span<const entity_t> brushModelEntities = scene->m_brushModelEntities;
	const auto visibleBrushModelEntityIndices = cullBrushModelEntities( brushModelEntities, &m_frustum, frusta, indices );
	std::span<const Scene::DynamicLight> dynamicLights { scene->m_dynamicLights.data(), scene->m_dynamicLights.size() };
	addBrushModelEntitiesToSortList( brushModelEntities.data(), visibleBrushModelEntityIndices, dynamicLights );

	const std::span<const entity_t> spriteEntities = scene->m_spriteEntities;
	const auto visibleSpriteEntityIndices = cullSpriteEntities( spriteEntities, &m_frustum, frusta, indices );
	addSpriteEntitiesToSortList( spriteEntities.data(), visibleSpriteEntityIndices );
}

void Frontend::collectVisibleParticles( Scene *scene, std::span<const Frustum> frusta ) {
	uint16_t tmpIndices[1024];
	const std::span<const Scene::ParticlesAggregate> particleAggregates = scene->m_particles;
	const auto visibleAggregateIndices = cullParticleAggregates( particleAggregates, &m_frustum, frusta, tmpIndices );
	addParticlesToSortList( scene->m_polyent, scene->m_particles.data(), visibleAggregateIndices );
}

void Frontend::collectVisibleExternalMeshes( Scene *scene, std::span<const Frustum> frusta ) {
	uint16_t tmpIndices[256];
	const std::span<const Scene::ExternalCompoundMesh> meshes = scene->m_externalMeshes;
	const auto visibleMeshesIndices = cullExternalMeshes( meshes, &m_frustum, frusta, tmpIndices );
	addExternalMeshesToSortList( scene->m_polyent, scene->m_externalMeshes.data(), visibleMeshesIndices );
}

auto Frontend::collectVisibleLights( Scene *scene, std::span<const Frustum> occluderFrusta )
	-> std::span<const uint16_t> {
	static_assert( decltype( Scene::m_dynamicLights )::capacity() == kMaxLightsInScene );
	uint16_t tmpCoronaLightIndices[kMaxLightsInScene], tmpProgramLightIndices[kMaxLightsInScene];

	const auto [visibleCoronaLightIndices, visibleProgramLightIndices] =
		cullLights( scene->m_dynamicLights, &m_frustum, occluderFrusta, tmpCoronaLightIndices, tmpProgramLightIndices );

	addCoronaLightsToSortList( scene->m_polyent, scene->m_dynamicLights.data(), visibleCoronaLightIndices );

	assert( m_numVisibleProgramLights == 0 );

	if( visibleProgramLightIndices.size() <= kMaxProgramLightsInView ) {
		std::copy( visibleProgramLightIndices.begin(), visibleProgramLightIndices.end(), m_programLightIndices );
		m_numVisibleProgramLights = visibleProgramLightIndices.size();
	} else {
		const float *const __restrict viewOrigin = m_state.viewOrigin;
		const Scene::DynamicLight *const lights = scene->m_dynamicLights.data();
		wsw::StaticVector<std::pair<unsigned, float>, kMaxLightsInScene> lightsHeap;
		const auto cmp = []( const std::pair<unsigned, float> &lhs, const std::pair<unsigned, float> &rhs ) {
			return lhs.second > rhs.second;
		};
		for( const unsigned index: visibleProgramLightIndices ) {
			const Scene::DynamicLight *light = &lights[index];
			const float squareDistance = DistanceSquared( light->origin, viewOrigin );
			const float score = light->programRadius * Q_RSqrt( squareDistance );
			lightsHeap.emplace_back( { index, score } );
			std::push_heap( lightsHeap.begin(), lightsHeap.end(), cmp );
		}
		unsigned numVisibleProgramLights = 0;
		do {
			std::pop_heap( lightsHeap.begin(), lightsHeap.end(), cmp );
			const unsigned lightIndex = lightsHeap.back().first;
			m_programLightIndices[numVisibleProgramLights++] = lightIndex;
			lightsHeap.pop_back();
		} while( numVisibleProgramLights < kMaxProgramLightsInView );
		m_numVisibleProgramLights = numVisibleProgramLights;
	}

	for( unsigned i = 0; i < m_numVisibleProgramLights; ++i ) {
		Scene::DynamicLight *const light = scene->m_dynamicLights.data() + m_programLightIndices[i];
		float *const mins = m_lightBoundingDops[i].mins, *const maxs = m_lightBoundingDops[i].maxs;
		createBounding14DopForSphere( mins, maxs, light->origin, light->programRadius );
	}

	return { m_programLightIndices, m_numVisibleProgramLights };
}

void Frontend::markLightsOfSurfaces( const Scene *scene,
									 std::span<std::span<const unsigned>> spansOfLeaves,
								   	 std::span<const uint16_t> visibleLightIndices ) {
	// TODO: Fuse these calls
	m_leafLightBitsOfSurfacesHolder.reserveZeroed( rsh.worldBrushModel->numModelSurfaces );
	unsigned *const lightBitsOfSurfaces = m_leafLightBitsOfSurfacesHolder.data.get();

	if( !visibleLightIndices.empty() ) {
		for( const std::span<const unsigned> &leaves: spansOfLeaves ) {
			markLightsOfLeaves( scene, leaves, visibleLightIndices, lightBitsOfSurfaces );
		}
	}
}

void Frontend::markLightsOfLeaves( const Scene *scene,
						           std::span<const unsigned> indicesOfLeaves,
						           std::span<const uint16_t> visibleLightIndices,
						           unsigned *leafLightBitsOfSurfaces ) {
	const uint16_t *const lightIndices = visibleLightIndices.data();
	const unsigned numVisibleLights = visibleLightIndices.size();
	const auto leaves = rsh.worldBrushModel->visleafs;

	assert( numVisibleLights && numVisibleLights <= kMaxProgramLightsInView );

	for( const unsigned leafIndex: indicesOfLeaves ) {
		const mleaf_t *const __restrict leaf = leaves[leafIndex];
		unsigned leafLightBits = 0;

		unsigned programLightNum = 0;
		do {
			const auto *lightDop = &m_lightBoundingDops[programLightNum];
			if( doOverlapTestFor14Dops( lightDop->mins, lightDop->maxs, leaf->mins, leaf->maxs ) ) {
				leafLightBits |= ( 1u << programLightNum );
			}
		} while( ++programLightNum < numVisibleLights );

		if( leafLightBits ) [[unlikely]] {
			const unsigned *const leafSurfaceNums = leaf->visSurfaces;
			const unsigned numLeafSurfaces = leaf->numVisSurfaces;
			assert( numLeafSurfaces );
			unsigned surfIndex = 0;
			do {
				const unsigned surfNum = leafSurfaceNums[surfIndex];
				leafLightBitsOfSurfaces[surfNum] |= leafLightBits;
			} while( ++surfIndex < numLeafSurfaces );
		}
	}
}

auto Frontend::cullWorldSurfaces()
	-> std::tuple<std::span<const Frustum>, std::span<const unsigned>, std::span<const unsigned>> {

	m_occludersSelectionFrame++;
	m_occlusionCullingFrame++;

	const unsigned numMergedSurfaces = rsh.worldBrushModel->numDrawSurfaces;
	const unsigned numWorldSurfaces = rsh.worldBrushModel->numModelSurfaces;
	const unsigned numWorldLeaves = rsh.worldBrushModel->numvisleafs;

	// Put the allocation code here, so we don't bloat the arch-specific code
	m_visibleLeavesBuffer.reserve( numWorldLeaves );
	m_visibleOccludersBuffer.reserve( numWorldSurfaces );
	m_occluderPassFullyVisibleLeavesBuffer.reserve( numWorldLeaves );
	m_occluderPassPartiallyVisibleLeavesBuffer.reserve( numWorldLeaves );

	m_drawSurfSurfSpans.reserve( numMergedSurfaces );
	MergedSurfSpan *const mergedSurfSpans = m_drawSurfSurfSpans.data.get();
	for( unsigned i = 0; i < numMergedSurfaces; ++i ) {
		mergedSurfSpans[i].firstSurface = std::numeric_limits<int>::max();
		mergedSurfSpans[i].lastSurface = std::numeric_limits<int>::min();
	}

	// Cull world leaves by the primary frustum
	const std::span<const unsigned> visibleLeaves = collectVisibleWorldLeaves();

	// Collect occluder surfaces of leaves that fall into the primary frustum and that are "good enough"
	const std::span<const SortedOccluder> visibleOccluders  = collectVisibleOccluders( visibleLeaves );
	// Build frusta of occluders, while performing some additional frusta pruning
	const std::span<const Frustum> occluderFrusta = buildFrustaOfOccluders( visibleOccluders );

	std::span<const unsigned> nonOccludedLeaves;
	std::span<const unsigned> partiallyOccludedLeaves;
	if( occluderFrusta.empty() ) {
		// No "good enough" occluders found.
		// Just mark every surface that falls into the primary frustum visible in this case.
		markSurfacesOfLeavesAsVisible( visibleLeaves, mergedSurfSpans );
		nonOccludedLeaves = visibleLeaves;
	} else {
		// Test every leaf that falls into the primary frustum against frusta of occluders
		std::tie( nonOccludedLeaves, partiallyOccludedLeaves ) = cullLeavesByOccluders( visibleLeaves, occluderFrusta );
		markSurfacesOfLeavesAsVisible( nonOccludedLeaves, mergedSurfSpans );
		// Test every surface that belongs to partially occluded leaves
		cullSurfacesInVisLeavesByOccluders( partiallyOccludedLeaves, occluderFrusta, mergedSurfSpans );
	}

	return { occluderFrusta, nonOccludedLeaves, partiallyOccludedLeaves };
}

void Frontend::addVisibleWorldSurfacesToSortList( Scene *scene ) {
	auto *const worldEnt = scene->m_worldent;

	const bool worldOutlines = mapConfig.forceWorldOutlines || ( m_state.refdef.rdflags & RDF_WORLDOUTLINES );
	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		// TODO: Shouldn't it affect culling?
		worldEnt->outlineHeight = std::max( 0.0f, r_outlines_world->value );
	} else {
		worldEnt->outlineHeight = 0;
	}

	Vector4Copy( mapConfig.outlineColor, worldEnt->outlineColor );

	msurface_t *const surfaces = rsh.worldBrushModel->surfaces;
	drawSurfaceBSP_t *const mergedSurfaces = rsh.worldBrushModel->drawSurfaces;
	const MergedSurfSpan *const mergedSurfSpans = m_drawSurfSurfSpans.data.get();
	const auto numWorldModelDrawSurfaces = rsh.worldBrushModel->numModelDrawSurfaces;
	std::span<const Scene::DynamicLight> dynamicLights { scene->m_dynamicLights.data(), scene->m_dynamicLights.size() };
	for( unsigned mergedSurfNum = 0; mergedSurfNum < numWorldModelDrawSurfaces; ++mergedSurfNum ) {
		const MergedSurfSpan &surfSpan = mergedSurfSpans[mergedSurfNum];
		if( surfSpan.firstSurface <= surfSpan.lastSurface ) {
			drawSurfaceBSP_t *const mergedSurf = mergedSurfaces + mergedSurfNum;
			msurface_t *const firstVisSurf = surfaces + surfSpan.firstSurface;
			msurface_t *const lastVisSurf = surfaces + surfSpan.lastSurface;
			addMergedBspSurfToSortList( worldEnt, mergedSurf, firstVisSurf, lastVisSurf, nullptr, dynamicLights );
		}
	}
}

void Frontend::submitSortedSurfacesToBackend( Scene *scene ) {
	const auto *list = m_state.list;
	if( list->empty() ) {
		return;
	}

	FrontendToBackendShared fsh;
	fsh.renderFlags          = m_state.renderFlags;
	fsh.dynamicLights        = scene->m_dynamicLights.data();
	fsh.programLightIndices  = m_programLightIndices;
	fsh.numProgramLights     = m_numVisibleProgramLights;
	fsh.particleAggregates   = scene->m_particles.data();
	fsh.coronaDrawSurfaces   = m_coronaDrawSurfaces;
	fsh.compoundMeshes       = scene->m_externalMeshes.data();
	std::memcpy( fsh.viewAxis, m_state.viewAxis, sizeof( mat3_t ) );

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

	const size_t numDrawSurfs = list->size();
	const sortedDrawSurf_t *const drawSurfs = list->data();
	for( size_t i = 0; i < numDrawSurfs; i++ ) {
		const sortedDrawSurf_t *sds = drawSurfs + i;
		const unsigned sortKey = sds->sortKey;
		const int drawSurfType = *(int *)sds->drawSurf;

		assert( drawSurfType > ST_NONE && drawSurfType < ST_MAX_TYPES );

		batchDrawSurf = ( r_batchDrawSurfCb[drawSurfType] ? true : false );

		unsigned shaderNum, entNum;
		int fogNum, portalNum;
		// decode draw surface properties
		R_UnpackSortKey( sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

		const shader_t *shader = MaterialCache::instance()->getMaterialById( shaderNum );
		const entity_t *entity = scene->m_entities[entNum];
		const mfog_t *fog = fogNum >= 0 ? rsh.worldBrushModel->fogs + fogNum : nullptr;
		const portalSurface_t *portalSurface = portalNum >= 0 ? m_state.portalSurfaces + portalNum : nullptr;
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
					Matrix4_Copy( m_state.projectionMatrix, projectionMatrix );
					Matrix4_PerspectiveProjectionToInfinity( Z_NEAR, projectionMatrix, glConfig.depthEpsilon );
					RB_LoadProjectionMatrix( projectionMatrix );
				} else {
					RB_LoadProjectionMatrix( m_state.projectionMatrix );
				}
			}

			if( batchDrawSurf ) {
				// don't transform batched surfaces
				if( !prevBatchDrawSurf ) {
					RB_LoadObjectMatrix( mat4x4_identity );
				}
			} else {
				if( ( entNum != prevEntNum ) || prevBatchDrawSurf ) {
					if( entity->number == kWorldEntNumber ) [[likely]] {
						R_TransformForWorld();
					} else if( entity->rtype == RT_MODEL ) {
						R_TransformForEntity( entity );
					} else if( shader->flags & SHADER_AUTOSPRITE ) {
						R_TranslateForEntity( entity );
					} else {
						R_TransformForWorld();
					}
				}
			}

			if( !batchDrawSurf ) {
				assert( r_drawSurfCb[drawSurfType] );

				RB_BindShader( entity, shader, fog );
				RB_SetPortalSurface( portalSurface );

				r_drawSurfCb[drawSurfType]( &fsh, entity, shader, fog, portalSurface, 0, sds->drawSurf );
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
			r_batchDrawSurfCb[drawSurfType]( &fsh, entity, shader, fog, portalSurface, 0, sds->drawSurf );
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

void Frontend::setupViewMatrices() {
	refdef_t *rd = &m_state.refdef;

	Matrix4_Modelview( rd->vieworg, rd->viewaxis, m_state.cameraMatrix );
	//Com_Printf( "RD vieworg: %f %f %f\n", rd->vieworg[0], rd->vieworg[1], rd->vieworg[2] );

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthogonalProjection( -rd->ortho_x, rd->ortho_x, -rd->ortho_y, rd->ortho_y,
									  -m_state.farClip, m_state.farClip, m_state.projectionMatrix );
	} else {
		Matrix4_PerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, m_state.farClip, m_state.projectionMatrix );
	}

	if( rd->rdflags & RDF_FLIPPED ) {
		m_state.projectionMatrix[0] = -m_state.projectionMatrix[0];
		m_state.renderFlags |= RF_FLIPFRONTFACE;
	}

	Matrix4_Multiply( m_state.projectionMatrix, m_state.cameraMatrix, m_state.cameraProjectionMatrix );
}

void Frontend::clearActiveFrameBuffer() {
	const bool rgbShadow = ( m_state.renderFlags & (RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB ) ) == (RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB );
	const bool depthPortal = ( m_state.renderFlags & (RF_MIRRORVIEW | RF_PORTALVIEW ) ) != 0 && ( m_state.renderFlags & RF_PORTAL_CAPTURE ) == 0;

	bool clearColor = false;
	vec4_t envColor;
	if( rgbShadow ) {
		clearColor = true;
		Vector4Set( envColor, 1, 1, 1, 1 );
	} else if( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) {
		clearColor = m_state.renderTarget != 0;
		Vector4Set( envColor, 1, 1, 1, 0 );
	} else {
		clearColor = !m_state.numDepthPortalSurfaces || R_FASTSKY();
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

void Frontend::renderViewFromThisCamera( Scene *scene, const refdef_t *fd ) {
	const bool shadowMap = m_state.renderFlags & RF_SHADOWMAPVIEW ? true : false;

	m_state.refdef = *fd;

	// load view matrices with default far clip value
	setupViewMatrices();

	m_state.fog_eye = nullptr;
	m_state.hdrExposure = 1;

	m_state.dlightBits = 0;

	m_state.numPortalSurfaces = 0;
	m_state.numDepthPortalSurfaces = 0;
	m_state.skyportalSurface = nullptr;

	if( r_novis->integer ) {
		m_state.renderFlags |= RF_NOVIS;
	}

	if( r_lightmap->integer ) {
		m_state.renderFlags |= RF_LIGHTMAP;
	}

	if( r_drawflat->integer ) {
		m_state.renderFlags |= RF_DRAWFLAT;
	}

	m_state.list->clear();
	if( rsh.worldBrushModel ) {
		m_state.list->reserve( rsh.worldBrushModel->numDrawSurfaces );
	}

	if( !rsh.worldModel && !( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}

	// build the transformation matrix for the given view angles
	VectorCopy( m_state.refdef.vieworg, m_state.viewOrigin );
	Matrix3_Copy( m_state.refdef.viewaxis, m_state.viewAxis );

	m_state.lod_dist_scale_for_fov = std::tan( m_state.refdef.fov_x * ( M_PI / 180 ) * 0.5f );

	// current viewcluster
	if( !( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		mleaf_t *leaf = Mod_PointInLeaf( m_state.pvsOrigin, rsh.worldModel );
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

	// TODO: This should be a member of m_state
	m_frustum.setupFor4Planes( fd->vieworg, fd->viewaxis, fd->fov_x, fd->fov_y );

	// we know the initial farclip at this point after determining visible world leafs
	// R_DrawEntities can make adjustments as well

	std::span<const Frustum> occluderFrusta;
	std::span<const unsigned> nonOccludedLeaves;
	std::span<const unsigned> partiallyOccludedLeaves;

	m_numVisibleProgramLights = 0;

	bool drawWorld = false;

	if( !shadowMap ) {
		if( !( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
			if( r_drawworld->integer && rsh.worldModel ) {
				drawWorld = true;
				std::tie( occluderFrusta, nonOccludedLeaves, partiallyOccludedLeaves ) = cullWorldSurfaces();
			}
		}

		m_state.fog_eye = getFogForSphere( m_state.viewOrigin, 0.5f );
		m_state.hdrExposure = 1.0f;

		collectVisiblePolys( scene );
	}

	if( const int dynamicLightValue = r_dynamiclight->integer ) {
		[[maybe_unused]] const auto visibleLightIndices = collectVisibleLights( scene, occluderFrusta );
		if( dynamicLightValue & 1 ) {
			std::span<const unsigned> spansStorage[2] { nonOccludedLeaves, partiallyOccludedLeaves };
			std::span<std::span<const unsigned>> spansOfLeaves = { spansStorage, 2 };
			markLightsOfSurfaces( scene, spansOfLeaves, visibleLightIndices );
		}
	}

	if( drawWorld ) {
		// We must know lights at this point
		addVisibleWorldSurfacesToSortList( scene );
	}

	if( r_drawentities->integer ) {
		collectVisibleEntities( scene, occluderFrusta );
		collectVisibleExternalMeshes( scene, occluderFrusta );
	}

	if( !shadowMap ) {
		collectVisibleParticles( scene, occluderFrusta );

		// now set  the real far clip value and reload view matrices
		m_state.farClip = getDefaultFarClip();

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
		std::sort( m_state.list->begin(), m_state.list->end(), cmp );
	}

	bindFrameBuffer( m_state.renderTarget );

	RB_Scissor( m_state.scissor[0], m_state.scissor[1], m_state.scissor[2], m_state.scissor[3] );
	RB_Viewport( m_state.viewport[0], m_state.viewport[1], m_state.viewport[2], m_state.viewport[3] );

	if( m_state.renderFlags & RF_CLIPPLANE ) {
		cplane_t *p = &m_state.clipPlane;
		Matrix4_ObliqueNearClipping( p->normal, -p->dist, m_state.cameraMatrix, m_state.projectionMatrix );
	}

	RB_SetZClip( Z_NEAR, m_state.farClip );
	RB_SetCamera( m_state.viewOrigin, m_state.viewAxis );
	RB_SetLightParams( m_state.refdef.minLight, ( m_state.refdef.rdflags & RDF_NOWORLDMODEL ) != 0, m_state.hdrExposure );
	RB_SetRenderFlags( m_state.renderFlags );
	RB_LoadProjectionMatrix( m_state.projectionMatrix );
	RB_LoadCameraMatrix( m_state.cameraMatrix );
	RB_LoadObjectMatrix( mat4x4_identity );

	if( m_state.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}

	if( ( m_state.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, GLSTATE_NO_COLORWRITE );
	}

	//drawPortals();

	if( r_portalonly->integer && !( m_state.renderFlags & (RF_MIRRORVIEW | RF_PORTALVIEW ) ) ) {
		return;
	}

	clearActiveFrameBuffer();

	submitSortedSurfacesToBackend( scene );

	if( r_showtris->integer && !( m_state.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_EnableWireframe( true );
		submitSortedSurfacesToBackend( scene );
		RB_EnableWireframe( false );
	}

	R_TransformForWorld();

	if( ( m_state.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, 0 );
	}

	if( m_state.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}

	RB_SetShaderStateMask( ~0, GLSTATE_NO_DEPTH_TEST );

	submitDebugStuffToBackend( scene );

	RB_SetShaderStateMask( ~0, 0 );
}

void Frontend::submitDebugStuffToBackend( Scene *scene ) {
	// TODO: Reduce this copying
	vec4_t verts[2];
	byte_vec4_t colors[2] { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
	elem_t elems[2] { 0, 1 };

	mesh_t mesh {};
	mesh.colorsArray[0] = colors;
	mesh.xyzArray = verts;
	mesh.numVerts = 2;
	mesh.numElems = 2;
	mesh.elems = elems;
	verts[0][3] = verts[1][3] = 1.0f;
	for( const DebugLine &line: m_debugLines ) {
		VectorCopy( line.p1, verts[0] );
		VectorCopy( line.p2, verts[1] );
		std::memcpy( colors[0], &line.color, 4 );
		std::memcpy( colors[1], &line.color, 4 );
		RB_AddDynamicMesh( scene->m_worldent, rsh.whiteShader, nullptr, nullptr, 0, &mesh, GL_LINES, 0.0f, 0.0f );
	}

	RB_FlushDynamicMeshes();

	m_debugLines.clear();
}

void Frontend::addDebugLine( const float *p1, const float *p2, int color ) {
	int rgbaColor = color;
	if( !COLOR_A( rgbaColor ) ) {
		rgbaColor = COLOR_RGBA( COLOR_R( color ), COLOR_G( color ), COLOR_B( color ), 255 );
	}
	m_debugLines.emplace_back( DebugLine {
		{ p1[0], p1[1], p1[2] }, { p2[0], p2[1], p2[2] }, rgbaColor
	});
}

auto Frontend::createDrawSceneRequest( const refdef_t &refdef ) -> DrawSceneRequest * {
	R_ClearSkeletalCache();

	assert( m_drawSceneRequestHolder.empty() );
	return new( m_drawSceneRequestHolder.unsafe_grow_back() )DrawSceneRequest( refdef );
}

void Frontend::submitDrawSceneRequest( DrawSceneRequest *request ) {
	assert( request == m_drawSceneRequestHolder.data() );
	renderScene( request, &request->m_refdef );
	m_drawSceneRequestHolder.clear();
}

Frontend::Frontend() {
	std::fill( std::begin( m_coronaDrawSurfaces ), std::end( m_coronaDrawSurfaces ), ST_CORONA );
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
	m_coronaShader = MaterialCache::instance()->loadDefaultMaterial( "$corona"_asView, SHADER_TYPE_CORONA );
	m_particleShader = MaterialCache::instance()->loadDefaultMaterial( "$particle"_asView, SHADER_TYPE_CORONA );
}

void Frontend::destroyVolatileAssets() {
	m_coronaShader = nullptr;
	m_particleShader = nullptr;
}

void Frontend::renderScene( Scene *scene, const refdef_s *fd ) {
	set2DMode( false );

	RB_SetTime( fd->time );

	m_state.refdef = *fd;
	if( !m_state.refdef.minLight ) {
		m_state.refdef.minLight = 0.1f;
	}

	fd = &m_state.refdef;

	m_state.renderFlags = RF_NONE;

	m_state.farClip = getDefaultFarClip();
	m_state.clipFlags = 15;
	if( rsh.worldModel && !( fd->rdflags & RDF_NOWORLDMODEL ) && rsh.worldBrushModel->globalfog ) {
		m_state.clipFlags |= 16;
	}

	m_state.list = &m_meshDrawList;
	m_state.dlightBits = 0;

	m_state.renderTarget = 0;
	m_state.multisampleDepthResolved = false;

	// clip new scissor region to the one currently set
	Vector4Set( m_state.scissor, fd->scissor_x, fd->scissor_y, fd->scissor_width, fd->scissor_height );
	Vector4Set( m_state.viewport, fd->x, fd->y, fd->width, fd->height );
	VectorCopy( fd->vieworg, m_state.pvsOrigin );
	VectorCopy( fd->vieworg, m_state.lodOrigin );

	bindFrameBuffer( 0 );

	renderViewFromThisCamera( scene, fd );

	R_RenderDebugSurface( fd );

	bindFrameBuffer( 0 );

	set2DMode( true );
}

void Frontend::dynLightDirForOrigin( const vec_t *origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal ) {
}

}

Scene::Scene() {
	m_worldent = m_localEntities.unsafe_grow_back();
	memset( m_worldent, 0, sizeof( entity_t ) );
	m_worldent->rtype = RT_MODEL;
	m_worldent->number = 0;
	m_worldent->model = rsh.worldModel;
	m_worldent->scale = 1.0f;
	Matrix3_Identity( m_worldent->axis );
	m_entities.push_back( m_worldent );

	m_polyent = m_localEntities.unsafe_grow_back();
	memset( m_polyent, 0, sizeof( entity_t ) );
	m_polyent->rtype = RT_MODEL;
	m_polyent->number = 1;
	m_polyent->model = nullptr;
	m_polyent->scale = 1.0f;
	Matrix3_Identity( m_polyent->axis );
	m_entities.push_back( m_polyent );
}

void DrawSceneRequest::addEntity( const entity_t *ent ) {
	if( !m_entities.full() && ent ) [[likely]] {
		entity_t *added = nullptr;

		if( ent->rtype == RT_MODEL ) {
			if( const model_t *__restrict model = ent->model ) [[likely]] {
				if( model->type == mod_alias ) {
					m_aliasModelEntities.push_back( *ent );
					added = std::addressof( m_aliasModelEntities.back() );
				} else if( model->type == mod_skeletal ) {
					m_skeletalModelEntities.push_back( *ent );
					added = std::addressof( m_skeletalModelEntities.back() );
				} else if( model->type == mod_brush ) {
					m_brushModelEntities.push_back( *ent );
					added = std::addressof( m_brushModelEntities.back() );
				}
			} else {
				m_nullModelEntities.push_back( *ent );
				added = std::addressof( m_nullModelEntities.back() );
			}
		} else if( ent->rtype == RT_SPRITE ) {
			m_spriteEntities.push_back( *ent );
			added = std::addressof( m_spriteEntities.back() );
			// simplifies further checks
			added->model = nullptr;
		}

		if( added ) {
			if( r_outlines_scale->value <= 0 ) {
				added->outlineHeight = 0;
			}

			if( !r_lerpmodels->integer ) {
				added->backlerp = 0;
			}

			if( added->renderfx & RF_ALPHAHACK ) {
				if( added->shaderRGBA[3] == 255 ) {
					added->renderfx &= ~RF_ALPHAHACK;
				}
			}

			m_entities.push_back( added );
			added->number = m_entities.size() - 1;

			// add invisible fake entity for depth write
			// TODO: This should belong to the CGame code
			if( ( added->renderfx & ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) == ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) {
				entity_t tent = *ent;
				tent.renderfx &= ~RF_ALPHAHACK;
				tent.renderfx |= RF_NOCOLORWRITE | RF_NOSHADOW;
				addEntity( &tent );
			}
		}
	}
}

void DrawSceneRequest::addPoly( const poly_t *poly ) {
	assert( sizeof( *poly->elems ) == sizeof( elem_t ) );

	if( !m_polys.full() && poly && poly->numverts ) [[likely]] {
		auto *dp = m_polys.unsafe_grow_back();

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
			//
			dp->fogNum = -1;

			/*
			 * TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			vec3_t dpmins, dpmaxs;
			ClearBounds( dpmins, dpmaxs );

			for( int i = 0; i < dp->numVerts; i++ ) {
				AddPointToBounds( dp->xyzArray[i], dpmins, dpmaxs );
			}

			mfog_t *const fog = getFogForBounds( dpmins, dpmaxs );
			dp->fogNum = fog ? ( fog - rsh.worldBrushModel->fogs + 1 ) : -1;
			 */
		}
	}
}

void DrawSceneRequest::addLight( const float *origin, float programRadius, float coronaRadius, float r, float g, float b ) {
	assert( ( r >= 0.0f && r >= 0.0f && b >= 0.0f ) && ( r > 0.0f || g > 0.0f || b > 0.0f ) );
	if( !m_dynamicLights.full() ) [[likely]] {
		if( const int cvarValue = r_dynamiclight->integer ) [[likely]] {
			const bool hasProgramLight = programRadius > 0.0f && ( cvarValue & 1 ) != 0;
			const bool hasCoronaLight = coronaRadius > 0.0f && ( cvarValue & 2 ) != 0;
			if( hasProgramLight | hasCoronaLight ) [[likely]] {
				m_dynamicLights.emplace_back( DynamicLight {
					.origin          = { origin[0], origin[1], origin[2] },
					.programRadius   = programRadius,
					.coronaRadius    = coronaRadius,
					.color           = { r, g, b },
					.hasProgramLight = hasProgramLight,
					.hasCoronaLight  = hasCoronaLight
				});
			}
		}
	}
}

void DrawSceneRequest::addParticles( const float *mins, const float *maxs,
									 const BaseParticle *particles, unsigned numParticles ) {
	assert( numParticles <= kMaxParticlesInAggregate );
	if( !m_particles.full() ) [[likely]] {
		m_particles.emplace_back( ParticlesAggregate {
			.mins = { mins[0], mins[1], mins[2], mins[3] },
			.maxs = { maxs[0], maxs[1], maxs[2], maxs[3] },
			.particles = particles, .numParticles = numParticles
		});
	}
}

void DrawSceneRequest::addExternalMesh( const float *mins, const float *maxs, const std::span<const ExternalMesh> parts ) {
	assert( parts.size() <= kMaxPartsInCompoundMesh );
	if( !m_externalMeshes.full() ) [[likely]] {
		m_externalMeshes.emplace_back( ExternalCompoundMesh {
			.mins  = { mins[0], mins[1], mins[2], mins[3] },
			.maxs  = { maxs[0], maxs[1], maxs[2], maxs[3] },
			.parts = parts
		});
	}
}