#include "weaponselector.h"
#include "../bot.h"

void BotWeaponSelector::Frame( const WorldState &cachedWorldState ) {
	if( nextFastWeaponSwitchActionCheckAt > level.time ) {
		return;
	}

	if( !bot->selectedEnemies.AreValid() ) {
		return;
	}

	// cachedWorldState is cached for Think() period and might be out of sync with selectedEnemies
	if( cachedWorldState.EnemyOriginVar().Ignore() ) {
		return;
	}

	// Disallow "fast weapon switch actions" while a bot has quad.
	// The weapon balance and usage is completely different for a quad bearer.
	if( cachedWorldState.HasQuadVar() ) {
		return;
	}

	if( checkFastWeaponSwitchAction( cachedWorldState ) ) {
		nextFastWeaponSwitchActionCheckAt = level.time + 750;
	}
}

void BotWeaponSelector::Think( const WorldState &cachedWorldState ) {
	if( bot->weaponsUsageModule.GetSelectedWeapons().AreValid() ) {
		return;
	}

	if( !bot->selectedEnemies.AreValid() ) {
		return;
	}

	// cachedWorldState is cached for Think() period and might be out of sync with selectedEnemies
	if( cachedWorldState.EnemyOriginVar().Ignore() ) {
		return;
	}

	if( weaponChoiceRandomTimeoutAt <= level.time ) {
		weaponChoiceRandom = random();
		weaponChoiceRandomTimeoutAt = level.time + 2000;
	}

	selectWeapon( cachedWorldState );
}

bool BotWeaponSelector::checkFastWeaponSwitchAction( const WorldState &worldState ) {
	if( game.edicts[bot->EntNum()].r.client->ps.stats[STAT_WEAPON_TIME] >= 64 ) {
		return false;
	}

	// Easy bots do not perform fast weapon switch actions
	if( bot->Skill() < 0.33f ) {
		return false;
	}

	if( bot->Skill() < 0.66f ) {
		// Mid-skill bots do these actions in non-think frames occasionally
		if( bot->ShouldSkipThinkFrame() && random() > bot->Skill() ) {
			return false;
		}
	}

	if( worldState.DamageToKill() > 50 ) {
		return false;
	}

	if( auto maybeChosenWeapon = suggestFinishWeapon( worldState ) ) {
		setSelectedWeapons( WeaponsToSelect::bultinOnly( *maybeChosenWeapon ), 100u );
		return true;
	}

	return false;
}

// TODO: Lift this to Bot?
bool BotWeaponSelector::hasWeakOrStrong( int weapon ) const {
	assert( weapon >= WEAP_NONE && weapon < WEAP_TOTAL );
	const auto *inventory = bot->Inventory();
	if( !inventory[weapon] ) {
		return false;
	}
	const int weaponShift = weapon - WEAP_GUNBLADE;
	return ( inventory[AMMO_GUNBLADE + weaponShift] | inventory[AMMO_WEAK_GUNBLADE + weaponShift] ) != 0;
}

