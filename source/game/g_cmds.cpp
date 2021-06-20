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

#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstdtypes.h"
#include "ai/navigation/aasworld.h"
#include "../qcommon/CommandsHandler.h"

/*
* G_Teleport
*
* Teleports client to specified position
* If client is not spectator teleporting is only done if position is free and teleport effects are drawn.
*/
static bool G_Teleport( edict_t *ent, vec3_t origin, vec3_t angles ) {
	int i;

	if( !ent->r.inuse || !ent->r.client ) {
		return false;
	}

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		trace_t tr;

		G_Trace( &tr, origin, ent->r.mins, ent->r.maxs, origin, ent, MASK_PLAYERSOLID );
		if( tr.fraction != 1.0f || tr.startsolid ) {
			return false;
		}

		G_TeleportEffect( ent, false );
	}

	VectorCopy( origin, ent->s.origin );
	VectorCopy( origin, ent->olds.origin );
	ent->s.teleported = true;

	VectorClear( ent->velocity );
	ent->r.client->ps.pmove.pm_time = 1;
	ent->r.client->ps.pmove.pm_flags |= PMF_TIME_TELEPORT;

	if( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		G_TeleportEffect( ent, true );
	}

	// set angles
	VectorCopy( angles, ent->s.angles );
	VectorCopy( angles, ent->r.client->ps.viewangles );

	// set the delta angle
	for( i = 0; i < 3; i++ )
		ent->r.client->ps.pmove.delta_angles[i] = ANGLE2SHORT( ent->r.client->ps.viewangles[i] ) - ent->r.client->ucmd.angles[i];

	return true;
}


//=================================================================================

/*
* Cmd_Give_f
*
* Give items to a client
*/
static void Cmd_Give_f( edict_t *ent ) {
	char *name;
	gsitem_t    *it;
	int i;
	bool give_all;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	name = trap_Cmd_Args();

	if( !Q_stricmp( name, "all" ) ) {
		give_all = true;
	} else {
		give_all = false;
	}

	if( give_all || !Q_stricmp( trap_Cmd_Argv( 1 ), "health" ) ) {
		if( trap_Cmd_Argc() == 3 ) {
			ent->health = atoi( trap_Cmd_Argv( 2 ) );
		} else {
			ent->health = ent->max_health;
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "weapons" ) ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( !( it->type & IT_WEAPON ) ) {
				continue;
			}

			ent->r.client->ps.inventory[i] += 1;
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "ammo" ) ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( !( it->type & IT_AMMO ) ) {
				continue;
			}

			Add_Ammo( ent->r.client, it, 1000, true );
		}
		if( !give_all ) {
			return;
		}
	}

	if( give_all || !Q_stricmp( name, "armor" ) ) {
		ent->r.client->armor = GS_Armor_MaxCountForTag( ARMOR_RA );
		if( !give_all ) {
			return;
		}
	}

	if( give_all ) {
		for( i = 0; i < GS_MAX_ITEM_TAGS; i++ ) {
			it = GS_FindItemByTag( i );
			if( !it ) {
				continue;
			}

			if( !( it->flags & ITFLAG_PICKABLE ) ) {
				continue;
			}

			if( it->type & ( IT_ARMOR | IT_WEAPON | IT_AMMO ) ) {
				continue;
			}

			ent->r.client->ps.inventory[i] = 1;
		}
		return;
	}

	it = GS_FindItemByName( name );
	if( !it ) {
		name = trap_Cmd_Argv( 1 );
		it = GS_FindItemByName( name );
		if( !it ) {
			G_PrintMsg( ent, "unknown item\n" );
			return;
		}
	}

	if( !( it->flags & ITFLAG_PICKABLE ) ) {
		G_PrintMsg( ent, "non-pickup (givable) item\n" );
		return;
	}

	if( it->type & IT_AMMO ) {
		if( trap_Cmd_Argc() == 3 ) {
			ent->r.client->ps.inventory[it->tag] = atoi( trap_Cmd_Argv( 2 ) );
		} else {
			ent->r.client->ps.inventory[it->tag] += it->quantity;
		}
	} else {
		if( it->tag && ( it->tag > 0 ) && ( it->tag < GS_MAX_ITEM_TAGS ) ) {
			if( GS_FindItemByTag( it->tag ) != NULL ) {
				ent->r.client->ps.inventory[it->tag]++;
			}
		} else {
			G_PrintMsg( ent, "non-pickup (givable) item\n" );
		}
	}
}

/*
* Cmd_God_f
* Sets client to godmode
* argv(0) god
*/
static void Cmd_God_f( edict_t *ent ) {
	const char *msg;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	ent->flags ^= FL_GODMODE;
	if( !( ent->flags & FL_GODMODE ) ) {
		msg = "godmode OFF\n";
	} else {
		msg = "godmode ON\n";
	}

	G_PrintMsg( ent, "%s", msg );
}

/*
* Cmd_Noclip_f
*
* argv(0) noclip
*/
static void Cmd_Noclip_f( edict_t *ent ) {
	const char *msg;

	if( !sv_cheats->integer ) {
		G_PrintMsg( ent, "Cheats are not enabled on this server.\n" );
		return;
	}

	if( ent->movetype == MOVETYPE_NOCLIP ) {
		ent->movetype = MOVETYPE_PLAYER;
		msg = "noclip OFF\n";
	} else {
		ent->movetype = MOVETYPE_NOCLIP;
		msg = "noclip ON\n";
	}

	G_PrintMsg( ent, "%s", msg );
}

/*
* Cmd_GameOperator_f
*/
static void Cmd_GameOperator_f( edict_t *ent ) {
	if( !g_operator_password->string[0] ) {
		G_PrintMsg( ent, "Operator is disabled in this server\n" );
		return;
	}

	if( trap_Cmd_Argc() < 2 ) {
		G_PrintMsg( ent, "Usage: 'operator <password>' or 'op <password>'\n" );
		return;
	}

	if( !Q_stricmp( trap_Cmd_Argv( 1 ), g_operator_password->string ) ) {
		if( !ent->r.client->isoperator ) {
			G_PrintMsg( NULL, "%s" S_COLOR_WHITE " is now a game operator\n", ent->r.client->netname.data() );
		}

		ent->r.client->isoperator = true;
		return;
	}

	G_PrintMsg( ent, "Incorrect operator password.\n" );
}

