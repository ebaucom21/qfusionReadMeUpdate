#include "worldstate.h"
#include "../bot.h"

inline Bot *WorldState::Self() { return (Bot *)self; }

inline const Bot *WorldState::Self() const { return (const Bot *)self; }

const short *WorldState::GetCoverSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetCoverSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayTeleportOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayTeleportOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayJumppadOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayJumppadOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayElevatorOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayElevatorOrigin( BotOriginData(), EnemyOriginData() );
}

template <typename Var>
struct ZippedIterator {
	const Var *thisVar;
	const Var *thatVar;

	ZippedIterator( const Var *thisVarsHead, const Var *thatVarsHead ) {
		thisVar = thisVarsHead;
		thatVar = thatVarsHead;
		assert( ( thisVar == nullptr ) == ( thatVar == nullptr ) );
	}

	bool HasNext() const { return thisVar != nullptr; }

	void Next() {
		assert( thisVar && thatVar );
		thisVar = thisVar->next;
		thatVar = thatVar->next;
		assert( ( thisVar == nullptr ) == ( thatVar == nullptr ) );
	}
};

template <typename Var>
inline void WorldState::CopyVarsFromThat( Var *thisVars, const Var *thatVars ) {
	ZippedIterator<Var> iterator( thisVars, thatVars );
	while( iterator.HasNext() ) {
		const_cast<Var *>( iterator.thisVar )->CopyFromThat( *iterator.thatVar );
		iterator.Next();
	}
}

template <typename Var>
inline bool WorldState::CheckSatisfiedBy( const Var *thisVars, const Var *thatVars ) {
	ZippedIterator<Var> iterator( thisVars, thatVars );
	while( iterator.HasNext() ) {
		if( !iterator.thisVar->IsSatisfiedBy( *iterator.thatVar ) ) {
			return false;
		}
		iterator.Next();
	}
	return true;
}

template <typename Var>
inline void WorldState::SetIgnore( Var *varsHead, bool ignore ) {
	for( Var *var = varsHead; var; var = var->next ) {
		var->SetIgnore( ignore );
	}
}

template <typename Var>
inline uint32_t WorldState::ComputeHash( const Var *varsHead ) {
	// TODO: Could be optimized if we know that a number of vars is even (and if this is really needed).
	// We can cut a loop-carried dependency chain by half in that case.
	uint32_t result = 0;
	for( const Var *var = varsHead; var; var = var->next ) {
		result += var->Hash();
	}
	return result;
}

template <typename Var>
inline bool WorldState::CheckEquality( const Var *thisVars, const Var *thatVars ) {
	ZippedIterator<Var> iterator( thisVars, thatVars );
	while( iterator.HasNext() ) {
		if( !( ( *iterator.thisVar ) == ( *iterator.thatVar ) ) ) {
			return false;
		}
		iterator.Next();
	}
	return true;
}

void WorldState::CopyFromOtherWorldState( const WorldState &that ) {
	CopyVarsFromThat( this->shortVarsHead, that.shortVarsHead );
	CopyVarsFromThat( this->unsignedVarsHead, that.unsignedVarsHead );
	CopyVarsFromThat( this->boolVarsHead, that.boolVarsHead );
	CopyVarsFromThat( this->originVarsHead, that.originVarsHead );
	CopyVarsFromThat( this->originLazyVarsHead, that.originLazyVarsHead );
	CopyVarsFromThat( this->dualOriginLazyVarsHead, that.dualOriginLazyVarsHead );

#ifndef PUBLIC_BUILD
	isCopiedFromOtherWorldState = true;
#endif
}

void WorldState::SetIgnoreAll( bool ignore ) {
	SetIgnore( shortVarsHead, ignore );
	SetIgnore( unsignedVarsHead, ignore );
	SetIgnore( boolVarsHead, ignore );
	SetIgnore( originVarsHead, ignore );
	SetIgnore( originLazyVarsHead, ignore );
	SetIgnore( dualOriginLazyVarsHead, ignore );
}

bool WorldState::IsSatisfiedBy( const WorldState &that ) const {
	if( !CheckSatisfiedBy( this->shortVarsHead, that.shortVarsHead ) ) {
		return false;
	}
	if( !CheckSatisfiedBy( this->unsignedVarsHead, that.unsignedVarsHead ) ) {
		return false;
	}
	if( !CheckSatisfiedBy( this->boolVarsHead, that.boolVarsHead ) ) {
		return false;
	}
	if( !CheckSatisfiedBy( this->originVarsHead, that.originVarsHead ) ) {
		return false;
	}
	if( !CheckSatisfiedBy( this->originLazyVarsHead, that.originLazyVarsHead ) ) {
		return false;
	}
	if( !CheckSatisfiedBy( this->dualOriginLazyVarsHead, that.dualOriginLazyVarsHead ) ) {
		return false;
	}
	return true;
}

