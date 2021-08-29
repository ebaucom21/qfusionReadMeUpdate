#ifndef WSW_fe1d72af_a22c_48bd_b858_ef25e129ccb1_H
#define WSW_fe1d72af_a22c_48bd_b858_ef25e129ccb1_H

#include "basemovementaction.h"

class LandOnSavedAreasAction : public BaseMovementAction {
	friend class HandleTriggeredJumppadAction;

	wsw::StaticVector<int, MAX_SAVED_LANDING_AREAS> savedLandingAreas;
	using FilteredAreas = wsw::StaticVector<AreaAndScore, MAX_SAVED_LANDING_AREAS * 2>;

	int currAreaIndex { 0 };
	unsigned totalTestedAreas { 0 };

	int FindJumppadAreaNum( const edict_t *jumppadEntity );

	// Returns a Z level when the landing is expected to be started
	float SaveJumppadLandingAreas( const edict_t *jumppadEntity );
	float SaveLandingAreasForJumppadTargetArea( const edict_t *jumppadEntity,
												int navTargetAreaNum,
												int jumppadTargetAreaNum );
	float SaveFilteredCandidateAreas( const edict_t *jumppadEntity,
									  int jumppadTargetAreaNum,
									  const FilteredAreas &filteredAreas );

public:
	explicit LandOnSavedAreasAction( BotMovementModule *module_ )
		: BaseMovementAction( module_, "LandOnSavedAreasAction", COLOR_RGB( 255, 0, 255 ) ) {}

	bool TryLandingStepOnArea( int areaNum, MovementPredictionContext *context );
	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void BeforePlanning() override;
	void AfterPlanning() override;
};

#endif
