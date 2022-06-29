/*
Copyright (C) 1997-2001 Id Software, Inc.

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
#include "g_local.h"
#include "chat.h"
#include "../qcommon/mmquery.h"
#include "g_gametypes.h"

#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstaticstring.h"

#include <cmath>
#include <cstdlib>
#include <chrono>
#include <new>
#include <utility>
#include <thread>

using wsw::operator""_asView;

static SingletonHolder<StatsowFacade> statsHolder;

//====================================================

static clientRating_t *g_ratingAlloc( const char *gametype, float rating, float deviation, mm_uuid_t uuid ) {
	auto *cr = (clientRating_t*)Q_malloc( sizeof( clientRating_t ) );
	if( !cr ) {
		return nullptr;
	}

	Q_strncpyz( cr->gametype, gametype, sizeof( cr->gametype ) - 1 );
	cr->rating = rating;
	cr->deviation = deviation;
	cr->next = nullptr;
	cr->uuid = uuid;

	return cr;
}

static clientRating_t *g_ratingCopy( const clientRating_t *other ) {
	return g_ratingAlloc( other->gametype, other->rating, other->deviation, other->uuid );
}

// free the list of clientRatings
static void g_ratingsFree( clientRating_t *list ) {
	clientRating_t *next;

	while( list ) {
		next = list->next;
		Q_free( list );
		list = next;
	}
}

void StatsowFacade::UpdateAverageRating() {
	clientRating_t avg;

	if( !ratingsHead ) {
		avg.rating = MM_RATING_DEFAULT;
		avg.deviation = MM_DEVIATION_DEFAULT;
	} else {
		Rating_AverageRating( &avg, ratingsHead );
	}

	// Com_Printf("g_serverRating: Updated server's skillrating to %f\n", avg.rating );

	trap_Cvar_ForceSet( "sv_skillRating", va( "%.0f", avg.rating ) );
}

void StatsowFacade::TransferRatings() {
	clientRating_t *cr, *found;
	edict_t *ent;
	Client *client;

	// shuffle the ratings back from game.ratings to clients->ratings and back
	// based on current gametype
	g_ratingsFree( ratingsHead );
	ratingsHead = nullptr;

	for( ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		client = ent->r.client;

		if( !client ) {
			continue;
		}
		if( !ent->r.inuse ) {
			continue;
		}

		// temphack for duplicate client entries
		found = Rating_FindId( ratingsHead, client->mm_session );
		if( found ) {
			continue;
		}

		found = Rating_Find( client->ratings, gs.gametypeName );

		// create a new default rating if this doesnt exist
		// DONT USE G_AddDefaultRating cause this will cause double entries in game.ratings
		if( !found ) {
			found = g_ratingAlloc( gs.gametypeName, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, client->mm_session );
			if( !found ) {
				continue;   // ??

			}
			found->next = client->ratings;
			client->ratings = found;
		}

		// add it to the games list
		cr = g_ratingCopy( found );
		cr->next = ratingsHead;
		ratingsHead = cr;
	}

	UpdateAverageRating();
}

// This doesnt update ratings, only inserts new default rating if it doesnt exist
// if gametype is NULL, use current gametype
clientRating_t *StatsowFacade::AddDefaultRating( edict_t *ent, const char *gametype ) {
	if( !gametype ) {
		gametype = gs.gametypeName;
	}

	auto *client = ent->r.client;
	if( !ent->r.inuse ) {
		return nullptr;
	}

	auto *cr = Rating_Find( client->ratings, gametype );
	if( !cr ) {
		cr = g_ratingAlloc( gametype, MM_RATING_DEFAULT, MM_DEVIATION_DEFAULT, ent->r.client->mm_session );
		if( !cr ) {
			return nullptr;
		}

		cr->next = client->ratings;
		client->ratings = cr;
	}

	TryUpdatingGametypeRating( client, cr, gametype );
	return cr;
}

// this inserts a new one, or updates the ratings if it exists
clientRating_t *StatsowFacade::AddRating( edict_t *ent, const char *gametype, float rating, float deviation ) {
	if( !gametype ) {
		gametype = gs.gametypeName;
	}

	auto *client = ent->r.client;
	if( !ent->r.inuse ) {
		return nullptr;
	}

	auto *cr = Rating_Find( client->ratings, gametype );
	if( cr ) {
		cr->rating = rating;
		cr->deviation = deviation;
	} else {
		cr = g_ratingAlloc( gametype, rating, deviation, ent->r.client->mm_session );
		if( !cr ) {
			return nullptr;
		}

		cr->next = client->ratings;
		client->ratings = cr;
	}

	TryUpdatingGametypeRating( client, cr, gametype );
	return cr;
}

void StatsowFacade::TryUpdatingGametypeRating( const Client *client,
											   const clientRating_t *addedRating,
											   const char *addedForGametype ) {
	// If the gametype is not a current gametype
	if( strcmp( addedForGametype, gs.gametypeName ) != 0 ) {
		return;
	}

	// add this rating to current game-ratings
	auto *found = Rating_FindId( ratingsHead, client->mm_session );
	if( !found ) {
		found = g_ratingCopy( addedRating );
		if( found ) {
			found->next = ratingsHead;
			ratingsHead = found;
		}
	} else {
		// update values
		found->rating = addedRating->rating;
		found->deviation = addedRating->deviation;
	}

	UpdateAverageRating();
}

// removes all references for given entity
void StatsowFacade::RemoveRating( edict_t *ent ) {
	Client *client;
	clientRating_t *cr;

	client = ent->r.client;

	// first from the game
	cr = Rating_DetachId( &ratingsHead, client->mm_session );
	if( cr ) {
		Q_free( cr );
	}

	// then the clients own list
	g_ratingsFree( client->ratings );
	client->ratings = nullptr;

	UpdateAverageRating();
}

RaceRun::RaceRun( const struct Client *client_, int numSectors_, uint32_t *times_ )
	: clientSessionId( client_->mm_session ), numSectors( numSectors_ ), times( times_ ) {
	assert( numSectors_ > 0 );
	// Check alignment of the provided times array
	assert( ( (uintptr_t)( times_ ) % 8 ) == 0 );

	SaveNickname( client_ );
}

void RaceRun::SaveNickname( const struct Client *client ) {
	if( client->mm_session.IsValidSessionId() ) {
		nickname[0] = '\0';
		return;
	}

	Q_strncpyz( nickname, client->netname.data(), MAX_NAME_BYTES );
}

RaceRun *StatsowFacade::NewRaceRun( const edict_t *ent, int numSectors ) {
	auto *const client = ent->r.client;

	// TODO: Raise an exception
	if( !ent->r.inuse || !client  ) {
		return nullptr;
	}

	auto *run = client->stats.currentRun;
	uint8_t *mem = nullptr;
	if( run ) {
		// Check whether we can actually reuse the underlying memory chunk
		if( run->numSectors == numSectors ) {
			mem = (uint8_t *)run;
		}
		run->~RaceRun();
		if( !mem ) {
			Q_free( run );
		}
	}

	if( !mem ) {
		mem = (uint8_t *)Q_malloc( sizeof( RaceRun ) + ( numSectors + 1 ) * sizeof( uint32_t ) );
	}

	static_assert( alignof( RaceRun ) == 8, "Make sure we do not need to align the times array separately" );
	auto *times = ( uint32_t * )( mem + sizeof( RaceRun ) );
	auto *newRun = new( mem )RaceRun( client, numSectors, times );
	// Set the constructed run as a current one for the client
	return ( client->stats.currentRun = newRun );
}

void StatsowFacade::ValidateRaceRun( const char *tag, const edict_t *owner ) {
	// TODO: Validate this at script wrapper layer and throw an exception
	// or throw a specific subclass of exceptions here and catch at script wrappers layers
	if( !owner ) {
		G_Error( "%s: The owner entity is not specified\n", tag );
	}

	const auto *client = owner->r.client;
	if( !client ) {
		G_Error( "%s: The owner entity is not a client\n", tag );
	}

	const auto *run = client->stats.currentRun;
	if( !run ) {
		G_Error( "%s: The client does not have a current race run\n", tag );
	}
}

void StatsowFacade::SetSectorTime( edict_t *owner, int sector, uint32_t time ) {
	const char *tag = "StatsowFacade::SetSectorTime()";
	ValidateRaceRun( tag, owner );

	auto *const client = owner->r.client;
	auto *const run = client->stats.currentRun;

	if( sector < 0 || sector >= run->numSectors ) {
		G_Error( "%s: the sector %d is out of valid bounds [0, %d)", tag, sector, run->numSectors );
	}

	run->times[sector] = time;
	// The nickname might have been changed, save it
	run->SaveNickname( client );
}

RunStatusQuery *StatsowFacade::CompleteRun( edict_t *owner, uint32_t finalTime, const char *runTag ) {
	ValidateRaceRun( "StatsowFacade::CompleteRun()", owner );

	auto *const client = owner->r.client;
	auto *const run = client->stats.currentRun;

	run->times[run->numSectors] = finalTime;
	run->utcTimestamp = game.utcTimeMillis;
	run->SaveNickname( client );

	// Transfer the ownership over the run
	client->stats.currentRun = nullptr;
	// We pass the tag as an argument since it's memory is not intended to be in the run ownership
	return SendRaceRunReport( run, runTag );
}

RunStatusQuery::RunStatusQuery( StatsowFacade *parent_, QueryObject *query_, const mm_uuid_t &runId_ )
	: createdAt( trap_Milliseconds() ), parent( parent_ ), query( query_ ), runId( runId_ ) {
	query->SetRaceRunId( runId_ );
}

RunStatusQuery::~RunStatusQuery() {
	if( query ) {
		trap_MM_DeleteQuery( query );
	}
}

void RunStatusQuery::CheckReadyForAccess( const char *tag ) const {
	// TODO: Throw an exception that is intercepted at script bindings layer
	if( outcome == 0 ) {
		G_Error( "%s: The object is not in a ready state\n", tag );
	}
}

void RunStatusQuery::CheckValidForAccess( const char *tag ) const {
	CheckReadyForAccess( tag );
	// TODO: Throw an exception that is intercepted at script bindings layer
	if( outcome <= 0 ) {
		G_Error( "%s: The object is not in a valid state to access a property\n", tag );
	}
}

void RunStatusQuery::ScheduleRetry( int64_t millisNow ) {
	query->ResetForRetry();
	nextRetryAt = millisNow + 1000;
	outcome = 0;
}

void RunStatusQuery::Update( int64_t millisNow ) {
	constexpr const char *tag = "RunStatusQuery::Update()";

	// Set it to a negative value by default as this is prevalent for all conditions in this method
	outcome = -1;

	// If the query has already been disposed
	if( !query ) {
		return;
	}

	/**
	 * Deletes the query and nullifies it's reference in the captured instance on scope exit.
	 * Immediate disposal of the underlying query object is important as keeping it is relatively expensive.
	 */
	struct QueryDeleter {
		RunStatusQuery *captured;

		explicit QueryDeleter( RunStatusQuery *captured_ ): captured( captured_ ) {}

		~QueryDeleter() {
			// Delete on success and on failure but keep if the outcome is not known yet
			if( captured->outcome != 0 ) {
				trap_MM_DeleteQuery( captured->query );
				captured->query = nullptr;
			}
		}
	} deleter( this );

	// Launch the query for status polling in this case
	if( !query->HasStarted() ) {
		if( millisNow >= nextRetryAt ) {
			trap_MM_SendQuery( query );
		}
		outcome = 0;
		return;
	}

	char buffer[UUID_BUFFER_SIZE];
	if( millisNow - createdAt > 30 * 1000 ) {
		G_Printf( S_COLOR_YELLOW "%s: The query for %s has timed out\n", tag, runId.ToString( buffer ) );
		return;
	}

	if( !query->IsReady() ) {
		outcome = 0;
		return;
	}

	if( !query->HasSucceeded() ) {
		if( query->ShouldRetry() ) {
			ScheduleRetry( millisNow );
			return;
		}
		const char *format = S_COLOR_YELLOW "%s: The underlying query for %s has unrecoverable errors\n";
		G_Printf( format, tag, runId.ToString( buffer ) );
		return;
	}

	const double status = query->GetRootDouble( "status", 0.0f );
	if( status == 0.0f ) {
		G_Printf( S_COLOR_YELLOW "%s: The query response has missing or zero `status` field\n", tag );
	}

	// Wait for a run arrival at the stats server in this case
	if( status < 0 ) {
		ScheduleRetry( millisNow );
		return;
	}

	if( ( personalRank = GetQueryField( "personal_rank" ) ) < 0 ) {
		return;
	}

	if( ( worldRank = GetQueryField( "world_rank" ) ) < 0 ) {
		return;
	}

	outcome = +1;
}

