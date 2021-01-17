#ifndef WSW_bb314e6a_1000_44f6_8404_85e663052b82_H
#define WSW_bb314e6a_1000_44f6_8404_85e663052b82_H

#include "weaponselector.h"
#include "../awareness/selectedenemies.h"

struct AimParams {
	vec3_t fireOrigin;
	vec3_t fireTarget;
	float suggestedCoordError;
};

class BotFireTargetCache {
	struct CachedFireTarget {
		Vec3 origin { 0, 0, 0 };
		unsigned selectedEnemiesInstanceId { 0 };
		unsigned selectedWeaponsInstanceId { 0 };
		int64_t invalidAt { 0 };

		bool IsValidFor( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons ) const {
			return selectedEnemies.InstanceId() == selectedEnemiesInstanceId &&
				   selectedWeapons.InstanceId() == selectedWeaponsInstanceId &&
				   invalidAt > level.time;
		}

		void CacheFor( const SelectedEnemies &selectedEnemies,
					   const SelectedWeapons &selectedWeapons,
					   const vec3_t origin_ ) {
			this->origin.Set( origin_ );
			selectedEnemiesInstanceId = selectedEnemies.InstanceId();
			selectedWeaponsInstanceId = selectedWeapons.InstanceId();
			invalidAt = level.time + 64;
		}
	};

	const Bot *const bot;

	CachedFireTarget cachedFireTarget;

	void SetupCoarseFireTarget( const SelectedEnemies &selectedEnemies,
								const GenericFireDef &fireDef,
								vec3_t fire_origin, vec3_t target );

	void AdjustPredictionExplosiveAimTypeParams( const SelectedEnemies &selectedEnemies,
												 const SelectedWeapons &selectedWeapons,
												 const GenericFireDef &fireDef,
												 AimParams *aimParams );

	void AdjustPredictionAimTypeParams( const SelectedEnemies &selectedEnemies,
										const SelectedWeapons &selectedWeapons,
										const GenericFireDef &fireDef,
										AimParams *aimParams );

	void AdjustDropAimTypeParams( const SelectedEnemies &selectedEnemies,
								  const SelectedWeapons &selectedWeapons,
								  const GenericFireDef &fireDef,
								  AimParams *aimParams );

	void AdjustInstantAimTypeParams( const SelectedEnemies &selectedEnemies,
									 const SelectedWeapons &selectedWeapons,
									 const GenericFireDef &fireDef,
									 AimParams *aimParams );

	void AdjustForShootableEnvironment( const SelectedEnemies &selectedEnemies, float splashRadius, AimParams *aimParams );

	void GetPredictedTargetOrigin( const SelectedEnemies &selectedEnemies,
								   const SelectedWeapons &selectedWeapons,
								   float projectileSpeed,
								   AimParams *aimParams );

	void PredictProjectileShot( const SelectedEnemies &selectedEnemies,
								float projectileSpeed,
								AimParams *aimParams,
								bool applyTargetGravity );

public:
	explicit BotFireTargetCache( const Bot *bot_ ): bot( bot_ ) {}

	void AdjustAimParams( const SelectedEnemies &selectedEnemies, const SelectedWeapons &selectedWeapons,
						  const GenericFireDef &fireDef, AimParams *aimParams );
};

#endif