/*
* Cmd_Use_f
* Use an inventory item
*/
static void Cmd_Use_f( edict_t *ent ) {
	gsitem_t    *it;

	assert( ent && ent->r.client );

	it = GS_Cmd_UseItem( &ent->r.client->ps, trap_Cmd_Args(), 0 );
	if( !it ) {
		return;
	}

	G_UseItem( ent, it );
}

/*
* Cmd_Kill_f
*/
static void Cmd_Kill_f( edict_t *ent ) {
	if( ent->r.solid == SOLID_NOT ) {
		return;
	}

	// can suicide after 5 seconds
	if( level.time < ent->r.client->spawnStateTimestamp + (GS_RaceGametype() ? 1000 : 5000 ) ) {
		return;
	}

	ent->flags &= ~FL_GODMODE;
	ent->health = 0;
	meansOfDeath = MOD_SUICIDE;

	// wsw : pb : fix /kill command
	G_Killed( ent, ent, ent, 100000, vec3_origin, MOD_SUICIDE );
}

void Cmd_ChaseNext_f( edict_t *ent ) {
	G_ChaseStep( ent, 1 );
}

void Cmd_ChasePrev_f( edict_t *ent ) {
	G_ChaseStep( ent, -1 );
}

/*
* Cmd_PutAway_f
*/
static void Cmd_PutAway_f( edict_t *ent ) {
	ent->r.client->showscores = false;
}

/*
* Cmd_Score_f
*/
static void Cmd_Score_f( edict_t *ent ) {
	bool newvalue;

	if( trap_Cmd_Argc() == 2 ) {
		newvalue = ( atoi( trap_Cmd_Argv( 1 ) ) != 0 ) ? true : false;
	} else {
		newvalue = !ent->r.client->showscores ? true : false;
	}

	ent->r.client->showscores = newvalue;
}

/*
* Cmd_CvarInfo_f - Contains a cvar name and string provided by the client
*/
static void Cmd_CvarInfo_f( edict_t *ent ) {
	if( trap_Cmd_Argc() < 2 ) {
		G_PrintMsg( ent, "Cmd_CvarInfo_f: invalid argument count\n" );
		return;
	}

	// see if the gametype script is requesting this info
	if( !GT_asCallGameCommand( ent->r.client, "cvarinfo", trap_Cmd_Args(), trap_Cmd_Argc() - 1 ) ) {
		// if the gametype script wasn't interested in this command, print the output to console
		G_Printf( "%s%s's cvar '%s' is '%s%s'\n", ent->r.client->netname.data(), S_COLOR_WHITE, trap_Cmd_Argv( 1 ), trap_Cmd_Argv( 2 ), S_COLOR_WHITE );
	}
}

static bool CheckStateForPositionCmd( edict_t *ent ) {
	if( sv_cheats->integer ) {
		return true;
	}
	if( GS_MatchState() <= MATCH_STATE_WARMUP ) {
		return true;
	}
	if ( ent->r.client->ps.pmove.pm_type != PM_SPECTATOR ) {
		return true;
	}
	G_PrintMsg( ent, "Position command is only available in warmup and in spectator mode.\n" );
	return false;
}

class PositionArgSeqMatcher {
protected:
	const char *const name;
	vec3_t values { 0, 0, 0 };
	const int numExpectedValues;
	bool hasResult { false };

	static float ArgToFloat( int argNum );
	virtual int Parse( int startArgNum );

	template <typename Iterable>
	static int MatchByFirst( edict_t *user, int argNum, const char *prefix, const Iterable &matchers );

	virtual const char *Validate() { return nullptr; }
public:
	explicit PositionArgSeqMatcher( const char *name_, int numExpectedValues_ )
		: name( name_ ), numExpectedValues( numExpectedValues_ ) {}

	virtual ~PositionArgSeqMatcher() = default;

	bool CanMatch( const char *argSeqHead ) const {
		return ( Q_stricmp( argSeqHead, name ) ) == 0;
	}

	const char *Name() const { return name; }
	bool HasResult() const { return hasResult; }

	void CopyTo( float *dest ) const {
		if( hasResult ) {
			std::copy_n( values, numExpectedValues, dest );
		} else {
			std::fill_n( dest, numExpectedValues, 0.0f );
		}
	}

	void CopyToIfHasResult( float *dest ) const {
		if( hasResult ) {
			std::copy_n( values, numExpectedValues, dest );
		}
	}

	template <typename Iterable>
	static bool MatchAndValidate( edict_t *user, int startArgNum, const char *argSeqPrefix, const Iterable &matchers );
};

struct OriginMatcher : public PositionArgSeqMatcher {
	OriginMatcher() : PositionArgSeqMatcher( "origin", 3 ) {}

	const char *Validate() override {
		vec3_t worldMins, worldMaxs;
		const float *playerMins = playerbox_stand_mins;
		const float *playerMaxs = playerbox_stand_maxs;
		trap_CM_InlineModelBounds( trap_CM_InlineModel( 0 ), worldMins, worldMaxs );
		for( int i = 0; i < 3; ++i ) {
			if( values[i] + playerMins[i] <= worldMins[i] || values[i] + playerMaxs[i] >= worldMaxs[i] ) {
				return "The position is outside of world bounds";
			}
		}
		return nullptr;
	}
};

struct AnglesMatcher : public PositionArgSeqMatcher {
	AnglesMatcher() : PositionArgSeqMatcher( "angles", 2 ) {}

	const char *Validate() override {
		if( values[PITCH] < -90.0f || values[PITCH] > +90.0f ) {
			return "The roll angle must be within [-90, +90] degrees range";
		}
		if( values[YAW] < 0 || values[YAW] > 360.0f ) {
			return "The yaw angle must be within [-180, +180] degrees range";
		}
		if( values[ROLL] != 0 ) {
			return "The roll angle must be zero";
		}
		return nullptr;
	}
};

struct VelocityMatcher : public PositionArgSeqMatcher {
	VelocityMatcher() : PositionArgSeqMatcher( "velocity", 3 ) {}

	const char *Validate() override {
		if( VectorLengthSquared( values ) > 9999 * 9999 ) {
			return "The velocity magnitude is way too large";
		}
		return nullptr;
	}
};

