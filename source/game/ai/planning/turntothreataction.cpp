#include "planninglocal.h"
#include "../bot.h"

void TurnToThreatOriginActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetPendingLookAtPoint( AiPendingLookAtPoint( threatPossibleOrigin, 3.0f ), 350 );
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
}

void TurnToThreatOriginActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetPendingLookAtPoint();
}

AiActionRecord::Status TurnToThreatOriginActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	const edict_t *ent = game.edicts + Self()->EntNum();

	vec3_t lookDir;
	AngleVectors( ent->s.angles, lookDir, nullptr, nullptr );

	Vec3 toThreatDir( threatPossibleOrigin );
	toThreatDir -= ent->s.origin;
	toThreatDir.NormalizeFast();

	if( toThreatDir.Dot( lookDir ) > Self()->FovDotFactor() ) {
		return COMPLETED;
	}

	return Self()->HasPendingLookAtPoint() ? VALID : INVALID;
}

PlannerNode *TurnToThreatOriginAction::TryApply( const WorldState &worldState ) {
	if( worldState.ThreatPossibleOriginVar().Ignore() ) {
		Debug( "Threat possible origin is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasReactedToThreatVar().Ignore() && worldState.HasReactedToThreatVar() ) {
		Debug( "Bot has already reacted to threat in the given world state\n" );
		return nullptr;
	}

	constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - Self()->Origin() ).SquaredLength() > squareDistanceError ) {
		Debug( "The action can be applied only to the current bot origin\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), worldState.ThreatPossibleOriginVar().Value() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 500;
	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().ThreatPossibleOriginVar().SetIgnore( true );
	// If a bot has reacted to threat, he can't hit current enemy (if any)
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false );
	plannerNode.WorldState().HasReactedToThreatVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}