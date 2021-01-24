#include "demos.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/links.h"
#include "../qcommon/wswtonum.h"
#include "../qcommon/version.h"
#include "../qcommon/wswfs.h"

namespace wsw::ui {

DemosModel::DemosModel( DemosResolver *resolver ) : m_resolver( resolver ) {
	connect( m_resolver, &DemosResolver::isReadyChanged, this, &DemosModel::onIsResolverReadyChanged );
}

auto DemosModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Section, "section" },
		{ Timestamp, "timestamp" },
		{ ServerName, "serverName" },
		{ DemoName, "demoName" },
		{ FileName, "fileName" },
		{ MapName, "mapName" },
		{ Gametype, "gametype" }
	};
}

auto DemosModel::rowCount( const QModelIndex & ) const -> int {
	return m_resolver->isReady() ? m_resolver->getCount() : 0;
}

[[nodiscard]]
static inline auto toQVariant( const wsw::StringView &view ) -> QVariant {
	return QByteArray( view.data(), view.size() );
}

[[nodiscard]]
static inline auto formatTimestamp( const QDateTime &timestamp ) -> QVariant {
	return timestamp.toString( Qt::DefaultLocaleShortDate );
}

auto DemosModel::data( const QModelIndex &index, int role ) const -> QVariant {
	assert( m_resolver->isReady() );
	if( index.isValid() ) {
		if( const int row = index.row(); (unsigned)row < (unsigned)m_resolver->getCount() ) {
			switch( role ) {
				case Section: return m_resolver->getEntry( row )->sectionDate;
				case Timestamp: return formatTimestamp( m_resolver->getEntry( row )->timestamp );
				case ServerName: return toQVariant( m_resolver->getEntry( row )->getServerName() );
				case DemoName: return toQVariant( m_resolver->getEntry( row )->getDemoName() );
				case FileName: return toQVariant( m_resolver->getEntry( row )->getFileName() );
				case MapName: return toQVariant( m_resolver->getEntry( row )->getMapName() );
				case Gametype: return toQVariant( m_resolver->getEntry( row )->getGametype() );
				default:;
			}
		}
	}
	return QVariant();
}

void DemosModel::onIsResolverReadyChanged( bool ) {
	beginResetModel();
	endResetModel();
}

DemosResolver::DemosResolver() {
	m_threadPool.setExpiryTimeout( 3000 );
	// Establish connections for inter-thread transmissions of task results
	Qt::ConnectionType connectionType = Qt::QueuedConnection;
	connect( this, &DemosResolver::resolveTaskCompleted, this, &DemosResolver::takeResolveTaskResult, connectionType );
	connect( this, &DemosResolver::resolveTasksReady, this, &DemosResolver::setReady, connectionType );
	connect( this, &DemosResolver::runQueryTasksReady, this, &DemosResolver::setReady, connectionType );
}

DemosResolver::~DemosResolver() {
	ResolveTaskResult *next;
	for( ResolveTaskResult *result = m_taskResultsHead; result; result = next ) {
		next = result->next;
		delete result;
	}
}

// A wrapper for using the custom thread pool
class EnumerateFilesTask : public QRunnable {
	DemosResolver *const m_resolver;
public:
	explicit EnumerateFilesTask( DemosResolver *resolver ) : m_resolver( resolver ) {}
	void run() override { m_resolver->enumerateFiles(); }
};

class RunQueryTask : public QRunnable {
	DemosResolver *const m_resolver;
public:
	explicit RunQueryTask( DemosResolver *resolver ) : m_resolver( resolver ) {}
	void run() override { m_resolver->runQuery(); }
};

void DemosResolver::reload() {
	assert( m_isReady );

	setReady( false );

	m_lastQueryResults.clear();
	m_lastQuery.clear();

	Q_EMIT progressUpdated( QVariant() );

	m_threadPool.start( new EnumerateFilesTask( this ) );
}