void BotWeaponSelector::selectWeapon( const WorldState &worldState ) {
	const auto timeout = weaponChoicePeriod;
	// Use instagib selection code for quad bearers as well
	// TODO: Select script weapon too
	if( GS_Instagib() || ( !worldState.HasQuadVar().Ignore() && worldState.HasQuadVar() ) ) {
		if( auto maybeBuiltinWeapon = suggestInstagibWeapon( worldState ) ) {
			setSelectedWeapons( WeaponsToSelect::bultinOnly( *maybeBuiltinWeapon ), timeout );
		}
		// TODO: Report failure/replan
		return;
	}

	std::optional<int> maybeBuiltinWeapon;
	if( worldState.EnemyIsOnSniperRange() ) {
		maybeBuiltinWeapon = suggestSniperRangeWeapon( worldState );
	} else if( worldState.EnemyIsOnFarRange() ) {
		maybeBuiltinWeapon = suggestFarRangeWeapon( worldState );
	} else if( worldState.EnemyIsOnMiddleRange() ) {
		maybeBuiltinWeapon = suggestMiddleRangeWeapon( worldState );
	} else {
		maybeBuiltinWeapon = suggestCloseRangeWeapon( worldState );
	}

	// TODO: Report failure/replan
	if( !maybeBuiltinWeapon ) {
		return;
	}

	const auto bultinNum = *maybeBuiltinWeapon;
	if( auto maybeScriptWeaponAndTier = suggestScriptWeapon( worldState ) ) {
		auto [scriptNum, tier] = *maybeScriptWeaponAndTier;
		if( tier >= BuiltinWeaponTier( *maybeBuiltinWeapon ) ) {
			setSelectedWeapons( WeaponsToSelect::builtinAndPrimaryScript( bultinNum, scriptNum ), timeout );
		} else {
			setSelectedWeapons( WeaponsToSelect::builtinAndSecondaryScript( bultinNum, scriptNum ), timeout );
		}
	} else {
		setSelectedWeapons( WeaponsToSelect::bultinOnly( bultinNum ), timeout );
	}
}

auto BotWeaponSelector::suggestFarOrSniperStaticCombatWeapon( const WorldState &ws, bool hasEB, bool hasMG )
	-> std::optional<int> {
	if( bot->WillAdvance() || bot->WillRetreat() ) {
		return std::nullopt;
	}

	const float dodgeThreshold = DEFAULT_DASHSPEED + 10.0f;
	if( bot->EntityPhysicsState()->Speed() > dodgeThreshold ) {
		return std::nullopt;
	}

	if( bot->selectedEnemies.ActualVelocity().SquaredLength() > dodgeThreshold * dodgeThreshold ) {
		return std::nullopt;
	}

	auto enemyWeapon = bot->selectedEnemies.PendingWeapon();
	// Try to counter enemy EB/MG by a complementary weapon
	if( enemyWeapon == WEAP_MACHINEGUN ) {
		if( hasEB ) {
			return WEAP_ELECTROBOLT;
		}
	} else if( enemyWeapon == WEAP_ELECTROBOLT ) {
		if( hasMG ) {
			return WEAP_MACHINEGUN;
		}
	}

	return std::nullopt;
}

static constexpr float kSwitchToMGForFinishingHP = 40.0f;

auto BotWeaponSelector::suggestSniperRangeWeapon( const WorldState &worldState ) -> std::optional<int> {
	const bool hasMG = hasWeakOrStrong( WEAP_MACHINEGUN );
	const float damageToKill = worldState.DamageToKill();
	if( damageToKill < kSwitchToMGForFinishingHP ) {
		if( hasMG ) {
			return WEAP_MACHINEGUN;
		}
	} else if( damageToKill < 0.5 * kSwitchToMGForFinishingHP ) {
		if( hasWeakOrStrong( WEAP_RIOTGUN ) ) {
			return WEAP_RIOTGUN;
		}
	}

	const bool hasEB = hasWeakOrStrong( WEAP_ELECTROBOLT );
	const bool hasIG = hasWeakOrStrong( WEAP_INSTAGUN );
	if( auto maybeWeapon = suggestFarOrSniperStaticCombatWeapon( worldState, hasEB, hasMG ) ) {
		return maybeWeapon;
	}

	if( hasIG ) {
		return WEAP_INSTAGUN;
	}
	if( hasEB ) {
		return WEAP_ELECTROBOLT;
	}
	if( hasMG ) {
		return WEAP_MACHINEGUN;
	}

	return std::nullopt;
}

