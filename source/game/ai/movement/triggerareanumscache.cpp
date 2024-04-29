#include "triggerareanumscache.h"
#include "movementlocal.h"
#include "../classifiedentitiescache.h"
#include "../manager.h"

TriggerAreaNumsCache triggerAreaNumsCache;

auto TriggerAreaNumsCache::getAreaNum( int entNum ) const -> int {
	int *const __restrict areaNumRef = &m_areaNums[entNum];
	// Put the likely case first
	if( *areaNumRef ) {
		return *areaNumRef;
	}

	// Find an area that has suitable flags matching the trigger type
	const auto *const __restrict aasWorld = AiAasWorld::instance();
	const auto aasAreaSettings = aasWorld->getAreaSettings();
	const auto *const __restrict aiManager = AiManager::Instance();

	int desiredAreaContents = ~0;
	const edict_t *__restrict ent = game.edicts + entNum;
	if( ent->classname ) {
		if( !Q_stricmp( ent->classname, "trigger_push" ) ) {
			desiredAreaContents = AREACONTENTS_JUMPPAD;
		} else if( !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			desiredAreaContents = AREACONTENTS_TELEPORTER;
		}
	}

	*areaNumRef = 0;

	int boxAreaNumsBuffer[64];
	const auto boxAreaNums = aasWorld->findAreasInBox( ent->r.absmin, ent->r.absmax, boxAreaNumsBuffer, 64 );
	for( const int areaNum: boxAreaNums ) {
		if( !( aasAreaSettings[areaNum].contents & desiredAreaContents ) ) {
			continue;
		}
		if( !aiManager->IsAreaReachableFromHubAreas( areaNum ) ) {
			continue;
		}
		*areaNumRef = areaNum;
		break;
	}

	return *areaNumRef;
}

auto TriggerAreaNumsCache::getTriggersForArea( int areaNum ) const -> const ClassTriggerNums * {
	const auto *const __restrict aasWorld = AiAasWorld::instance();

	if( m_testedTriggersForArea[areaNum] ) {
		if( m_hasTriggersForArea[areaNum] ) {
			const auto it = m_triggersForArea.find( areaNum );
			assert( it != m_triggersForArea.end() );
			return &it->second;
		}
		return nullptr;
	}

	m_testedTriggersForArea[areaNum] = true;

	unsigned numTeleporters = 0, numJumppads = 0, numPlatforms = 0;
	uint16_t teleporterNums[MAX_EDICTS], jumppadNums[MAX_EDICTS], platformNums[MAX_EDICTS];

	const auto &area = aasWorld->getAreas()[areaNum];
	const float *__restrict areaMins = area.mins;
	const float *__restrict areaMaxs = area.maxs;

	const auto *const __restrict gameEnts = game.edicts;
	const auto *const __restrict entitiesCache = wsw::ai::ClassifiedEntitiesCache::instance();
	for( const uint16_t num : entitiesCache->getAllPersistentMapJumppads() ) {
		const auto *const __restrict ent = gameEnts + num;
		if( BoundsIntersect( ent->r.absmin, ent->r.absmax, areaMins, areaMaxs ) ) {
			jumppadNums[numJumppads++] = num;
		}
	}

	for( const uint16_t num : entitiesCache->getAllPersistentMapTeleporters() ) {
		const auto *const __restrict ent = gameEnts + num;
		if( BoundsIntersect( ent->r.absmin, ent->r.absmax, areaMins, areaMaxs ) ) {
			teleporterNums[numTeleporters++] = num;
		}
	}

	for( const uint16_t num : entitiesCache->getAllPersistentMapPlatformTriggers() ) {
		const auto *const __restrict ent = gameEnts + num;
		if( BoundsIntersect( ent->r.absmin, ent->r.absmax, areaMins, areaMaxs ) ) {
			platformNums[numPlatforms++] = num;
		}
	}

	m_testedTriggersForArea[areaNum] = true;
	if( const auto numOfNums = ( numTeleporters + numJumppads + numPlatforms ) ) {
		m_hasTriggersForArea[areaNum] = true;
		auto [it, _] = m_triggersForArea.insert( std::make_pair( areaNum, ClassTriggerNums {} ) );
		ClassTriggerNums *const storage = &it->second;
		storage->m_teleporterNumsSpan   = storage->addNums( teleporterNums, numTeleporters );
		storage->m_jumppadNumsSpan      = storage->addNums( jumppadNums, numJumppads );
		storage->m_platformNumsSpan     = storage->addNums( platformNums, numPlatforms );
		return storage;
	}
	return nullptr;
}