#include "planninglocal.h"
#include "../bot.h"

PlannerNode *KillEnemyAction::TryApply( const WorldState &worldState ) {
	if( !worldState.getOriginVar( WorldState::EnemyOrigin ) ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( !isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasPositionalAdvantage ) ) ) {
		Debug( "Bot does not have positional advantage in the given world state\n" );
		return nullptr;
	}
	if( !isSpecifiedAndTrue( worldState.getBoolVar( WorldState::CanHitEnemy ) ) ) {
		Debug( "Bot can't hit enemy in the given world state\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	UIntVar stateDistinctionVar( Self()->NextSimilarWorldStateInstanceId() );

	plannerNode->worldState.setBoolVar( WorldState::HasJustKilledEnemy, BoolVar( true ) );
	plannerNode->worldState.setUIntVar( WorldState::SimilarWorldStateInstanceId, stateDistinctionVar );

	return plannerNode;
}