auto BotWeaponSelector::suggestFarRangeWeapon( const WorldState &worldState ) -> std::optional<int> {
	const bool hasMG = hasWeakOrStrong( WEAP_MACHINEGUN );
	if( worldState.DamageToKill() < kSwitchToMGForFinishingHP ) {
		if( hasMG ) {
			return WEAP_MACHINEGUN;
		}
		if( hasWeakOrStrong( WEAP_RIOTGUN ) ) {
			return WEAP_RIOTGUN;
		}
	}

	const bool hasIG = hasWeakOrStrong( WEAP_INSTAGUN );
	const bool hasEB = hasWeakOrStrong( WEAP_ELECTROBOLT );
	if( auto maybeWeapon = suggestFarOrSniperStaticCombatWeapon( worldState, hasEB, hasMG ) ) {
		return maybeWeapon;
	}

	const bool hasPG = hasWeakOrStrong( WEAP_PLASMAGUN );
	const bool hasSW = hasWeakOrStrong( WEAP_SHOCKWAVE );
	if( bot->WillRetreat() ) {
		if( hasPG ) {
			return WEAP_PLASMAGUN;
		}
		if( hasSW ) {
			return WEAP_SHOCKWAVE;
		}
	}

	if( hasIG ) {
		return WEAP_INSTAGUN;
	}
	if( hasEB ) {
		return WEAP_ELECTROBOLT;
	}
	if( hasMG ) {
		return WEAP_MACHINEGUN;
	}

	if( bot->WillAdvance() ) {
		if( hasPG ) {
			return WEAP_PLASMAGUN;
		}
		if( hasSW ) {
			return WEAP_SHOCKWAVE;
		}
		if( hasWeakOrStrong( WEAP_ROCKETLAUNCHER ) ) {
			return WEAP_ROCKETLAUNCHER;
		}
	}

	// Using gunblade is fine in this case
	if( bot->WillRetreat() ) {
		const auto *inventory = bot->Inventory();
		return inventory[WEAP_GUNBLADE] && inventory[AMMO_GUNBLADE];
	}

	return std::nullopt;
}

auto BotWeaponSelector::suggestMiddleRangeWeapon( const WorldState &worldState ) -> std::optional<int> {
	const float distance = worldState.DistanceToEnemy();

	const auto *const lgDef = GS_GetWeaponDef( WEAP_LASERGUN );
	assert( lgDef->firedef.timeout == lgDef->firedef_weak.timeout );
	const auto lgRange = (float)lgDef->firedef.timeout;

	const bool hasLG = hasWeakOrStrong( WEAP_LASERGUN );
	if( hasLG && bot->IsInSquad() ) {
		return WEAP_LASERGUN;
	}

	const bool hasRL = hasWeakOrStrong( WEAP_ROCKETLAUNCHER );
	if( distance < lgRange / 2 || bot->WillAdvance() ) {
		if( hasRL ) {
			return WEAP_ROCKETLAUNCHER;
		}
	}

	if( distance > lgRange / 2 || bot->WillRetreat() ) {
		if( hasLG ) {
			return WEAP_LASERGUN;
		}
	}

	// Drop any randomness of choice in fighting against enemy LG
	if( weaponChoiceRandom > 0.5f || bot->selectedEnemies.PendingWeapon() == WEAP_LASERGUN ) {
		if ( hasLG ) {
			return WEAP_LASERGUN;
		}
		if ( hasRL ) {
			return WEAP_ROCKETLAUNCHER;
		}
	}

	const bool hasPG = hasWeakOrStrong( WEAP_PLASMAGUN );
	if( hasPG && bot->WillAdvance() ) {
		return WEAP_PLASMAGUN;
	}

	const bool hasSW = hasWeakOrStrong( WEAP_SHOCKWAVE );
	if( hasSW && bot->WillRetreat() ) {
		return WEAP_SHOCKWAVE;
	}

	if( hasLG ) {
		return WEAP_LASERGUN;
	}
	if( hasRL ) {
		return WEAP_ROCKETLAUNCHER;
	}
	if( hasSW ) {
		return WEAP_SHOCKWAVE;
	}
	if( hasPG ) {
		return WEAP_PLASMAGUN;
	}

	const bool hasMG = hasWeakOrStrong( WEAP_MACHINEGUN );
	if( hasMG && bot->WillRetreat() ) {
		return WEAP_MACHINEGUN;
	}
	const bool hasRG = hasWeakOrStrong( WEAP_RIOTGUN );
	if( hasRG && bot->WillAdvance() ) {
		return WEAP_RIOTGUN;
	}

	const bool hasIG = hasWeakOrStrong( WEAP_INSTAGUN );
	const bool hasEB = hasWeakOrStrong( WEAP_ELECTROBOLT );
	if( distance > lgRange / 2 && bot->WillRetreat() ) {
		if( hasIG ) {
			return WEAP_INSTAGUN;
		}
		if( hasEB ) {
			return WEAP_ELECTROBOLT;
		}
	}

	if( hasRG ) {
		return WEAP_RIOTGUN;
	}
	if( hasMG ) {
		return WEAP_MACHINEGUN;
	}
	if( hasIG ) {
		return WEAP_INSTAGUN;
	}
	if( hasEB ) {
		return WEAP_ELECTROBOLT;
	}

	if( distance > lgRange / 2 && bot->WillRetreat() ) {
		if( hasWeakOrStrong( WEAP_GRENADELAUNCHER ) ) {
			return WEAP_GRENADELAUNCHER;
		}
	}

	const auto *inventory = bot->Inventory();
	if( inventory[WEAP_GUNBLADE] && inventory[AMMO_GUNBLADE] ) {
		return WEAP_GUNBLADE;
	}

	return std::nullopt;
}