void DemosResolver::query( const QString &query ) {
	assert( m_isReady );

	const QByteArray queryBytes( query.toLatin1() );
	assert( (unsigned)queryBytes.size() < m_lastQuery.capacity() );
	const wsw::StringView queryView( queryBytes.data(), queryBytes.size() );

	if( m_lastQuery.asView().equalsIgnoreCase( queryView ) ) {
		return;
	}

	setReady( false );

	m_lastQuery.assign( queryView );
	m_lastQueryResults.clear();

	Q_EMIT progressUpdated( QVariant() );

	m_threadPool.start( new RunQueryTask( this ) );
}

void DemosResolver::setReady( bool ready ) {
	m_isReady = ready;
	Q_EMIT isReadyChanged( ready );
}

static const wsw::StringView kSearchPathRoot( "demos" );
static const wsw::StringView kDemoExtension( APP_DEMO_EXTENSION_STR );

template <typename Container>
static bool contains( Container &c, const wsw::StringView &v ) {
	for( const wsw::StringView &existing: c ) {
		if( existing.equalsIgnoreCase( v ) ) {
			return true;
		}
	}
	return false;
}

void DemosResolver::enumerateFiles() {
	wsw::fs::SearchResultHolder holder;
	const auto &oldFileNames = m_fileNameSpans[m_turn];
	const auto nextTurn = ( m_turn + 1 ) % 2;
	auto &newFileNames = m_fileNameSpans[nextTurn];
	newFileNames.clear();

	// TODO: Add subdirectories!!!!
	// TODO: Do we just use fixed subdirectories set???
	wsw::StaticString<MAX_QPATH> path;
	if( auto maybeResult = holder.findDirFiles( kSearchPathRoot, kDemoExtension ) ) {
		path.clear();
		for( const wsw::StringView &fileName: *maybeResult ) {
			path.clear();
			path << kSearchPathRoot << '/' << fileName;
			newFileNames.add( path.asView() );
		}
	}

	m_addedNew.clear();
	m_goneOld.clear();

	// TODO: Rewrite to use a "hash-join" (RDBMS-like)
	for( unsigned i = 0; i < newFileNames.size(); ++i ) {
		if( !contains( oldFileNames, newFileNames[i] ) ) {
			m_addedNew.push_back( i );
		}
	}
	for( unsigned i = 0; i < oldFileNames.size(); ++i ) {
		if( !contains( newFileNames, oldFileNames[i] ) ) {
			m_goneOld.push_back( i );
		}
	}

	if( m_addedNew.empty() && m_goneOld.empty() ) {
		Q_EMIT resolveTasksReady( true );
		return;
	}

	m_turn = ( m_turn + 1 ) % 2;
	for( unsigned oldIndex: m_goneOld ) {
		purgeMetadata( oldFileNames[oldIndex] );
	}

	Q_EMIT progressUpdated( 0 );

	m_numPendingTasks = 1;
	m_numCompletedTasks = 0;
	m_taskResultsToProcess.clear();
	// Continue this task for now
	resolveMetadata( 0, m_addedNew.size() );
}

void DemosResolver::purgeMetadata( const wsw::StringView &fileName ) {
	const auto hash = wsw::HashedStringView( fileName ).getHash();
	const auto binIndex = hash % kNumBins;
	for( MetadataEntry *entry = m_hashBins[binIndex]; entry; entry = entry->next ) {
		if( entry->fileNameHash == hash && entry->parent->storage[entry->fileNameIndex] == fileName ) {
			wsw::unlink( entry, &m_hashBins[binIndex] );
			entry->parent->numRefs--;
			if( !entry->parent->numRefs ) {
				wsw::unlink( entry->parent, &m_taskResultsHead );
				delete entry->parent;
			}
			return;
		}
	}
	throw std::logic_error( "unreachable" );
}

void DemosResolver::resolveMetadata( unsigned first, unsigned last ) {
	ResolveTaskResult *taskResult = nullptr;
	try {
		taskResult = new ResolveTaskResult;
		for ( unsigned index = first; index < last; ++index ) {
			resolveMetadata( index, &taskResult->entries, &taskResult->storage );
		}
		Q_EMIT takeResolveTaskResult( taskResult );
	} catch (...) {
		delete taskResult;
		Q_EMIT takeResolveTaskResult( nullptr );
	}
}

