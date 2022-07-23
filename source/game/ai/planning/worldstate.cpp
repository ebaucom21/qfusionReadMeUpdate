#include "worldstate.h"
#include "../bot.h"

template <typename T>
static auto computeHash( const std::optional<T> *vars, size_t numVars ) -> uint32_t {
	uint32_t result = 0;
	for( size_t i = 0; i < numVars; ++i ) {
		result = result * 31;
		if( const std::optional<T> &var = vars[i]; var.has_value() ) {
			result += var->computeHash();
		}
	}
	return result;
}

template <typename T>
static bool checkEquality( const std::optional<T> *theseVars, const std::optional<T> *thatVars, size_t numVars ) {
	for( size_t i = 0; i < numVars; ++i ) {
		const std::optional<T> &thisVar = theseVars[i];
		const std::optional<T> &thatVar = thatVars[i];
		if( thisVar.has_value() ) {
			if( !thatVar.has_value() ) {
				return false;
			}
			return *thisVar == *thatVar;
		} else {
			if( thatVar.has_value() ) {
				return false;
			}
		}
	}
	return true;
}

auto WorldState::computeHash() const -> uint32_t {
	uint32_t result = 0;
	result = result * 31 + ::computeHash( m_floatVars, std::size( m_floatVars ) );
	result = result * 31 + ::computeHash( m_uintVars, std::size( m_uintVars ) );
	result = result * 31 + ::computeHash( m_boolVars, std::size( m_boolVars ) );
	result = result * 31 + ::computeHash( m_originVars, std::size( m_originVars ) );
	return result;
}

bool WorldState::operator==( const WorldState &that ) const {
	return
		::checkEquality( m_floatVars, that.m_floatVars, std::size( m_floatVars ) ) &&
		::checkEquality( m_uintVars, that.m_uintVars, std::size( m_uintVars ) ) &&
		::checkEquality( m_boolVars, that.m_boolVars, std::size( m_boolVars ) ) &&
		::checkEquality( m_originVars, that.m_originVars, std::size( m_originVars ) );
}

#define PRINT_VAR( varName ) do {} while( 0 ); // TODO

void WorldState::DebugPrint( const char *tag ) const {
	// We have to list all vars manually
	// A list of vars does not (and should not) exist
	// since vars instances do not (and should not) exist in optimized by a compiler code
	// (WorldState members are accessed directly instead)

	PRINT_VAR( GoalItemWaitTime );
	PRINT_VAR( SimilarWorldStateInstanceId );

	PRINT_VAR( BotOrigin );
	PRINT_VAR( EnemyOrigin );
	PRINT_VAR( NavTargetOrigin );
	PRINT_VAR( PendingOrigin );

	PRINT_VAR( HasThreateningEnemy );
	PRINT_VAR( HasPickedGoalItem );

	PRINT_VAR( HasPositionalAdvantage );
	PRINT_VAR( CanHitEnemy );
	PRINT_VAR( EnemyCanHit );
	PRINT_VAR( HasJustKilledEnemy );

	PRINT_VAR( IsRunningAway );
	PRINT_VAR( HasRunAway );
}

#define PRINT_DIFF( varName ) do {} while( 0 ) // TODO

void WorldState::DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const {
	PRINT_DIFF( GoalItemWaitTime );
	PRINT_DIFF( SimilarWorldStateInstanceId );

	PRINT_DIFF( BotOrigin );
	PRINT_DIFF( EnemyOrigin );
	PRINT_DIFF( NavTargetOrigin );
	PRINT_DIFF( PendingOrigin );

	PRINT_DIFF( HasThreateningEnemy );
	PRINT_DIFF( HasPickedGoalItem );

	PRINT_DIFF( HasPositionalAdvantage );
	PRINT_DIFF( CanHitEnemy );
	PRINT_DIFF( EnemyCanHit );
	PRINT_DIFF( HasJustKilledEnemy );

	PRINT_DIFF( IsRunningAway );
	PRINT_DIFF( HasRunAway );
}