int RunStatusQuery::GetQueryField( const char *fieldName ) {
	double value = query->GetRootDouble( fieldName, std::numeric_limits<double>::infinity() );
	if( !std::isfinite( value ) ) {
		return -1;
	}
	const char *tag = "RunStatusQuery::GetQueryField()";
	if( value < 0 ) {
		const char *fmt = "%s%s: The value %ld for field %s was negative. Don't use this method if the value is valid\n";
		G_Printf( fmt, S_COLOR_YELLOW, tag, value, fieldName );
		return -1;
	}
	if( (double)( (volatile int)value ) != value ) {
		const char *fmt = "%s%s: The value %ld for field %s cannot be exactly represented as int\n";
		G_Printf( fmt, S_COLOR_YELLOW, tag, value, fieldName );
	}
	return (int)value;
}

void RunStatusQuery::DeleteSelf() {
	parent->DeleteRunStatusQuery( this );
}

RunStatusQuery *StatsowFacade::AddRunStatusQuery( const mm_uuid_t &runId ) {
	QueryObject *underlyingQuery = trap_MM_NewPostQuery( "server/race/runStatus" );
	if( !underlyingQuery ) {
		return nullptr;
	}

	void *mem = Q_malloc( sizeof( RunStatusQuery ) );
	auto *statusQuery = new( mem )RunStatusQuery( this, underlyingQuery, runId );
	return wsw::link( statusQuery, &runQueriesHead );
}

