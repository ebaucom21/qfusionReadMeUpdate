#include "planninglocal.h"
#include "../bot.h"
#include "../combat/sidestepdodgeproblemsolver.h"

void AttackFromCurrentPositionActionRecord::Activate() {
	BotActionRecord::Activate();
	Self()->GetMiscTactics().PreferAttackRatherThanRun();
	Self()->SetNavTarget( &navSpot );
}

void AttackFromCurrentPositionActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status AttackFromCurrentPositionActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	if( !CheckCommonCombatConditions( currWorldState ) ) {
		return INVALID;
	}

	if( navSpot.Origin().SquareDistance2DTo( Self()->Origin() ) < 16 * 16 ) {
		vec3_t spotOrigin;
		const edict_t *ent = game.edicts + Self()->EntNum();
		SideStepDodgeProblemSolver::OriginParams originParams( ent, 192.0f, AiAasRouteCache::Shared() );
		const Vec3 keepVisibleOrigin( Self()->GetSelectedEnemies().LastSeenOrigin() );
		SideStepDodgeProblemSolver::ProblemParams problemParams( keepVisibleOrigin );
		SideStepDodgeProblemSolver solver( originParams, problemParams );
		if( solver.findSingle( spotOrigin ) ) {
			self->SetNavTarget( Vec3( spotOrigin ), 4.0f );
		}
	}

	// This action is likely to be deactivate on goal search/reevaluation, do not do extra tests.
	return VALID;
}

PlannerNode *AttackFromCurrentPositionAction::TryApply( const WorldState &worldState ) {
	// Allow attacking from current position on high offensiveness even if a tactical spot exist
	// (attacking from tactical spots has more restrictive conditions on kill/be killed damage ratio).

	if( worldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is ignored in the given world state\n" );
		return nullptr;
	}
	if( !worldState.HasPositionalAdvantageVar().Ignore() && worldState.HasPositionalAdvantageVar() ) {
		Debug( "Bot already has a positional advantage in the given world state\n" );
		return nullptr;
	}
	if( worldState.HealthVar().Ignore() || worldState.ArmorVar().Ignore() ) {
		Debug( "Health or armor are ignored in the given world state\n" );
		return nullptr;
	}

	float offensiveness = Self()->GetEffectiveOffensiveness();
	if( offensiveness <= 0.5f && !worldState.HasThreateningEnemyVar() ) {
		return nullptr;
	}

	if( offensiveness != 1.0f ) {
		if( worldState.EnemyIsOnSniperRange() ) {
			if( !worldState.HasGoodSniperRangeWeaponsVar() ) {
				if( offensiveness <= 0.5f && !worldState.HasGoodFarRangeWeaponsVar() ) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnFarRange()) {
			if( offensiveness <= 0.5f ) {
				if( !worldState.HasGoodFarRangeWeaponsVar() && !worldState.HasGoodSniperRangeWeaponsVar()) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnMiddleRange()) {
			if( !worldState.HasGoodMiddleRangeWeaponsVar()) {
				if( offensiveness <= 0.5f && !worldState.HasGoodFarRangeWeaponsVar() ) {
					return nullptr;
				}
			}
		} else if( worldState.EnemyIsOnCloseRange()) {
			if( !worldState.HasGoodCloseRangeWeaponsVar()) {
				if( offensiveness <= 0.5f ) {
					if( !worldState.HasGoodMiddleRangeWeaponsVar() && !worldState.HasGoodFarRangeWeaponsVar() ) {
						return nullptr;
					}
				}
			}
		}

		// It is faster to check this apriori before spot assignation but the code becomes unmaintainable
		if( worldState.HasThreateningEnemyVar() && offensiveness != 1.0f ) {
			if( worldState.EnemyIsOnFarRange() || worldState.EnemyIsOnFarRange() ) {
				if( offensiveness <= 0.5f ) {
					if( worldState.DamageToBeKilled() < 80.0f && worldState.KillToBeKilledDamageRatio() > 2.0f ) {
						return nullptr;
					}
				}
			} else {
				if( worldState.KillToBeKilledDamageRatio() > 1.3f * offensiveness ) {
					return nullptr;
				}
			}
		}
	}

	Vec3 navTargetOrigin( worldState.BotOriginVar().Value() );
	unsigned selectedEnemiesInstanceId = Self()->GetSelectedEnemies().InstanceId();
	PlannerNodePtr plannerNode( NewNodeForRecord( pool.New( Self(), navTargetOrigin, selectedEnemiesInstanceId ) ) );
	if( !plannerNode ) {
		return nullptr;
	}

	// Set a huge penalty from attacking from a current position and not from a found tactical spot
	plannerNode.Cost() = 999999.0f;

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( navTargetOrigin )
		.SetSatisfyOp( OriginVar::SatisfyOp::EQ, TACTICAL_SPOT_RADIUS );
	// Setting this is required to satisfy the BotKillEnemyAction preconditions
	// (even they are not really met from human point of view).
	plannerNode.WorldState().HasPositionalAdvantageVar().SetValue( true ).SetIgnore( false );

	return plannerNode.PrepareActionResult();
}