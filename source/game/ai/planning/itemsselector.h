#ifndef WSW_9aafc0da_a31d_468a_855d_638b0fdfd95b_H
#define WSW_9aafc0da_a31d_468a_855d_638b0fdfd95b_H

#include "../ailocal.h"
#include "goalentities.h"
#include "../../../qcommon/wswstaticvector.h"

class SelectedNavEntity {
	friend class Bot;
	friend class BotItemsSelector;
	friend class AiSquad;

	const NavEntity *navEntity;
	float cost;
	float pickupGoalWeight;
	int64_t selectedAt;
	int64_t timeoutAt;

	SelectedNavEntity( const NavEntity *navEntity_,
					   float cost_,
					   float pickupGoalWeight_,
					   int64_t timeoutAt_ )
		: navEntity( navEntity_ )
		, cost( cost_ )
		, pickupGoalWeight( pickupGoalWeight_ )
		, selectedAt( level.time )
		, timeoutAt( timeoutAt_ ) {}

	void CheckValid( const char *message = nullptr ) const {
		if( !IsValid() ) {
			if( message ) {
				AI_FailWith( "SelectedNavEntity::CheckValid()", "%s\n", message );
			} else {
				AI_FailWith( "SelectedNavEntity::CheckValid()", "A check has failed\n" );
			}
		}
	}

public:
	bool IsEmpty() const { return navEntity == nullptr; }
	// Empty one is considered valid (until it times out)
	bool IsValid() const { return timeoutAt > level.time; }

	void InvalidateNextFrame() {
		timeoutAt = level.time + 1;
	}

	// Avoid class/method name clash by using Get prefix
	const NavEntity *GetNavEntity() const {
		CheckValid();
		return navEntity;
	}

	float GetCost() const {
		CheckValid();
		return cost;
	}

	float PickupGoalWeight() const {
		CheckValid();
		return pickupGoalWeight;
	}
};

class BotItemsSelector {
	const Bot *const bot;

	int64_t disabledForSelectionUntil[MAX_EDICTS];

	float internalEntityWeights[MAX_EDICTS];
	float overriddenEntityWeights[MAX_EDICTS];

	// For each item contains a goal weight that would a corresponding AI pickup goal have.
	float internalPickupGoalWeights[MAX_EDICTS];

	float GetEntityWeight( int entNum ) const {
		float overriddenEntityWeight = overriddenEntityWeights[entNum];
		if( overriddenEntityWeight != 0 ) {
			return overriddenEntityWeight;
		}
		return internalEntityWeights[entNum];
	}

	float GetGoalWeight( int entNum ) const {
		float overriddenEntityWeight = overriddenEntityWeights[entNum];
		// Make goal weight based on overridden entity weight
		if( overriddenEntityWeight != 0 ) {
			// High weight items would have 2.0f goal weight
			return 2.0f * Q_Sqrt( std::max( overriddenEntityWeight, 10.0f ) * Q_Rcp( 10.0f ) );
		}
		return internalPickupGoalWeights[entNum];
	}

	void UpdateInternalItemAndGoalWeights();

	struct ItemAndGoalWeights {
		float itemWeight;
		float goalWeight;

		ItemAndGoalWeights( float itemWeight_, float goalWeight_ )
			: itemWeight( itemWeight_ ), goalWeight( goalWeight_ ) {}
	};

	ItemAndGoalWeights ComputeItemWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeWeaponWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeAmmoWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeArmorWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputeHealthWeights( const gsitem_t *item ) const;
	ItemAndGoalWeights ComputePowerupWeights( const gsitem_t *item ) const;

#ifndef _MSC_VER
	void Debug( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void Debug( _Printf_format_string_ const char *format, ... );
#endif

	SelectedNavEntity SelectEmpty() {
		return SelectedNavEntity( nullptr, std::numeric_limits<float>::max(), 0.0f, level.time + 200 );
	}

	SelectedNavEntity Select( const NavEntity *navEntity, float cost, unsigned timeout ) {
		return SelectedNavEntity( navEntity, cost, GetGoalWeight( navEntity->Id() ), level.time + timeout );
	}

	bool IsShortRangeReachable( const NavEntity *navEntity, const int *fromAreaNums, int numFromAreas ) const;
public:
	explicit BotItemsSelector( const Bot *bot_ ) : bot( bot_ ) {
		// We zero only this array as its content does not get cleared in SuggestGoalEntity() calls
		memset( disabledForSelectionUntil, 0, sizeof( disabledForSelectionUntil ) );
	}

	void ClearOverriddenEntityWeights() {
		memset( overriddenEntityWeights, 0, sizeof( overriddenEntityWeights ) );
	}

	// This weight overrides internal one computed by this brain itself.
	void OverrideEntityWeight( const edict_t *ent, float weight ) {
		overriddenEntityWeights[ENTNUM( const_cast<edict_t*>( ent ) )] = weight;
	}

	void MarkAsDisabled( const NavEntity &navEntity, unsigned millis ) {
		disabledForSelectionUntil[navEntity.Id()] = level.time + millis;
	}

	bool IsTopTierItem( const NavTarget *navTarget ) const {
		return navTarget && navTarget->IsTopTierItem( overriddenEntityWeights );
	}

	SelectedNavEntity SuggestGoalNavEntity( const SelectedNavEntity &currSelectedNavEntity );
};

#endif
