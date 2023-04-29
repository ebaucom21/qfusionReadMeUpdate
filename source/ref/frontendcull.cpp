/*
Copyright (C) 2007 Victor Luchits
Copyright (C) 2021 Chasseur de bots

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

#include <algorithm>

void Frustum::setPlaneComponentsAtIndex( unsigned index, const float *n, float d ) {
	const uint32_t blendsForSign[2] { 0, ~( (uint32_t)0 ) };

	const float nX = planeX[index] = n[0];
	const float nY = planeY[index] = n[1];
	const float nZ = planeZ[index] = n[2];

	planeD[index] = d;

	xBlendMasks[index] = blendsForSign[nX < 0];
	yBlendMasks[index] = blendsForSign[nY < 0];
	zBlendMasks[index] = blendsForSign[nZ < 0];
}

void Frustum::fillComponentTails( unsigned indexOfPlaneToReplicate ) {
	// Sanity check
	assert( indexOfPlaneToReplicate >= 3 && indexOfPlaneToReplicate < 8 );
	for( unsigned i = indexOfPlaneToReplicate; i < 8; ++i ) {
		planeX[i] = planeX[indexOfPlaneToReplicate];
		planeY[i] = planeY[indexOfPlaneToReplicate];
		planeZ[i] = planeZ[indexOfPlaneToReplicate];
		planeD[i] = planeD[indexOfPlaneToReplicate];
		xBlendMasks[i] = xBlendMasks[indexOfPlaneToReplicate];
		yBlendMasks[i] = yBlendMasks[indexOfPlaneToReplicate];
		zBlendMasks[i] = zBlendMasks[indexOfPlaneToReplicate];
	}
}

void Frustum::setupFor4Planes( const float *viewOrigin, const mat3_t viewAxis, float fovX, float fovY ) {
	const float *const forward = &viewAxis[AXIS_FORWARD];
	const float *const left    = &viewAxis[AXIS_RIGHT];
	const float *const up      = &viewAxis[AXIS_UP];

	const vec3_t right { -left[0], -left[1], -left[2] };

	const float xRotationAngle = 90.0f - 0.5f * fovX;
	const float yRotationAngle = 90.0f - 0.5f * fovY;

	vec3_t planeNormals[4];
	RotatePointAroundVector( planeNormals[0], up, forward, -xRotationAngle );
	RotatePointAroundVector( planeNormals[1], up, forward, +xRotationAngle );
	RotatePointAroundVector( planeNormals[2], right, forward, +yRotationAngle );
	RotatePointAroundVector( planeNormals[3], right, forward, -yRotationAngle );

	for( unsigned i = 0; i < 4; ++i ) {
		setPlaneComponentsAtIndex( i, planeNormals[i], DotProduct( viewOrigin, planeNormals[i] ) );
	}
}

namespace wsw::ref {

auto Frontend::cullWorldSurfaces( StateForCamera *stateForCamera )
	-> std::tuple<std::span<const Frustum>, std::span<const unsigned>, std::span<const unsigned>> {

	m_occlusionCullingFrame++;

	const unsigned numMergedSurfaces = rsh.worldBrushModel->numMergedSurfaces;
	const unsigned numWorldSurfaces  = rsh.worldBrushModel->numModelSurfaces;
	const unsigned numWorldLeaves    = rsh.worldBrushModel->numvisleafs;
	const unsigned numOccluders      = rsh.worldBrushModel->numOccluders;

	// Put the allocation code here, so we don't bloat the arch-specific code
	stateForCamera->visibleLeavesBuffer->reserve( numWorldLeaves );
	stateForCamera->visibleOccludersBuffer->reserve( numWorldSurfaces );
	stateForCamera->occluderPassFullyVisibleLeavesBuffer->reserve( numWorldLeaves );
	stateForCamera->occluderPassPartiallyVisibleLeavesBuffer->reserve( numWorldLeaves );

	stateForCamera->visibleOccludersBuffer->reserve( numOccluders );
	stateForCamera->sortedOccludersBuffer->reserve( numOccluders );

	stateForCamera->drawSurfSurfSpansBuffer->reserve( numMergedSurfaces );
	stateForCamera->bspDrawSurfacesBuffer->reserve( numMergedSurfaces );

	MergedSurfSpan *const mergedSurfSpans = stateForCamera->drawSurfSurfSpansBuffer->get( numMergedSurfaces );
	for( unsigned i = 0; i < numMergedSurfaces; ++i ) {
		mergedSurfSpans[i].firstSurface = std::numeric_limits<int>::max();
		mergedSurfSpans[i].lastSurface  = std::numeric_limits<int>::min();
	}

	// Cull world leaves by the primary frustum
	const std::span<const unsigned> visibleLeaves = collectVisibleWorldLeaves( stateForCamera );

	// TODO: Can run in parallel with leaves collection
	// Collect occluder surfaces of leaves that fall into the primary frustum and that are "good enough"
	const std::span<const SortedOccluder> visibleOccluders = collectVisibleOccluders( stateForCamera );
	// Build frusta of occluders, while performing some additional frusta pruning
	const std::span<const Frustum> occluderFrusta = buildFrustaOfOccluders( stateForCamera, visibleOccluders );

	std::span<const unsigned> nonOccludedLeaves;
	std::span<const unsigned> partiallyOccludedLeaves;
	if( occluderFrusta.empty() ) {
		// No "good enough" occluders found.
		// Just mark every surface that falls into the primary frustum visible in this case.
		markSurfacesOfLeavesAsVisible( visibleLeaves, mergedSurfSpans );
		nonOccludedLeaves = visibleLeaves;
	} else {
		// Test every leaf that falls into the primary frustum against frusta of occluders
		std::tie( nonOccludedLeaves, partiallyOccludedLeaves ) = cullLeavesByOccluders( stateForCamera,
																						visibleLeaves, occluderFrusta );
		markSurfacesOfLeavesAsVisible( nonOccludedLeaves, mergedSurfSpans );
		// Test every surface that belongs to partially occluded leaves
		cullSurfacesInVisLeavesByOccluders( partiallyOccludedLeaves, occluderFrusta, mergedSurfSpans );
	}

	return { occluderFrusta, nonOccludedLeaves, partiallyOccludedLeaves };
}

void Frontend::collectVisiblePolys( StateForCamera *stateForCamera, Scene *scene, std::span<const Frustum> frusta ) {
	VisTestedModel *tmpModels = stateForCamera->visTestedModelsBuffer->reserveAndGet( MAX_QUAD_POLYS );
	QuadPoly **quadPolys      = scene->m_quadPolys.data();

	uint16_t tmpIndices[MAX_QUAD_POLYS];
	const auto visibleIndices = cullQuadPolys( stateForCamera, quadPolys, scene->m_quadPolys.size(), frusta, tmpIndices, tmpModels );

	const auto *polyEntity = scene->m_polyent;
	for( const unsigned index: visibleIndices ) {
		QuadPoly *const p = quadPolys[index];
		addEntryToSortList( stateForCamera, polyEntity, nullptr, p->material, 0, index, nullptr, quadPolys[index], ST_QUAD_POLY );
	}
}

void Frontend::collectVisibleEntities( StateForCamera *stateForCamera, Scene *scene, std::span<const Frustum> frusta ) {
	uint16_t indices[MAX_ENTITIES], indices2[MAX_ENTITIES];

	VisTestedModel *const visModels = stateForCamera->visTestedModelsBuffer->reserveAndGet( MAX_ENTITIES );

	const std::span<const entity_t> nullModelEntities = scene->m_nullModelEntities;
	const auto nullModelIndices = cullNullModelEntities( stateForCamera, nullModelEntities, frusta, indices, visModels );
	addNullModelEntitiesToSortList( stateForCamera, nullModelEntities.data(), nullModelIndices );

	const std::span<const entity_t> aliasModelEntities = scene->m_aliasModelEntities;
	const auto aliasModelIndices = cullAliasModelEntities( stateForCamera, aliasModelEntities, frusta, indices, visModels );
	addAliasModelEntitiesToSortList( stateForCamera,  aliasModelEntities.data(),
									 { visModels, aliasModelIndices.size() }, aliasModelIndices );

	const std::span<const entity_t> skeletalModelEntities = scene->m_skeletalModelEntities;
	const auto skeletalModelIndices = cullSkeletalModelEntities( stateForCamera, skeletalModelEntities, frusta, indices, visModels );
	addSkeletalModelEntitiesToSortList( stateForCamera, skeletalModelEntities.data(),
										{ visModels, skeletalModelIndices.size() }, skeletalModelIndices );

	const std::span<const entity_t> brushModelEntities = scene->m_brushModelEntities;
	const auto brushModelIndices = cullBrushModelEntities( stateForCamera, brushModelEntities, frusta, indices, visModels );
	const std::span<const Scene::DynamicLight> dynamicLights { scene->m_dynamicLights.data(), scene->m_dynamicLights.size() };
	const std::span<const VisTestedModel> brushVisModels { visModels, brushModelIndices.size() };
	addBrushModelEntitiesToSortList( stateForCamera, brushModelEntities.data(), brushVisModels, brushModelIndices, dynamicLights );

	const std::span<const entity_t> spriteEntities = scene->m_spriteEntities;
	const auto spriteModelIndices = cullSpriteEntities( stateForCamera, spriteEntities, frusta, indices, indices2, visModels );
	addSpriteEntitiesToSortList( stateForCamera, spriteEntities.data(), spriteModelIndices );
}

void Frontend::collectVisibleParticles( StateForCamera *stateForCamera, Scene *scene, std::span<const Frustum> frusta ) {
	uint16_t tmpIndices[1024];
	const auto visibleAggregateIndices = cullParticleAggregates( stateForCamera, scene->m_particles, frusta, tmpIndices );
	addParticlesToSortList( stateForCamera, scene->m_polyent, scene->m_particles.data(), visibleAggregateIndices );
}

void Frontend::collectVisibleDynamicMeshes( StateForCamera *stateForCamera,  Scene *scene, std::span<const Frustum> occluderFrusta ) {
	uint16_t tmpIndices[wsw::max( Scene::kMaxDynamicMeshes, Scene::kMaxCompoundDynamicMeshes )];

	const std::span<const DynamicMesh *> meshes = scene->m_dynamicMeshes;
	const auto visibleDynamicMeshIndices = cullDynamicMeshes( stateForCamera, meshes.data(),
															  meshes.size(), occluderFrusta, tmpIndices );
	addDynamicMeshesToSortList( stateForCamera, scene->m_polyent, scene->m_dynamicMeshes.data(), visibleDynamicMeshIndices );

	const std::span<const Scene::CompoundDynamicMesh> compoundMeshes = scene->m_compoundDynamicMeshes;
	const auto visibleCompoundMeshesIndices = cullCompoundDynamicMeshes( stateForCamera, compoundMeshes,
																		 occluderFrusta, tmpIndices );
	addCompoundDynamicMeshesToSortList( stateForCamera, scene->m_polyent,
										scene->m_compoundDynamicMeshes.data(), visibleCompoundMeshesIndices );
}

auto Frontend::collectVisibleLights( StateForCamera *stateForCamera, Scene *scene, std::span<const Frustum> occluderFrusta )
	-> std::pair<std::span<const uint16_t>, std::span<const uint16_t>> {
	static_assert( decltype( Scene::m_dynamicLights )::capacity() == kMaxLightsInScene );

	const auto [allVisibleLightIndices, visibleCoronaLightIndices, visibleProgramLightIndices] =
		cullLights( stateForCamera, scene->m_dynamicLights, occluderFrusta,
					stateForCamera->allVisibleLightIndices, stateForCamera->visibleCoronaLightIndices,
					stateForCamera->visibleProgramLightIndices );

	assert( stateForCamera->numVisibleProgramLights == 0 );
	assert( stateForCamera->numAllVisibleLights == 0 );

	stateForCamera->numAllVisibleLights     = allVisibleLightIndices.size();
	stateForCamera->numVisibleProgramLights = visibleProgramLightIndices.size();

	// Prune m_visibleProgramLightIndices in-place
	if( visibleProgramLightIndices.size() > kMaxProgramLightsInView ) {
		const float *const __restrict viewOrigin = stateForCamera->viewOrigin;
		const Scene::DynamicLight *const lights  = scene->m_dynamicLights.data();
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
			stateForCamera->visibleProgramLightIndices[numVisibleProgramLights++] = lightIndex;
			lightsHeap.pop_back();
		} while( numVisibleProgramLights < kMaxProgramLightsInView );
		stateForCamera->numVisibleProgramLights = numVisibleProgramLights;
	}

	for( unsigned i = 0; i < stateForCamera->numVisibleProgramLights; ++i ) {
		Scene::DynamicLight *const light = scene->m_dynamicLights.data() + stateForCamera->visibleProgramLightIndices[i];
		float *const mins = stateForCamera->lightBoundingDops[i].mins;
		float *const maxs = stateForCamera->lightBoundingDops[i].maxs;
		createBounding14DopForSphere( mins, maxs, light->origin, light->programRadius );
	}

	return { { stateForCamera->visibleProgramLightIndices, stateForCamera->numVisibleProgramLights }, visibleCoronaLightIndices };
}

void Frontend::markSurfacesOfLeavesAsVisible( std::span<const unsigned> indicesOfLeaves,
											  MergedSurfSpan *mergedSurfSpans ) {
	const auto surfaces = rsh.worldBrushModel->surfaces;
	const auto leaves   = rsh.worldBrushModel->visleafs;
	for( const unsigned leafNum: indicesOfLeaves ) {
		const auto *__restrict leaf = leaves[leafNum];
		for( unsigned i = 0; i < leaf->numVisSurfaces; ++i ) {
			const unsigned surfNum = leaf->visSurfaces[i];
			assert( surfaces[surfNum].mergedSurfNum > 0 );
			const unsigned mergedSurfNum = surfaces[surfNum].mergedSurfNum - 1;
			MergedSurfSpan *const __restrict span = &mergedSurfSpans[mergedSurfNum];
			// TODO: Branchless min/max
			span->firstSurface = wsw::min( span->firstSurface, (int)surfNum );
			span->lastSurface = wsw::max( span->lastSurface, (int)surfNum );
		}
	}
}

void Frontend::markLightsOfSurfaces( StateForCamera *stateForCamera, const Scene *scene,
									 std::span<std::span<const unsigned>> spansOfLeaves,
									 std::span<const uint16_t> visibleLightIndices ) {
	unsigned *const lightBitsOfSurfaces = stateForCamera->leafLightBitsOfSurfacesBuffer
		->reserveZeroedAndGet( rsh.worldBrushModel->numsurfaces );

	if( !visibleLightIndices.empty() ) {
		for( const std::span<const unsigned> &leaves: spansOfLeaves ) {
			markLightsOfLeaves( stateForCamera, scene, leaves, visibleLightIndices, lightBitsOfSurfaces );
		}
	}
}

void Frontend::markLightsOfLeaves( StateForCamera *stateForCamera, const Scene *scene,
								   std::span<const unsigned> indicesOfLeaves,
								   std::span<const uint16_t> visibleLightIndices,
								   unsigned *leafLightBitsOfSurfaces ) {
	const unsigned numVisibleLights = visibleLightIndices.size();
	const auto leaves               = rsh.worldBrushModel->visleafs;

	assert( numVisibleLights && numVisibleLights <= kMaxProgramLightsInView );

	for( const unsigned leafIndex: indicesOfLeaves ) {
		const mleaf_t *const __restrict leaf = leaves[leafIndex];
		unsigned leafLightBits = 0;

		unsigned programLightNum = 0;
		do {
			const auto *__restrict lightDop = &stateForCamera->lightBoundingDops[programLightNum];
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

auto Frontend::cullNullModelEntities( StateForCamera *stateForCamera, std::span<const entity_t> entitiesSpan,
									  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices,
									  VisTestedModel *tmpModels ) -> std::span<const uint16_t> {
	const auto *const entities  = entitiesSpan.data();
	const unsigned numEntities  = entitiesSpan.size();

	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		VisTestedModel *const __restrict model  = &tmpModels[entIndex];
		Vector4Set( model->absMins, -16, -16, -16, -0 );
		Vector4Set( model->absMaxs, +16, +16, +16, +1 );
		VectorAdd( model->absMins, entity->origin, model->absMins );
		VectorAdd( model->absMaxs, entity->origin, model->absMaxs );
	}

	// The number of bounds has an exact match with the number of entities in this case
	return cullEntriesWithBounds( tmpModels, numEntities, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), &stateForCamera->frustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullAliasModelEntities( StateForCamera *stateForCamera, std::span<const entity_t> entitiesSpan,
									   std::span<const Frustum> occluderFrusta, uint16_t *tmpIndicesBuffer,
									   VisTestedModel *selectedModelsBuffer ) -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned weaponModelFlagEnts[16];
	const model_t *weaponModelFlagLods[16];
	unsigned numWeaponModelFlagEnts = 0;

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		if( entity->flags & RF_VIEWERMODEL ) [[unlikely]] {
			if( !( stateForCamera->renderFlags & ( RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
				continue;
			}
		}
		if( entity->flags & RF_WEAPONMODEL ) [[unlikely]] {
			if( stateForCamera->renderFlags & RF_NONVIEWERREF ) {
				continue;
			}
		}

		const model_t *mod = R_AliasModelLOD( entity, stateForCamera->lodOrigin, stateForCamera->lodScaleForFov );
		const auto *aliasmodel = ( const maliasmodel_t * )mod->extradata;
		// TODO: Could this ever happen
		if( !aliasmodel ) [[unlikely]] {
			continue;
		}
		if( !aliasmodel->nummeshes ) [[unlikely]] {
			continue;
		}

		// Don't let it to be culled away
		// TODO: Keep it separate from other models?
		if( entity->flags & RF_WEAPONMODEL ) [[unlikely]] {
			assert( numWeaponModelFlagEnts < std::size( weaponModelFlagEnts ) );
			weaponModelFlagEnts[numWeaponModelFlagEnts] = entIndex;
			weaponModelFlagLods[numWeaponModelFlagEnts] = mod;
			numWeaponModelFlagEnts++;
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = mod;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		R_AliasModelLerpBBox( entity, mod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	const size_t numPassedOtherEnts = cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels,
															 offsetof( VisTestedModel, absMins ), sizeof( VisTestedModel ),
															 &stateForCamera->frustum, occluderFrusta, tmpIndicesBuffer ).size();

	for( unsigned i = 0; i < numWeaponModelFlagEnts; ++i ) {
		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels];
		visTestedModel->selectedLod               = weaponModelFlagLods[i];
		visTestedModel->indexInEntitiesGroup      = weaponModelFlagEnts[i];

		const entity_t *const __restrict entity = &entities[visTestedModel->indexInEntitiesGroup];

		// Just for consistency?
		R_AliasModelLerpBBox( entity, visTestedModel->selectedLod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;

		tmpIndicesBuffer[numPassedOtherEnts + i] = numSelectedModels;
		numSelectedModels++;
	}

	return { tmpIndicesBuffer, numPassedOtherEnts + numWeaponModelFlagEnts };
}

auto Frontend::cullSkeletalModelEntities( StateForCamera *stateForCamera, std::span<const entity_t> entitiesSpan,
										  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndicesBuffer,
										  VisTestedModel *selectedModelsBuffer ) -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	const float *const stateLodOrigin = stateForCamera->lodOrigin;
	const float stateLodScaleForFov   = stateForCamera->lodScaleForFov;
	const unsigned stateRenderFlags   = stateForCamera->renderFlags;

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; entIndex++ ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		if( entity->flags & RF_VIEWERMODEL ) [[unlikely]] {
			if( !( stateRenderFlags & (RF_MIRRORVIEW | RF_SHADOWMAPVIEW ) ) ) {
				continue;
			}
		}

		const model_t *mod = R_SkeletalModelLOD( entity, stateLodOrigin, stateLodScaleForFov );
		const mskmodel_t *skmodel = ( const mskmodel_t * )mod->extradata;
		if( !skmodel ) [[unlikely]] {
			continue;
		}
		if( !skmodel->nummeshes ) [[unlikely]] {
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = mod;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		R_SkeletalModelLerpBBox( entity, mod, visTestedModel->absMins, visTestedModel->absMaxs );
		VectorAdd( visTestedModel->absMins, entity->origin, visTestedModel->absMins );
		VectorAdd( visTestedModel->absMaxs, entity->origin, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	return cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), &stateForCamera->frustum, occluderFrusta, tmpIndicesBuffer );
}

auto Frontend::cullBrushModelEntities( StateForCamera *stateForCamera, std::span<const entity_t> entitiesSpan,
									   std::span<const Frustum> occluderFrusta, uint16_t *tmpIndicesBuffer,
									   VisTestedModel *selectedModelsBuffer ) -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned numSelectedModels = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		const model_t *const model              = entity->model;
		const auto *const brushModel            = ( mbrushmodel_t * )model->extradata;
		if( !brushModel->numModelMergedSurfaces ) [[unlikely]] {
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &selectedModelsBuffer[numSelectedModels++];
		visTestedModel->selectedLod               = model;
		visTestedModel->indexInEntitiesGroup      = entIndex;

		// Returns absolute bounds
		R_BrushModelBBox( entity, visTestedModel->absMins, visTestedModel->absMaxs );
		visTestedModel->absMins[3] = 0.0f, visTestedModel->absMaxs[3] = 1.0f;
	}

	return cullEntriesWithBounds( selectedModelsBuffer, numSelectedModels, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), &stateForCamera->frustum, occluderFrusta, tmpIndicesBuffer );
}

auto Frontend::cullSpriteEntities( StateForCamera *stateForCamera, std::span<const entity_t> entitiesSpan,
								   std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices,
								   uint16_t *tmpIndices2, VisTestedModel *tmpModels ) -> std::span<const uint16_t> {
	const auto *const entities = entitiesSpan.data();
	const unsigned numEntities = entitiesSpan.size();

	unsigned numResultEntities    = 0;
	unsigned numVisTestedEntities = 0;
	for( unsigned entIndex = 0; entIndex < numEntities; ++entIndex ) {
		const entity_t *const __restrict entity = &entities[entIndex];
		// TODO: This condition should be eliminated from this path
		if( entity->flags & RF_NOSHADOW ) [[unlikely]] {
			if( stateForCamera->renderFlags & RF_SHADOWMAPVIEW ) [[unlikely]] {
				continue;
			}
		}

		if( entity->radius <= 0 || entity->customShader == nullptr || entity->scale <= 0 ) [[unlikely]] {
			continue;
		}

		// Hacks for ET_RADAR indicators
		if( entity->flags & RF_NODEPTHTEST ) [[unlikely]] {
			tmpIndices[numResultEntities++] = entIndex;
			continue;
		}

		VisTestedModel *__restrict visTestedModel = &tmpModels[numVisTestedEntities++];
		visTestedModel->indexInEntitiesGroup      = entIndex;

		const float *origin = entity->origin;
		const float halfRadius = 0.5f * entity->radius;

		Vector4Set( visTestedModel->absMins, origin[0] - halfRadius, origin[1] - halfRadius, origin[2] - halfRadius, 0.0f );
		Vector4Set( visTestedModel->absMaxs, origin[0] + halfRadius, origin[1] + halfRadius, origin[2] + halfRadius, 1.0f );
	}

	const auto passedTestIndices = cullEntriesWithBounds( tmpModels, numVisTestedEntities,
														  offsetof( VisTestedModel, absMins ), sizeof( VisTestedModel ),
														  &stateForCamera->frustum, occluderFrusta, tmpIndices2 );

	for( const auto testedModelIndex: passedTestIndices ) {
		tmpIndices[numResultEntities++] = tmpModels[testedModelIndex].indexInEntitiesGroup;
	}

	return { tmpIndices, numResultEntities };
}

auto Frontend::cullLights( StateForCamera *stateForCamera,
						   std::span<const Scene::DynamicLight> lightsSpan,
						   std::span<const Frustum> occluderFrusta,
						   uint16_t *tmpAllLightIndices,
						   uint16_t *tmpCoronaLightIndices,
						   uint16_t *tmpProgramLightIndices )
	-> std::tuple<std::span<const uint16_t>, std::span<const uint16_t>, std::span<const uint16_t>> {

	const auto *const lights = lightsSpan.data();
	const unsigned numLights = lightsSpan.size();

	static_assert( offsetof( Scene::DynamicLight, mins ) + 4 * sizeof( float ) == offsetof( Scene::DynamicLight, maxs ) );
	const auto visibleAllLightIndices = cullEntriesWithBounds( lights, numLights, offsetof( Scene::DynamicLight, mins ),
															   sizeof( Scene::DynamicLight ), &stateForCamera->frustum,
															   occluderFrusta, tmpAllLightIndices );

	unsigned numPassedCoronaLights  = 0;
	unsigned numPassedProgramLights = 0;

	for( const auto index: visibleAllLightIndices ) {
		const Scene::DynamicLight *light               = &lights[index];
		tmpCoronaLightIndices[numPassedCoronaLights]   = index;
		tmpProgramLightIndices[numPassedProgramLights] = index;
		numPassedCoronaLights  += light->hasCoronaLight;
		numPassedProgramLights += light->hasProgramLight;
	}

	return { visibleAllLightIndices,
			 { tmpCoronaLightIndices,  numPassedCoronaLights },
			 { tmpProgramLightIndices, numPassedProgramLights } };
}

auto Frontend::cullParticleAggregates( StateForCamera *stateForCamera, std::span<const Scene::ParticlesAggregate> aggregatesSpan,
									   std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices ) -> std::span<const uint16_t> {
	static_assert( offsetof( Scene::ParticlesAggregate, mins ) + 16 == offsetof( Scene::ParticlesAggregate, maxs ) );
	return cullEntriesWithBounds( aggregatesSpan.data(), aggregatesSpan.size(), offsetof( Scene::ParticlesAggregate, mins ),
								  sizeof( Scene::ParticlesAggregate ), &stateForCamera->frustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullCompoundDynamicMeshes( StateForCamera *stateForCamera, std::span<const Scene::CompoundDynamicMesh> meshesSpan,
										  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices ) -> std::span<const uint16_t> {
	static_assert( offsetof( Scene::CompoundDynamicMesh, cullMins ) + 16 == offsetof( Scene::CompoundDynamicMesh, cullMaxs ) );
	return cullEntriesWithBounds( meshesSpan.data(), meshesSpan.size(), offsetof( Scene::CompoundDynamicMesh, cullMins ),
								  sizeof( Scene::CompoundDynamicMesh ), &stateForCamera->frustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullQuadPolys( StateForCamera *stateForCamera, QuadPoly **polys, unsigned numPolys,
							  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices,
							  VisTestedModel *tmpModels ) -> std::span<const uint16_t> {
	for( unsigned polyNum = 0; polyNum < numPolys; ++polyNum ) {
		const QuadPoly *const __restrict poly  = polys[polyNum];
		VisTestedModel *const __restrict model = &tmpModels[polyNum];

		Vector4Set( model->absMins, -poly->halfExtent, -poly->halfExtent, -poly->halfExtent, -0 );
		Vector4Set( model->absMaxs, +poly->halfExtent, +poly->halfExtent, +poly->halfExtent, +1 );
		VectorAdd( model->absMins, poly->origin, model->absMins );
		VectorAdd( model->absMaxs, poly->origin, model->absMaxs );
	}

	return cullEntriesWithBounds( tmpModels, numPolys, offsetof( VisTestedModel, absMins ),
								  sizeof( VisTestedModel ), &stateForCamera->frustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullDynamicMeshes( StateForCamera *stateForCamera,
								  const DynamicMesh **meshes, unsigned numMeshes,
								  std::span<const Frustum> occluderFrusta,
								  uint16_t *tmpIndices ) -> std::span<const uint16_t> {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	constexpr unsigned boundsFieldOffset = offsetof( DynamicMesh, cullMins );
	static_assert( offsetof( DynamicMesh, cullMins ) + 4 * sizeof( float ) == offsetof( DynamicMesh, cullMaxs ) );
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	return cullEntryPtrsWithBounds( (const void **)meshes, numMeshes, boundsFieldOffset,
									&stateForCamera->frustum, occluderFrusta, tmpIndices );
}

auto Frontend::collectVisibleWorldLeaves( StateForCamera *stateForCamera ) -> std::span<const unsigned> {
	return ( this->*m_collectVisibleWorldLeavesArchMethod )( stateForCamera );
}

auto Frontend::collectVisibleOccluders( StateForCamera *stateForCamera ) -> std::span<const SortedOccluder> {
	return ( this->*m_collectVisibleOccludersArchMethod )( stateForCamera );
}

auto Frontend::buildFrustaOfOccluders( StateForCamera *stateForCamera, std::span<const SortedOccluder> sortedOccluders )
	-> std::span<const Frustum> {
	// Put the check here so it does not get replicated for every arch
	if( stateForCamera->renderFlags & RF_NOOCCLUSIONCULLING ) {
		return { stateForCamera->occluderFrusta, 0 };
	}

	return ( this->*m_buildFrustaOfOccludersArchMethod )( stateForCamera, sortedOccluders );
}

auto Frontend::cullLeavesByOccluders( StateForCamera *stateForCamera, std::span<const unsigned> indicesOfLeaves,
									  std::span<const Frustum> occluderFrusta )
	-> std::pair<std::span<const unsigned>, std::span<const unsigned>> {
	return ( this->*m_cullLeavesByOccludersArchMethod )( stateForCamera, indicesOfLeaves, occluderFrusta );
}

void Frontend::cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfLeaves,
												   std::span<const Frustum> occluderFrusta,
												   MergedSurfSpan *mergedSurfSpans ) {
	return ( this->*m_cullSurfacesInVisLeavesByOccludersArchMethod )( indicesOfLeaves, occluderFrusta, mergedSurfSpans );
}

auto Frontend::cullEntriesWithBounds( const void *entries, unsigned numEntries, unsigned boundsFieldOffset,
									  unsigned strideInBytes, const Frustum *__restrict primaryFrustum,
									  std::span<const Frustum> occluderFrusta, uint16_t *tmpIndices )
	-> std::span<const uint16_t> {
	return ( this->*m_cullEntriesWithBoundsArchMethod )( entries, numEntries, boundsFieldOffset, strideInBytes,
														 primaryFrustum, occluderFrusta, tmpIndices );
}

auto Frontend::cullEntryPtrsWithBounds( const void **entryPtrs, unsigned numEntries, unsigned boundsFieldOffset,
										const Frustum *__restrict primaryFrustum, std::span<const Frustum> occluderFrusta,
										uint16_t *tmpIndices )
	-> std::span<const uint16_t> {
	return ( this->*m_cullEntryPtrsWithBoundsArchMethod )( entryPtrs, numEntries, boundsFieldOffset,
														   primaryFrustum, occluderFrusta, tmpIndices );
}

}