auto BotWeaponSelector::suggestCloseRangeWeapon( const WorldState &worldState ) -> std::optional<int> {
	const float damageToBeKilled = worldState.DamageToBeKilled();

	bool tryAvoidingSelfDamage = false;
	if( GS_SelfDamage() && worldState.DistanceToEnemy() < 150.0f ) {
		if( damageToBeKilled < 100 || !( level.gametype.spawnableItemsMask & IT_HEALTH ) ) {
			tryAvoidingSelfDamage = true;
		}
	}

	const bool hasLG = hasWeakOrStrong( WEAP_LASERGUN );
	const bool hasPG = hasWeakOrStrong( WEAP_PLASMAGUN );
	const bool hasRG = hasWeakOrStrong( WEAP_RIOTGUN );
	if( tryAvoidingSelfDamage ) {
		if( hasLG ) {
			return WEAP_LASERGUN;
		}
		if( hasPG && worldState.DistanceToEnemy() > 72.0f ) {
			return WEAP_PLASMAGUN;
		}
		if( hasRG ) {
			return WEAP_RIOTGUN;
		}
	}

	if( hasWeakOrStrong( WEAP_ROCKETLAUNCHER ) ) {
		return WEAP_ROCKETLAUNCHER;
	}
	if( hasWeakOrStrong( WEAP_SHOCKWAVE ) ) {
		return WEAP_SHOCKWAVE;
	}

	if( hasLG ) {
		return WEAP_LASERGUN;
	}
	if( hasPG ) {
		return WEAP_PLASMAGUN;
	}
	if( hasRG ) {
		return WEAP_RIOTGUN;
	}

	if( hasWeakOrStrong( WEAP_MACHINEGUN ) ) {
		return WEAP_MACHINEGUN;
	}

	const auto *inventory = bot->Inventory();
	const bool hasGB = inventory[WEAP_GUNBLADE] && inventory[AMMO_GUNBLADE];
	if( hasGB ) {
		if( !tryAvoidingSelfDamage ) {
			return WEAP_GUNBLADE;
		}
		if( worldState.DistanceToEnemy() > 72.0f ) {
			return WEAP_GUNBLADE;
		}
	}

	if( hasWeakOrStrong( WEAP_INSTAGUN ) ) {
		return WEAP_INSTAGUN;
	}
	if( hasWeakOrStrong( WEAP_ELECTROBOLT ) ) {
		return WEAP_ELECTROBOLT;
	}

	if( !tryAvoidingSelfDamage && hasWeakOrStrong( WEAP_GRENADELAUNCHER ) ) {
		return WEAP_GRENADELAUNCHER;
	}

	if( hasGB ) {
		return WEAP_GUNBLADE;
	}

	return std::nullopt;
}

