#ifndef WSW_bbfd3c31_cdfd_4dd8_882f_b8854c95351e_H
#define WSW_bbfd3c31_cdfd_4dd8_882f_b8854c95351e_H

#include "baseaction.h"

class RidePlatformAction : public BaseAction {
	friend class MovementSubsystem;

public:
	explicit RidePlatformAction( MovementSubsystem *subsystem ) :
		BaseAction( subsystem, "RidePlatformAction", COLOR_RGB( 128, 128, 0 ) ) {}
	void PlanPredictionStep( PredictionContext *context ) override;
	void CheckPredictionStepResults( PredictionContext *context ) override;

	void BeforePlanning() override {
		BaseAction::BeforePlanning();
		currTestedAreaIndex = 0;
	}

	void OnApplicationSequenceStopped( PredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;

	static constexpr auto MAX_SAVED_AREAS = PredictionContext::MAX_SAVED_LANDING_AREAS;
	using ExitAreasVector = wsw::StaticVector<int, MAX_SAVED_AREAS>;

private:
	ExitAreasVector tmpExitAreas;
	unsigned currTestedAreaIndex { 0 };

	const edict_t *GetPlatform( PredictionContext *context ) const;
	// A context might be null!
	void TrySaveExitAreas( PredictionContext *context, const edict_t *platform );
	const ExitAreasVector &SuggestExitAreas( PredictionContext *context, const edict_t *platform );
	void FindExitAreas( PredictionContext *context, const edict_t *platform, ExitAreasVector &exitAreas );

	void SetupIdleRidingPlatformMovement( PredictionContext *context, const edict_t *platform );
	void SetupExitPlatformMovement( PredictionContext *context, const edict_t *platform );
};

#endif
