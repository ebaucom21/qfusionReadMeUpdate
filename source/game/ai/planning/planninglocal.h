#ifndef WSW_09ba68bc_04d2_46ea_aa2e_be5b4ecfb29b_H
#define WSW_09ba68bc_04d2_46ea_aa2e_be5b4ecfb29b_H

#include "../bot.h"

inline PlannerNode::PlannerNode( PoolBase *pool, Ai *self )
	: PoolItem( pool ),	worldState( self ) {}

inline const BotWeightConfig &BotAction::WeightConfig() const {
	return Self()->WeightConfig();
}

#endif