auto BotWeaponSelector::suggestScriptWeapon( const WorldState &worldState )
	-> std::optional<std::pair<int, int>> {
	const auto &scriptWeaponDefs = bot->weaponsUsageModule.scriptWeaponDefs;
	const auto &scriptWeaponCooldown = bot->weaponsUsageModule.scriptWeaponCooldown;

	int effectiveTier = 0;
	float bestScore = 0.000001f;
	int bestWeaponNum = -1;
	const float distanceToEnemy = worldState.DistanceToEnemy();

	for( unsigned i = 0; i < scriptWeaponDefs.size(); ++i ) {
		const auto &weaponDef = scriptWeaponDefs[i];
		int cooldown = scriptWeaponCooldown[i];
		if( cooldown >= 1000 ) {
			continue;
		}

		if( distanceToEnemy > weaponDef.maxRange ) {
			continue;
		}
		if( distanceToEnemy < weaponDef.minRange ) {
			continue;
		}

		float score = 1.0f;

		score *= 1.0f - BoundedFraction( cooldown, 1000.0f );

		if( GS_SelfDamage() ) {
			float estimatedSelfDamage = 0.0f;
			estimatedSelfDamage = weaponDef.maxSelfDamage;
			estimatedSelfDamage *= ( 1.0f - BoundedFraction( worldState.DistanceToEnemy(), weaponDef.splashRadius ) );
			if( estimatedSelfDamage > 100.0f ) {
				continue;
			}
			if( worldState.DistanceToEnemy() < estimatedSelfDamage ) {
				continue;
			}
			score *= 1.0f - BoundedFraction( estimatedSelfDamage, 100.0f );
		}

		// We assume that maximum ordinary tier is 3
		score *= weaponDef.tier / 3.0f;

		// Treat points in +/- 192 units of best range as in best range too
		float bestRangeLowerBounds = weaponDef.bestRange - std::min( 192.0f, weaponDef.bestRange - weaponDef.minRange );
		float bestRangeUpperBounds = weaponDef.bestRange + std::min( 192.0f, weaponDef.maxRange - weaponDef.bestRange );

		if( distanceToEnemy < bestRangeLowerBounds ) {
			score *= distanceToEnemy / bestRangeLowerBounds;
		} else if( distanceToEnemy > bestRangeUpperBounds ) {
			score *= ( distanceToEnemy - bestRangeUpperBounds ) / ( weaponDef.maxRange - bestRangeLowerBounds );
		}

		if( score > bestScore ) {
			bestScore = score;
			bestWeaponNum = (int)i;
			effectiveTier = (int)( score * 3.0f + 0.99f );
		}
	}

	return bestWeaponNum < 0 ? std::nullopt : std::make_optional( std::make_pair( bestWeaponNum, effectiveTier ) );
}

