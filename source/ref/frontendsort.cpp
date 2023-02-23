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

	return ( shaderSort << 26 ) | ( wsw::max( 0x400 - (int)dist, 0 ) << 15 ) | ( order & 0x7FFF );
}

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

namespace wsw::ref {

static drawSurfaceType_t spriteDrawSurf = ST_SPRITE;

void Frontend::addAliasModelEntitiesToSortList( const entity_t *aliasModelEntities,
												std::span<const VisTestedModel> models,
												std::span<const uint16_t> indices ) {
	for( const auto modelIndex: indices ) {
		const VisTestedModel &__restrict visTestedModel = models[modelIndex];
		const entity_t *const __restrict entity          = aliasModelEntities + visTestedModel.indexInEntitiesGroup;

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
						addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, drawSurf, ST_ALIAS );
					}
				}
				continue;
			}

			if( shader ) {
				void *drawSurf = aliasmodel->drawSurfs + meshNum;
				const unsigned drawOrder = R_PackOpaqueOrder( fog, shader, 0, false );
				addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, drawSurf, ST_ALIAS );
			}
		}
	}
}

void Frontend::addSkeletalModelEntitiesToSortList( const entity_t *skeletalModelEntities,
												   std::span<const VisTestedModel> models,
												   std::span<const uint16_t> indices ) {
	for( const auto modelIndex: indices ) {
		const VisTestedModel &__restrict visTestedModel = models[modelIndex];
		const entity_t *const __restrict entity         = skeletalModelEntities + visTestedModel.indexInEntitiesGroup;

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
				addEntryToSortList( entity, fog, shader, distance, drawOrder, nullptr, drawSurf, ST_SKELETAL );
			}
		}
	}
}

void Frontend::addNullModelEntitiesToSortList( const entity_t *nullModelEntities, std::span<const uint16_t> indices ) {
	for( const auto index: indices ) {
		(void)addEntryToSortList( nullModelEntities + index, nullptr, rsh.whiteShader, 0, 0, nullptr, nullptr, ST_NULLMODEL );
	}
}

void Frontend::addBrushModelEntitiesToSortList( const entity_t *brushModelEntities, std::span<const VisTestedModel> models,
												std::span<const uint16_t> indices, std::span<const Scene::DynamicLight> lights ) {
	drawSurfaceBSP_t *const mergedSurfaces = rsh.worldBrushModel->drawSurfaces;
	msurface_s *const surfaces = rsh.worldBrushModel->surfaces;

	for( const auto modelIndex: indices ) {
		const VisTestedModel &visTestedModel = models[modelIndex];
		const auto *const model              = visTestedModel.selectedLod;
		const auto *const entity             = brushModelEntities + visTestedModel.indexInEntitiesGroup;
		const auto *const brushModel         = ( mbrushmodel_t * )model->extradata;
		assert( brushModel->numModelDrawSurfaces );

		vec3_t origin;
		VectorAvg( visTestedModel.absMins, visTestedModel.absMaxs, origin );

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
			addEntryToSortList( entity, fog, entity->customShader, dist, 0, nullptr, &spriteDrawSurf, ST_SPRITE );
		}
	}
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
	drawSurf->listSurf = addEntryToSortList( entity, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface, drawSurf, ST_BSP );
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
				resultDist = wsw::max( resultDist, *maybeDistance );
			}
		}
	}

	unsigned dlightBits = 0;
	if( m_numVisibleProgramLights ) {
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
		const unsigned mergeabilityKey = aggregateIndex;
		for( unsigned particleIndex = 0; particleIndex < pa->numParticles; ++particleIndex ) {
			const Particle *__restrict particle = pa->particles + particleIndex;

			vec3_t toParticle;
			VectorSubtract( particle->origin, viewOrigin, toParticle );
			const float distanceLike = DotProduct( toParticle, forwardAxis );

			// TODO: Account for fogs
			const mfog_t *fog = nullptr;

			auto *const drawSurf     = &particleDrawSurfaces[numParticleDrawSurfaces++];
			drawSurf->aggregateIndex = aggregateIndex;
			drawSurf->particleIndex  = particleIndex;

			shader_s *material = pa->appearanceRules.materials[particle->instanceMaterialIndex];

			// TODO: Inline/add some kind of bulk insertion
			addEntryToSortList( particleEntity, fog, material, distanceLike, 0, nullptr, drawSurf,
								ST_PARTICLE, mergeabilityKey );
		}
	}
}

