#include "planninglocal.h"
#include "../bot.h"
#include "../groundtracecache.h"

PlannerNode *StartGotoRunAwayElevatorAction::TryApply( const WorldState &worldState ) {
	if( !checkCommonPreconditionsForStartingRunningAway( worldState ) ) {
		return nullptr;
	}

	if( worldState.getOriginVar( WorldState::PendingElevatorDest ) ) {
		Debug( "Pending origin is already present in the given world state\n" );
		return nullptr;
	}

	const Vec3 enemyOrigin = worldState.getOriginVar( WorldState::EnemyOrigin ).value();
	const Vec3 botOrigin = worldState.getOriginVar( WorldState::BotOrigin ).value();
	std::optional<DualOrigin> maybeFromTo = module->tacticalSpotsCache.getRunAwayElevatorOrigin( botOrigin, enemyOrigin );
	if( !maybeFromTo ) {
		Debug( "Failed to find a (cached) runaway elevator\n" );
		return nullptr;
	}

	PlannerNode *const plannerNode = newNodeForRecord( pool.New( Self() ), worldState, 1.0f );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode->worldState.setOriginVar( WorldState::NavTargetOrigin, OriginVar( maybeFromTo->first ) );
	plannerNode->worldState.setOriginVar( WorldState::PendingElevatorDest, OriginVar( maybeFromTo->second ) );

	return plannerNode;
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
	const std::optional<OriginVar> pendingOriginVar( worldState.getOriginVar( WorldState::PendingElevatorDest ) );
	if( !pendingOriginVar ) {
		Debug( "The pending elevator dest origin is missing in the given world state\n" );
		return nullptr;
	}

	const Vec3 botOrigin       = worldState.getOriginVar( WorldState::BotOrigin ).value();
	const Vec3 navTargetOrigin = worldState.getOriginVar( WorldState::NavTargetOrigin ).value();

	if( botOrigin.FastDistanceTo( navTargetOrigin ) > GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Bot is too far from the nav target (elevator origin)\n" );
		return nullptr;
	}

	const Vec3 &elevatorOrigin = navTargetOrigin;
	unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();

	const float elevatorDistance = ( elevatorOrigin - *pendingOriginVar ).LengthFast();
	// Assume that elevator speed is 400 units per second
	const float speedInUnitsPerMillis = 400 / 1000.0f;
	const float cost = elevatorDistance * Q_Rcp( speedInUnitsPerMillis );

	DoRunAwayViaElevatorActionRecord *record = pool.New( Self(), elevatorOrigin, selectedEnemiesInstanceId );
	
	PlannerNode *const plannerNode = newNodeForRecord( record, worldState, cost );
	if( !plannerNode ) {
		return nullptr;
	}

	// Set bot origin to the elevator destination
	plannerNode->worldState.setOriginVar( WorldState::BotOrigin, *pendingOriginVar );
	// Reset pending origin
	plannerNode->worldState.clearOriginVar( WorldState::PendingElevatorDest );

	plannerNode->worldState.setBoolVar( WorldState::IsRunningAway, BoolVar( true ) );
	plannerNode->worldState.setBoolVar( WorldState::CanHitEnemy, BoolVar( false ) );
	plannerNode->worldState.setBoolVar( WorldState::EnemyCanHit, BoolVar( false ) );

	return plannerNode;
}