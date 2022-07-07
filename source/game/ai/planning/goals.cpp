#include "planninglocal.h"
#include "goals.h"
#include "../bot.h"
#include <cmath>
#include <cstdlib>

BotGoal::BotGoal( BotPlanningModule *module_, const char *name_, int debugColor_, unsigned updatePeriod_ )
	: AiGoal( module_->bot, name_, updatePeriod_ ), module( module_ ) {
	this->debugColor = debugColor_;
}

inline const std::optional<SelectedNavEntity> &BotGoal::getSelectedNavEntity() const {
	return Self()->GetSelectedNavEntity();
}

inline const SelectedEnemies &BotGoal::SelectedEnemies() const {
	return Self()->GetSelectedEnemies();
}

inline const BotWeightConfig &BotGoal::WeightConfig() const {
	return Self()->WeightConfig();
}

inline PlannerNode *BotGoal::ApplyExtraActions( PlannerNode *firstTransition, const WorldState &worldState ) {
	for( AiAction *action: extraApplicableActions ) {
		if( PlannerNode *currTransition = action->TryApply( worldState ) ) {
			currTransition->nextTransition = firstTransition;
			firstTransition = currTransition;
		}
	}
	return firstTransition;
}

void GrabItemGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	const std::optional<SelectedNavEntity> &maybeSelectedNavEntity = getSelectedNavEntity();
	if( !maybeSelectedNavEntity ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.grabItem;
	this->weight = configGroup.baseWeight;
	this->weight += configGroup.selectedGoalWeightScale * maybeSelectedNavEntity->pickupGoalWeight;

	// Hack! Lower a weight of this goal if there are threatening enemies
	// and we have to wait for an item while being attacking
	// and the gametype seems to be round based (this is primarily for bomb).

	// If the assigned weight is not significant
	if( this->weight <= 1.0f ) {
		return;
	}

	const auto *navEntity = maybeSelectedNavEntity->navEntity;
	// Skip if we do not have to wait for nav entity reached signal
	if( !navEntity->ShouldBeReachedOnEvent() ) {
		return;
	}

	// This is a hack to cut off most non-round-based gametypes
	if( level.gametype.spawnableItemsMask & IT_HEALTH ) {
		return;
	}

	const auto &selectedEnemies = SelectedEnemies();
	// Skip if there's no active threatening enemies
	if( !selectedEnemies.AreValid() || !selectedEnemies.AreThreatening() ) {
		return;
	}

	// Rush to the item site if it is far or is not in PVS
	const Vec3 botOrigin( currWorldState.BotOriginVar().Value() );

	// LG range seems to be an appropriate threshold
	if( botOrigin.SquareDistanceTo( navEntity->Origin() ) > wsw::square( kLasergunRange ) ) {
		return;
	}

	if( !trap_inPVS( botOrigin.Data(), navEntity->Origin().Data() ) ) {
		return;
	}

	// Force killing enemies instead
	this->module->killEnemyGoal.SetAdditionalWeight( this->weight );
	// Clamp the weight of this goal
	this->weight = wsw::min( this->weight, 1.0f );
}

void GrabItemGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
}

#define TRY_APPLY_ACTION( actionName )                                                       \
	do {                                                                                     \
		if( PlannerNode *currTransition = module->actionName.TryApply( worldState ) ) {      \
			currTransition->nextTransition = firstTransition;                                \
			firstTransition = currTransition;                                                \
		}                                                                                    \
	} while( 0 )

PlannerNode *GrabItemGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( runToNavEntityAction );
	TRY_APPLY_ACTION( pickupNavEntityAction );
	TRY_APPLY_ACTION( waitForNavEntityAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void KillEnemyGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	// Retrieve the additional weight possibly set by GrabItemGoal
	const float additionalWeightSet = GetAndResetAdditionalWeight();
	// Skip this goal in case when there's a top-tier nav target but make sure the bot wasn't forced to kill enemies
	// (todo: should this action decide everything instead?)
	if( additionalWeightSet <= 0 && Self()->IsNavTargetATopTierItem() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.killEnemy;

	this->weight = configGroup.baseWeight;
	this->weight += configGroup.offCoeff * Self()->GetEffectiveOffensiveness();
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}

	this->weight += additionalWeightSet;
}

void KillEnemyGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *KillEnemyGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	return ApplyExtraActions( firstTransition, worldState );
}

void RunAwayGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}
	if( !SelectedEnemies().AreThreatening() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.runAway;

	this->weight = configGroup.baseWeight;
	this->weight = configGroup.offCoeff * ( 1.0f - Self()->GetEffectiveOffensiveness() );
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}
}

void RunAwayGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasRunAwayVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *RunAwayGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( fleeToSpotAction );

	TRY_APPLY_ACTION( startGotoCoverAction );
	TRY_APPLY_ACTION( takeCoverAction );

	TRY_APPLY_ACTION( startGotoRunAwayTeleportAction );
	TRY_APPLY_ACTION( doRunAwayViaTeleportAction );

	TRY_APPLY_ACTION( startGotoRunAwayJumppadAction );
	TRY_APPLY_ACTION( doRunAwayViaJumppadAction );

	TRY_APPLY_ACTION( startGotoRunAwayElevatorAction );
	TRY_APPLY_ACTION( doRunAwayViaElevatorAction );

	TRY_APPLY_ACTION( stopRunningAwayAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void AttackOutOfDespairGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.attackOutOfDespair;

	if( SelectedEnemies().FireDelay() > configGroup.nmyFireDelayThreshold ) {
		return;
	}

	// The bot already has the maximal offensiveness, changing it would have the same effect as using duplicated search.
	if( Self()->GetEffectiveOffensiveness() == 1.0f ) {
		return;
	}

	this->weight = configGroup.baseWeight;
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight += configGroup.nmyThreatExtraWeight;
	}
	float damageWeightPart = BoundedFraction( SelectedEnemies().TotalInflictedDamage(), configGroup.dmgUpperBound );
	this->weight += configGroup.dmgFracCoeff * damageWeightPart;
}

void AttackOutOfDespairGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

void AttackOutOfDespairGoal::OnPlanBuildingStarted() {
	// Hack: save the bot's base offensiveness and enrage the bot
	this->oldOffensiveness = Self()->GetBaseOffensiveness();
	Self()->SetBaseOffensiveness( 1.0f );
}

void AttackOutOfDespairGoal::OnPlanBuildingCompleted( const AiActionRecord *planHead ) {
	// Hack: restore the bot's base offensiveness
	Self()->SetBaseOffensiveness( this->oldOffensiveness );
}

PlannerNode *AttackOutOfDespairGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToHazardGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToHazardVar().SetIgnore( false ).SetValue( true );
}

void ReactToHazardGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.PotentialHazardDamageVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToHazard;

	float damageFraction = currWorldState.PotentialHazardDamageVar() / DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageFraction;
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound * Q_Sqrt( weight_ );

	this->weight = weight_;
}

PlannerNode *ReactToHazardGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( dodgeToSpotAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToThreatGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.ThreatPossibleOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToThreat;
	float damageRatio = currWorldState.ThreatInflictedDamageVar() / DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageRatio;
	float offensiveness = Self()->GetEffectiveOffensiveness();
	if( offensiveness >= 0.5f ) {
		weight_ *= ( 1.0f + configGroup.offCoeff * ( offensiveness - 0.5f ) );
	}
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound * Q_Sqrt( weight_ );

	this->weight = weight_;
}

void ReactToThreatGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToThreatVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *ReactToThreatGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToThreatOriginAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToEnemyLostGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToEnemyLost;
	const float offensiveness = Self()->GetEffectiveOffensiveness();
	this->weight = configGroup.baseWeight + configGroup.offCoeff * offensiveness;

	if( currWorldState.MightSeeLostEnemyAfterTurnVar() ) {
		ModifyWeightForTurningBack( currWorldState );
	} else {
		ModifyWeightForPursuit( currWorldState );
	}
}

void ReactToEnemyLostGoal::ModifyWeightForTurningBack( const WorldState &currWorldState ) {
	// There's really nothing more to do than shooting and avoiding to be shot in instagib
	if( GS_Instagib() ) {
		this->weight *= 3.0f;
		return;
	}

	const float offensiveness = Self()->GetEffectiveOffensiveness();
	// We know a certain distance threshold that losing enemy out of sight can be very dangerous. This is LG range.
	const float distanceToEnemy = currWorldState.LostEnemyLastSeenOriginVar().DistanceTo( currWorldState.BotOriginVar() );
	if( distanceToEnemy < kLasergunRange ) {
		this->weight *= 1.75f + 3.0f * offensiveness;
		return;
	}

	// TODO!!!!!
	// bool hasSniperRangeWeapons = currWorldState.EnemyHasGoodSniperRangeWeaponsVar();
	// bool hasFarRangeWeapons = currWorldState.EnemyHasGoodFarRangeWeaponsVar();
	//if( !hasSniperRangeWeapons && !hasFarRangeWeapons ) {
	//	return;
	//}

	// TODO!!!!
	this->weight *= 1.0f + 1.0f * offensiveness; // ( (int)hasSniperRangeWeapons + (int)hasFarRangeWeapons ) * offensiveness;
}

