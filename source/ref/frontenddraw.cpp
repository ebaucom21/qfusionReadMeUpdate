/*
Copyright (C) 2007 Victor Luchits
Copyright (C) 2023 Chasseur de bots

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
#include "materiallocal.h"

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

namespace wsw::ref {

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
		/* ST_QUAD_POLY */
		nullptr,
		/* ST_DYNAMIC_MESH */
		nullptr,
		/* ST_PARTICLE */
		nullptr,
		/* ST_CORONA */
		nullptr,
		/* ST_NULLMODEL */
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
		( batchDrawSurf_cb ) & R_SubmitSpriteSurfsToBackend,
		/* ST_QUAD_POLY */
		( batchDrawSurf_cb ) & R_SubmitQuadPolysToBackend,
		/* ST_DYNAMIC_MESH */
		( batchDrawSurf_cb ) & R_SubmitDynamicMeshesToBackend,
		/* ST_PARTICLE */
		( batchDrawSurf_cb ) & R_SubmitParticleSurfsToBackend,
		/* ST_CORONA */
		( batchDrawSurf_cb ) & R_SubmitCoronaSurfsToBackend,
		/* ST_NULLMODEL */
		nullptr,
	};

void Frontend::submitSortedSurfacesToBackend( Scene *scene ) {
	const auto *list = m_state.list;
	if( list->empty() ) {
		return;
	}

	FrontendToBackendShared fsh;
	fsh.dynamicLights               = scene->m_dynamicLights.data();
	fsh.particleAggregates          = scene->m_particles.data();
	fsh.allVisibleLightIndices      = { m_allVisibleLightIndices, m_numAllVisibleLights };
	fsh.visibleProgramLightIndices  = { m_visibleProgramLightIndices, m_numVisibleProgramLights };
	fsh.renderFlags                 = m_state.renderFlags;
	fsh.fovTangent                  = m_state.lod_dist_scale_for_fov;
	std::memcpy( fsh.viewAxis, m_state.viewAxis, sizeof( mat3_t ) );
	VectorCopy( m_state.viewOrigin, fsh.viewOrigin );

	auto *const materialCache = MaterialCache::instance();

	unsigned prevShaderNum                 = ~0;
	unsigned prevEntNum                    = ~0;
	int prevPortalNum                      = ~0;
	int prevFogNum                         = ~0;
	unsigned prevMergeabilitySeparator     = ~0;
	unsigned prevSurfType                  = ~0;
	bool prevIsDrawSurfBatched             = false;
	const sortedDrawSurf_t *batchSpanBegin = nullptr;

	float depthmin = 0.0f, depthmax = 0.0f;
	bool depthHack = false, cullHack = false;
	bool prevInfiniteProj = false;
	int prevEntityFX = -1;

	const mfog_t *prevFog = nullptr;
	const portalSurface_t *prevPortalSurface = nullptr;

	const size_t numDrawSurfs = list->size();
	const sortedDrawSurf_t *const drawSurfs = list->data();
	for( size_t i = 0; i < numDrawSurfs; i++ ) {
		const sortedDrawSurf_t *sds = drawSurfs + i;
		const unsigned sortKey      = sds->sortKey;
		const unsigned surfType     = sds->surfType;

		assert( surfType > ST_NONE && surfType < ST_MAX_TYPES );

		const bool isDrawSurfBatched = ( r_batchDrawSurfCb[surfType] ? true : false );

		unsigned shaderNum, entNum;
		int fogNum, portalNum;
		// decode draw surface properties
		R_UnpackSortKey( sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

		const shader_t *shader    = materialCache->getMaterialById( shaderNum );
		const entity_t *entity    = scene->m_entities[entNum];
		const mfog_t *fog         = fogNum >= 0 ? rsh.worldBrushModel->fogs + fogNum : nullptr;
		const auto *portalSurface = portalNum >= 0 ? m_state.portalSurfaces + portalNum : nullptr;
		const int entityFX        = entity->renderfx;

		// TODO?
		// const bool depthWrite     = shader->flags & SHADER_DEPTHWRITE ? true : false;

		// see if we need to reset mesh properties in the backend

		// TODO: Use a single 64-bit compound mergeability key

		bool reset = false;
		if( !prevIsDrawSurfBatched ) {
			reset = true;
		} else if( surfType != prevSurfType ) {
			reset = true;
		} else if( shaderNum != prevShaderNum ) {
			reset = true;
		} else if( sds->mergeabilitySeparator != prevMergeabilitySeparator ) {
			reset = true;
		} else if( fogNum != prevFogNum ) {
			reset = true;
		} else if( portalNum != prevPortalNum ) {
			reset = true;
		} else if( entNum != prevEntNum ) {
			reset = true;
		} else if( entityFX != prevEntityFX ) {
			reset = true;
		}

		if( reset ) {
			if( batchSpanBegin ) {
				batchDrawSurf_cb callback            = r_batchDrawSurfCb[batchSpanBegin->surfType];
				const shader_s *prevShader           = materialCache->getMaterialById( prevShaderNum );
				const entity_t *prevEntity           = scene->m_entities[prevEntNum];
				const sortedDrawSurf_t *batchSpanEnd = sds;

				assert( batchSpanEnd > batchSpanBegin );

				RB_FlushDynamicMeshes();
				callback( &fsh, prevEntity, prevShader, prevFog, prevPortalSurface, { batchSpanBegin, batchSpanEnd } );
				RB_FlushDynamicMeshes();
			}

			if( isDrawSurfBatched ) {
				batchSpanBegin = sds;
			} else {
				batchSpanBegin = nullptr;
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

			if( isDrawSurfBatched ) {
				// don't transform batched surfaces
				if( !prevIsDrawSurfBatched ) {
					RB_LoadObjectMatrix( mat4x4_identity );
				}
			} else {
				if( ( entNum != prevEntNum ) || prevIsDrawSurfBatched ) {
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

			if( !isDrawSurfBatched ) {
				assert( r_drawSurfCb[surfType] );

				RB_BindShader( entity, shader, fog );
				RB_SetPortalSurface( portalSurface );

				r_drawSurfCb[surfType]( &fsh, entity, shader, fog, portalSurface, sds->drawSurf );
			}

			prevInfiniteProj = infiniteProj;
		}

		prevShaderNum = shaderNum;
		prevEntNum = entNum;
		prevFogNum = fogNum;
		prevIsDrawSurfBatched = isDrawSurfBatched;
		prevSurfType = surfType;
		prevMergeabilitySeparator = sds->mergeabilitySeparator;
		prevPortalNum = portalNum;
		prevEntityFX = entityFX;
		prevFog = fog;
		prevPortalSurface = portalSurface;
	}

	if( batchSpanBegin ) {
		batchDrawSurf_cb callback            = r_batchDrawSurfCb[batchSpanBegin->surfType];
		const shader_t *prevShader           = materialCache->getMaterialById( prevShaderNum );
		const entity_t *prevEntity           = scene->m_entities[prevEntNum];
		const sortedDrawSurf_t *batchSpanEnd = drawSurfs + numDrawSurfs;

		assert( batchSpanEnd > batchSpanBegin );

		RB_FlushDynamicMeshes();
		callback( &fsh, prevEntity, prevShader, prevFog, prevPortalSurface, { batchSpanBegin, batchSpanEnd } );
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

	m_numAllVisibleLights     = 0;
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

		collectVisiblePolys( scene, occluderFrusta );
	}

	if( const int dynamicLightValue = r_dynamiclight->integer ) {
		[[maybe_unused]]
		const auto [visibleProgramLightIndices, visibleCoronaLightIndices] = collectVisibleLights( scene, occluderFrusta );
		if( dynamicLightValue & 2 ) {
			addCoronaLightsToSortList( scene->m_polyent, scene->m_dynamicLights.data(), visibleCoronaLightIndices );
		}
		if( dynamicLightValue & 1 ) {
			std::span<const unsigned> spansStorage[2] { nonOccludedLeaves, partiallyOccludedLeaves };
			std::span<std::span<const unsigned>> spansOfLeaves = { spansStorage, 2 };
			markLightsOfSurfaces( scene, spansOfLeaves, visibleProgramLightIndices );
		}
	}

	if( drawWorld ) {
		// We must know lights at this point
		addVisibleWorldSurfacesToSortList( scene );
	}

	if( r_drawentities->integer ) {
		collectVisibleEntities( scene, occluderFrusta );
		collectVisibleDynamicMeshes( scene, occluderFrusta );
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

}