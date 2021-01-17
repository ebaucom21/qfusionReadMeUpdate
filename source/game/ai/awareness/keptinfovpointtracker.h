#ifndef WSW_373346d9_e782_47e7_bb1a_b44a9cc7ef75_H
#define WSW_373346d9_e782_47e7_bb1a_b44a9cc7ef75_H

#include "enemiestracker.h"
#include "../selection.h"

class Bot;
class BotAwarenessModule;
class SelectedEnemies;

/**
 * A helper class that encapsulates details of "kept in fov" point maintenance.
 * A "kept in fov point" is an origin a bot tries to keep looking at while moving.
 */
class KeptInFovPointTracker {
	struct LostOrHiddenEnemy: public Selection {
		const TrackedEnemy *enemy { nullptr };
		unsigned instanceId { 0 };
		static unsigned nextInstanceId;

		unsigned InstanceId() const override { return instanceId; }
		bool ValidAsSelection() const override { return enemy != nullptr; }

		void Activate( const TrackedEnemy *enemy_, unsigned instanceId_ ) {
			this->enemy = enemy_;
			this->instanceId = instanceId_;
		}

		void Deactivate() {
			this->enemy = nullptr;
			this->instanceId = 0;
		}
	};

	struct KeptPoint: public Selection {
		const Selection *activator { nullptr };
		Vec3 origin { 0, 0, 0 };
		unsigned instanceId { 0 };
		float viewDot { -1.0f };

		float ComputeViewDot( const vec3_t origin_, const Bot *bot );

		void Activate( const Vec3 &origin_, const Selection *activator_, const Bot *bot ) {
			Activate( origin_.Data(), activator_, bot );
		}

		void Activate( const vec3_t origin_, const Selection *activator_, const Bot *bot );

		void TryDeactivate( const Vec3 &actualOrigin, const Selection *activator_, Bot *bot ) {
			TryDeactivate( actualOrigin.Data(), activator_, bot );
		}

		void TryDeactivate( const vec3_t actualOrigin, const Selection *activator_, Bot *bot );

		void Update( const Vec3 &actualOrigin, const Selection *activator_, Bot *bot ) {
			Update( actualOrigin.Data(), activator_, bot );
		}

		void Update( const vec3_t actualOrigin, const Selection *activator_, Bot *bot );

		inline void Deactivate() { activator = nullptr; }
		inline bool IsActive() const { return activator != nullptr; }

		inline const Vec3 &Origin() const {
			assert( IsActive() );
			return origin;
		}

		bool ValidAsSelection() const override { return IsActive(); }
		unsigned InstanceId() const override { return instanceId; }
	};

	Bot *const bot;
	BotAwarenessModule *const awarenessModule;

	KeptPoint point;
	LostOrHiddenEnemy lostOrHiddenEnemy;

	void TryActivateWithEnemies( const SelectedEnemies &selectedEnemies );
	void TryActivateWithLostOrHiddenEnemy( const TrackedEnemy *enemy );
public:
	KeptInFovPointTracker( Bot *bot_, BotAwarenessModule *awarenessModule_ )
		: bot( bot_ ), awarenessModule( awarenessModule_ ) {}

	void Update();

	const float *GetActivePoint() const {
		return point.IsActive() ? point.Origin().Data() : nullptr;
	};
};

#endif
