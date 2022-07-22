#include "../bot.h"
#include "../groundtracecache.h"
#include "../teamplay/squadbasedteam.h"
#include "botplanner.h"
#include "planninglocal.h"
#include "../combat/dodgehazardproblemsolver.h"

BotPlanner::BotPlanner( Bot *bot_, BotPlanningModule *module_ )
	: AiPlanner( bot_ ), bot( bot_ ), module( module_ ) {}

void BotPlanner::PrepareCurrWorldState( WorldState *worldState ) {
	worldState->setOriginVar( WorldState::BotOrigin, OriginVar( Vec3( bot->Origin() ) ) );

	if( const std::optional<SelectedEnemy> &selectedEnemy = bot->GetSelectedEnemy() ) {
		worldState->setOriginVar( WorldState::EnemyOrigin, OriginVar( selectedEnemy->LastSeenOrigin() ) );
		worldState->setBoolVar( WorldState::HasThreateningEnemy, BoolVar( selectedEnemy->IsThreatening() ) );
		worldState->setBoolVar( WorldState::CanHitEnemy, BoolVar( selectedEnemy->EnemyCanBeHit() ) );
		worldState->setBoolVar( WorldState::EnemyCanHit, BoolVar( selectedEnemy->EnemyCanHit() ) );
	}

	if( const std::optional<SelectedEnemy> &lostEnemies = bot->m_lostEnemy ) {
		const Vec3 &enemyOrigin( lostEnemies->LastSeenOrigin() );
		worldState->setOriginVar( WorldState::LostEnemyLastSeenOrigin, OriginVar( enemyOrigin ) );
		Vec3 toEnemiesDir( enemyOrigin - bot->Origin() );
		if( toEnemiesDir.normalizeFast() ) {
			if( toEnemiesDir.Dot( bot->EntityPhysicsState()->ForwardDir() ) < bot->FovDotFactor() ) {
				edict_t *self = game.edicts + bot->EntNum();
				if( EntitiesPvsCache::Instance()->AreInPvs( self, lostEnemies->TraceKey() ) ) {
					trace_t trace;
					G_Trace( &trace, self->s.origin, nullptr, nullptr, enemyOrigin.Data(), self, MASK_AISOLID );
					if( trace.fraction == 1.0f || game.edicts + trace.ent == lostEnemies->TraceKey() ) {
						worldState->setBoolVar( WorldState::MightSeeLostEnemyAfterTurn, BoolVar( true ) );
					}
				}
			}
		}
	}

	if( const std::optional<SelectedNavEntity> &currSelectedNavEntity = bot->GetOrUpdateSelectedNavEntity() ) {
		const NavEntity *navEntity = currSelectedNavEntity->navEntity;
		worldState->setOriginVar( WorldState::NavTargetOrigin, OriginVar( navEntity->Origin() ) );

		if( const int64_t spawnTime = navEntity->SpawnTime(); spawnTime >= level.time ) {
			// Find a travel time to the goal itme nav entity in milliseconds
			// We hope this router call gets cached by AAS subsystem
			int areaNums[2] = { 0, 0 };
			int numAreas = bot->EntityPhysicsState()->PrepareRoutingStartAreas( areaNums );
			const auto *routeCache = bot->RouteCache();
			int travelTime = 10 * routeCache->PreferredRouteToGoalArea( areaNums, numAreas, navEntity->AasAreaNum() );
			// AAS returns 1 seconds^-2 as a lowest feasible value
			if( travelTime <= 10 ) {
				travelTime = 0;
			}
			// If the goal item spawns after the moment when it gets reached
			if( const int64_t reachTime = level.time + travelTime; reachTime > spawnTime ) {
				const int64_t waitTime = reachTime - spawnTime;
				// Sanity checks
				// TODO: What about script-spawned items?
				assert( waitTime > 0 && waitTime < 60'000 );
				worldState->setUIntVar( WorldState::GoalItemWaitTime, UIntVar( waitTime ) );
			}
		}
	} else {
		// HACK! If there is no selected nav entity, set the value to the roaming spot origin.
		if( bot->ShouldUseRoamSpotAsNavTarget() ) {
			Vec3 spot( module->roamingManager.GetCachedRoamingSpot() );
			Debug( "Using a roaming spot @ %.1f %.1f %.1f as a world state nav target var\n", spot.X(), spot.Y(), spot.Z() );
			worldState->setOriginVar( WorldState::NavTargetOrigin, OriginVar( spot ) );
		}
	}

	if( bot->Skill() > 0.33f ) {
		if( const Hazard *activeHazard = bot->PrimaryHazard() ) {
			worldState->setFloatVar( WorldState::PotentialHazardDamage, FloatVar( activeHazard->damage ) );
		}
	}

	if( const auto *activeThreat = bot->ActiveHurtEvent() ) {
		worldState->setFloatVar( WorldState::ThreatInflictedDamage, FloatVar( activeThreat->totalDamage ) );
		worldState->setOriginVar( WorldState::ThreatPossibleOrigin, OriginVar( activeThreat->possibleOrigin ) );
	}

	cachedWorldState = *worldState;
}

// Cannot be defined in the header
bool BotPlanner::ShouldSkipPlanning() const {
	return !bot->CanInterruptMovement();
}

void BotPlanner::BeforePlanning() {
	AiPlanner::BeforePlanning();

	module->tacticalSpotsCache.clear();
}