auto BotWeaponSelector::suggestInstagibWeapon( const WorldState &worldState ) -> std::optional<int> {
	const bool hasMG = hasWeakOrStrong( WEAP_MACHINEGUN );
	const bool hasRG = hasWeakOrStrong( WEAP_RIOTGUN );
	const bool hasPG = hasWeakOrStrong( WEAP_PLASMAGUN );
	const bool hasSW = hasWeakOrStrong( WEAP_SHOCKWAVE );
	const bool hasIG = hasWeakOrStrong( WEAP_INSTAGUN );
	const bool hasEB = hasWeakOrStrong( WEAP_ELECTROBOLT );
	const auto *const inventory = bot->Inventory();
	if( worldState.EnemyIsOnFarRange() || worldState.EnemyIsOnSniperRange() ) {
		if( hasMG ) {
			return WEAP_MACHINEGUN;
		}
		if( hasRG ) {
			return WEAP_RIOTGUN;
		}

		if( worldState.EnemyIsOnFarRange() && bot->WillAdvance() ) {
			if( hasPG ) {
				return WEAP_PLASMAGUN;
			}
			if( inventory[WEAP_GUNBLADE] && inventory[AMMO_GUNBLADE] ) {
				return WEAP_GUNBLADE;
			}
			if( hasSW ) {
				return WEAP_SHOCKWAVE;
			}
		}

		if( hasIG ) {
			return WEAP_INSTAGUN;
		}
		if( hasEB ) {
			return WEAP_ELECTROBOLT;
		}

		return std::nullopt;
	}

	if( hasWeakOrStrong( WEAP_LASERGUN ) ) {
		return WEAP_LASERGUN;
	}
	if( hasPG ) {
		return WEAP_PLASMAGUN;
	}
	if( hasMG ) {
		return WEAP_MACHINEGUN;
	}
	if( hasRG ) {
		return WEAP_RIOTGUN;
	}

	const bool hasRL = hasWeakOrStrong( WEAP_ROCKETLAUNCHER );
	if( worldState.EnemyIsOnMiddleRange() || !GS_SelfDamage() ) {
		if ( hasSW ) {
			return WEAP_SHOCKWAVE;
		}
		if ( hasRL ) {
			return WEAP_ROCKETLAUNCHER;
		}
	}

	if( inventory[WEAP_GUNBLADE] && inventory[AMMO_GUNBLADE] ) {
		return WEAP_GUNBLADE;
	}

	if( hasIG ) {
		return WEAP_INSTAGUN;
	}
	if( hasEB ) {
		return WEAP_ELECTROBOLT;
	}

	return std::nullopt;
}

auto BotWeaponSelector::suggestFinishWeapon( const WorldState &worldState ) -> std::optional<int> {
	const float distance = worldState.DistanceToEnemy();
	const float damageToBeKilled = worldState.DamageToBeKilled();
	const float damageToKill = worldState.DamageToKill();
	const auto *const inventory = bot->Inventory();

	if( worldState.EnemyIsOnCloseRange() ) {
		if( inventory[WEAP_GUNBLADE] && inventory[AMMO_WEAK_GUNBLADE] ) {
			if( damageToBeKilled > 0 && distance > 1.0f && distance < 64.0f ) {
				Vec3 dirToEnemy( worldState.EnemyOriginVar().Value() );
				dirToEnemy *= Q_Rcp( distance );
				Vec3 lookDir( bot->EntityPhysicsState()->ForwardDir() );
				if ( lookDir.Dot( dirToEnemy ) > 0.7f ) {
					return WEAP_GUNBLADE;
				}
			}
		}

		if( damageToKill < 30 ) {
			if( hasWeakOrStrong( WEAP_LASERGUN ) ) {
				return WEAP_LASERGUN;
			}
		}

		if( inventory[WEAP_RIOTGUN] && inventory[AMMO_SHELLS] ) {
			return WEAP_RIOTGUN;
		}

		if( !GS_SelfDamage() ) {
			bool canRefillHealth = level.gametype.spawnableItemsMask & IT_HEALTH;
			if( canRefillHealth && ( damageToBeKilled > 150.0f || inventory[POWERUP_SHELL] ) ) {
				if ( hasWeakOrStrong( WEAP_ROCKETLAUNCHER ) ) {
					return WEAP_ROCKETLAUNCHER;
				}
			}
		}

		if( inventory[WEAP_RIOTGUN] && inventory[AMMO_WEAK_SHELLS] ) {
			return WEAP_RIOTGUN;
		}

		return std::nullopt;
	}

	if( worldState.EnemyIsOnMiddleRange() ) {
		if( damageToBeKilled > 75 ) {
			if( hasWeakOrStrong( WEAP_LASERGUN ) ) {
				return WEAP_LASERGUN;
			}
		}
		return std::nullopt;
	}

	if( damageToKill < 30 ) {
		if( hasWeakOrStrong( WEAP_RIOTGUN ) ) {
			return WEAP_RIOTGUN;
		}
	}

	return std::nullopt;
}
