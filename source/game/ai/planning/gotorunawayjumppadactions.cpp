#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StartGotoRunAwayJumppadAction::TryApply( const WorldState &worldState ) {
	if( !checkCommonPreconditionsForStartingRunningAway( worldState ) ) {
		return nullptr;
	}

	if( worldState.getOriginVar( WorldState::PendingJumppadDest ) ) {
		Debug( "The pending origin is already present in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const Vec3 enemyOrigin = worldState.getOriginVar( WorldState::EnemyOrigin ).value();
	std::optional<DualOrigin> maybeFromTo = module->tacticalSpotsCache.getRunAwayJumppadOrigin( botOrigin, enemyOrigin );
	if( !maybeFromTo ) {
		Debug( "Failed to find a (cached) runaway jumppad\n" );
		return nullptr;
	}

	PlannerNode *plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::NavTargetOrigin, OriginVar( maybeFromTo->first ) );
	plannerNode->worldState.setOriginVar( WorldState::PendingJumppadDest, OriginVar( maybeFromTo->second ) );

	return plannerNode;
}

void DoRunAwayViaJumppadActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetNavTarget( &navSpot );
}

void DoRunAwayViaJumppadActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status DoRunAwayViaJumppadActionRecord::UpdateStatus( const WorldState &currWorldState )  {
	if( level.time - Self()->LastTeleportTouchTime() < 64 ) {
		return COMPLETED;
	}

	if( selectedEnemiesInstanceId != Self()->GetSelectedEnemies().InstanceId() ) {
		Debug( "New enemies have been selected\n" );
		return INVALID;
	}

	// Use the same radius as for goal items pickups
	// (running actions for picking up an item and running away might be shared)
	if( ( navSpot.Origin() - Self()->Origin() ).LengthFast() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the jumppad trigger\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *DoRunAwayViaJumppadAction::TryApply( const WorldState &worldState ) {
	const std::optional<OriginVar> pendingOriginVar = worldState.getOriginVar( WorldState::PendingJumppadDest );
	if( !pendingOriginVar ) {
		Debug( "The pending jumppad dest is missing in the current world state\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.getOriginVar( WorldState::NavTargetOrigin ).value();
	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();

	if( botOrigin.FastDistanceTo( navTargetOrigin ) > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (jumppad origin)" );
		return nullptr;
	}

	const Vec3 &jumppadOrigin = navTargetOrigin;
	// Use distance from jumppad origin to target as an estimation for travel time millis
	const float cost = ( jumppadOrigin - *pendingOriginVar ).LengthFast();

	unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();
	DoRunAwayViaJumppadActionRecord *record = pool.New( Self(), jumppadOrigin, selectedEnemiesInstanceId );
	
	PlannerNode *const plannerNode = newNodeForRecord( record, worldState, cost );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::BotOrigin, *pendingOriginVar );
	plannerNode->worldState.clearOriginVar( WorldState::PendingJumppadDest );

	plannerNode->worldState.setBoolVar( WorldState::IsRunningAway, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::CanHitEnemy, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::EnemyCanHit, BoolVar( false ) );

	return plannerNode;
}