void StatsowFacade::DeleteRunStatusQuery( RunStatusQuery *query ) {
	wsw::unlink( query, &runQueriesHead );
	query->~RunStatusQuery();
	Q_free( query );
}

void StatsowFacade::AddToRacePlayTime( const Client *client, int64_t timeToAdd ) {
	// Put this check first so we get warnings on misuse of the API even if there's no Statsow connection
	if( timeToAdd <= 0 ) {
		const char *tag = "StatsowFacade::AddToRacePlayTime()";
		G_Printf( S_COLOR_YELLOW "%s: The time to add %" PRIi64 " <= 0\n", tag, timeToAdd );
		return;
	}

	if( !IsValid() ) {
		return;
	}

	// While playing anonymous and making records is allowed in race don't report playtimes of these players
	if( !client->mm_session.IsValidSessionId() ) {
		return;
	}

	PlayTimeEntry *entry = FindPlayTimeEntry( client->mm_session );
	if( !entry ) {
		entry = bufferedPlayTimes.New( client->mm_session );
	}

	entry->timeToAdd += timeToAdd;
}

StatsowFacade::PlayTimeEntry *StatsowFacade::FindPlayTimeEntry( const mm_uuid_t &clientSessionId ) {
	StatsSequence<PlayTimeEntry>::iterator it( bufferedPlayTimes.begin() );
	StatsSequence<PlayTimeEntry>::iterator end( bufferedPlayTimes.end() );
	for (; it != end; ++it ) {
		PlayTimeEntry *entry = &( *it );
		if( entry->clientSessionId == clientSessionId ) {
			return entry;
		}
	}

	return nullptr;
}