struct SpeedMatcher : public PositionArgSeqMatcher {
	SpeedMatcher(): PositionArgSeqMatcher( "speed", 1 ) {}

	const char *Validate() override {
		if( std::fabs( values[0] ) > 9999 ) {
			return "The speed is way too large";
		}
		return nullptr;
	}
};

int PositionArgSeqMatcher::Parse( int startArgNum ) {
	assert( startArgNum > 0 && numExpectedValues > 0 );
	if( startArgNum + numExpectedValues > trap_Cmd_Argc() ) {
		return -1;
	}
	for( int i = 0; i < numExpectedValues; ++i ) {
		float val = ArgToFloat( startArgNum + i );
		if( !std::isfinite( val ) ) {
			return -1;
		}
		values[i] = val;
	}
	hasResult = true;
	return numExpectedValues;
}

float PositionArgSeqMatcher::ArgToFloat( int argNum ) {
	const char *arg = trap_Cmd_Argv( argNum );
	if( !arg || !*arg ) {
		return std::numeric_limits<float>::infinity();
	}
	// C/C++ scrubs do not have sane portable locale-independent conversions... just use ::atof()
	double val = ::atof( arg );
	if( !std::isfinite( val ) ) {
		return std::numeric_limits<float>::infinity();
	}
	if( val == 0.0f ) {
		// This is not 100% correct but generally does a good job at making hints.
		// Otherwise keep following "garbage in, garbage out" as expected.
		if( ::strspn( arg, "0." ) != ::strlen( arg ) ) {
			return std::numeric_limits<float>::infinity();
		}
	}
	return (float)val;
}

template <typename Iterable>
bool PositionArgSeqMatcher::MatchAndValidate( edict_t *user, int argNum, const char *prefix, const Iterable &matchers ) {
    const int endArgNum = trap_Cmd_Argc();
    while( argNum < endArgNum ) {
    	int matchedLength = MatchByFirst( user, argNum, prefix, matchers );
    	if( matchedLength < 0 ) {
    		return false;
    	}
    	argNum += matchedLength;
    }

	bool validationSuccess = true;
    for ( auto it = std::begin( matchers ); it != std::end( matchers ); ++it ) {
    	PositionArgSeqMatcher *matcher = *it;
    	if( !matcher->HasResult() ) {
    		continue;
    	}
    	const char *err = matcher->Validate();
    	if( !err ) {
    		continue;
    	}
    	G_PrintMsg( user, "%s\n", err );
    	validationSuccess = false;
    }

    return validationSuccess;
}

template <typename Iterable>
int PositionArgSeqMatcher::MatchByFirst( edict_t *user, int argNum, const char *prefix, const Iterable &matchers ) {
	const char *seqHead = trap_Cmd_Argv( argNum++ );
	size_t prefixOffset = ::strlen( prefix );
	if( Q_strnicmp( seqHead, prefix, prefixOffset ) != 0 ) {
		G_PrintMsg( user, "Unknown arg sequence `%s`\n", seqHead );
		return -1;
	}

	for ( auto it = std::begin( matchers ); it != std::end( matchers ); ++it ) {
		PositionArgSeqMatcher *matcher = *it;
		if( !matcher->CanMatch( seqHead + prefixOffset ) ) {
			continue;
		}
		int numParsedArgs = matcher->Parse( argNum );
		if( numParsedArgs > 0 ) {
			return numParsedArgs + 1;
		}
		G_PrintMsg( user, "Failed to parse `%s`\n", matcher->Name() );
		return -1;
	}

	G_PrintMsg( user, "Unknown arg sequence `%s`\n", seqHead );
	return -1;
}

static void SetOrLoadPosition( edict_t *ent, const char *argSeqPrefix, const char *verb, bool tryLoadingMissing ) {
	vec3_t origin { 0, 0, 0 };
	vec3_t angles { 0, 0, 0 };

	// Fill by saved values if needed
	if( tryLoadingMissing && ent->r.client->position_saved ) {
		VectorCopy( ent->r.client->position_origin, origin );
		VectorCopy( ent->r.client->position_angles, angles );
	}

	OriginMatcher originMatcher;
	AnglesMatcher anglesMatcher;
	VelocityMatcher velocityMatcher;
	SpeedMatcher speedMatcher;
	PositionArgSeqMatcher *matchers[] = { &originMatcher, &anglesMatcher, &velocityMatcher, &speedMatcher };
	if( !PositionArgSeqMatcher::MatchAndValidate( ent, 2, argSeqPrefix, matchers ) ) {
		return;
	}

	if( !originMatcher.HasResult() ) {
		// If we are not allowed to load saved origin
		if( !tryLoadingMissing ) {
			G_PrintMsg( ent, "An origin must always be specified\n" );
			return;
		}
		// If there's nothing saved
		if( !ent->r.client->position_saved ) {
			G_PrintMsg( ent, "A saved or specified origin must always be present\n" );
			return;
		}
	}

	originMatcher.CopyToIfHasResult( origin );
	anglesMatcher.CopyToIfHasResult( angles );

	if( ent->r.client->chase.active ) {
		G_SpectatorMode( ent );
	}

	if( !G_Teleport( ent, origin, angles ) ) {
		G_PrintMsg( ent, "Position not available.\n" );
		return;
	}

	if( !velocityMatcher.HasResult() && !speedMatcher.HasResult() ) {
		G_PrintMsg( ent, "Position %s\n", verb );
		return;
	}

	vec3_t velocity;
	VectorCopy( ent->velocity, velocity );
	velocityMatcher.CopyToIfHasResult( velocity );
	if( speedMatcher.HasResult() ) {
		float newSpeed = 0.0f;
		speedMatcher.CopyTo( &newSpeed );
		const float oldSpeed = VectorLength( velocity );
		if( oldSpeed < 1 ) {
			AngleVectors( angles, velocity, nullptr, nullptr );
			VectorScale( velocity, newSpeed, velocity );
		} else {
			float scale = newSpeed / oldSpeed;
			VectorScale( velocity, scale, velocity );
		}
	}

	VectorCopy( velocity, ent->velocity );
	G_PrintMsg( ent, "Position %s with modified velocity.\n", verb );
}

