#include "planninglocal.h"
#include "../bot.h"

void PickupNavEntityActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->GetMiscTactics().shouldMoveCarefully = true;
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
	assert( m_selectedNavEntity.isSame( Self()->GetSelectedNavEntity() ) );
	const NavEntity *const navEntity = m_selectedNavEntity.navEntity;
	Self()->SetCampingSpot( AiCampingSpot( navEntity->Origin(), GOAL_PICKUP_ACTION_RADIUS, 0.5f ) );
	Self()->SetNavTarget( navEntity );
}

void PickupNavEntityActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
	Self()->ResetCampingSpot();
}

AiActionRecord::Status PickupNavEntityActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	if( currWorldState.HasJustPickedGoalItemVar() ) {
		Debug( "Goal item has been just picked up\n" );
		return COMPLETED;
	}

	if( !m_selectedNavEntity.isSame( Self()->GetSelectedNavEntity() ) ) {
		Debug( "The actual selected nav entity differs from the stored one\n" );
		return INVALID;
	}
	if( m_selectedNavEntity.navEntity->SpawnTime() - level.time > 0 ) {
		Debug( "The nav entity requires waiting for it\n" );
		return INVALID;
	}
	if( currWorldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "The nav entity is too far from the bot to pickup it\n" );
		return INVALID;
	}
	if( currWorldState.HasThreateningEnemyVar() ) {
		Debug( "The bot has threatening enemy\n" );
		return INVALID;
	}

	return VALID;
}

PlannerNode *PickupNavEntityAction::TryApply( const WorldState &worldState ) {
	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item is ignored in the given world state\n" );
		return nullptr;
	}

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to goal item nav target is too large to pick up an item in the given world state\n" );
		return nullptr;
	}

	if( worldState.HasJustPickedGoalItemVar().Ignore() ) {
		Debug( "Has bot picked a goal item is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustPickedGoalItemVar() ) {
		Debug( "Bot has just picked a goal item in the given world state\n" );
		return nullptr;
	}

	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item wait time is not specified in the given world state\n" );
		return nullptr;
	}
	if( worldState.GoalItemWaitTimeVar() > 0 ) {
		Debug( "Goal item wait time is non-zero in the given world state\n" );
		return nullptr;
	}

	const std::optional<SelectedNavEntity> &maybeSelectedNavEntity = Self()->GetSelectedNavEntity();
	const SelectedNavEntity &selectedNavEntity = maybeSelectedNavEntity.value();
	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( Self(), selectedNavEntity ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Picking up an item costs almost nothing
	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().BotOriginVar().SetValue( selectedNavEntity.navEntity->Origin() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, 12.0f );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}