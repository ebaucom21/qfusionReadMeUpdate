#ifndef QFUSION_SNAP_H
#define QFUSION_SNAP_H

#define SNAP_MAX_DEMO_META_DATA_SIZE    16 * 1024

// define this 0 to disable compression of demo files
#define SNAP_DEMO_GZ                    FS_GZ

void SNAP_ParseBaseline( struct msg_s *msg, entity_state_t *baselines );
void SNAP_SkipFrame( struct msg_s *msg, struct snapshot_s *header );
struct snapshot_s *SNAP_ParseFrame( struct msg_s *msg, struct snapshot_s *lastFrame, int *suppressCount,
									struct snapshot_s *backup, entity_state_t *baselines, int showNet );

void SNAP_WriteFrameSnapToClient( const struct ginfo_s *gi, struct client_s *client, struct msg_s *msg,
								  int64_t frameNum, int64_t gameTime,
								  const entity_state_t *baselines, const struct client_entities_s *client_entities,
								  int numcmds, const gcommand_t *commands, const char *commandsData );

// Use PVS culling for sounds.
// Note: changes gameplay experience, use with caution.
#define SNAP_HINT_CULL_SOUND_WITH_PVS     ( 1 )
// Try determining visibility via raycasting in collision world.
// Might lead to false negatives and heavy CPU load,
// but overall is recommended to use in untrusted environments.
#define SNAP_HINT_USE_RAYCAST_CULLING     ( 2 )
// Cull entities that are not in the front hemisphere of viewer.
// Currently it is a last hope against cheaters in an untrusted environment,
// but would be useful for slowly-turning vehicles in future.
#define SNAP_HINT_USE_VIEW_DIR_CULLING    ( 4 )
// If an entity would have been culled without attached sound events,
// but cannot be culled due to mentioned attachments presence,
// transmit fake angles, etc. to confuse a wallhack user
#define SNAP_HINT_SHADOW_EVENTS_DATA      ( 8 )

// Names for vars corresponding to the hint flags
#define SNAP_VAR_CULL_SOUND_WITH_PVS     ( "sv_snap_aggressive_sound_culling" )
#define SNAP_VAR_USE_RAYCAST_CULLING     ( "sv_snap_players_raycast_culling" )
#define SNAP_VAR_USE_VIEWDIR_CULLING     ( "sv_snap_aggressive_fov_culling" )
#define SNAP_VAR_SHADOW_EVENTS_DATA      ( "sv_snap_shadow_events_data" )

void SNAP_BuildClientFrameSnap( struct cmodel_state_s *cms, struct ginfo_s *gi, int64_t frameNum, int64_t timeStamp,
								struct fatvis_s *fatvis, struct client_s *client,
								const game_state_t *gameState, const ReplicatedScoreboardData *scoreboardData,
								struct client_entities_s *client_entities, int snapHintFlags );

void SNAP_FreeClientFrames( struct client_s *client );

void SNAP_RecordDemoMessage( int demofile, struct msg_s *msg, int offset );
int SNAP_ReadDemoMessage( int demofile, struct msg_s *msg );

void SNAP_BeginDemoRecording( int demofile, unsigned int spawncount, unsigned int snapFrameTime,
							  const char *sv_name, unsigned int sv_bitflags, struct purelist_s *purelist,
							  char *configstrings, entity_state_t *baselines );

namespace wsw { class ConfigStringStorage; }

void SNAP_BeginDemoRecording( int demofile, unsigned int spawncount, unsigned int snapFrameTime,
							  const char *sv_name, unsigned int sv_bitflags, struct purelist_s *purelist,
							  const wsw::ConfigStringStorage &configStrings, entity_state_t *baselines );

void SNAP_StopDemoRecording( int demofile );
void SNAP_WriteDemoMetaData( const char *filename, const char *meta_data, size_t meta_data_realsize );
size_t SNAP_ClearDemoMeta( char *meta_data, size_t meta_data_max_size );
size_t SNAP_ReadDemoMetaData( int demofile, char *meta_data, size_t meta_data_size );

#include "wswstaticvector.h"
#include "wswstringview.h"
#include "wswstdtypes.h"

namespace wsw {

// These keys are mandatory.
// We could have settled on fixed binary offsets instead of keys but an extensible k/v set is a better approach.
const wsw::StringView kDemoKeyServerName( "ServerName"_asView );
const wsw::StringView kDemoKeyTimestamp( "Timestamp"_asView );
const wsw::StringView kDemoKeyDuration( "Duration"_asView );
const wsw::StringView kDemoKeyMapName( "MapName"_asView );
const wsw::StringView kDemoKeyMapChecksum( "MapChecksum"_asView );
const wsw::StringView kDemoKeyGametype( "Gametype"_asView );

const wsw::StringView kDemoTagSinglePov( "SinglePOV" );
const wsw::StringView kDemoTagMultiPov( "MultiPOV" );

class DemoMetadataWriter {
	char *const m_basePtr;
	unsigned m_writeOff { 0 };
	unsigned m_numPairsWritten { 0 };
	unsigned m_numTagsWritten { 0 };
	bool m_incomplete { false };

	[[nodiscard]]
	auto freeBytesLeft() const -> unsigned { return SNAP_MAX_DEMO_META_DATA_SIZE - m_writeOff; }

