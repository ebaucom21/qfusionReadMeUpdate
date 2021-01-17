#include "keptinfovpointtracker.h"
#include "../bot.h"

#include <typeinfo>

unsigned KeptInFovPointTracker::LostOrHiddenEnemy::nextInstanceId { 0 };

float KeptInFovPointTracker::KeptPoint::ComputeViewDot( const vec3_t origin_, const Bot *bot ) {
	Vec3 botToOrigin( origin_ );
	botToOrigin -= bot->Origin();
	botToOrigin.NormalizeFast();
	return botToOrigin.Dot( bot->EntityPhysicsState()->ForwardDir() );
}

void KeptInFovPointTracker::KeptPoint::Activate( const vec_t *origin_, const Selection *activator_, const Bot *bot ) {
	this->origin.Set( origin_ );
	assert( activator_->ValidAsSelection() );
	this->activator = activator_;
	this->instanceId = activator_->InstanceId();
	this->viewDot = ComputeViewDot( origin_, bot );
}

void KeptInFovPointTracker::KeptPoint::TryDeactivate( const vec3_t actualOrigin, const Selection *activator_, Bot *bot ) {
	if( !this->IsActive() ) {
		return;
	}

	if( this->instanceId != activator_->InstanceId() ) {
		Deactivate();
		return;
	}

	// The address of the activator should not have been changed.
	// Otherwise this object should be invalidated even if the activator
	// is structurally equal (a caller is doing something wrong).
	// Activators of different types should have different addresses as well.
	// Checking the activator types is important as instance ids might be same for different activators
	// and we won't invalidate this object as we should have been done in this case.
	if( activator_ != this->activator ) {
		assert( typeid( activator_ ) != typeid( this->activator ) );
		Deactivate();
		return;
	}

	if( this->origin.SquareDistanceTo( actualOrigin ) < 32 * 32 ) {
		Deactivate();
		return;
	}

	float actualDot = ComputeViewDot( actualOrigin, bot );
	// Do not deactivate if an origin has been changed but the view angles are approximately the same
	if( fabsf( viewDot - actualDot ) > 0.1f ) {
		Deactivate();
		return;
	}
}

void KeptInFovPointTracker::KeptPoint::Update( const vec3_t actualOrigin, const Selection *activator_, Bot *bot ) {
	TryDeactivate( actualOrigin, activator_, bot );

	if( !IsActive() ) {
		Activate( actualOrigin, activator_, bot );
	}
}

void KeptInFovPointTracker::Update() {
	const auto &botMiscTactics = bot->GetMiscTactics();
	if( botMiscTactics.shouldRushHeadless ) {
		point.Deactivate();
		lostOrHiddenEnemy.Deactivate();
		return;
	}

	const auto &selectedEnemies = bot->GetSelectedEnemies();
	if( selectedEnemies.AreValid() ) {
		TryActivateWithEnemies( selectedEnemies );
		// Deactivate tracked "lost or hidden" enemy
		// as we have obviously preferred visible ones.
		lostOrHiddenEnemy.Deactivate();
		return;
	}

	unsigned timeout = botMiscTactics.shouldKeepXhairOnEnemy ? 2000 : 1000;
	if( botMiscTactics.willRetreat ) {
		timeout = ( timeout * 3u ) / 2u;
	}

	if( const TrackedEnemy *enemy = awarenessModule->ChooseLostOrHiddenEnemy( timeout ) ) {
		TryActivateWithLostOrHiddenEnemy( enemy );
		return;
	}

	if( const auto *hurtEvent = awarenessModule->GetValidHurtEvent() ) {
		point.Activate( hurtEvent->possibleOrigin, hurtEvent, bot );
		return;
	}

	point.Deactivate();
}

void KeptInFovPointTracker::TryActivateWithEnemies( const SelectedEnemies &selectedEnemies ) {
	Vec3 origin( selectedEnemies.ClosestEnemyOrigin( bot->Origin() ) );
	if( !bot->GetMiscTactics().shouldKeepXhairOnEnemy ) {
		if( !selectedEnemies.HaveQuad() && !selectedEnemies.HaveCarrier() ) {
			float distanceThreshold = 768.0f + 1024.0f * selectedEnemies.MaxThreatFactor();
			distanceThreshold *= 0.5f + 0.5f * bot->GetEffectiveOffensiveness();
			if( origin.SquareDistanceTo( bot->Origin() ) > distanceThreshold * distanceThreshold ) {
				return;
			}
		}
	}

	point.Update( origin, &selectedEnemies, bot );

}

void KeptInFovPointTracker::TryActivateWithLostOrHiddenEnemy( const TrackedEnemy *enemy ) {
	Vec3 origin( enemy->LastSeenOrigin() );
	if( !bot->GetMiscTactics().shouldKeepXhairOnEnemy ) {
		float distanceThreshold = 384.0f;
		if( enemy->ent ) {
			// Compute a threat factor this "lost or hidden" enemy could have had
			// if this enemy was included in "selected enemies"
			distanceThreshold += 1024.0f * bot->GetSelectedEnemies().ComputeThreatFactor( enemy->ent );
		}
		distanceThreshold *= 0.5f + 0.5f * bot->GetEffectiveOffensiveness();
		if( origin.SquareDistanceTo( bot->Origin() ) > distanceThreshold * distanceThreshold ) {
			lostOrHiddenEnemy.Deactivate();
			return;
		}
	}

	if( !lostOrHiddenEnemy.ValidAsSelection() ) {
		lostOrHiddenEnemy.Activate( enemy, ++LostOrHiddenEnemy::nextInstanceId );
	}

	point.Update( origin, &lostOrHiddenEnemy, bot );
}