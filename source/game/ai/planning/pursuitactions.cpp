#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StartLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::IsReactingToEnemyLost ) ) ) {
		Debug( "Bot is already reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasReactedToEnemyLost ) ) ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}

	std::optional<OriginVar> lostEnemyOriginVar = worldState.getOriginVar( WorldState::LostEnemyLastSeenOrigin );
	if( !lostEnemyOriginVar ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const float distanceToEnemy = botOrigin.FastDistanceTo( *lostEnemyOriginVar );
	if( distanceToEnemy < GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is already close to the last seen enemy origin\n" );
		return nullptr;
	}

	// Vary pursuit max distance threshold depending on offensiveness.
	// Never pursue enemies farther than LG range (otherwise a poor bot behaviour is observed).
	const float maxDistanceThreshold = 96.0f + ( kLasergunRange - 96.0f ) * Self()->GetEffectiveOffensiveness();
	if( distanceToEnemy > maxDistanceThreshold ) {
		Debug( "The enemy is way too far for pursuing it\n" );
		return nullptr;
	}

	if( ( botOrigin - Self()->Origin() ).SquaredLength() > 1.0f ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	// TODO: Query in lazy fashion?
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::MightSeeLostEnemyAfterTurn ) ) ) {
		Debug( "Bot might see lost enemy after turn, pursuit would be pointless\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setBoolVar( WorldState::IsReactingToEnemyLost, BoolVar( true ) );
	plannerNode->worldState.setOriginVar( WorldState::NavTargetOrigin, *lostEnemyOriginVar );

	return plannerNode;
}

PlannerNode *StopLostEnemyPursuitAction::TryApply( const WorldState &worldState ) {
	if( isUnspecifiedOrFalse( worldState.getBoolVar( WorldState::IsReactingToEnemyLost ) ) ) {
		Debug( "Bot is not reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasReactedToEnemyLost ) ) ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}

	std::optional<OriginVar> lostEnemyOriginVar = worldState.getOriginVar( WorldState::LostEnemyLastSeenOrigin );
	if( !lostEnemyOriginVar ) {
		Debug( "Lost enemy origin is ignored in the given world state\n" );
		return nullptr;
	}

	std::optional<OriginVar> navTargetOriginVar = worldState.getOriginVar( WorldState::NavTargetOrigin );
	if( !navTargetOriginVar ) {
		Debug( "Nav target origin is ignored in the given world state\n" );
		return nullptr;
	}

	const Vec3 lostEnemyOrigin = *lostEnemyOriginVar;
	const Vec3 navTargetOrigin = *navTargetOriginVar;
	const Vec3 botOrigin       = worldState.getOriginVar( WorldState::BotOrigin ).value();

	if( lostEnemyOrigin.SquareDistanceTo( navTargetOrigin ) > 1.0f ) {
		Debug( "The lost enemy origin does not match nav target in the given world state\n" );
		return nullptr;
	}

	if( botOrigin.SquareDistanceTo( navTargetOrigin ) > wsw::square( GOAL_PICKUP_ACTION_RADIUS ) ) {
		Debug( "The bot is way too far from nav target in the given world state\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.clearOriginVar( WorldState::NavTargetOrigin );
	plannerNode->worldState.setBoolVar( WorldState::HasReactedToEnemyLost, BoolVar( true ) );
	plannerNode->worldState.setBoolVar( WorldState::IsReactingToEnemyLost, BoolVar( false ) );

	return plannerNode;
}