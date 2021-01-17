#include "planninglocal.h"
#include "../bot.h"
#include "../groundtracecache.h"

PlannerNode *StartGotoRunAwayElevatorAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( worldState.HasJustEnteredElevatorVar().Ignore() ) {
		Debug( "Has bot just entered an elevator is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasJustEnteredElevatorVar() ) {
		Debug( "Bot has just entered an elevator in the given world state\n" );
		return nullptr;
	}
	if( !worldState.PendingOriginVar().Ignore() ) {
		Debug( "Pending origin is already present in the given world state\n" );
		return nullptr;
	}
	if( worldState.RunAwayElevatorOriginVar().IgnoreOrAbsent() ) {
		Debug( "An elevator for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self() ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = 1.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasPendingRunAwayElevatorVar().SetValue( true ).SetIgnore( false );
	// Set nav target to the elevator origin
	plannerNode.WorldState().NavTargetOriginVar().SetValue( worldState.RunAwayElevatorOriginVar().Value() );
	plannerNode.WorldState().NavTargetOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().NavTargetOriginVar().SetIgnore( false );
	// Set pending origin to the elevator destination
	plannerNode.WorldState().PendingOriginVar().SetValue( worldState.RunAwayElevatorOriginVar().Value2() );
	plannerNode.WorldState().PendingOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().PendingOriginVar().SetIgnore( false );

	return plannerNode.PrepareActionResult();
}

void DoRunAwayViaElevatorActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->SetNavTarget( &navSpot );
}

void DoRunAwayViaElevatorActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status DoRunAwayViaElevatorActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	// Checking of this action record differs from other run away action record.
	// We want the bot to stand on a platform until it finishes its movement.

	// We do not want to invalidate an action due to being a bit in air above the platform, don't check self->groundentity
	trace_t selfTrace;
	const edict_t *ent = game.edicts + Self()->EntNum();
	AiGroundTraceCache::Instance()->GetGroundTrace( ent, 64.0f, &selfTrace );

	if( selfTrace.fraction == 1.0f ) {
		Debug( "Bot is too high above the ground (if any)\n" );
		return INVALID;
	}
	if( selfTrace.ent <= gs.maxclients || game.edicts[selfTrace.ent].use != Use_Plat ) {
		Debug( "Bot is not above a platform\n" );
		return INVALID;
	}

	// If there are no valid enemies, just keep standing on the platform
	const auto &selectedEnemies = Self()->GetSelectedEnemies();
	if( selectedEnemies.AreValid() ) {
		trace_t enemyTrace;
		AiGroundTraceCache::Instance()->GetGroundTrace( selectedEnemies.Ent(), 128.0f, &enemyTrace );
		if( enemyTrace.fraction != 1.0f && enemyTrace.ent == selfTrace.ent ) {
			Debug( "Enemy is on the same platform!\n" );
			return INVALID;
		}
	}

	if( game.edicts[selfTrace.ent].moveinfo.state == STATE_TOP ) {
		return COMPLETED;
	}

	return VALID;
}

PlannerNode *DoRunAwayViaElevatorAction::TryApply( const WorldState &worldState ) {
	if( !CheckCommonRunAwayPreconditions( worldState ) ) {
		return nullptr;
	}

	if( !worldState.HasJustEnteredElevatorVar().Ignore() && worldState.HasJustEnteredElevatorVar() ) {
		Debug( "Bot has just entered elevator in the given world state\n" );
		return nullptr;
	}
	if( worldState.HasPendingRunAwayElevatorVar().Ignore() || !worldState.HasPendingRunAwayElevatorVar() ) {
		Debug( "Has bot a pending elevator for running away is ignored or absent in the given world state\n" );
		return nullptr;
	}

#ifdef _DEBUG
	// Sanity check
	if( worldState.NavTargetOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( this->name, "Nav target origin is ignored in the given world state\n" );
	}
	if( worldState.PendingOriginVar().Ignore() ) {
		worldState.DebugPrint( "Given WS" );
		AI_FailWith( this->name, "Pending origin is ignored in the given world state\n" );
	}
#endif

	if( worldState.DistanceToNavTarget() > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (elevator origin)\n" );
		return nullptr;
	}

	Vec3 elevatorOrigin = worldState.NavTargetOriginVar().Value();
	unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), elevatorOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	float elevatorDistance = ( elevatorOrigin - worldState.PendingOriginVar().Value() ).LengthFast();
	// Assume that elevator speed is 400 units per second
	float speedInUnitsPerMillis = 400 / 1000.0f;
	plannerNode.Cost() = elevatorDistance / speedInUnitsPerMillis;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().HasJustEnteredElevatorVar().SetValue( true ).SetIgnore( false );
	// Set bot origin to the elevator destination
	plannerNode.WorldState().BotOriginVar().SetValue( worldState.PendingOriginVar().Value() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	// Reset pending origin
	plannerNode.WorldState().PendingOriginVar().SetIgnore( true );
	plannerNode.WorldState().HasPendingRunAwayElevatorVar().SetIgnore( true );
	// Tactical spots should be recomputed for the new bot origin
	plannerNode.WorldState().ResetTacticalSpots();

	plannerNode.WorldState().IsRunningAwayVar().SetValue( true ).SetIgnore( false );
	plannerNode.WorldState().CanHitEnemyVar().SetValue( false ).SetIgnore( false );
	plannerNode.WorldState().EnemyCanHitVar().SetValue( false ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}