void Frontend::addDynamicMeshesToSortList( const entity_t *meshEntity, const DynamicMesh **meshes,
										   std::span<const uint16_t> indicesOfMeshes ) {
	const float *const __restrict viewOrigin = m_state.viewOrigin;

	for( const unsigned meshIndex: indicesOfMeshes ) {
		const DynamicMesh *const __restrict mesh = meshes[meshIndex];

		vec3_t meshCenter;
		VectorAvg( mesh->cullMins, mesh->cullMaxs, meshCenter );
		const float distance = DistanceFast( meshCenter, viewOrigin );

		const mfog_t *fog = nullptr;
		const void *drawSurf = mesh;
		const shader_s *material = mesh->material ? mesh->material : rsh.whiteShader;
		addEntryToSortList( meshEntity, fog, material, distance, 0, nullptr, drawSurf, ST_DYNAMIC_MESH );
	}
}

void Frontend::addCompoundDynamicMeshesToSortList( const entity_t *meshEntity,
												   const Scene::CompoundDynamicMesh *meshes,
												   std::span<const uint16_t> indicesOfMeshes ) {
	const float *const __restrict viewOrigin  = m_state.viewOrigin;
	float distances[Scene::kMaxCompoundDynamicMeshes];

	for( const unsigned compoundMeshIndex: indicesOfMeshes ) {
		const Scene::CompoundDynamicMesh *const __restrict compoundMesh = meshes + compoundMeshIndex;

		float bestDistance    = std::numeric_limits<float>::max();
		for( size_t partIndex = 0; partIndex < compoundMesh->numParts; ++partIndex ) {
			const DynamicMesh *const __restrict mesh = compoundMesh->parts[partIndex];
			assert( mesh );

			// This is very incorrect, but still produces satisfiable results
			// with the .useDrawOnTopHack flag set appropriately and with the current appearance of hulls.
			// Order-independent transparency is the proper solution.
			// Splitting the hull in two parts, front and back one is also more correct than the present code,
			// but this would have huge performance impact with the current dynamic submission of vertices.

			vec3_t meshCenter;
			VectorAvg( mesh->cullMins, mesh->cullMaxs, meshCenter );
			const float distance = DistanceFast( meshCenter, viewOrigin );
			distances[partIndex] = distance;
			bestDistance         = wsw::min( distance, bestDistance );
		}

		for( size_t partIndex = 0; partIndex < compoundMesh->numParts; ++partIndex ) {
			const DynamicMesh *const __restrict mesh = compoundMesh->parts[partIndex];
			assert( mesh );

			float distance = distances[partIndex];
			if( std::optional( (uint8_t)partIndex ) == compoundMesh->drawOnTopPartIndex ) [[unlikely]] {
				distance = wsw::max( 0.0f, bestDistance - 1.0f );
			}

			// TODO: Account for fogs
			const mfog_t *fog        = nullptr;
			const void *drawSurf     = mesh;
			const shader_s *material = mesh->material ? mesh->material : rsh.whiteShader;
			addEntryToSortList( meshEntity, fog, material, distance, 0, nullptr, drawSurf, ST_DYNAMIC_MESH );
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
		void *drawSurf = const_cast<Scene::DynamicLight *>( light );
		addEntryToSortList( polyEntity, fog, m_coronaShader, distanceLike, 0, nullptr, drawSurf, ST_CORONA );
	}
}

void Frontend::addVisibleWorldSurfacesToSortList( Scene *scene ) {
	auto *const worldEnt = scene->m_worldent;

	const bool worldOutlines = mapConfig.forceWorldOutlines || ( m_state.refdef.rdflags & RDF_WORLDOUTLINES );
	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		// TODO: Shouldn't it affect culling?
		worldEnt->outlineHeight = wsw::max( 0.0f, r_outlines_world->value );
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

void *Frontend::addEntryToSortList( const entity_t *e, const mfog_t *fog, const shader_t *shader, float dist,
									unsigned order, const portalSurface_t *portalSurf, const void *drawSurf,
									unsigned surfType, unsigned mergeabilitySeparator ) {
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
					.drawSurf              = (drawSurfaceType_t *)drawSurf,
					.distKey               = distKey,
					.sortKey               = R_PackSortKey( shader->id, fogNum, portalNum, e->number ),
					.surfType              = surfType,
					.mergeabilitySeparator = mergeabilitySeparator
				});

				return std::addressof( m_state.list->back() );
			}
		}
	}

	return nullptr;
}

auto Frontend::tryAddingPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) -> portalSurface_t * {
	if( shader ) {
		if( m_state.numPortalSurfaces < MAX_PORTAL_SURFACES ) {
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


}