uint32_t WorldState::Hash() const {
	uint32_t result = 17;
	result = result * 31 + ComputeHash( shortVarsHead );
	result = result * 31 + ComputeHash( unsignedVarsHead );
	result = result * 31 + ComputeHash( boolVarsHead );
	result = result * 31 + ComputeHash( originVarsHead );
	result = result * 31 + ComputeHash( originLazyVarsHead );
	result = result * 31 + ComputeHash( dualOriginLazyVarsHead );
	return result;
}

bool WorldState::operator==( const WorldState &that ) const {
	if( !CheckEquality( this->shortVarsHead, that.shortVarsHead ) ) {
		return false;
	}
	if( !CheckEquality( this->unsignedVarsHead, that.unsignedVarsHead ) ) {
		return false;
	}
	if( !CheckEquality( this->boolVarsHead, that.boolVarsHead ) ) {
		return false;
	}
	if( !CheckEquality( this->originVarsHead, that.originVarsHead ) ) {
		return false;
	}
	if( !CheckEquality( this->originLazyVarsHead, that.originLazyVarsHead ) ) {
		return false;
	}
	if( !CheckEquality( this->dualOriginLazyVarsHead, that.dualOriginLazyVarsHead ) ) {
		return false;
	}
	return true;
}

#define PRINT_VAR( varName ) varName##Var().DebugPrint( tag, #varName )

void WorldState::DebugPrint( const char *tag ) const {
	// We have to list all vars manually
	// A list of vars does not (and should not) exist
	// since vars instances do not (and should not) exist in optimized by a compiler code
	// (WorldState members are accessed directly instead)

	PRINT_VAR( GoalItemWaitTime );
	PRINT_VAR( SimilarWorldStateInstanceId );

	PRINT_VAR( Health );
	PRINT_VAR( Armor );
	PRINT_VAR( RawDamageToKill );

	PRINT_VAR( BotOrigin );
	PRINT_VAR( EnemyOrigin );
	PRINT_VAR( NavTargetOrigin );
	PRINT_VAR( PendingOrigin );

	PRINT_VAR( HasQuad );
	PRINT_VAR( HasShell );
	PRINT_VAR( EnemyHasQuad );
	PRINT_VAR( HasThreateningEnemy );
	PRINT_VAR( HasJustPickedGoalItem );

	PRINT_VAR( HasPositionalAdvantage );
	PRINT_VAR( CanHitEnemy );
	PRINT_VAR( EnemyCanHit );
	PRINT_VAR( HasJustKilledEnemy );

	PRINT_VAR( IsRunningAway );
	PRINT_VAR( HasRunAway );

	PRINT_VAR( HasJustTeleported );
	PRINT_VAR( HasJustTouchedJumppad );
	PRINT_VAR( HasJustEnteredElevator );

	PRINT_VAR( HasPendingCoverSpot );
	PRINT_VAR( HasPendingRunAwayTeleport );
	PRINT_VAR( HasPendingRunAwayJumppad );
	PRINT_VAR( HasPendingRunAwayElevator );

	PRINT_VAR( RunAwayTeleportOrigin );
	PRINT_VAR( RunAwayJumppadOrigin );
	PRINT_VAR( RunAwayElevatorOrigin );
}

#define PRINT_DIFF( varName )                                           \
	do {                                                                \
		if( !( this->varName ## Var() == that.varName ## Var() ) ) {    \
			this->varName ## Var().DebugPrint( oldTag, #varName );      \
			that.varName ## Var().DebugPrint( newTag, #varName );       \
		}                                                               \
	} while( 0 )

void WorldState::DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const {
	PRINT_DIFF( GoalItemWaitTime );
	PRINT_DIFF( SimilarWorldStateInstanceId );

	PRINT_DIFF( Health );
	PRINT_DIFF( Armor );
	PRINT_DIFF( RawDamageToKill );

	PRINT_DIFF( BotOrigin );
	PRINT_DIFF( EnemyOrigin );
	PRINT_DIFF( NavTargetOrigin );
	PRINT_DIFF( PendingOrigin );

	PRINT_DIFF( HasQuad );
	PRINT_DIFF( HasShell );
	PRINT_DIFF( EnemyHasQuad );
	PRINT_DIFF( HasThreateningEnemy );
	PRINT_DIFF( HasJustPickedGoalItem );

	PRINT_DIFF( HasPositionalAdvantage );
	PRINT_DIFF( CanHitEnemy );
	PRINT_DIFF( EnemyCanHit );
	PRINT_DIFF( HasJustKilledEnemy );

	PRINT_DIFF( IsRunningAway );
	PRINT_DIFF( HasRunAway );

	PRINT_DIFF( HasJustTeleported );
	PRINT_DIFF( HasJustTouchedJumppad );
	PRINT_DIFF( HasJustEnteredElevator );

	PRINT_DIFF( HasPendingCoverSpot );
	PRINT_DIFF( HasPendingRunAwayTeleport );
	PRINT_DIFF( HasPendingRunAwayJumppad );
	PRINT_DIFF( HasPendingRunAwayElevator );

	PRINT_DIFF( RunAwayTeleportOrigin );
	PRINT_DIFF( RunAwayJumppadOrigin );
	PRINT_DIFF( RunAwayElevatorOrigin );
}
