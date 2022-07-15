#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StartGotoRunAwayTeleportAction::TryApply( const WorldState &worldState ) {
	if( !checkCommonPreconditionsForStartingRunningAway( worldState ) ) {
		return nullptr;
	}

	if( worldState.getOriginVar( WorldState::PendingTeleportDest ) ) {
		Debug( "The pending origin is already present in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const Vec3 enemyOrigin = worldState.getOriginVar( WorldState::EnemyOrigin ).value();

	std::optional<DualOrigin> maybeFromTo = module->tacticalSpotsCache.getRunAwayTeleportOrigin( botOrigin, enemyOrigin );
	if( !maybeFromTo ) {
		Debug( "Failed to find a (cached) runaway teleport\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode( newNodeForRecord( pool.New( Self() ), worldState, 1.0f ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::NavTargetOrigin, OriginVar( maybeFromTo->first ) );
	plannerNode->worldState.setOriginVar( WorldState::PendingTeleportDest, OriginVar( maybeFromTo->second ) );

	return plannerNode;
}

void DoRunAwayViaTeleportActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetNavTarget( &navSpot );
}

void DoRunAwayViaTeleportActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

PlannerNode *DoRunAwayViaTeleportAction::TryApply( const WorldState &worldState ) {
	const std::optional<OriginVar> pendingOriginVar = worldState.getOriginVar( WorldState::PendingTeleportDest );
	if( !pendingOriginVar ) {
		Debug( "The pending teleport dest is missing in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const Vec3 navTargetOrigin = worldState.getOriginVar( WorldState::NavTargetOrigin ).value();

	if( botOrigin.SquareDistanceTo( navTargetOrigin ) > wsw::square( GOAL_PICKUP_ACTION_RADIUS ) ) {
		Debug( "Bot is too far from the nav target (teleport origin)\n" );
		return nullptr;
	}

	const Vec3 &teleportOrigin = navTargetOrigin;
	const std::optional<SelectedEnemy> &selectedEnemy = Self()->GetSelectedEnemy();
	assert( selectedEnemy );
	DoRunAwayViaTeleportActionRecord *record = pool.New( Self(), teleportOrigin, selectedEnemy->InstanceId() );
	
	PlannerNode *const plannerNode = newNodeForRecord( record, worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::BotOrigin, *pendingOriginVar );
	plannerNode->worldState.clearOriginVar( WorldState::PendingTeleportDest );
	plannerNode->worldState.setBoolVar( WorldState::IsRunningAway, BoolVar( true ) );
	plannerNode->worldState.setBoolVar( WorldState::EnemyCanHit, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::CanHitEnemy, BoolVar( false ) );

	return plannerNode;
}

AiActionRecord::Status DoRunAwayViaTeleportActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	if( level.time - Self()->LastTeleportTouchTime() < 64 ) {
		return COMPLETED;
	}

	// Use the same radius as for goal items pickups
	// (running actions for picking up an item and running away might be shared)
	if( ( navSpot.Origin() - Self()->Origin() ).SquaredLength() > wsw::square( GOAL_PICKUP_ACTION_RADIUS ) ) {
		Debug( "Bot is too far from the teleport trigger\n" );
		return INVALID;
	}

	return VALID;
}