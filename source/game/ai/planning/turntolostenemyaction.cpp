#include "planninglocal.h"
#include "../bot.h"

void TurnToLostEnemyActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetPendingLookAtPoint( AiPendingLookAtPoint( lastSeenEnemyOrigin, 3.0f ), 400 );
	Self()->GetMiscTactics().PreferRunRatherThanAttack();
}

void TurnToLostEnemyActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetPendingLookAtPoint();
}

AiActionRecord::Status TurnToLostEnemyActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	const edict_t *ent = game.edicts + Self()->EntNum();

	vec3_t lookDir;
	AngleVectors( ent->s.angles, lookDir, nullptr, nullptr );

	Vec3 toEnemyDir( lastSeenEnemyOrigin );
	toEnemyDir -= ent->s.origin;
	if( !toEnemyDir.normalizeFast() ) {
		return COMPLETED;
	}

	if( toEnemyDir.Dot( lookDir ) >= Self()->FovDotFactor() ) {
		return COMPLETED;
	}

	return Self()->HasPendingLookAtPoint() ? VALID : INVALID;
}

PlannerNode *TurnToLostEnemyAction::TryApply( const WorldState &worldState ) {
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::IsReactingToEnemyLost ) ) ) {
		Debug( "Bot is already reacting to enemy lost in the given world state\n" );
		return nullptr;
	}
	if( isSpecifiedAndTrue( worldState.getBoolVar( WorldState::HasReactedToEnemyLost ) ) ) {
		Debug( "Bot has already reacted to enemy lost in the given world state\n" );
		return nullptr;
	}

	std::optional<OriginVar> lostEnemyOrigin = worldState.getOriginVar( WorldState::LostEnemyLastSeenOrigin );
	if( !lostEnemyOrigin ) {
		Debug( "Lost enemy origin is missing in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin( worldState.getOriginVar( WorldState::BotOrigin ).value() );
	if( botOrigin.FastDistanceTo( Self()->Origin() ) > 1.0f ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	if( isUnspecifiedOrFalse( worldState.getBoolVar( WorldState::MightSeeLostEnemyAfterTurn ) ) ) {
		Debug( "Bot cannot see lost enemy after turn in the given world state\n" );
		return nullptr;
	}

	TurnToLostEnemyActionRecord *record = pool.New( Self(), *lostEnemyOrigin );

	PlannerNode *plannerNode = newNodeForRecord( record, worldState, 500.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	// Can't hit current enemy (if any) after applying this action
	plannerNode->worldState.setBoolVar( WorldState::CanHitEnemy, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::HasReactedToEnemyLost, BoolVar( true ) );

	return plannerNode;
}