	[[nodiscard]]
	bool tryAppendingTag( const wsw::StringView &tag ) {
		assert( m_numPairsWritten );
		if( tag.length() + 1 <= freeBytesLeft() ) {
			tag.copyTo( m_basePtr + m_writeOff, freeBytesLeft() );
			assert( m_basePtr[m_writeOff + tag.length()] == '\0' );
			m_writeOff += tag.length() + 1;
			return true;
		}
		return false;
	}

	[[nodiscard]]
	bool tryAppendingPair( const wsw::StringView &key, const wsw::StringView &value ) {
		assert( !m_numTagsWritten );
		if( key.length() + value.length() + 2 <= freeBytesLeft() ) {
			key.copyTo( m_basePtr + m_writeOff, freeBytesLeft() );
			assert( m_basePtr[m_writeOff + key.length()] == '\0' );
			m_writeOff += key.length() + 1;

			value.copyTo( m_basePtr + m_writeOff, freeBytesLeft() );
			assert( m_basePtr[m_writeOff + value.length()] == '\0' );
			m_writeOff += value.length() + 1;

			return true;
		}
		return false;
	}
public:
	explicit DemoMetadataWriter( char *data ) : m_basePtr( data ) {
		std::memset( data, 0, SNAP_MAX_DEMO_META_DATA_SIZE );
		m_writeOff += 8;
	}

	[[maybe_unused]]
	bool writePair( const wsw::StringView &key, const wsw::StringView &value ) {
		if( !m_incomplete ) {
			if( m_numTagsWritten ) {
				throw std::logic_error( "Attempt to write a key-value pair after tags" );
			}
			if( tryAppendingPair( key, value ) ) {
				m_numPairsWritten++;
				return true;
			}
			m_incomplete = true;
		}
		return false;
	}

	[[maybe_unused]]
	bool writeTag( const wsw::StringView &tag ) {
		if( !m_incomplete ) {
			if( !m_numPairsWritten ) {
				throw std::logic_error( "Attempt to write a tag prior to key-value pairs" );
			}
			if( tryAppendingTag( tag ) ) {
				m_numTagsWritten++;
				return true;
			}
			m_incomplete = true;
		}
		return false;
	}

	[[nodiscard]]
	auto markCurrentResult() const -> std::pair<size_t, bool> {
		uint32_t numPairsToMark = LittleLong( m_numPairsWritten );
		uint32_t numTagsToMark = LittleLong( m_numTagsWritten );
		std::memcpy( m_basePtr + 0, &numPairsToMark, 4 );
		std::memcpy( m_basePtr + 4, &numTagsToMark, 4 );
		return { m_writeOff, !m_incomplete };
	}
};

class DemoMetadataReader {
	const char *const m_basePtr;
	const unsigned m_dataSize;
	unsigned m_readOff { 0 };
	unsigned m_numPairs { 0 };
	unsigned m_numTags { 0 };
	unsigned m_numPairsRead { 0 };
	unsigned m_numTagsRead { 0 };
public:
	DemoMetadataReader( const char *data, unsigned dataSize )
		: m_basePtr( data ), m_dataSize( dataSize ) {
		assert( m_basePtr[dataSize] == '\0' );
		if( dataSize >= 8 ) {
			unsigned numPairs, numTags;
			std::memcpy( &numPairs, data + 0, 4 );
			std::memcpy( &numTags, data + 4, 4 );
			m_numPairs = LittleLong( numPairs );
			m_numTags = LittleLong( numTags );
			m_readOff += 8;
		}
	}

	[[nodiscard]]
	bool hasNextPair() const { return m_numPairsRead < m_numPairs; }

	[[nodiscard]]
	auto readNextPair() -> std::optional<std::pair<wsw::StringView, wsw::StringView>> {
		if( !hasNextPair() ) {
			throw std::logic_error( "No key-value pairs to read" );
		}
		const auto keyLen = std::strlen( m_basePtr + m_readOff );
		if( m_readOff + keyLen + 1 < m_dataSize ) {
			assert( m_basePtr[m_readOff + keyLen] == '\0' );
			const auto valueLen = std::strlen( m_basePtr + m_readOff + keyLen + 1 );
			if( m_readOff + keyLen + valueLen + 2 <= m_dataSize ) {
				assert( m_basePtr[m_readOff + keyLen + valueLen + 1] == '\0' );
				wsw::StringView key( m_basePtr + m_readOff, keyLen, wsw::StringView::ZeroTerminated );
				wsw::StringView value( m_basePtr + m_readOff + keyLen + 1, valueLen, wsw::StringView::ZeroTerminated );
				m_readOff += keyLen + valueLen + 2;
				m_numPairsRead++;
				return std::make_pair( key, value );
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	bool hasNextTag() const { return m_numTagsRead < m_numTags; }

	[[nodiscard]]
	auto readNextTag() -> std::optional<wsw::StringView> {
		if( hasNextPair() ) {
			throw std::logic_error( "Reading of key-value pairs is incomplete" );
		}
		if( !hasNextTag() ) {
			throw std::logic_error( "No tags to read" );
		}
		const auto tagLen = std::strlen( m_basePtr + m_readOff );
		if( m_readOff + tagLen < m_dataSize ) {
			assert( m_basePtr[m_readOff + tagLen] == '\0' );
			wsw::StringView tag( m_basePtr + m_readOff, tagLen, wsw::StringView::ZeroTerminated );
			m_readOff += tagLen + 1;
			m_numTagsRead++;
			return tag;
		}
		return std::nullopt;
	}
};

}

#endif

