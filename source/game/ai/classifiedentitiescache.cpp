#include "classifiedentitiescache.h"
#include "../../qcommon/singletonholder.h"

namespace wsw::ai {

static SingletonHolder<ClassifiedEntitiesCache> instanceHolder;

void ClassifiedEntitiesCache::init() {
	instanceHolder.init();
}

void ClassifiedEntitiesCache::shutdown() {
	instanceHolder.shutdown();
}

auto ClassifiedEntitiesCache::instance() -> ClassifiedEntitiesCache * {
	return instanceHolder.instance();
}

template <typename Container>
inline void add( Container *c, int num ) {
	if( !c->full() ) {
		c->push_back( (uint16_t)num );
	} else {
		wsw::failWithRuntimeError( "Too many entities of such kind, AI is incapable to function on this map" );
	}
}

void ClassifiedEntitiesCache::retrievePersistentEntities() {
	m_allJumppads.clear();
	m_allTeleporters.clear();
	m_allPlatforms.clear();

	const auto *__restrict gameEnts = game.edicts;
	const int numEnts = game.numentities;
	for( int i = gs.maxclients + 1; i < numEnts; ++i ) {
		const auto *__restrict ent = gameEnts + i;
		if( ent->r.inuse && ent->r.solid == SOLID_TRIGGER ) {
			if( const char *const classname = ent->classname ) {
				if( !Q_stricmp( classname, "trigger_push" ) ) {
					add( &m_allJumppads, ent->s.number );
					m_persistentEntitiesMask.set( (size_t)i );
				} else if( !Q_stricmp( classname, "trigger_teleport" ) ) {
					add( &m_allTeleporters, ent->s.number );
					m_persistentEntitiesMask.set( (size_t)i );
				} else if( !Q_stricmp( classname, "trigger_platform" ) ) {
					add( &m_allPlatforms, ent->s.number );
					m_persistentEntitiesMask.set( (size_t)i );
				}
			}
		}
	}
}

void ClassifiedEntitiesCache::update() {
	// TODO: Call this explicitly upon map loading
	if( !m_hasRetrievedPersistentEntities ) {
		retrievePersistentEntities();
		m_hasRetrievedPersistentEntities = true;
	}

	m_allOtherTriggers.clear();

	const auto *__restrict gameEnts = game.edicts;
	const int numEnts = game.numentities;
	for( int i = gs.maxclients + 1; i < numEnts; ++i ) {
		const auto *__restrict ent = gameEnts + i;
		if( ent->r.inuse && ent->r.solid == SOLID_TRIGGER ) {
			if( !m_persistentEntitiesMask[i] ) {
				m_allOtherTriggers.push_back( (uint16_t) ent->s.number );
			}
		}
	}
}

}