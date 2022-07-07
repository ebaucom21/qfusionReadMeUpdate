#include "planninglocal.h"
#include "../bot.h"

PlannerNode *StartGotoCoverAction::TryApply( const WorldState &worldState ) {
	if( !checkCommonPreconditionsForStartingRunningAway( worldState ) ) {
		return nullptr;
	}

	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::IsRunningAway ) ) ) {
		return nullptr;
	}

	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasRunAway ) ) ) {
		return nullptr;
	}

	if( worldState.getOriginVar( WorldState::PendingCoverSpot ) ) {
		Debug( "Pending origin is already present in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	if( botOrigin.FastDistanceTo( Self()->Origin() ) > 1.0f ) {
		Debug( "This action is only applicable to the actual bot origin\n" );
		return nullptr;
	}

	const Vec3 enemyOrigin = worldState.getOriginVar( WorldState::EnemyOrigin ).value();

	const std::optional<Vec3> maybeSpotOrigin = module->tacticalSpotsCache.getCoverSpot( botOrigin, enemyOrigin );
	if( !maybeSpotOrigin ) {
		return nullptr;
	}

	PlannerNode *const plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::NavTargetOrigin, OriginVar( *maybeSpotOrigin ) );
	plannerNode->worldState.setOriginVar( WorldState::PendingCoverSpot, OriginVar( *maybeSpotOrigin ) );

	return plannerNode;
}

void TakeCoverActionRecord::Activate() {
	BotActionRecord::Activate();
	// Since bot should be already close to the nav target, give (a defencive) aiming a higher priority
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
	Self()->SetNavTarget( &navSpot );
}

void TakeCoverActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status TakeCoverActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	static_assert( GOAL_PICKUP_ACTION_RADIUS > TACTICAL_SPOT_RADIUS );

	float distanceToActionNavTarget = ( navSpot.Origin() - Self()->Origin() ).SquaredLength();
	if( distanceToActionNavTarget > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from nav target\n" );
		return INVALID;
	}

	return ( distanceToActionNavTarget < TACTICAL_SPOT_RADIUS ) ? COMPLETED : VALID;
}

PlannerNode *TakeCoverAction::TryApply( const WorldState &worldState ) {
	const std::optional<OriginVar> navTargetOriginVar = worldState.getOriginVar( WorldState::NavTargetOrigin );
	if( !navTargetOriginVar ) {
		return nullptr;
	}

	const std::optional<OriginVar> pendingOriginVar = worldState.getOriginVar( WorldState::PendingCoverSpot );
	if( !pendingOriginVar ) {
		return nullptr;
	}

	if( Vec3( *pendingOriginVar ).SquareDistanceTo( *navTargetOriginVar ) > 1.0f ) {
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	if( botOrigin.SquareDistanceTo( *navTargetOriginVar ) > TACTICAL_SPOT_RADIUS ) {
		Debug( "Bot is too far from the nav target (pending cover spot)\n" );
		return nullptr;
	}

	const unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();
	TakeCoverActionRecord *record = pool.New( Self(), *navTargetOriginVar, selectedEnemiesInstanceId );
	
	PlannerNode *const plannerNode = newNodeForRecord( record, worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	// Bot origin var remains the same (it is close to nav target)

	plannerNode->worldState.clearOriginVar( WorldState::PendingCoverSpot );

	plannerNode->worldState.setBoolVar( WorldState::IsRunningAway, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::HasRunAway, BoolVar( true ) );
	plannerNode->worldState.setBoolVar( WorldState::CanHitEnemy, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::EnemyCanHit, BoolVar( false ) );

	return plannerNode;
}