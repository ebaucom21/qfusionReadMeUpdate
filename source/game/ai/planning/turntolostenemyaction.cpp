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

	constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - Self()->Origin() ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}
	if( worldState.MightSeeLostEnemyAfterTurnVar().Ignore() ) {
		Debug( "Might bot see lost enemy after turn is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.MightSeeLostEnemyAfterTurnVar() ) {
		Debug( "Bot cannot see lost enemy after turn in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), worldState.LostEnemyLastSeenOriginVar().Value() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 400.0f;
	plannerNode.WorldState() = worldState;
	// Can't hit current enemy (if any) after applying this action
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false );
	plannerNode.WorldState().HasReactedToEnemyLostVar().SetValue( true );

	return plannerNode.PrepareActionResult();
}
