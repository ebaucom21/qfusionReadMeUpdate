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

// Preserve the template signature for structural compatibility	regardless of the build kind
template <typename SpansBuffer = wsw::StaticVector<std::pair<uint16_t, uint16_t>, 64>>
class DemoMetadataWriter {
#ifndef PUBLIC_BUILD
	SpansBuffer m_spansBuffer;
#endif
	char *const m_basePtr;
	unsigned m_writeOff { 0 };
	bool m_incomplete { false };

	[[nodiscard]]
	auto freeBytesLeft() const -> unsigned { return SNAP_MAX_DEMO_META_DATA_SIZE - m_writeOff; }

// Checking for duplicated keys makes sense only in developer builds for now
#ifndef PUBLIC_BUILD
	[[nodiscard]]
	auto findExistingKeyIndex( const wsw::StringView &key ) const -> std::optional<unsigned> {
		for( unsigned i = 0; i < m_spansBuffer.size(); i += 2 ) {
			const auto [keyOff, keyLen] = m_spansBuffer[i + 0];
			const wsw::StringView existingKey( m_basePtr + keyOff, keyLen, wsw::StringView::ZeroTerminated );
			if( existingKey.equalsIgnoreCase( key ) ) {
				return i;
			}
		}
		return std::nullopt;
	}
#endif

	[[nodiscard]]
	bool tryAppendingValue( const wsw::StringView &key, const wsw::StringView &value ) {
		if( key.length() + value.length() + 2 <= freeBytesLeft() ) {
			[[maybe_unused]] const auto keyOff = m_writeOff;
			key.copyTo( m_basePtr + m_writeOff, freeBytesLeft() );
			assert( m_basePtr[m_writeOff + key.length()] == '\0' );
			m_writeOff += key.length() + 1;

			[[maybe_unused]] const auto valueOff = m_writeOff;
			value.copyTo( m_basePtr + m_writeOff, freeBytesLeft() );
			assert( m_basePtr[m_writeOff + value.length()] == '\0' );
			m_writeOff += value.length() + 1;

#ifndef PUBLIC_BUILD
			using OffType = typename std::remove_reference<decltype( m_spansBuffer.front().first )>::type;
			using LenType = typename std::remove_reference<decltype( m_spansBuffer.front().second )>::type;
			assert( keyOff <= std::numeric_limits<OffType>::max() );
			assert( key.length() <= std::numeric_limits<LenType>::max() );
			m_spansBuffer.emplace_back( std::make_pair( keyOff, (LenType)key.length() ) );
			assert( valueOff <= std::numeric_limits<OffType>::max() );
			assert( value.length() <= std::numeric_limits<LenType>::max() );
			m_spansBuffer.emplace_back( std::make_pair( valueOff, (LenType)value.length() ) );
#endif
			return true;
		}
		return false;
	}
public:
	explicit DemoMetadataWriter( char *basePtr ) : m_basePtr( basePtr ) {
		std::memset( basePtr, 0, SNAP_MAX_DEMO_META_DATA_SIZE );
	}

	void write( const wsw::StringView &key, const wsw::StringView &value ) {
#ifndef PUBLIC_BUILD
		if( !m_incomplete ) {
			if( findExistingKeyIndex( key ) == std::nullopt ) {
				m_incomplete |= !tryAppendingValue( key, value );
			} else {
				throw std::logic_error( "Supplying duplicated keys is currently disallowed" );
			}
		}
#else
		if( !m_incomplete ) {
			m_incomplete |= !tryAppendingValue( key, value );
		}
#endif
	}

	[[nodiscard]]
	auto resultSoFar() const -> std::pair<size_t, bool> { return { m_writeOff, !m_incomplete }; }
};

}

#endif

