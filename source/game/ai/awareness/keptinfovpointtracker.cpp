#include "keptinfovpointtracker.h"
#include "../bot.h"

void KeptInFovPointTracker::update() {
	m_point = selectCurrentPoint();
}

auto KeptInFovPointTracker::selectCurrentPoint() -> std::optional<Vec3> {
	const auto &botMiscTactics = m_bot->GetMiscTactics();
	if( !botMiscTactics.shouldRushHeadless ) {
		if( const auto &selectedEnemies = m_bot->GetSelectedEnemies(); selectedEnemies.AreValid() ) {
			if( auto maybePoint = selectPointBasedOnEnemies( selectedEnemies ) ) {
				return maybePoint;
			}
		}

		unsigned timeout = botMiscTactics.shouldKeepXhairOnEnemy ? 2000 : 1000;
		// Care longer if retreating
		if( botMiscTactics.willRetreat ) {
			timeout = 2 * timeout;
		}

		if( const TrackedEnemy *enemy = m_awarenessModule->ChooseLostOrHiddenEnemy( timeout ) ) {
			if( auto maybePoint = selectPointBasedOnLostOrHiddenEnemy( enemy ) ) {
				return maybePoint;
			}
		}

		if( const BotAwarenessModule::HurtEvent *hurtEvent = m_awarenessModule->GetValidHurtEvent() ) {
			return hurtEvent->possibleOrigin;
		}
	}

	return std::nullopt;
}

auto KeptInFovPointTracker::selectPointBasedOnEnemies( const SelectedEnemies &selectedEnemies ) -> std::optional<Vec3> {
	// TODO: Test against all enemies, not only the closest one
	const Vec3 enemyOrigin( selectedEnemies.ClosestEnemyOrigin( m_bot->Origin() ) );
	if( !m_bot->GetMiscTactics().shouldKeepXhairOnEnemy ) {
		if( !selectedEnemies.HaveQuad() && !selectedEnemies.HaveCarrier() ) {
			float distanceThreshold = 768.0f + 1024.0f * selectedEnemies.MaxThreatFactor();
			distanceThreshold *= 0.5f + 0.5f * m_bot->GetEffectiveOffensiveness();
			if( enemyOrigin.SquareDistanceTo( m_bot->Origin() ) > distanceThreshold * distanceThreshold ) {
				return std::nullopt;
			}
		}
	}

	return enemyOrigin;
}

auto KeptInFovPointTracker::selectPointBasedOnLostOrHiddenEnemy( const TrackedEnemy *enemy ) -> std::optional<Vec3> {
	Vec3 origin( enemy->LastSeenOrigin() );
	if( !m_bot->GetMiscTactics().shouldKeepXhairOnEnemy ) {
		float distanceThreshold = 384.0f;
		if( enemy->ent ) {
			// Compute a threat factor this "lost or hidden" enemy could have had
			// if this enemy was included in "selected enemies"
			distanceThreshold += 1024.0f * m_bot->GetSelectedEnemies().ComputeThreatFactor( enemy->ent );
		}
		distanceThreshold *= 0.5f + 0.5f * m_bot->GetEffectiveOffensiveness();
		if( origin.SquareDistanceTo( m_bot->Origin() ) > distanceThreshold * distanceThreshold ) {
			return std::nullopt;
		}
	}

	return origin;
}