void DemosResolver::resolveMetadata( unsigned index, wsw::Vector<MetadataEntry> *entries, StringDataStorage *storage ) {
	const wsw::StringView fileName( m_fileNameSpans[m_turn][index].data() );
	assert( fileName.isZeroTerminated() );

	std::pair<unsigned, unsigned> baseNameSpan( 0, fileName.length() );
	if( const auto slashIndex = fileName.lastIndexOf( '/' ) ) {
		const unsigned droppedLen = *slashIndex + 1;
		baseNameSpan.first = droppedLen;
		baseNameSpan.second -= droppedLen;
	}
	if( fileName.endsWith( kDemoExtension ) ) {
		baseNameSpan.second -= kDemoExtension.length();
	}
	assert( baseNameSpan.first + baseNameSpan.second <= fileName.length() );

	int handle = 0;
	int length = FS_FOpenFile( fileName.data(), &handle, FS_READ | SNAP_DEMO_GZ );
	if( length > 0 ) {
		char metadata[SNAP_MAX_DEMO_META_DATA_SIZE];
		size_t realSize = SNAP_ReadDemoMetaData( handle, metadata, sizeof( metadata ) );
		if( realSize <= sizeof( metadata ) ) {
			parseMetadata( metadata, realSize, fileName, baseNameSpan, entries, storage );
		}
	}

	FS_FCloseFile( handle );
}

static wsw::StringView kMandatoryKeys[6] {
	kDemoKeyServerName, kDemoKeyTimestamp, kDemoKeyDuration, kDemoKeyMapName, kDemoKeyMapChecksum, kDemoKeyGametype
};

void DemosResolver::parseMetadata( const char *data, size_t dataSize,
								   const wsw::StringView &fileName,
								   const std::pair<unsigned, unsigned> &baseNameSpan,
								   wsw::Vector<MetadataEntry> *entries,
								   StringDataStorage *stringData ) {
	wsw::StaticVector<std::pair<wsw::StringView, wsw::StringView>, kMaxOtherKeysAndValues> otherKeysAndValues;
	std::optional<wsw::StringView> parsedMandatoryValues[6];
	wsw::StaticVector<wsw::StringView, kMaxTags> tags;

	wsw::DemoMetadataReader reader( data, dataSize );
	while( reader.hasNextPair() ) {
		const auto maybeKeyValue = reader.readNextPair();
		if( !maybeKeyValue ) {
			return;
		}
		const auto [key, value] = *maybeKeyValue;
		bool isAMandatoryKey = false;
		for( const auto &mandatoryKey: kMandatoryKeys ) {
			if( mandatoryKey.equalsIgnoreCase( key ) ) {
				parsedMandatoryValues[std::addressof( mandatoryKey ) - kMandatoryKeys] = value;
				isAMandatoryKey = true;
				break;
			}
		}
		if( !isAMandatoryKey ) {
			if( otherKeysAndValues.size() != otherKeysAndValues.capacity() ) {
				otherKeysAndValues.push_back( *maybeKeyValue );
			}
			// Continue pairs retrieval regardless of pairs capacity exhaustion
		}
	}

	while( reader.hasNextTag() ) {
		if( const auto maybeTag = reader.readNextTag() ) {
			if( tags.size() != tags.capacity() ) {
				tags.push_back( *maybeTag );
			} else {
				break;
			}
		} else {
			return;
		}
	}

	const auto valuesEnd = parsedMandatoryValues + std::size( parsedMandatoryValues );
	if( std::find( parsedMandatoryValues, valuesEnd, std::nullopt ) == valuesEnd ) {
		if( const auto maybeTimestamp = wsw::toNum<uint64_t>( *parsedMandatoryValues[1] ) ) {
			if( const auto maybeDuration = wsw::toNum<int>( *parsedMandatoryValues[2] ) ) {
				// TODO: Looking forward to being able using designated initializers
				MetadataEntry entry;
				entry.fileNameIndex  = stringData->add( fileName );
				entry.fileNameHash   = wsw::HashedStringView( fileName ).getHash();
				entry.baseNameSpan   = baseNameSpan;
				entry.hashBinIndex   = entry.fileNameHash % kNumBins;
				entry.rawTimestamp   = *maybeTimestamp;
				entry.timestamp      = QDateTime::fromSecsSinceEpoch( *maybeTimestamp );
				entry.sectionDate    = entry.timestamp.date();
				entry.duration       = *maybeDuration;
				entry.serverNameIndex   = stringData->add( *parsedMandatoryValues[0] );
				entry.mapNameIndex      = stringData->add( *parsedMandatoryValues[3] );
				entry.mapChecksumIndex  = stringData->add( *parsedMandatoryValues[4] );
				entry.gametypeIndex 	= stringData->add( *parsedMandatoryValues[5] );
				entry.numOtherKeysAndValues = 0;
				for( const auto &[key, value]: otherKeysAndValues ) {
					const auto keyIndex   = stringData->add( key );
					const auto valueIndex = stringData->add( value );
					entry.otherKeysAndValues[entry.numOtherKeysAndValues++] = { keyIndex, valueIndex };
				}
				for( const auto &tag: tags ) {
					entry.tagIndices[entry.numTags++] = stringData->add( tag );
				}
				entries->emplace_back( std::move( entry ) );
			}
		}
	}
}

