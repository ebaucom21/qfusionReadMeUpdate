#include "planninglocal.h"
#include "../bot.h"

void FleeToSpotActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
	Self()->SetNavTarget( &navSpot );
}

void FleeToSpotActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status FleeToSpotActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	const auto &selectedEnemies = Self()->GetSelectedEnemies();
	if( selectedEnemies.AreValid() && selectedEnemies.CouldBeHitIfBotTurns() ) {
		Self()->GetMiscTactics().PreferAttackRatherThanRun();
	} else {
		Self()->GetMiscTactics().PreferRunRatherThanAttack();
	}

	// It really gets invalidated on goal reevaluation

	if( ( navSpot.Origin() - Self()->Origin() ).LengthFast() <= GOAL_PICKUP_ACTION_RADIUS ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *FleeToSpotAction::TryApply( const WorldState &worldState ) {
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is absent in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too close to the nav target\n" );
		return nullptr;
	}

	const Vec3 navTargetOrigin = worldState.NavTargetOriginVar().Value();
	if( const std::optional<SelectedNavEntity> &maybeSelectedNavEntity = Self()->GetSelectedNavEntity() ) {
		const Vec3 navEntityOrigin = maybeSelectedNavEntity->navEntity->Origin();
		constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
		if( ( navEntityOrigin - navTargetOrigin ).SquaredLength() < squareDistanceError ) {
			Debug( "Action is not applicable for goal entities (there are specialized actions for these kinds of nav target\n" );
			return nullptr;
		}
	}

	// As a contrary to combat actions, illegal travel time (when the destination is not reachable for AAS) is allowed.
	// Combat actions require simple kinds of movement to keep crosshair on enemy.
	// Thus tactical spot should be reachable in common way for combat actions.
	// In case of retreating, some other kinds of movement AAS is not aware of might be used.
	int travelTimeMillis = Self()->CheckTravelTimeMillis( worldState.BotOriginVar().Value(), navTargetOrigin );
	// If the travel time is 0, set it to maximum allowed AAS travel time
	// (AAS stores time as seconds^-2 in a short value)
	if( !travelTimeMillis ) {
		travelTimeMillis = 10 * std::numeric_limits<short>::max();
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), navTargetOrigin ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = travelTimeMillis;

	plannerNode.WorldState() = worldState;
	// Move bot origin
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin );
	// Since bot origin has been moved, tactical spots should be recomputed
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}