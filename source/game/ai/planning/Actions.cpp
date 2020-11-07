#include "Actions.h"
#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../combat/TacticalSpotsRegistry.h"

BotActionRecord::BotActionRecord( PoolBase *pool_, Bot *self_, const char *name_ )
	: AiActionRecord( pool_, self_, name_ ) {}

BotAction::BotAction( BotPlanningModule *module_, const char *name_ )
	: AiAction( module_->bot, name_ ), module( module_ ) {}

void BotActionRecord::Activate() {
	AiActionRecord::Activate();
	Self()->GetMiscTactics().Clear();
}

void BotActionRecord::Deactivate() {
	AiActionRecord::Deactivate();
	Self()->GetMiscTactics().Clear();
}

bool CombatActionRecord::CheckCommonCombatConditions( const WorldState &currWorldState ) const {
	if( currWorldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is not specified\n" );
		return false;
	}
	if( Self()->GetSelectedEnemies().InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "New enemies have been selected\n" );
		return false;
	}
	return true;
}