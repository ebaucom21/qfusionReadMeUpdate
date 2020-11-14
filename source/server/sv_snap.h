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

#ifndef WSW_3a2448b0_e389_4f5a_b648_17ce20af13b7_H
#define WSW_3a2448b0_e389_4f5a_b648_17ce20af13b7_H

#include "../server/server.h"

#undef EDICT_NUM
#undef NUM_FOR_EDICT

#define EDICT_NUM( n ) ( (edict_t *)( (uint8_t *)gi->edicts + gi->edict_size * ( n ) ) )
#define NUM_FOR_EDICT( e ) ( ( (uint8_t *)( e ) - (uint8_t *)gi->edicts ) / gi->edict_size )

/**
 * Stores a "shadowed" state of entities for every client.
 * Shadowing an entity means transmission of randomized data
 * for fields that should not be really transmitted but
 * we are forced to transmit some parts of it (that's how the current netcode works).
 * Shadowing has an anti-cheat purpose.
 */
class SnapShadowTable {
	template <typename> friend class SingletonHolder;

	bool *table;

	SnapShadowTable();

	~SnapShadowTable() {
		::free( table );
	}
public:
	static void Init();
	static void Shutdown();
	static SnapShadowTable *Instance();

	void MarkEntityAsShadowed( int playerNum, int targetEntNum ) {
		assert( (unsigned)playerNum < (unsigned)MAX_CLIENTS );
		table[playerNum * MAX_CLIENTS + targetEntNum] = true;
	}

	bool IsEntityShadowed( int playerNum, int targetEntNum ) const {
		assert( (unsigned)playerNum < (unsigned)MAX_CLIENTS );
		return table[playerNum * MAX_CLIENTS + targetEntNum];
	}

	void Clear() {
		::memset( table, 0, ( MAX_CLIENTS ) * ( MAX_EDICTS ) * sizeof( bool ) );
	}
};

struct edict_s;

/**
 * Stores a visibility state of entities for every client.
 * We try using more aggressive culling of transmitted entities
 * rather than just sending everything in PVS.
 * These tests are not conservative and sometimes are prone to false negatives
 * but real play-tests do not show a noticeable gameplay impact.
 * In worst case a false negative is comparable to a lost packet.
 * For performance reasons only entities that are clients are tested for visibility.
 * An introduction of aggressive transmitted entities visibility culling greatly reduces wallhack utility.
 * Moreover this cached visibility table can be used for various server-side purposes (like AI vision).
 */
class SnapVisTable {
	template <typename> friend class SingletonHolder;

	cmodel_state_t *const cms;
	int8_t *table;
	float collisionWorldRadius;

	explicit SnapVisTable( cmodel_state_t *cms_ );

	bool CastRay( const vec3_t from, const vec3_t to, int topNodeHint );
	bool DoCullingByCastingRays( const edict_s *clientEnt, const vec3_t viewOrigin, const edict_s *targetEnt );

	void MarkCachedResult( int entNum1, int entNum2, bool isVisible ) {
		const int clientNum1 = entNum1 - 1;
		const int clientNum2 = entNum2 - 1;
		assert( (unsigned)clientNum1 < (unsigned)( MAX_CLIENTS ) );
		assert( (unsigned)clientNum2 < (unsigned)( MAX_CLIENTS ) );
		auto value = (int8_t)( isVisible ? +1 : -1 );
		table[clientNum1 * MAX_CLIENTS + clientNum2] = value;
		table[clientNum2 * MAX_CLIENTS + clientNum1] = value;
	}
public:
	static void Init( cmodel_state_t *cms_ );
	static void Shutdown();
	static SnapVisTable *Instance();

	void Clear() {
		memset( table, 0, ( MAX_CLIENTS ) * ( MAX_CLIENTS ) * sizeof( int8_t ) );
	}

	void MarkAsInvisible( int entNum1, int entNum2 ) {
		MarkCachedResult( entNum1, entNum2, false );
	}

	void MarkAsVisible( int entNum1, int entNum2 ) {
		MarkCachedResult( entNum1, entNum2, true );
	}

	int GetExistingResult( int povEntNum, int targetEntNum ) {
		// Check whether they are at least valid entity numbers
		assert( (unsigned)povEntNum < (unsigned)( MAX_EDICTS ) );
		assert( (unsigned)targetEntNum < (unsigned)( MAX_EDICTS ) );
		const int clientNum1 = povEntNum - 1;
		const int clientNum2 = targetEntNum - 1;
		if( (unsigned)clientNum1 >= (unsigned)( MAX_CLIENTS ) ) {
			return 0;
		}
		if( (unsigned)clientNum2 >= (unsigned)( MAX_CLIENTS ) ) {
			return 0;
		}
		return table[clientNum1 * MAX_CLIENTS + clientNum2];
	}

	bool TryCullingByCastingRays( const edict_s *clientEnt, const vec3_t viewOrigin, const edict_s *targetEnt );
};

#endif