/*
* Cmd_Position_f
*/
static void Cmd_Position_f( edict_t *ent ) {
	// flood protect
	if( ent->r.client->position_lastcmd + 500 > game.realtime ) {
		return;
	}

	ent->r.client->position_lastcmd = game.realtime;

	const char *action = trap_Cmd_Argv( 1 );

	if( !Q_stricmp( action, "save" ) ) {
		if( !CheckStateForPositionCmd( ent ) ) {
			return;
		}
		ent->r.client->position_saved = true;
		VectorCopy( ent->s.origin, ent->r.client->position_origin );
		VectorCopy( ent->s.angles, ent->r.client->position_angles );
		G_PrintMsg( ent, "Position saved.\n" );
		return;
	}

	if( !Q_stricmp( action, "showSaved" ) ) {
		if( !ent->r.client->position_saved ) {
			G_PrintMsg( ent, "There's no saved position\n" );
			return;
		}
		const float *o = ent->r.client->position_origin;
		const float *a = ent->r.client->position_angles;
		G_PrintMsg( ent, "Saved origin: %.6f %.6f %.6f, angles: %.6f %.6f\n", o[0], o[1], o[2], a[0], a[1] );
		return;
	}

	if( !Q_stricmp( action, "load" ) ) {
		if( CheckStateForPositionCmd( ent ) ) {
			SetOrLoadPosition( ent, "with", "loaded", true );
		}
		return;
	}

	if( !Q_stricmp( action, "set" ) ) {
		if( CheckStateForPositionCmd( ent ) ) {
			SetOrLoadPosition( ent, "", "set", false );
		}
		return;
	}

	if( !Q_stricmp( action, "details" ) ) {
		wsw::StringStream ss;
		ss << va( "Origin: %.6f %.6f %.6f, ", ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] );
		ss << va( "angles: %.6f %.6f\n", ent->s.angles[0], ent->s.angles[1] );
		if( G_ISGHOSTING( ent ) ) {
			G_PrintMsg( ent, "%s\n", ss.str().c_str() );
			return;
		}

		const int topNode = trap_CM_FindTopNodeForBox( ent->r.absmin, ent->r.absmax );
		int tmp, nums[32];
		const int numLeaves = trap_CM_BoxLeafnums( ent->r.absmin, ent->r.absmax, nums, 32, &tmp, topNode );
		ss << "CM top node for box: " << topNode << ", leaves: [";
		for( int i = 0; i < numLeaves; ++i ) {
			ss << nums[i] << ( ( i + 1 != numLeaves ) ? "," : "" );
		}
		ss << "]";

		const auto *aasWorld = AiAasWorld::Instance();
		if( !aasWorld->IsLoaded() ) {
			G_PrintMsg( ent, "%s\n", ss.str().c_str() );
			return;
		}

		const int numAreas = aasWorld->BBoxAreas( ent->r.absmin, ent->r.absmax, nums, 32 );
		ss << ", AAS areas for box: [";
		for( int i = 0; i < numAreas; ++i ) {
			ss << nums[i] << ( ( i + 1 != numAreas ) ? "," : "" );
		}
		ss << "]";

		G_PrintMsg( ent, "%s", ss.str().c_str() );
		return;
	}

	wsw::StringStream ss;
	ss << "Usage:\n";
	ss << "position save - Save the current position (origin and angles)\n";
	ss << "position showSaved - Displays an information about the saved position\n";
	ss << "position load - Teleport to a saved position. Saved parameters can be overridden:\n";
	ss << "position load [withOrigin <x> >y> <z>] [withAngles <pitch> <yaw>]";
	   ss << " [withVelocity <vx> <vy> <vz>] [withSpeed <s>]\n";
	ss << "position set - Teleport to a specified position. Parameters should be specified this way:\n";
	ss << "position set origin <x> <y> <z> [angles <pitch> <yaw>] [velocity <vx> <vy> <vz>] [speed <s>]\n";
	ss << "position details - Display a detailed information about the current position\n";

	G_PrintMsg( ent, "%s", ss.str().c_str() );
}

/*
* Cmd_PlayersExt_f
*/
static void Cmd_PlayersExt_f( edict_t *ent, bool onlyspecs ) {
	int i;
	int count = 0;
	int start = 0;
	char line[64];
	char msg[1024];

	if( trap_Cmd_Argc() > 1 ) {
		start = atoi( trap_Cmd_Argv( 1 ) );
	}
	Q_clamp( start, 0, gs.maxclients - 1 );

	// print information
	msg[0] = 0;

	for( i = start; i < gs.maxclients; i++ ) {
		if( trap_GetClientState( i ) >= CS_SPAWNED ) {
			edict_t *clientEnt = &game.edicts[i + 1];
			Client *cl;

			if( onlyspecs && clientEnt->s.team != TEAM_SPECTATOR ) {
				continue;
			}

			cl = clientEnt->r.client;

			wsw::StringView login;
			if( cl->mm_session.IsValidSessionId() ) {
				login = cl->getInfoValueOrEmpty( wsw::HashedStringView( "cl_mm_login" ) );
			}

			Q_snprintfz( line, sizeof( line ), "%3i %s" S_COLOR_WHITE "%s%s%s%s\n", i, cl->netname.data(),
						 login[0] ? "(" S_COLOR_YELLOW : "", login.data(), login[0] ? S_COLOR_WHITE ")" : "",
						 cl->isoperator ? " op" : "" );

			if( strlen( line ) + strlen( msg ) > sizeof( msg ) - 100 ) {
				// can't print all of them in one packet
				Q_strncatz( msg, "...\n", sizeof( msg ) );
				break;
			}

			if( count == 0 ) {
				Q_strncatz( msg, "num name\n", sizeof( msg ) );
				Q_strncatz( msg, "--- ------------------------------\n", sizeof( msg ) );
			}

			Q_strncatz( msg, line, sizeof( msg ) );
			count++;
		}
	}

	if( count ) {
		Q_strncatz( msg, "--- ------------------------------\n", sizeof( msg ) );
	}
	Q_strncatz( msg, va( "%3i %s\n", count, trap_Cmd_Argv( 0 ) ), sizeof( msg ) );
	G_PrintMsg( ent, "%s", msg );

	if( i < gs.maxclients ) {
		G_PrintMsg( ent, "Type '%s %i' for more %s\n", trap_Cmd_Argv( 0 ), i, trap_Cmd_Argv( 0 ) );
	}
}

