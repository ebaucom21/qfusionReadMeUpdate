#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StartLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsReactingToEnemyLostVar().Ignore() ) {
		Debug( "Is bot reacting to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.IsReactingToEnemyLostVar() ) {
		Debug( "Bot is already reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar().Ignore() ) {
		Debug( "Has bot reacted to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar() ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}

	const float distanceToEnemy = worldState.BotOriginVar().DistanceTo( worldState.LostEnemyLastSeenOriginVar() );
	if( distanceToEnemy < 1.5f * GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is already close to the last seen enemy origin\n" );
		return nullptr;
	}

	// Vary pursuit max distance threshold depending of offensiveness.
	// Never pursue enemies farther than LG range (otherwise a poor bot behaviour is observed).
	const float maxDistanceThreshold = 96.0f + ( kLasergunRange - 96.0f ) * Self()->GetEffectiveOffensiveness();
	if( distanceToEnemy > maxDistanceThreshold ) {
		Debug( "The enemy is way too far for pursuing it\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - Self()->Origin() ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar().Ignore() ) {
		Debug( "Might bot see lost enemy after turn is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar() ) {
		Debug( "Bot might see lost enemy after turn in the given world state. Pursuing makes no sense.\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// (this action is dummy)
	plannerNode.Cost() = 1.0f;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.LostEnemyLastSeenOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, maxDistanceThreshold );
	plannerNode.WorldState().IsReactingToEnemyLostVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

PlannerNode *StopLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( worldState.IsReactingToEnemyLostVar().Ignore() ) {
		Debug( "Is bot reacting to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.IsReactingToEnemyLostVar() ) {
		Debug( "Bot is not reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar().Ignore() ) {
		Debug( "Has bot reacted to enemy lost is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasReactedToEnemyLostVar() ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( worldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() > 1.5f * GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasReactedToEnemyLostVar().SetValue( true );
	plannerNode.WorldState().IsReactingToEnemyLostVar().SetValue( false );

	return plannerNode.PrepareActionResult();
}