void DemosResolver::takeResolveTaskResult( ResolveTaskResult *result ) {
	if( result ) {
		m_taskResultsToProcess.push_back( result );
	}
	m_numCompletedTasks++;
	const auto progress = (int)( ( (float)m_numCompletedTasks * 100.0f ) / (float)m_numPendingTasks );
	Q_EMIT progressUpdated( progress );
	if( m_numPendingTasks == m_numCompletedTasks ) {
		processTaskResults();
		Q_EMIT resolveTasksReady( true );
	}
}

void DemosResolver::processTaskResults() {
	for( ResolveTaskResult *const result: m_taskResultsToProcess ) {
		for( MetadataEntry &entry: result->entries ) {
			// Link to parent
			entry.parent = result;
			// Link to hash bin by name
			wsw::link( std::addressof( entry ), &m_hashBins[entry.hashBinIndex] );
		}
		result->numRefs = (int)result->entries.size();
		wsw::link( result, &m_taskResultsHead );
	}

	m_taskResultsToProcess.clear();
	updateDefaultDisplayedList();
}

void DemosResolver::updateDefaultDisplayedList() {
	m_entries.clear();
	for( ResolveTaskResult *result = m_taskResultsHead; result; result = result->next ) {
		for( const MetadataEntry &entry: result->entries ) {
			m_entries.push_back( std::addressof( entry ) );
		}
	}

	// Sort the default list by date
	const auto cmp = []( const MetadataEntry *lhs, const MetadataEntry *rhs ) {
		return lhs->rawTimestamp < rhs->rawTimestamp;
	};
	std::sort( m_entries.begin(), m_entries.end(), cmp );
}

void DemosResolver::runQuery() {
	if( m_lastQuery.empty() ) {
		// Just reset
		// This has been still required to force the model update
		Q_EMIT runQueryTasksReady( true );
		return;
	}

	assert( m_lastQueryResults.empty() );
	const wsw::StringView lastQueryView( m_lastQuery.asView() );
	for( unsigned index = 0; index < m_entries.size(); ++index ) {
		const MetadataEntry *const entry = m_entries[index];
		// Just basic for now
		if( entry->getFileName().contains( lastQueryView ) ) {
			m_lastQueryResults.push_back( index );
		} else if( entry->getServerName().contains( lastQueryView ) ) {
			m_lastQueryResults.push_back( index );
		} else if( entry->getMapName().contains( lastQueryView ) ) {
			m_lastQueryResults.push_back( index );
		}
	}

	Q_EMIT runQueryTasksReady( true );
}

}