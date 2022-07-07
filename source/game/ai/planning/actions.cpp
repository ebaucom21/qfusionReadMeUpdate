#include "actions.h"
#include "../bot.h"
#include "../groundtracecache.h"
#include "../combat/tacticalspotsregistry.h"

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