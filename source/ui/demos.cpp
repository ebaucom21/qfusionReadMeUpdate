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
		{ FileName, "fileName" },
		{ MapName, "mapName" }
	};
}

auto DemosModel::rowCount( const QModelIndex & ) const -> int {
	return m_resolver->isReady() ? m_resolver->getCount() : 0;
}

[[nodiscard]]
static inline auto toQVariant( const wsw::StringView &view ) -> QVariant {
	return QByteArray( view.data(), view.size() );
}

auto DemosModel::data( const QModelIndex &index, int role ) const -> QVariant {
	assert( m_resolver->isReady() );
	if( index.isValid() ) {
		if( const int row = index.row(); (unsigned)row < (unsigned)m_resolver->getCount() ) {
			switch( role ) {
				case Section: return getEntry( row )->sectionDate;
				case Timestamp: return getEntry( row )->timestamp;
				case ServerName: return toQVariant( getEntry( row )->getServerName() );
				case FileName: return toQVariant( getEntry( row )->getFileName() );
				case MapName: return toQVariant( getEntry( row )->getMapName() );
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
	connect( this, &DemosResolver::taskCompleted, this, &DemosResolver::takeTaskResult, Qt::QueuedConnection );
	connect( this, &DemosResolver::backgroundTasksReady, this, &DemosResolver::setReady, Qt::QueuedConnection );
}

DemosResolver::~DemosResolver() {
	TaskResult *next;
	for( TaskResult *result = m_taskResultsHead; result; result = next ) {
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

void DemosResolver::update() {
	assert( m_isReady );

	setReady( false );

	Q_EMIT progressUpdated( QVariant() );

	m_threadPool.start( new EnumerateFilesTask( this ) );
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
		Q_EMIT backgroundTasksReady( true );
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
	TaskResult *taskResult = nullptr;
	try {
		taskResult = new TaskResult;
		for ( unsigned index = first; index < last; ++index ) {
			resolveMetadata( index, &taskResult->entries, &taskResult->storage );
		}
		Q_EMIT takeTaskResult( taskResult );
	} catch (...) {
		delete taskResult;
		Q_EMIT takeTaskResult( nullptr );
	}
}

void DemosResolver::resolveMetadata( unsigned index, wsw::Vector<MetadataEntry> *entries, StringDataStorage *storage ) {
	const wsw::StringView fileName( m_fileNameSpans[m_turn][index].data() );
	assert( fileName.isZeroTerminated() );

	int handle = 0;
	int length = FS_FOpenFile( fileName.data(), &handle, FS_READ | SNAP_DEMO_GZ );
	if( length > 0 ) {
		char metadata[SNAP_MAX_DEMO_META_DATA_SIZE];
		size_t realSize = SNAP_ReadDemoMetaData( handle, metadata, sizeof( metadata ) );
		if( realSize <= sizeof( metadata ) ) {
			parseMetadata( metadata, realSize, fileName, entries, storage );
		}
	}
	FS_FCloseFile( handle );
}

static wsw::StringView kMandatoryKeys[6] {
	kDemoKeyServerName, kDemoKeyTimestamp, kDemoKeyDuration, kDemoKeyMapName, kDemoKeyMapChecksum, kDemoKeyGametype
};

void DemosResolver::parseMetadata( const char *data, size_t dataSize,
								   const wsw::StringView &fileName,
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

void DemosResolver::takeTaskResult( TaskResult *result ) {
	if( result ) {
		m_taskResultsToProcess.push_back( result );
	}
	m_numCompletedTasks++;
	const auto progress = (int)( ( (float)m_numCompletedTasks * 100.0f ) / (float)m_numPendingTasks );
	Q_EMIT progressUpdated( progress );
	if( m_numPendingTasks == m_numCompletedTasks ) {
		processTaskResults();
		Q_EMIT backgroundTasksReady( true );
	}
}

void DemosResolver::processTaskResults() {
	for( TaskResult *const result: m_taskResultsToProcess ) {
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
	updateDisplayedList();
}

void DemosResolver::updateDisplayedList() {
	m_displayedEntries.clear();
	for( TaskResult *result = m_taskResultsHead; result; result = result->next ) {
		for( const MetadataEntry &entry: result->entries ) {
			m_displayedEntries.push_back( std::addressof( entry ) );
		}
	}

	// Sort by date for now
	const auto cmp = []( const MetadataEntry *lhs, const MetadataEntry *rhs ) {
		return lhs->rawTimestamp < rhs->rawTimestamp;
	};
	std::sort( m_displayedEntries.begin(), m_displayedEntries.end(), cmp );
}

}