void StatsowFacade::FlushRacePlayTimes() {
	if( !IsValid() ) {
		return;
	}

	if( bufferedPlayTimes.empty() ) {
		return;
	}

	QueryObject *query = trap_MM_NewPostQuery( "server/race/timeReport" );
	if( !query ) {
		return;
	}

	JsonWriter writer( query->RequestJsonRoot() );
	writer << "gametype" << g_gametype->string;
	writer << "map_name" << level.mapname;

	writer << "entries" << '[';
	{
		for( const PlayTimeEntry &entry: bufferedPlayTimes ) {
			writer << '{' << "session_id" << entry.clientSessionId << "time_to_add" << entry.timeToAdd << '}';
		}
	}
	writer << ']';

	trap_MM_EnqueueReport( query );
}

static SingletonHolder<StatsowFacade> statsInstanceHolder;

void StatsowFacade::Init() {
	statsInstanceHolder.init();
}

void StatsowFacade::Shutdown() {
	statsInstanceHolder.shutdown();
}

StatsowFacade *StatsowFacade::Instance() {
	return statsInstanceHolder.instance();
}

void StatsowFacade::ClearEntries() {
	ClientEntry *next;
	for( ClientEntry *e = clientEntriesHead; e; e = next ) {
		next = e->next;
		e->~ClientEntry();
		Q_free( e );
	}

	clientEntriesHead = nullptr;
}

void StatsowFacade::OnClientHadPlaytime( const Client *client ) {
	if( !IsValid() ) {
		return;
	}

	if( GS_RaceGametype() ) {
		return;
	}

	const char *reason = nullptr;
	const edict_t *ent = game.edicts + ENTNUM( client );
	// Check whether it's a bot first (they do not have valid session ids as well)
	if( ent->ai ) {
		if( ent->bot ) {
			reason = "A bot had a play-time";
		}
		// The report is still valid if it's an AI but not a bot.
		// TODO: Are logged frags valid as well in this case?
	} else {
		if( !client->mm_session.IsValidSessionId() ) {
			reason = va( "An anonymous player `%s` had a play-time", client->netname.data() );
		}
	}

	if( !reason ) {
		return;
	}

	// Print to everybody
	G_PrintMsg( nullptr, S_COLOR_YELLOW "%s. Discarding match report...\n", reason );
	isDiscarded = true;
	SendMatchAbortedReport();
}

void StatsowFacade::OnClientDisconnected( edict_t *ent ) {
	if( GS_RaceGametype() ) {
		return;
	}

	if( ent->r.client->team == TEAM_SPECTATOR ) {
		return;
	}

	const bool isMatchOver = GS_MatchState() == MATCH_STATE_POSTMATCH;
	// If not in match-time and not in post-match ignore this
	if( !isMatchOver && ( GS_MatchState() != MATCH_STATE_PLAYTIME ) ) {
		return;
	}

	ChatHandlersChain::instance()->onClientDisconnected( ent );
	AddPlayerReport( ent, isMatchOver );
}