/*
* Cmd_Players_f
*/
static void Cmd_Players_f( edict_t *ent ) {
	Cmd_PlayersExt_f( ent, false );
}

/*
* Cmd_Spectators_f
*/
static void Cmd_Spectators_f( edict_t *ent ) {
	Cmd_PlayersExt_f( ent, true );
}

void ChatHandlersChain::Frame() {
	if( g_floodprotection_messages->modified ) {
		if( g_floodprotection_messages->integer < 0 ) {
			trap_Cvar_Set( "g_floodprotection_messages", "0" );
		}
		if( g_floodprotection_messages->integer > MAX_FLOOD_MESSAGES ) {
			trap_Cvar_Set( "g_floodprotection_messages", va( "%i", MAX_FLOOD_MESSAGES ) );
		}
		g_floodprotection_messages->modified = false;
	}

	if( g_floodprotection_team->modified ) {
		if( g_floodprotection_team->integer < 0 ) {
			trap_Cvar_Set( "g_floodprotection_team", "0" );
		}
		if( g_floodprotection_team->integer > MAX_FLOOD_MESSAGES ) {
			trap_Cvar_Set( "g_floodprotection_team", va( "%i", MAX_FLOOD_MESSAGES ) );
		}
		g_floodprotection_team->modified = false;
	}

	if( g_floodprotection_seconds->modified ) {
		if( g_floodprotection_seconds->value <= 0 ) {
			trap_Cvar_Set( "g_floodprotection_seconds", "4" );
		}
		g_floodprotection_seconds->modified = false;
	}

	if( g_floodprotection_penalty->modified ) {
		if( g_floodprotection_penalty->value < 0 ) {
			trap_Cvar_Set( "g_floodprotection_penalty", "10" );
		}
		g_floodprotection_penalty->modified = false;
	}

	if( respectHandler.lastFrameMatchState == MATCH_STATE_PLAYTIME && GS_MatchState() == MATCH_STATE_POSTMATCH ) {
		// Unlock to say `gg` postmatch
		muteFilter.Reset();
		floodFilter.Reset();
	}

	respectHandler.Frame();
}

