#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StopRunningAwayAction::TryApply( const WorldState &worldState ) {
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::IsRunningAway ) ) ) {
		Debug( "Bot is not running away in the given world state\n" );
		return nullptr;
	}
	if( !isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasRunAway ) ) ) {
		Debug( "Bot has already run away in the given world state\n" );
		return nullptr;
	}
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::EnemyCanHit ) ) ) {
		Debug( "Enemy still can hit in the given world state\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode( newNodeForRecord( pool.New( Self() ), worldState, 1.0f ) );
	if( !plannerNode ) {
		return nullptr;
	}

	UIntVar stateDistinctionVar( Self()->NextSimilarWorldStateInstanceId() );

	plannerNode->worldState.setBoolVar( WorldState::IsRunningAway, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::HasRunAway, BoolVar( true ) );
	plannerNode->worldState.setUIntVar( WorldState::SimilarWorldStateInstanceId, stateDistinctionVar );

	return plannerNode;
}