void StatsowFacade::OnClientJoinedTeam( edict_t *ent, int newTeam ) {
	ChatHandlersChain::instance()->onClientJoinedTeam( ent, newTeam );

	if( !IsValid() ) {
		return;
	}

	if( ent->r.client->team == TEAM_SPECTATOR ) {
		return;
	}
	if( newTeam != TEAM_SPECTATOR ) {
		return;
	}

	if ( GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	StatsowFacade::Instance()->AddPlayerReport( ent, false );
}

void StatsowFacade::OnMatchStateLaunched( int oldState, int newState ) {
	if( isDiscarded ) {
		return;
	}

	// Send any reports (if needed) on transition from "post-match" state
	if( newState != MATCH_STATE_POSTMATCH && oldState == MATCH_STATE_POSTMATCH ) {
		SendFinalReport();
	}

	if( newState == MATCH_STATE_PLAYTIME && oldState != MATCH_STATE_PLAYTIME ) {
		SendMatchStartedReport();
	}
}

void StatsowFacade::SendGenericMatchStateEvent( const char *event ) {
	// This should be changed if a stateful competitive race gametype is really implemented
	if( GS_RaceGametype() ) {
		return;
	}

	if( !IsValid() ) {
		return;
	}

	constexpr const char *tag = "StatsowFacade::SendGenericMatchStateEvent()";
	G_Printf( "%s: Sending `%s` event\n", tag, event );

	char url[MAX_STRING_CHARS];
	va_r( url, sizeof( url ), "server/match/%s", event );

	QueryObject *query = trap_MM_NewPostQuery( url );
	if( !query ) {
		G_Printf( S_COLOR_YELLOW "%s: The server executable has not created a query object\n", tag );
		return;
	}

	// Get session ids of all players that had playtime

	const auto *edicts = game.edicts;

	int numActiveClients = 0;
	int activeClientNums[MAX_CLIENTS];
	for( int i = 0; i < gs.maxclients; ++i ) {
		const edict_t *ent = edicts + i + 1;
		if( !ent->r.inuse || !ent->r.client ) {
			continue;
		}
		if( ent->s.team == TEAM_SPECTATOR ) {
			continue;
		}
		if( trap_GetClientState( i ) < CS_SPAWNED ) {
			continue;
		}
		// TODO: This is a sequential search
		if( FindEntryById( ent->r.client->mm_session ) ) {
			continue;
		}
		activeClientNums[numActiveClients++] = i;
	}

	int numParticipants = numActiveClients;
	// TODO: Cache number of entries?
	for( ClientEntry *entry = clientEntriesHead; entry; entry = entry->next ) {
		numParticipants++;
	}

	// Chosen having bomb in mind
	mm_uuid_t idsLocalBuffer[10];
	mm_uuid_t *idsBuffer = idsLocalBuffer;
	if( numParticipants > 10 ) {
		idsBuffer = (mm_uuid_t *)::malloc( numParticipants * sizeof( mm_uuid_t ) );
	}

	int numIds = 0;
	for( int i = 0; i < numActiveClients; ++i ) {
		const edict_t *ent = edicts + 1 + activeClientNums[i];
		idsBuffer[numIds++] = ent->r.client->mm_session;
	}

	for( ClientEntry *entry = clientEntriesHead; entry; entry = entry->next ) {
		idsBuffer[numIds++] = entry->mm_session;
	}

	assert( numIds == numParticipants );

	// Check clients that are currently in-game
	// Check clients that have quit the game

	query->SetMatchId( trap_GetConfigString( CS_MATCHUUID ) );
	query->SetGametype( gs.gametypeName );
	query->SetParticipants( idsBuffer, idsBuffer + numIds );

	if( idsBuffer != idsLocalBuffer ) {
		::free( idsBuffer );
	}

	trap_MM_EnqueueReport( query );
}

void StatsowFacade::AddPlayerReport( edict_t *ent, bool final ) {
	char uuid_buffer[UUID_BUFFER_SIZE];

	if( !ent->r.inuse ) {
		return;
	}

	// This code path should not be entered by race gametypes
	assert( !GS_RaceGametype() );

	if( !IsValid() ) {
		return;
	}

	const auto *cl = ent->r.client;
	if( !cl || cl->team == TEAM_SPECTATOR ) {
		return;
	}

	constexpr const char *format = "StatsowFacade::AddPlayerReport(): %s" S_COLOR_WHITE " (%s)\n";
	G_Printf( format, cl->netname.data(), cl->mm_session.ToString( uuid_buffer ) );

	ClientEntry *entry = FindEntryById( cl->mm_session );
	if( entry ) {
		AddToExistingEntry( ent, final, entry );
	} else {
		entry = NewPlayerEntry( ent, final );
		// put it to the list
		entry->next = clientEntriesHead;
		clientEntriesHead = entry;
	}

	ChatHandlersChain::instance()->addToReportStats( ent, &entry->respectStats );
}

void StatsowFacade::AddToExistingEntry( edict_t *ent, bool final, ClientEntry *e ) {
	auto *const cl = ent->r.client;

	// we can merge
	Q_strncpyz( e->netname, cl->netname.data(), sizeof( e->netname ) );
	e->team = cl->team;
	e->timePlayed += ( level.time - cl->teamStateTimestamp ) / 1000;
	e->final = final;

	e->stats.awards += cl->stats.awards;
	e->stats.score += cl->stats.score;

	for( const auto &keyAndValue : cl->stats ) {
		e->stats.AddToEntry( keyAndValue );
	}

	for( int i = 0; i < ( AMMO_TOTAL - AMMO_GUNBLADE ); i++ ) {
		auto &stats = e->stats;
		const auto &thatStats = cl->stats;
		stats.accuracy_damage[i] += thatStats.accuracy_damage[i];
		stats.accuracy_frags[i] += thatStats.accuracy_frags[i];
		stats.accuracy_hits[i] += thatStats.accuracy_hits[i];
		stats.accuracy_hits_air[i] += thatStats.accuracy_hits_air[i];
		stats.accuracy_hits_direct[i] += thatStats.accuracy_hits_direct[i];
		stats.accuracy_shots[i] += thatStats.accuracy_shots[i];
	}

	// requires handling of duplicates
	MergeAwards( e->stats.awardsSequence, std::move( cl->stats.awardsSequence ) );
}

void StatsowFacade::MergeAwards( StatsSequence<LoggedAward> &to, StatsSequence<LoggedAward> &&from ) {
	for( const LoggedAward &mergable: from ) {
		// search for matching one
		StatsSequence<LoggedAward>::iterator it( to.begin() );
		StatsSequence<LoggedAward>::iterator end( to.end() );
		for(; it != end; ++it ) {
			if( ( *it ).name.size() != mergable.name.size() ) {
				continue;
			}
			if( Q_strnicmp( ( *it ).name.data(), mergable.name.data(), mergable.name.size() ) != 0 ) {
				continue;
			}
			( *it ).count += mergable.count;
			break;
		}
		if( it != end ) {
			to.New( mergable.name, mergable.count );
		}
	}

	// we can free the old awards
	from.Clear();
}

StatsowFacade::ClientEntry *StatsowFacade::FindEntryById( const mm_uuid_t &playerSessionId ) {
	for( ClientEntry *entry = clientEntriesHead; entry; entry = entry->next ) {
		if( entry->mm_session == playerSessionId ) {
			return entry;
		}
	}
	return nullptr;
}

RespectStats *StatsowFacade::FindRespectStatsById( const mm_uuid_t &playerSessionId ) {
	if( ClientEntry *entry = FindEntryById( playerSessionId ) ) {
		return &entry->respectStats;
	}
	return nullptr;
}

StatsowFacade::ClientEntry *StatsowFacade::NewPlayerEntry( edict_t *ent, bool final ) {
	auto *cl = ent->r.client;

	auto *const e = new( Q_malloc( sizeof( ClientEntry ) ) )ClientEntry;

	// fill in the data
	Q_strncpyz( e->netname, cl->netname.data(), sizeof( e->netname ) );
	e->team = cl->team;
	e->timePlayed = ( level.time - cl->teamStateTimestamp ) / 1000;
	e->final = final;
	e->mm_session = cl->mm_session;
	// TODO: What if not `final`?
	e->stats = std::move( cl->stats );
	return e;
}

void StatsowFacade::AddMetaAward( const edict_t *ent, const char *awardMsg ) {
	if( GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	if( ChatHandlersChain::instance()->skipStatsForClient( ent ) ) {
		return;
	}

	AddAward( ent, awardMsg );
}

void StatsowFacade::AddAward( const edict_t *ent, const char *awardMsg ) {
	if( !IsValid() ) {
		return;
	}

	if( GS_MatchState() != MATCH_STATE_PLAYTIME && GS_MatchState() != MATCH_STATE_POSTMATCH ) {
		return;
	}

	if( ChatHandlersChain::instance()->skipStatsForClient( ent ) ) {
		return;
	}

	auto &awardsSequence = ent->r.client->stats.awardsSequence;
	// first check if we already have this one on the clients list
	const size_t msgLen = ::strlen( awardMsg );

	StatsSequence<LoggedAward>::iterator end( awardsSequence.end() );
	for( StatsSequence<LoggedAward>::iterator it = awardsSequence.begin(); it != end; ++it ) {
		LoggedAward &existing = *it;
		const wsw::StringView &name = existing.name;
		if( name.size() != msgLen ) {
			continue;
		}
		if( Q_strnicmp( name.data(), awardMsg, name.size() ) != 0 ) {
			continue;
		}
		existing.count++;
		return;
	}

	const char *name = G_RegisterLevelString( awardMsg );
	awardsSequence.New( wsw::StringView( name, msgLen ), 1 );
}

mm_uuid_t StatsowFacade::SessionOf( const edict_t *ent ) {
	if( ent ) {
		if( const auto *client = ent->r.client ) {
			if( client->mm_session.IsValidSessionId() ) {
				return client->mm_session;
			}
		}
	}
	return Uuid_ZeroUuid();
}

void StatsowFacade::AddFrag( const edict_t *attacker, const edict_t *victim, int mod ) {
	if( !IsValid() ) {
		return;
	}

	if( GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	const mm_uuid_t attackerId = SessionOf( attacker );
	const mm_uuid_t victimId = SessionOf( victim );
	// Skip logging of the frag in this case.
	// This is very likely to be some non-player entities fragging each other
	if( !attackerId.IsValidSessionId() && !victimId.IsValidSessionId() ) {
		return;
	}

	int ammoTag = G_ModToAmmo( mod );
	// There's no distinction between weak and strong ammo
	static_assert( AMMO_GUNBLADE < AMMO_WEAK_GUNBLADE, "" );
	if( ammoTag >= AMMO_WEAK_GUNBLADE ) {
		// Eliminate weak ammo values shift
		ammoTag -= AMMO_WEAK_GUNBLADE - AMMO_GUNBLADE;
	}

	const int weapon = ammoTag - AMMO_NONE;
	static_assert( WEAP_NONE == 0, "The server WEAP_NONE value assumption is broken" );
	assert( weapon >= 0 && weapon < WEAP_TOTAL );

	// Changed to millis for the new stats server
	const auto time = (int)( game.serverTime - GS_MatchStartTime() );

	fragsSequence.New( attackerId, victimId, time, weapon );
}

void StatsowFacade::WriteHeaderFields( JsonWriter &writer, int teamGame ) {
	// Note: booleans are transmitted as integers due to underlying api limitations
	writer << "match_id"       << trap_GetConfigString( CS_MATCHUUID );
	writer << "gametype"       << gs.gametypeName;
	writer << "map_name"       << level.mapname;
	writer << "server_name"    << trap_Cvar_String( "sv_hostname" );
	writer << "time_played"    << level.finalMatchDuration / 1000;
	writer << "time_limit"     << GS_MatchDuration() / 1000;
	writer << "score_limit"    << g_scorelimit->integer;
	writer << "is_instagib"    << ( GS_Instagib() ? 1 : 0 );
	writer << "is_team_game"   << ( teamGame ? 1 : 0 );
	writer << "is_race_game"   << ( GS_RaceGametype() ? 1 : 0 );
	writer << "mod_name"       << trap_Cvar_String( "fs_game" );
	writer << "utc_start_time" << game.utcMatchStartTime;

	if( g_autorecord->integer ) {
		writer << "demo_filename" << va( "%s%s", level.autorecord_name, game.demoExtension );
	}
}

void StatsowFacade::SendMatchFinishedReport() {
	// Feature: do not report matches with duration less than 1 minute (actually 66 seconds)
	if( level.finalMatchDuration <= SIGNIFICANT_MATCH_DURATION ) {
		return;
	}

	QueryObject *query = trap_MM_NewPostQuery( "server/match/completed" );
	JsonWriter writer( query->RequestJsonRoot() );

	// ch : race properties through GS_RaceGametype()

	// official duel frag support
	int duelGame = GS_TeamBasedGametype() && GS_MaxPlayersInTeam() == 1 ? 1 : 0;

	int teamGame;
	// ch : fixme do mark duels as teamgames
	if( duelGame ) {
		teamGame = 0;
	} else if( !GS_TeamBasedGametype()) {
		teamGame = 0;
	} else {
		teamGame = 1;
	}

	WriteHeaderFields( writer, teamGame );

	// Write team properties (if any)
	if( teamlist[TEAM_ALPHA].numplayers > 0 && teamGame != 0 ) {
		writer << "teams" << '[';
		{
			for( int i = TEAM_ALPHA; i <= TEAM_BETA; i++ ) {
				writer << '{';
				{
					writer << "name" << trap_GetConfigString( CS_TEAM_SPECTATOR_NAME + ( i - TEAM_SPECTATOR ));
					// TODO:... What do Statsow controllers expect?
					writer << "index" << i - TEAM_ALPHA;
					writer << "score" << teamlist[i].stats.score;
				}
				writer << '}';
			}
		}
		writer << ']';
	}

	const char *weaponNames[WEAP_TOTAL];
	for( int weapon = WEAP_GUNBLADE; weapon < WEAP_TOTAL; ++weapon ) {
		weaponNames[weapon] = GS_FindItemByTag( weapon )->shortname;
	}

	// Supply weapon indices for the stats server as an object keyed by weapon names
	// Note that redundant weapons (that were not used) are allowed to be present here
	writer << "weapon_indices" << '{';
	{
		for( int weapon = WEAP_GUNBLADE; weapon < WEAP_TOTAL; ++weapon ) {
			writer << weaponNames[weapon] << weapon;
		}
	}
	writer << '}';

	// Supply logged frags
	writer << "logged_frags" << '[';
	{
		for( const LoggedFrag &frag: fragsSequence ) {
			writer << '{';
			{
				writer << "attacker" << frag.attacker;
				writer << "victim" << frag.victim;
				writer << "time" << frag.time;
				writer << "weapon" << frag.weapon;
			}
			writer << '}';
		}
	}
	writer << ']';

	// Write player properties
	writer << "players" << '[';
	for( ClientEntry *cl = clientEntriesHead; cl; cl = cl->next ) {
		writer << '{';
		cl->WriteToReport( writer, teamGame != 0, weaponNames );
		writer << '}';
	}
	writer << ']';

	trap_MM_EnqueueReport( query );
}

void StatsowFacade::ClientEntry::WriteToReport( JsonWriter &writer, bool teamGame, const char **weaponNames ) {
	writer << "session_id"  << mm_session;
	writer << "name"        << netname;
	writer << "score"       << stats.score;
	writer << "time_played" << timePlayed;
	writer << "is_final"    << ( final ? 1 : 0 );
	if( teamGame != 0 ) {
		writer << "team" << team - TEAM_ALPHA;
	}

	writer << "respect_stats" << '{';
	{
		writer << "status";
		if( respectStats.hasViolatedCodex ) {
			writer << "violated";
		} else if( respectStats.hasIgnoredCodex ) {
			writer << "ignored";
		} else {
			writer << "followed";
			writer << "token_stats" << '{';
			{
				for( const auto &keyAndValue: respectStats ) {
					writer << keyAndValue.first << keyAndValue.second;
				}
			}
			writer << '}';
		}
	}
	writer << '}';

	if( respectStats.hasViolatedCodex || respectStats.hasIgnoredCodex ) {
		return;
	}

	writer << "various_stats" << '{';
	{
		for( const auto &keyAndValue: stats ) {
			writer << keyAndValue.first << keyAndValue.second;
		}
	}
	writer << '}';

	AddAwards( writer );
	AddWeapons( writer, weaponNames );
}

void StatsowFacade::ClientEntry::AddAwards( JsonWriter &writer ) {
	writer << "awards" << '[';
	{
		for( const LoggedAward &award: stats.awardsSequence ) {
			writer << '{';
			{
				writer << "name"  << award.name.data();
				writer << "count" << award.count;
			}
			writer << '}';
		}
	}
	writer << ']';
}

// TODO: Should be lifted to gameshared
static inline double ComputeAccuracy( int hits, int shots ) {
	if( !hits ) {
		return 0.0;
	}
	if( hits == shots ) {
		return 100.0;
	}

	// copied from cg_scoreboard.c, but changed the last -1 to 0 (no hits is zero acc, right??)
	return ( wsw::min( (int)( std::floor( ( 100.0f * ( hits ) ) / ( (float)( shots ) ) + 0.5f ) ), 99 ) );
}

void StatsowFacade::ClientEntry::AddWeapons( JsonWriter &writer, const char **weaponNames ) {
	int i;

	// first pass calculate the number of weapons, see if we even need this section
	for( i = 0; i < ( AMMO_TOTAL - WEAP_TOTAL ); i++ ) {
		if( stats.accuracy_shots[i] > 0 ) {
			break;
		}
	}

	if( i >= ( AMMO_TOTAL - WEAP_TOTAL ) ) {
		return;
	}

	writer << "weapons" << '[';
	{
		int j;

		// we only loop thru the lower section of weapons since we put both
		// weak and strong shots inside the same weapon
		for( j = 0; j < AMMO_WEAK_GUNBLADE - WEAP_TOTAL; j++ ) {
			const int weak = j + ( AMMO_WEAK_GUNBLADE - WEAP_TOTAL );
			// Don't submit unused weapons
			if( stats.accuracy_shots[j] == 0 && stats.accuracy_shots[weak] == 0 ) {
				continue;
			}

			writer << '{';
			{
				writer << "name" << weaponNames[j];

				writer << "various_stats" << '{';
				{
					// STRONG
					int hits = stats.accuracy_hits[j];
					int shots = stats.accuracy_shots[j];

					writer << "strong_hits"   << hits;
					writer << "strong_shots"  << shots;
					writer << "strong_acc"    << ComputeAccuracy( hits, shots );
					writer << "strong_dmg"    << stats.accuracy_damage[j];
					writer << "strong_frags"  << stats.accuracy_frags[j];

					// WEAK
					hits = stats.accuracy_hits[weak];
					shots = stats.accuracy_shots[weak];

					writer << "weak_hits"   << hits;
					writer << "weak_shots"  << shots;
					writer << "weak_acc"    << ComputeAccuracy( hits, shots );
					writer << "weak_dmg"    << stats.accuracy_damage[weak];
					writer << "weak_frags"  << stats.accuracy_frags[weak];
				}
				writer << '}';
			}
			writer << '}';
		}
	}
	writer << ']';
}

void StatsowFacade::Frame() {
	const auto millisNow = game.realtime;
	for( RunStatusQuery *query = runQueriesHead; query; query = query->next ) {
		query->Update( millisNow );
	}

	if( millisNow - lastPlayTimesFlushAt > 30000 ) {
		FlushRacePlayTimes();
		lastPlayTimesFlushAt = millisNow;
	}
}

StatsowFacade::~StatsowFacade() {
	RunStatusQuery *nextQuery;
	for( RunStatusQuery *query = runQueriesHead; query; query = nextQuery ) {
		nextQuery = query;
		query->~RunStatusQuery();
		Q_free( query );
	}

	FlushRacePlayTimes();
}

bool StatsowFacade::IsValid() const {
	// This has to be computed on demand as the server starts logging in
	// at the first ran frame and not at the SVStatsowFacade instance creation

	if( isDiscarded ) {
		return false;
	}
	if( !( GS_MMCompatible() ) ) {
		return false;
	}
	return trap_Cvar_Value( "sv_mm_enable" ) && trap_Cvar_Value( "dedicated" );
}

void StatsowFacade::SendFinalReport() {
	if( !IsValid() ) {
		ClearEntries();
		return;
	}

	if( GS_RaceGametype() ) {
		ClearEntries();
		return;
	}

	// merge game.clients with game.quits
	for( edict_t *ent = game.edicts + 1; PLAYERNUM( ent ) < gs.maxclients; ent++ ) {
		AddPlayerReport( ent, true );
	}

	if( isDiscarded ) {
		G_Printf( S_COLOR_YELLOW "SendFinalReport(): The match report has been discarded\n" );
	} else {
		// check if we have enough players to report (at least 2)
		if( clientEntriesHead && clientEntriesHead->next ) {
			SendMatchFinishedReport();
		} else {
			SendMatchAbortedReport();
		}
	}

	ClearEntries();
}

RunStatusQuery *StatsowFacade::SendRaceRunReport( RaceRun *raceRun, const char *runTag ) {
	if( !raceRun ) {
		return nullptr;
	}

	if( !IsValid() ) {
		raceRun->~RaceRun();
		Q_free( raceRun );
		return nullptr;
	}

	QueryObject *query = trap_MM_NewPostQuery( "server/race/runReport" );
	if( !query ) {
		raceRun->~RaceRun();
		Q_free( raceRun );
		return nullptr;
	}

	JsonWriter writer( query->RequestJsonRoot() );
	WriteHeaderFields( writer, false );

	// Create a unique id for the run for tracking of its status/remote rank
	const mm_uuid_t runId( mm_uuid_t::Random() );

	writer << "race_runs" << '[';
	{
		writer << '{';
		{
			writer << "id" << runId;

			if( runTag ) {
				writer << "tag" << runTag;
			}

			// Setting session id and nickname is mutually exclusive
			if( raceRun->clientSessionId.IsValidSessionId() ) {
				writer << "session_id" << raceRun->clientSessionId;
			} else {
				writer << "nickname" << raceRun->nickname;
			}

			writer << "timestamp" << raceRun->utcTimestamp;

			writer << "times" << '[';
			{
				// Accessing the "+1" element is legal (its the final time). Supply it along with other times.
				for( int j = 0; j < raceRun->numSectors + 1; j++ )
					writer << raceRun->times[j];
			}
			writer << ']';
		}
		writer << '}';
	}

	// We do not longer need this object.
	// We have acquired an ownership over it in this call.
	// TODO: We can recycle the memory chunk by putting in a free list here

	raceRun->~RaceRun();
	Q_free( raceRun );

	trap_MM_EnqueueReport( query );

	return AddRunStatusQuery( runId );
}

#ifndef GAME_HARD_LINKED
// While most of the stuff is defined inline in the class
// and some parts that rely on qcommon too much are imported
// this implementation is specific for a binary we use this stuff in.
void QueryObject::FailWith( const char *format, ... ) {
	char buffer[2048];

	va_list va;
	va_start( va, format );
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	va_end( va );

	trap_Error( buffer );
}
#endif

