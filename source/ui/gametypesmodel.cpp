#include "gametypesmodel.h"

#include "../qcommon/stringspanstorage.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/maplist.h"

#include <QJsonObject>

using wsw::operator""_asView;

namespace wsw::ui {

auto GametypesModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Title, "title" },
		{ Flags, "flags" },
		{ Maps, "maps" },
		{ Desc, "desc" }
	};
};

auto GametypesModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_gametypes.size();
};

static inline auto toQString( const wsw::StringView &view ) -> QString {
	return QString::fromUtf8( view.data(), view.size() );
}

auto GametypesModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( !index.isValid() ) {
		return QVariant();
	}
	const int row = index.row();
	if( (unsigned)row >= m_gametypes.size() ) {
		return QVariant();
	}
	switch( role ) {
		case Title: return toQString( m_gametypes[row].getTitle() );
		case Flags: return (int)m_gametypes[row].getFlags();
		case Maps: return getListOfMaps( m_gametypes[row] );
		case Desc: return toQString( m_gametypes[row].getDesc() );
		default: return QVariant();
	}
}

auto GametypesModel::getListOfMaps( const GametypeDef &def ) const -> QJsonArray {
	QJsonArray result;

	const auto &mapInfoList = def.m_mapInfo;
	for( const auto &info: mapInfoList ) {
		int minPlayers = 0, maxPlayers = 0;
		if( auto maybeNumOfPlayers = info.numPlayers ) {
			std::tie( minPlayers, maxPlayers ) = *maybeNumOfPlayers;
		}
		result.append( QJsonObject({
			{ "name", toQString( def.getString( info.nameSpan ) ) },
			{ "minPlayers", minPlayers },
			{ "maxPlayers", maxPlayers }
		}));
	}

	return result;
}

auto GametypesModel::getSuggestedNumBots( const GametypeDef &def, int mapNum ) const -> QJsonObject {
	assert( (unsigned)mapNum < def.m_mapInfo.size() );

	const auto botConfig = def.m_botConfig;
	if( botConfig == GametypeDef::NoBots ) {
		return QJsonObject( { { "allowed", false } } );
	}
	if( botConfig == GametypeDef::ScriptSpawnedBots ) {
		return QJsonObject( { { "allowed", true }, { "defined", false } } );
	}

	int number;
	bool fixed;
	if( botConfig == GametypeDef::ExactNumBots ) {
		number = (int)def.m_exactNumBots.value();
		fixed = true;
	} else {
		assert( botConfig == GametypeDef::FixedNumBotsForMap || botConfig == GametypeDef::BestNumBotsForMap );
		auto [minPlayers, maxPlayers] = def.m_mapInfo[mapNum].numPlayers.value();
		assert( minPlayers && maxPlayers && minPlayers < maxPlayers );
		number = (int)(( minPlayers + maxPlayers ) / 2 );
		fixed = botConfig == GametypeDef::FixedNumBotsForMap;
	}

	assert( number > 0 );
	return QJsonObject( { { "allowed", true }, { "defined", true }, { "number", number }, { "fixed", fixed } } );
}

class MapExistenceCache {
	// Fits the small dataset well (the number of "certified" maps declared in .gtd is small)
	using NamesList = wsw::StringSpanStaticStorage<uint16_t, uint8_t, 32, 1024>;

	NamesList existingMaps;
	NamesList missingMaps;

	[[nodiscard]]
	static bool findInList( const wsw::StringView &v, const NamesList &namesList ) {
		for( const wsw::StringView &name: namesList ) {
			if( name.equalsIgnoreCase( v ) ) {
				return true;
			}
		}
		return false;
	}
public:
	[[nodiscard]]
	bool exists( const wsw::StringView &mapFileName ) {
		if( findInList( mapFileName, existingMaps ) ) {
			return true;
		}
		if( findInList( mapFileName, missingMaps ) ) {
			return false;
		}
		assert( mapFileName.isZeroTerminated() );
		bool exists = ML_FilenameExists( mapFileName.data() );
		NamesList &list = exists ? existingMaps : missingMaps;
		if( list.size() == list.spansCapacity() ) {
			list.pop_back();
		}
		// We assume that the last pop back could've been sufficient.
		// This is not a 100% guarantee of having a room for a string.
		// Just skip caching of a result in this case without trying to restore the list state if it has been modified.
		if( list.canAdd( mapFileName ) ) {
			list.add( mapFileName );
		}
		return exists;
	}
};

GametypesModel::GametypesModel() {
	const wsw::StringView dir( "progs/gametypes"_asView );
	const wsw::StringView ext( ".gtd"_asView );

	wsw::StaticString<MAX_QPATH> path;
	path << dir << '/';

	wsw::fs::SearchResultHolder searchResultHolder;
	const auto maybeCallResult = searchResultHolder.findDirFiles( dir, ext );
	if( !maybeCallResult ) {
		return;
	}

	// Map existence checks require an FS access...
	// Use an existence cache as many maps are shared by gametypes.
	MapExistenceCache mapExistenceCache;

	for( const wsw::StringView &fileName: *maybeCallResult ) {
		assert( fileName.endsWith( ext ) );
		path.erase( dir.length() + 1 );
		path << fileName;

		auto maybeDef = GametypeDefParser::exec( path.asView() );
		if( !maybeDef ) {
			continue;
		}

		auto def( *maybeDef );
		auto &mapInfo = def.m_mapInfo;
		for( unsigned i = 0; i < mapInfo.size(); ) {
			const wsw::StringView mapName( def.getString( mapInfo[i].nameSpan ) );
			if( mapExistenceCache.exists( mapName ) ) {
				++i;
			} else {
				mapInfo.erase( mapInfo.begin() + i );
			}
		}
		if( !mapInfo.empty() ) {
			m_gametypes.emplace_back( std::move( def ) );
		}
	}
}

}