void ReactToEnemyLostGoal::ModifyWeightForPursuit( const WorldState &currWorldState ) {
	// Disallow pursuit explicitly if there's an active nav target that is a top tier item
	if( Self()->IsNavTargetATopTierItem() ) {
		weight = 0.0f;
		return;
	}

	const float offensiveness = Self()->GetEffectiveOffensiveness();
	if( HuntEnemiesLeftInMinority( currWorldState ) ) {
		weight = 1.0f + weight + 2.0f * ( weight + offensiveness );
		return;
	}

	// Don't add weight for pursuing far enemies
	float distanceThreshold = 192.0f;
	const auto *inventory = Self()->Inventory();
	const bool hasOffensivePowerups = inventory[POWERUP_QUAD] || inventory[POWERUP_SHELL];
	// Increase the threshold wearing powerups or in duel-like gametypes
	if( hasOffensivePowerups ) {
		distanceThreshold = 1024.0f + 256.0f;
	} else if( GS_IndividualGameType() ) {
		distanceThreshold = 768.0f;
	}

	const float distanceToEnemy = currWorldState.LostEnemyLastSeenOriginVar().DistanceTo( currWorldState.BotOriginVar() );
	if( distanceToEnemy > distanceThreshold ) {
		return;
	}

	// Add an additive part in this case
	if( hasOffensivePowerups ) {
		this->weight += 1.0f;
	}

	// Force pursuit if the enemy is very close
	float distanceFactor = 1.0f - Q_Sqrt( distanceToEnemy * Q_Rcp( distanceThreshold ) );
	this->weight *= 1.25f + 3.0f * distanceFactor * offensiveness;
}

bool ReactToEnemyLostGoal::HuntEnemiesLeftInMinority( const WorldState &currWorldState ) const {
	// Don't chase if there's a valid (non-negative) assigned defence spot id
	if( Self()->DefenceSpotId() >= 0 ) {
		return false;
	}

	const edict_t *const gameEdicts = game.edicts;
	const edict_t *const self = gameEdicts + Self()->EntNum();
	int enemyTeam = TEAM_PLAYERS;
	if( self->s.team > TEAM_PLAYERS ) {
		enemyTeam = self->s.team == TEAM_ALPHA ? TEAM_BETA : TEAM_ALPHA;
	}

	// Check whether dead enemies have to wait for a round end
	if( G_SpawnQueue_GetSystem( enemyTeam ) != SPAWNSYSTEM_HOLD ) {
		return false;
	}

	// If there's no teammates in this gametype check if the bot is left 1v1
	if( enemyTeam == TEAM_PLAYERS ) {
		return FindNumPlayersAlive( TEAM_PLAYERS ) == 2;
	}

	const int ourTeam = enemyTeam == TEAM_ALPHA ? TEAM_BETA: TEAM_ALPHA;
	return FindNumPlayersAlive( ourTeam ) >= 2 * FindNumPlayersAlive( enemyTeam );
}

int ReactToEnemyLostGoal::FindNumPlayersAlive( int team ) const {
	int result = 0;
	const edict_t *gameEdicts = game.edicts;
	const auto &__restrict list = ::teamlist[team];
	for( int i = 0; i < list.numplayers; ++i ) {
		const edict_t *ent = gameEdicts + list.playerIndices[i];
		if( !G_ISGHOSTING( ent ) ) {
			result++;
		}
	}
	return result;
}

void ReactToEnemyLostGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToEnemyLostVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *ReactToEnemyLostGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToLostEnemyAction );
	TRY_APPLY_ACTION( startLostEnemyPursuitAction );
	TRY_APPLY_ACTION( fleeToSpotAction );
	TRY_APPLY_ACTION( stopLostEnemyPursuitAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void RoamGoal::UpdateWeight( const WorldState &currWorldState ) {
	// This goal is a fallback goal. Set the lowest feasible weight if it should be positive.
	if( Self()->ShouldUseRoamSpotAsNavTarget() ) {
		this->weight = 0.000001f;
		return;
	}

	this->weight = 0.0f;
}

void RoamGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	const Vec3 &spotOrigin = module->roamingManager.GetCachedRoamingSpot();
	worldState->BotOriginVar().SetValue( spotOrigin );
	worldState->BotOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, 32.0f );
	worldState->BotOriginVar().SetIgnore( false );
}

PlannerNode *RoamGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( fleeToSpotAction );

	return ApplyExtraActions( firstTransition, worldState );
}