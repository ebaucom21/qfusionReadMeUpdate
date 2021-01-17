#include "planninglocal.h"
#include "../bot.h"

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
	static_assert( GOAL_PICKUP_ACTION_RADIUS > TACTICAL_SPOT_RADIUS, "" );

	if( selectedEnemiesInstanceId != Self()->GetSelectedEnemies().InstanceId() ) {
		Debug( "New enemies have been selected\n" );
		return INVALID;
	}

	float distanceToActionNavTarget = ( navSpot.Origin() - Self()->Origin() ).SquaredLength();
	if( distanceToActionNavTarget > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from nav target\n" );
		return INVALID;
	}

	return ( distanceToActionNavTarget < TACTICAL_SPOT_RADIUS ) ? COMPLETED : VALID;
}

PlannerNode *TakeCoverAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPendingCoverSpotVar().Ignore() || !worldState.HasPendingCoverSpotVar() ) {
		Debug( "Has bot pending cover spot is ignored or absent in the given world state\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.NavTargetOriginVar().Value();

#ifdef _DEBUG
	// Sanity check
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( "BotTakeCoverAction", "PendingOriginVar() is ignored in the given world state\n" );
	}

	constexpr float distanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.PendingOriginVar().Value() - navTargetOrigin ).SquaredLength() > distanceError ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( "BotTakeCoverAction", "PendingOrigin and NavTargetOrigin differ in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (pending cover spot)\n" );
		return nullptr;
	}

	unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), navTargetOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingCoverSpotVar().SetIgnore( true );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	// Bot origin var remains the same (it is close to nav target)
	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}