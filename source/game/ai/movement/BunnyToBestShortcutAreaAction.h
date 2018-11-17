#ifndef QFUSION_BUNNYTOBESTSHORTCUTAREAACTION_H
#define QFUSION_BUNNYTOBESTSHORTCUTAREAACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyToBestShortcutAreaAction final : public BunnyTestingSavedLookDirsAction {
	using Super = BunnyTestingSavedLookDirsAction;

	friend class BunnyStraighteningReachChainAction;
	static constexpr const char *NAME = "BunnyToBestShortcutAreaAction";
	static constexpr int MAX_BBOX_AREAS = 32;

	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;

	inline int FindBBoxAreas( MovementPredictionContext *context, int *areaNums, int maxAreas );

	// Returns candidates end iterator
	AreaAndScore *SelectCandidateAreas( MovementPredictionContext *context,
										AreaAndScore *candidatesBegin,
										int startTravelTime );
public:
	explicit BunnyToBestShortcutAreaAction( BotMovementModule *module_ );

	void BeforePlanning() override {
		Super::BeforePlanning();
		// Reset to the action default value every frame
		maxSuggestedLookDirs = 2;
	}
};

#endif