bool FloodFilter::DetectFlood( const edict_t *ent, bool teamonly ) {
	// TODO: Rewrite so the client do not actually have to maintain its flood state

	int i;
	Client *client;

	assert( ent != NULL );

	client = ent->r.client;
	assert( client != NULL );



	// old protection still active
	if( !teamonly || g_floodprotection_team->integer ) {
		if( game.realtime < client->flood_locktill ) {
			G_PrintMsg( ent, "You can't talk for %d more seconds\n",
						(int)( ( client->flood_locktill - game.realtime ) / 1000.0f ) + 1 );
			return true;
		}
	}


	if( teamonly ) {
		if( g_floodprotection_team->integer && g_floodprotection_penalty->value > 0 ) {
			i = client->flood_team_whenhead - g_floodprotection_team->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->flood_team_when[i] && client->flood_team_when[i] <= game.realtime &&
				( game.realtime < client->flood_team_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->flood_locktill = game.realtime + g_floodprotection_penalty->value * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->flood_team_whenhead = ( client->flood_team_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->flood_team_when[client->flood_team_whenhead] = game.realtime;
	} else {
		if( g_floodprotection_messages->integer && g_floodprotection_penalty->value > 0 ) {
			i = client->flood_whenhead - g_floodprotection_messages->integer + 1;
			if( i < 0 ) {
				i = MAX_FLOOD_MESSAGES + i;
			}

			if( client->flood_when[i] && client->flood_when[i] <= game.realtime &&
				( game.realtime < client->flood_when[i] + g_floodprotection_seconds->integer * 1000 ) ) {
				client->flood_locktill = game.realtime + g_floodprotection_penalty->value * 1000;
				G_PrintMsg( ent, "Flood protection: You can't talk for %d seconds.\n", g_floodprotection_penalty->integer );
				return true;
			}
		}

		client->flood_whenhead = ( client->flood_whenhead + 1 ) % MAX_FLOOD_MESSAGES;
		client->flood_when[client->flood_whenhead] = game.realtime;
	}

	return false;
}

static void Cmd_CoinToss_f( edict_t *ent ) {
	bool qtails;
	char *s;
	char upper[MAX_STRING_CHARS];

	if( GS_MatchState() > MATCH_STATE_WARMUP && !GS_MatchPaused() ) {
		G_PrintMsg( ent, "You can only toss coins during warmup or timeouts\n" );
		return;
	}
	if( ChatHandlersChain::Instance()->DetectFlood( ent, false ) ) {
		return;
	}

	if( trap_Cmd_Argc() < 2 || ( Q_stricmp( "heads", trap_Cmd_Argv( 1 ) ) && Q_stricmp( "tails", trap_Cmd_Argv( 1 ) ) ) ) {
		//it isn't a valid token
		G_PrintMsg( ent, "You have to choose heads or tails when tossing a coin\n" );
		return;
	}

	Q_strncpyz( upper, trap_Cmd_Argv( 1 ), sizeof( upper ) );
	s = upper;
	while( *s ) {
		*s = toupper( *s );
		s++;
	}

	qtails = ( Q_stricmp( "heads", trap_Cmd_Argv( 1 ) ) != 0 ) ? true : false;
	if( qtails == ( rand() & 1 ) ) {
		G_PrintMsg( NULL, S_COLOR_YELLOW "COINTOSS %s: " S_COLOR_WHITE "It was %s! %s " S_COLOR_WHITE "tossed a coin and " S_COLOR_GREEN "won!\n", upper, trap_Cmd_Argv( 1 ), ent->r.client->netname.data() );
		return;
	}

	G_PrintMsg( NULL, S_COLOR_YELLOW "COINTOSS %s: " S_COLOR_WHITE "It was %s! %s " S_COLOR_WHITE "tossed a coin and " S_COLOR_RED "lost!\n", upper, qtails ? "heads" : "tails", ent->r.client->netname.data() );
}

static SingletonHolder<ChatHandlersChain> chatHandlersChainHolder;

void ChatHandlersChain::Init() {
	::chatHandlersChainHolder.Init();
}

void ChatHandlersChain::Shutdown() {
	::chatHandlersChainHolder.Shutdown();
}

ChatHandlersChain *ChatHandlersChain::Instance() {
	return ::chatHandlersChainHolder.Instance();
}

void ChatHandlersChain::Reset() {
	authFilter.Reset();
	muteFilter.Reset();
	floodFilter.Reset();
	respectHandler.Reset();
	ignoreFilter.Reset();
}

void ChatHandlersChain::ResetForClient( int clientNum ) {
	authFilter.ResetForClient( clientNum );
	muteFilter.ResetForClient( clientNum );
	floodFilter.ResetForClient( clientNum );
	respectHandler.ResetForClient( clientNum );
	ignoreFilter.ResetForClient( clientNum );
}

bool ChatHandlersChain::HandleMessage( const edict_t *ent, const char *message ) {
	// We want to call overridden methods directly just to avoid pointless virtual invocations.
	// Filters are applied in order of their priority.
	if( authFilter.HandleMessage( ent, message ) || muteFilter.HandleMessage( ent, message ) ) {
		return true;
	}
	if( floodFilter.HandleMessage( ent, message ) || respectHandler.HandleMessage( ent, message ) ) {
		return true;
	}

	ChatPrintHelper chatPrintHelper( ent, "%s", message );
	// Dispatch the message using `this` as an ignore filter
	chatPrintHelper.PrintToEverybody( this );
	return true;
}

void ChatAuthFilter::Reset() {
	authOnly = sv_mm_enable->integer && trap_Cvar_Value( "sv_mm_chat_loginonly" ) != 0;
}

bool ChatAuthFilter::HandleMessage( const edict_t *ent, const char * ) {
	if( !authOnly ) {
		return false;
	}

	if( GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return false;
	}

	// Allow talking in timeouts
	if( GS_MatchPaused() ) {
		return false;
	}

	if( ent->r.client->mm_session.IsValidSessionId() ) {
		return false;
	}

	// unauthed players are only allowed to chat to public at non play-time
	G_PrintMsg( ent, S_COLOR_YELLOW "Register at Warsow.net and log in to say public chat messages during a match\n" );
	return true;
}

/*
* Cmd_Say_f
*/
void Cmd_Say_f( edict_t *ent, bool arg0 ) {
	char *p;
	char text[2048];
	size_t arg0len = 0;

	if( trap_Cmd_Argc() < 2 && !arg0 ) {
		return;
	}

	text[0] = 0;

	if( arg0 ) {
		Q_strncatz( text, trap_Cmd_Argv( 0 ), sizeof( text ) );
		Q_strncatz( text, " ", sizeof( text ) );
		arg0len = strlen( text );
		Q_strncatz( text, trap_Cmd_Args(), sizeof( text ) );
	} else {
		p = trap_Cmd_Args();

		if( *p == '"' ) {
			if( p[strlen( p ) - 1] == '"' ) {
				p[strlen( p ) - 1] = 0;
			}
			p++;
		}
		Q_strncatz( text, p, sizeof( text ) );
	}

	// don't let text be too long for malicious reasons
	text[arg0len + ( MAX_CHAT_BYTES - 1 )] = 0;

	ChatHandlersChain::Instance()->HandleMessage( ent, text );
}

/*
* Cmd_SayCmd_f
*/
static void Cmd_SayCmd_f( edict_t *ent ) {
	Cmd_Say_f( ent, false );
}

/*
* Cmd_SayTeam_f
*/
static void Cmd_SayTeam_f( edict_t *ent ) {
	G_Say_Team( ent, trap_Cmd_Args(), true );
}

/*
* Cmd_Join_f
*/
static void Cmd_Join_f( edict_t *ent ) {
	if( ChatHandlersChain::Instance()->DetectFlood( ent, false ) ) {
		return;
	}

	G_Teams_Join_Cmd( ent );
}

/*
* Cmd_Timeout_f
*/
static void Cmd_Timeout_f( edict_t *ent ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR || GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	if( GS_TeamBasedGametype() ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( GS_MatchPaused() && ( level.timeout.endtime - level.timeout.time ) >= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "Timeout already in progress\n" );
		return;
	}

	if( g_maxtimeouts->integer != -1 && level.timeout.used[num] >= g_maxtimeouts->integer ) {
		if( g_maxtimeouts->integer == 0 ) {
			G_PrintMsg( ent, "Timeouts are not allowed on this server\n" );
		} else if( GS_TeamBasedGametype() ) {
			G_PrintMsg( ent, "Your team doesn't have any timeouts left\n" );
		} else {
			G_PrintMsg( ent, "You don't have any timeouts left\n" );
		}
		return;
	}

	G_PrintMsg( NULL, "%s%s called a timeout\n", ent->r.client->netname.data(), S_COLOR_WHITE );

	if( !GS_MatchPaused() ) {
		G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEOUT_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );
	}

	level.timeout.used[num]++;
	GS_GamestatSetFlag( GAMESTAT_FLAG_PAUSED, true );
	level.timeout.caller = num;
	level.timeout.endtime = level.timeout.time + TIMEOUT_TIME + FRAMETIME;
}

/*
* Cmd_Timeout_f
*/
static void Cmd_Timein_f( edict_t *ent ) {
	int num;

	if( ent->s.team == TEAM_SPECTATOR ) {
		return;
	}

	if( !GS_MatchPaused() ) {
		G_PrintMsg( ent, "No timeout in progress.\n" );
		return;
	}

	if( level.timeout.endtime - level.timeout.time <= 2 * TIMEIN_TIME ) {
		G_PrintMsg( ent, "The timeout is about to end already.\n" );
		return;
	}

	if( GS_TeamBasedGametype() ) {
		num = ent->s.team;
	} else {
		num = ENTNUM( ent ) - 1;
	}

	if( level.timeout.caller != num ) {
		if( GS_TeamBasedGametype() ) {
			G_PrintMsg( ent, "Your team didn't call this timeout.\n" );
		} else {
			G_PrintMsg( ent, "You didn't call this timeout.\n" );
		}
		return;
	}

	level.timeout.endtime = level.timeout.time + TIMEIN_TIME + FRAMETIME;

	G_AnnouncerSound( NULL, trap_SoundIndex( va( S_ANNOUNCER_TIMEOUT_TIMEIN_1_to_2, ( rand() & 1 ) + 1 ) ), GS_MAX_TEAMS, true, NULL );

	G_PrintMsg( NULL, "%s%s called a timein\n", ent->r.client->netname.data(), S_COLOR_WHITE );
}

/*
* Cmd_Awards_f
*/
static void Cmd_Awards_f( edict_t *ent ) {
	Client *client;
	static char entry[MAX_TOKEN_CHARS];

	assert( ent && ent->r.client );
	client = ent->r.client;

	Q_snprintfz( entry, sizeof( entry ), "Awards for %s\n", client->netname.data() );

	const auto &awards = client->stats.awardsSequence;
	if( !awards.empty() ) {
		for( const LoggedAward &ga: awards ) {
			Q_strncatz( entry, va( "\t%dx %s\n", ga.count, ga.name.data() ), sizeof( entry ) );
		}
		G_PrintMsg( ent, "%s", entry );
	}
}

/*
* G_StatsMessage
*
* Generates stats message for the entity
* The returned string must be freed by the caller using G_Free
* Note: This string must never contain " characters
*/
char *G_StatsMessage( edict_t *ent ) {
	Client *client;
	gsitem_t *item;
	int i, shot_weak, hit_weak, shot_strong, hit_strong, shot_total, hit_total;
	static char entry[MAX_TOKEN_CHARS];

	assert( ent && ent->r.client );
	client = ent->r.client;

	// message header
	Q_snprintfz( entry, sizeof( entry ), "%d", PLAYERNUM( ent ) );

	for( i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		item = GS_FindItemByTag( i );
		assert( item );

		hit_weak = hit_strong = 0;
		shot_weak = shot_strong = 0;

		if( item->weakammo_tag != AMMO_NONE ) {
			hit_weak = client->stats.accuracy_hits[item->weakammo_tag - AMMO_GUNBLADE];
			shot_weak = client->stats.accuracy_shots[item->weakammo_tag - AMMO_GUNBLADE];
		}

		if( item->ammo_tag != AMMO_NONE ) {
			hit_strong = client->stats.accuracy_hits[item->ammo_tag - AMMO_GUNBLADE];
			shot_strong = client->stats.accuracy_shots[item->ammo_tag - AMMO_GUNBLADE];
		}

		hit_total = hit_weak + hit_strong;
		shot_total = shot_weak + shot_strong;

		Q_strncatz( entry, va( " %d", shot_total ), sizeof( entry ) );
		if( shot_total < 1 ) {
			continue;
		}
		Q_strncatz( entry, va( " %d", hit_total ), sizeof( entry ) );

		// strong
		Q_strncatz( entry, va( " %d", shot_strong ), sizeof( entry ) );
		if( shot_strong != shot_total ) {
			Q_strncatz( entry, va( " %d", hit_strong ), sizeof( entry ) );
		}
	}

	Q_strncatz( entry, va( " %d %d", (int)client->stats.GetEntry( "dmg_given" ), (int)client->stats.GetEntry( "dmg_taken" ) ), sizeof( entry ) );
	Q_strncatz( entry, va( " %d %d", (int)client->stats.GetEntry( "health_taken" ), (int)client->stats.GetEntry( "armor_taken" ) ), sizeof( entry ) );

	// add enclosing quote
	Q_strncatz( entry, "\"", sizeof( entry ) );

	return entry;
}

/*
* Cmd_ShowStats_f
*/
static void Cmd_ShowStats_f( edict_t *ent ) {
	edict_t *target;

	if( trap_Cmd_Argc() > 2 ) {
		G_PrintMsg( ent, "Usage: stats [player]\n" );
		return;
	}

	if( trap_Cmd_Argc() == 2 ) {
		target = G_PlayerForText( trap_Cmd_Argv( 1 ) );
		if( target == NULL ) {
			G_PrintMsg( ent, "No such player\n" );
			return;
		}
	} else {
		if( ent->r.client->chase.active && game.edicts[ent->r.client->chase.target].r.client ) {
			target = &game.edicts[ent->r.client->chase.target];
		} else {
			target = ent;
		}
	}

	if( target->s.team == TEAM_SPECTATOR ) {
		G_PrintMsg( ent, "No stats for spectators\n" );
		return;
	}

	trap_GameCmd( ent, va( "plstats 1 \"%s\"", G_StatsMessage( target ) ) );
}

/*
* Cmd_Whois_f
*/
static void Cmd_Whois_f( edict_t *ent ) {
	edict_t *target;
	Client *cl;

	if( trap_Cmd_Argc() > 2 ) {
		G_PrintMsg( ent, "Usage: whois [player]\n" );
		return;
	}

	if( trap_Cmd_Argc() == 2 ) {
		target = G_PlayerForText( trap_Cmd_Argv( 1 ) );
		if( target == NULL ) {
			G_PrintMsg( ent, "No such player\n" );
			return;
		}
	} else {
		if( ent->r.client->chase.active && game.edicts[ent->r.client->chase.target].r.client ) {
			target = &game.edicts[ent->r.client->chase.target];
		} else {
			target = ent;
		}
	}

	cl = target->r.client;

	if( !cl->mm_session.IsValidSessionId() ) {
		G_PrintMsg( ent, "Unregistered player\n" );
		return;
	}

	const char *login = "unknown";
	if( const auto maybeLoginName = cl->getMMLoginName() ) {
		assert( maybeLoginName->isZeroTerminated() );
		login = maybeLoginName->data();
	}

	G_PrintMsg( ent, "%s%s is %s\n", cl->netname.data(), S_COLOR_WHITE, login );
}

/*
* Cmd_Upstate_f
*
* Update client on the state of things
*/
static void Cmd_Upstate_f( edict_t *ent ) {
	G_UpdatePlayerMatchMsg( ent, true );
	G_SetPlayerHelpMessage( ent, ent->r.client->helpmessage, true );
}

//===========================================================
//	client commands
//===========================================================



static SingletonHolder<ClientCommandsHandler> clientCommandsHandlerHolder;

void ClientCommandsHandler::init() {
	::clientCommandsHandlerHolder.Init();
}

void ClientCommandsHandler::shutdown() {
	::clientCommandsHandlerHolder.Shutdown();
}

ClientCommandsHandler *ClientCommandsHandler::instance() {
	return ::clientCommandsHandlerHolder.Instance();
}

void ClientCommandsHandler::precacheCommands() {
	int i = 0;
	for( auto *callback = listHead; callback; callback = callback->nextInList() ) {
		// TODO: This assumes zero-terminated string views!
		trap_ConfigString( CS_GAMECOMMANDS + i, callback->name.data() );
		i++;
	}
	for(; i < MAX_GAMECOMMANDS; ++i ) {
		trap_ConfigString( CS_GAMECOMMANDS + i, "" );
	}
}

static const wsw::StringView callvoteValidate( "callvoteValidate" );
static const wsw::StringView callvotePassed( "callvotePassed" );

bool ClientCommandsHandler::isWriteProtected( const wsw::StringView &name ) {
	for( const wsw::StringView &s: { callvoteValidate, callvotePassed } ) {
		if( s.equalsIgnoreCase( name ) ) {
			return true;
		}
	}
	return false;
}

bool ClientCommandsHandler::addOrReplace( GenericCommandCallback *callback ) {
	// TODO: The code assumes zero-terminated string views!

	if( isWriteProtected( callback->name ) ) {
		G_Printf( "WARNING: G_AddCommand: command name '%s' is write protected\n", callback->name.data() );
		return false;
	}

	// If there was an existing command
	if( !CommandsHandler::addOrReplace( callback ) ) {
		return false;
	}

	// If the size has grew up over this value after the AddOrReplace() call
	if( size > MAX_GAMECOMMANDS ) {
		G_Error( "ClientCommandsHandler::AddOrReplace(`%s`): Too many commands\n", callback->name.data() );
	}

	// add the configstring if the precache process was already done
	if( level.canSpawnEntities ) {
		trap_ConfigString( CS_GAMECOMMANDS + ( size - 1 ), callback->name.data() );
	}

	return true;
}

ClientCommandsHandler::ClientCommandsHandler() {
	auto adapter( adapterForTag( "builtin" ) );
	adapter.addOrFail( "cvarinfo", Cmd_CvarInfo_f );
	adapter.addOrFail( "position", Cmd_Position_f );
	adapter.addOrFail( "players", Cmd_Players_f );
	adapter.addOrFail( "spectators", Cmd_Spectators_f );
	adapter.addOrFail( "stats", Cmd_ShowStats_f );
	adapter.addOrFail( "say", Cmd_SayCmd_f );
	adapter.addOrFail( "say_team", Cmd_SayTeam_f );
	adapter.addOrFail( "svscore", Cmd_Score_f );
	adapter.addOrFail( "god", Cmd_God_f );
	adapter.addOrFail( "noclip", Cmd_Noclip_f );
	adapter.addOrFail( "use", Cmd_Use_f );
	adapter.addOrFail( "give", Cmd_Give_f );
	adapter.addOrFail( "kill", Cmd_Kill_f );
	adapter.addOrFail( "putaway", Cmd_PutAway_f );
	adapter.addOrFail( "chase", Cmd_ChaseCam_f );
	adapter.addOrFail( "chasenext", Cmd_ChaseNext_f );
	adapter.addOrFail( "chaseprev", Cmd_ChasePrev_f );
	adapter.addOrFail( "spec", Cmd_Spec_f );
	adapter.addOrFail( "enterqueue", G_Teams_JoinChallengersQueue );
	adapter.addOrFail( "leavequeue", G_Teams_LeaveChallengersQueue );
	adapter.addOrFail( "camswitch", Cmd_SwitchChaseCamMode_f );
	adapter.addOrFail( "timeout", Cmd_Timeout_f );
	adapter.addOrFail( "timein", Cmd_Timein_f );
	adapter.addOrFail( "cointoss", Cmd_CoinToss_f );
	adapter.addOrFail( "whois", Cmd_Whois_f );

	// callvotes commands
	adapter.addOrFail( "callvote", G_CallVote_Cmd );
	adapter.addOrFail( "vote", G_CallVotes_CmdVote );

	adapter.addOrFail( "opcall", G_OperatorVote_Cmd );
	adapter.addOrFail( "operator", Cmd_GameOperator_f );
	adapter.addOrFail( "op", Cmd_GameOperator_f );

	// teams commands
	adapter.addOrFail( "ready", G_Match_Ready );
	adapter.addOrFail( "unready", G_Match_NotReady );
	adapter.addOrFail( "notready", G_Match_NotReady );
	adapter.addOrFail( "toggleready", G_Match_ToggleReady );
	adapter.addOrFail( "join", Cmd_Join_f );

	// coach commands
	adapter.addOrFail( "coach", G_Teams_Coach );
	adapter.addOrFail( "lockteam", G_Teams_CoachLockTeam );
	adapter.addOrFail( "unlockteam", G_Teams_CoachUnLockTeam );
	adapter.addOrFail( "invite", G_Teams_Invite_f );

	// bot commands
	adapter.addOrFail( "botnotarget", AI_Cheat_NoTarget );

	// ch : addOrFailed awards
	adapter.addOrFail( "awards", Cmd_Awards_f );

	// ignore-related commands
	adapter.addOrFail( "ignore", ChatHandlersChain::HandleIgnoreCommand );
	adapter.addOrFail( "unignore", ChatHandlersChain::HandleUnignoreCommand );
	adapter.addOrFail( "ignorelist", ChatHandlersChain::HandleIgnoreListCommand );

	// misc
	adapter.addOrFail( "upstate", Cmd_Upstate_f );
}

void ClientCommandsHandler::handleClientCommand( edict_t *ent ) {
	// Check whether the client is fully in-game
	if( !ent->r.client || trap_GetClientState( PLAYERNUM( ent ) ) < CS_SPAWNED ) {
		return;
	}

	const char *cmd = trap_Cmd_Argv( 0 );

	// Skip cvarinfo cmds because they are automatic responses
	if( Q_stricmp( cmd, "cvarinfo" ) != 0 ) {
		G_Client_UpdateActivity( ent->r.client ); // activity detected
	}

	if( Super::handle( cmd, ent ) ) {
		return;
	}

	G_PrintMsg( ent, "Bad user command: %s\n", cmd );
}

void ClientCommandsHandler::addScriptCommand( const char *name ) {
	(void)add( new ScriptCommandCallback( wsw::String( name ) ) );
}

bool ClientCommandsHandler::ScriptCommandCallback::operator()( edict_t *arg ) {
	return GT_asCallGameCommand( arg->r.client, name.data(), trap_Cmd_Args(), trap_Cmd_Argc() - 1 );
}

