#include "frameentitiescache.h"
#include "../../qcommon/singletonholder.h"

namespace wsw::ai {

static SingletonHolder<FrameEntitiesCache> instanceHolder;

void FrameEntitiesCache::init() {
	instanceHolder.Init();
}

void FrameEntitiesCache::shutdown() {
	instanceHolder.Shutdown();
}

auto FrameEntitiesCache::instance() -> FrameEntitiesCache * {
	return instanceHolder.Instance();
}

template <typename Container>
inline void add( Container *c, int num ) {
	if( c->size() != c->capacity() ) {
		c->push_back( (uint16_t)num );
	} else {
		throw std::length_error( "Too many entities of such kind, AI is incapable to function on this map" );
	}
}

void FrameEntitiesCache::update() {
	// TODO: Update triggers only upon a map change...
	m_allJumppads.clear();
	m_allTeleports.clear();
	m_allPlatforms.clear();
	m_allOtherTriggers.clear();

	const auto *__restrict gameEnts = game.edicts;
	const int numEnts = game.numentities;
	for( int i = gs.maxclients + 1; i < numEnts; ++i ) {
		const auto *__restrict ent = gameEnts + i;
		if( ent->r.inuse && ent->r.solid == SOLID_TRIGGER ) {
			if( const char *classname = ent->classname ) {
				if( !Q_stricmp( classname, "trigger_push" ) ) {
					add( &m_allJumppads, ent->s.number );
					continue;
				}
				if( !Q_stricmp( classname, "trigger_teleport" ) ) {
					add( &m_allTeleports, ent->s.number );
					continue;
				}
				if( !Q_stricmp( classname, "trigger_platform" ) ) {
					add( &m_allPlatforms, ent->s.number );
					continue;
				}
			}
			m_allOtherTriggers.push_back( (uint16_t) ent->s.number );
		}
	}
}

}