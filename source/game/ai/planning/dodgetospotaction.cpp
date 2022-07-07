#include "planninglocal.h"
#include "../bot.h"

void DodgeToSpotActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetNavTarget( &navSpot );
	timeoutAt = level.time + Hazard::TIMEOUT;
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
}

void DodgeToSpotActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status DodgeToSpotActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	// If the bot has reached the spot, consider the action completed
	// (use a low threshold because dodging is a precise movement)
	if( ( navSpot.Origin() - Self()->Origin() ).SquaredLength() < 16 * 16 ) {
		return COMPLETED;
	}

	// Return INVALID if has not reached the spot when the action timed out
	return timeoutAt > level.time ? VALID : INVALID;
}

PlannerNode *DodgeToSpotAction::TryApply( const WorldState &worldState ) {
	if( !worldState.getFloatVar( WorldState::PotentialHazardDamage ) ) {
		Debug( "Potential hazard damage is ignored in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const float *actualOrigin = Self()->Origin();
	if( botOrigin.DistanceTo( actualOrigin ) >= 1.0f ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	// TODO: Make it to be a lazy var
	const std::optional<OriginVar> dodgeSpotVar = worldState.getOriginVar( WorldState::DodgeHazardSpot );
	if( !dodgeSpotVar ) {
		Debug( "Spot for dodging a hazard is missing in the given world state, can't dodge\n" );
		return nullptr;
	}

	const Vec3 spotOrigin = *dodgeSpotVar;
	const int travelTimeMillis = Self()->CheckTravelTimeMillis( botOrigin, spotOrigin );
	if( !travelTimeMillis ) {
		Debug( "Warning: can't find travel time from the bot origin to the spot origin in the given world state\n" );
		return nullptr;
	}
	
	DodgeToSpotActionRecord *record = pool.New( Self(), spotOrigin );

	PlannerNode *const plannerNode = newNodeForRecord( record, worldState, (float)travelTimeMillis );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::BotOrigin, OriginVar( spotOrigin ) );
	plannerNode->worldState.setBoolVar( WorldState::HasReactedToHazard, BoolVar( true ) );
	plannerNode->worldState.clearFloatVar( WorldState::PotentialHazardDamage );
	plannerNode->worldState.clearOriginVar( WorldState::HazardHitPoint );
	plannerNode->worldState.clearOriginVar( WorldState::HazardDirection );

	return plannerNode;
}
