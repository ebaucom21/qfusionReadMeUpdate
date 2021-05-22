#include "combatdodgetotargetaction.h"
#include "movementlocal.h"

void CombatDodgeSemiRandomlyToTargetAction::UpdateKeyMoveDirs( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *combatMovementState = &context->movementState->keyMoveDirsState;
	Assert( !combatMovementState->IsActive() );

	vec3_t closestFloorPoint;
	std::optional<Vec3> maybeTarget;
	const float *__restrict botOrigin = entityPhysicsState.Origin();
	if( context->sameFloorClusterAreasCache.GetClosestToTargetPoint( context, closestFloorPoint ) ) {
		if( Distance2DSquared( botOrigin, closestFloorPoint ) > SQUARE( 1.0f ) ) {
			maybeTarget = Vec3( closestFloorPoint );
		}
	} else if( const int nextReachNum = context->NextReachNum() ) {
		const auto &__restrict nextReach = AiAasWorld::Instance()->Reachabilities()[nextReachNum];
		// This check is not just a normalization check but also is a logical one (switch to end if close to start)
		if( Distance2DSquared( botOrigin, nextReach.start ) > SQUARE( 16.0f ) ) {
			maybeTarget = Vec3( nextReach.start );
		} else if( Distance2DSquared( botOrigin, nextReach.end ) > SQUARE( 1.0f ) ) {
			maybeTarget = Vec3( nextReach.end );
		}
	} else if( context->NavTargetAasAreaNum() ) {
		Vec3 navTargetOrigin( context->NavTargetOrigin() );
		if( navTargetOrigin.SquareDistance2DTo( botOrigin ) > SQUARE( 1.0f ) ) {
			maybeTarget = navTargetOrigin;
		}
	}

	struct DirAndScore {
		unsigned dir;
		float score;
		[[nodiscard]]
		bool operator<( const DirAndScore &that ) const {
			// Give a preference to dirs with larger scores
			return score > that.score;
		}
	};

	DirAndScore dirsAndScores[std::size( kSideDirFractions )];
	assert( std::size( dirsAndScores ) == std::size( dirIndices ) );

	bool hasDefinedMoveDir = false;
	// Slightly randomize (use fully random dirs in a half of cases).
	// This remains biased towards the target as random dirs in the
	// another half of cases still could conform to the desired direction.
	if( maybeTarget && ( random() > 0.5f ) ) {
		Vec3 desiredMoveDir( Vec3( *maybeTarget ) - botOrigin );
		desiredMoveDir.Z() *= Z_NO_BEND_SCALE;
		const float desiredDirSquareLen = desiredMoveDir.SquaredLength();
		if( desiredDirSquareLen > SQUARE( 12.0f ) ) {
			const Vec3 forwardDir( entityPhysicsState.ForwardDir() ), rightDir( entityPhysicsState.RightDir() );
			hasDefinedMoveDir = true;
			desiredMoveDir *= Q_RSqrt( desiredDirSquareLen );
			Assert( std::fabs( desiredMoveDir.LengthFast() - 1.0f ) < 0.01f );
			for( unsigned i = 0; i < std::size( kSideDirFractions ); ++i ) {
				const float *const fractions = kSideDirFractions[i];
				const Vec3 forwardPart( forwardDir * fractions[0] );
				const Vec3 rightPart( rightDir * fractions[1] );
				Vec3 moveDir( forwardPart + rightPart );
				const float moveDirSquareLen = moveDir.SquaredLength();
				moveDir *= Q_RSqrt( moveDirSquareLen );
				Assert( std::fabs( moveDir.LengthFast() - 1.0f ) < 0.01f );
				dirsAndScores[i] = { i, desiredMoveDir.Dot( moveDir ) };
			}
			std::sort( std::begin( dirsAndScores ), std::end( dirsAndScores ) );
			for( unsigned i = 0; i < std::size( dirsAndScores ); ++i ) {
				this->dirIndices[i] = dirsAndScores[i].dir;
			}
		}
	}
	if( !hasDefinedMoveDir ) {
		// Poor man's shuffle (we don't have an STL-compatible RNG).
		// This is acceptable to randomize directions between frames.
		const auto randomOffset = (unsigned)( level.framenum );
		for( unsigned i = 0; i < std::size( dirsAndScores ); ++i ) {
			// TODO: Fix the RNG interface/the RNG in general, use a proper shuffle.
			this->dirIndices[( i + randomOffset ) % std::size( this->dirIndices )] = i;
		}
	}

	const auto *directions = kSideDirSigns[this->dirIndices[0]];
	combatMovementState->Activate( directions[0], directions[1], dirsTimeout );
}

void CombatDodgeSemiRandomlyToTargetAction::PlanPredictionStep( Context *context ) {
	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->canOverrideLookVec = true;
	botInput->shouldOverrideLookVec = true;
	botInput->isUcmdSet = true;

	if( attemptNum == maxAttempts ) {
		if( IsAllowedToFail() ) {
			Debug( "All attempts have failed. Switching to the fallback/dummy action\n" );
			Assert( this->allowFailureUsingThatAsNextAction );
			Assert( this->allowFailureUsingThatAsNextAction != this );
			this->DisableWithAlternative( context, this->allowFailureUsingThatAsNextAction );
			return;
		}
		Debug( "Attempts count has reached its limit. Should stop planning\n" );
		// There is no fallback action since this action is a default one for combat state.
		botInput->SetForwardMovement( 0 );
		botInput->SetRightMovement( 0 );
		botInput->SetUpMovement( bot->IsCombatCrouchingAllowed() ? -1 : +1 );
		context->isCompleted = true;
	}

	// If there are "selected enemies", this look dir will be overridden
	// using more appropriate value by aiming subsystem
	// but still has to be provided for movement prediction.
	// Otherwise the bot will be looking at "kept in fov" point.
	botInput->SetIntendedLookDir( lookDir, true );

	const short *pmStats = context->currPlayerState->pmove.stats;
	if( entityPhysicsState.GroundEntity() ) {
		if( isCombatDashingAllowed && !pmStats[PM_STAT_DASHTIME] && ( pmStats[PM_STAT_FEATURES] & PMFEAT_DASH ) ) {
			const float speedThreshold = context->GetDashSpeed() - 10;
			if( entityPhysicsState.Speed() < speedThreshold ) {
				botInput->SetSpecialButton( true );
				context->predictionStepMillis = context->DefaultFrameTime();
			}
		}
		auto *const combatMovementState = &context->movementState->keyMoveDirsState;
		botInput->SetForwardMovement( combatMovementState->ForwardMove() );
		botInput->SetRightMovement( combatMovementState->RightMove() );
		// Set at least a single key or button while on ground (forward/right move keys might both be zero)
		if( !botInput->ForwardMovement() && !botInput->RightMovement() && !botInput->UpMovement() ) {
			if( !botInput->IsSpecialButtonSet() ) {
				botInput->SetUpMovement( isCompatCrouchingAllowed ? -1 : +1 );
			}
		}
	} else {
		if( ( pmStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) && !pmStats[PM_STAT_WJTIME] && !pmStats[PM_STAT_STUN] ) {
			botInput->SetSpecialButton( true );
			context->predictionStepMillis = context->DefaultFrameTime();
		}

		const float skill = bot->Skill();
		Assert( skill >= 0.0f && skill <= 1.0f );
		const float runSpeed = context->GetRunSpeed();
		if( !botInput->IsSpecialButtonSet() && ( entityPhysicsState.Speed2D() < runSpeed * ( 1.0f + skill ) ) ) {
			const float accelFrac = skill;
			context->CheatingAccelerate( accelFrac );
		}
	}
}

void CombatDodgeSemiRandomlyToTargetAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	if( !this->bestTravelTimeSoFar ) {
		if( IsAllowedToFail() ) {
			Debug( "The initial travel time was undefined\n" );
			context->SetPendingRollback();
			return;
		}
	}

	const int newTravelTimeToTarget = context->TravelTimeToNavTarget();
	// If there is no definite current travel time to target
	if( !newTravelTimeToTarget ) {
		// If there was a definite initial/best travel time to target
		if( this->bestTravelTimeSoFar ) {
			Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}
	}

	const int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( newTravelTimeToTarget <= this->bestTravelTimeSoFar ) {
		this->bestTravelTimeSoFar = newTravelTimeToTarget;
		this->bestFloorClusterSoFar = AiAasWorld::Instance()->FloorClusterNum( currGroundedAreaNum );
	} else {
		// If this flag is set, rollback immediately.
		// We need to be sure the action leads to advancing to the nav target.
		// Otherwise a reliable fallback action should be used.
		if( IsAllowedToFail() ) {
			Debug( "A prediction step has lead to an increased travel time to the nav target\n" );
			context->SetPendingRollback();
			return;
		}

		if( newTravelTimeToTarget > this->bestTravelTimeSoFar + 50 ) {
			bool rollback = true;
			// If we're still in the best floor cluster, use more lenient increased travel time threshold
			if( AiAasWorld::Instance()->FloorClusterNum( currGroundedAreaNum ) == bestFloorClusterSoFar ) {
				if( newTravelTimeToTarget < this->bestTravelTimeSoFar + 100 ) {
					rollback = false;
				}
			}
			if( rollback ) {
				Debug( "A prediction step has lead to an increased travel time to the nav target\n" );
				context->SetPendingRollback();
				return;
			}
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Check for prediction termination...
	if( !entityPhysicsState.GroundEntity() || this->SequenceDuration( context ) < dirsTimeout ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	float minDistance = 16.0f;
	if( IsAllowedToFail() ) {
		// Using "combat dodging" over fallback movement is unjustified if the resulting speed is this low
		if( entityPhysicsState.Speed2D() < context->GetRunSpeed() ) {
			Debug( "The 2D speed is way too low and does not justify using this combat action over fallback one\n" );
			context->SetPendingRollback();
			return;
		}
		minDistance = 24.0f;
	}

	// Check for blocking
	if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin() ) < SQUARE( minDistance ) ) {
		Debug( "The total covered distance since the sequence start is too low\n" );
		context->SetPendingRollback();
		return;
	}

	context->isCompleted = true;
}

void CombatDodgeSemiRandomlyToTargetAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );

	this->bestTravelTimeSoFar = context->TravelTimeToNavTarget();
	this->bestFloorClusterSoFar = 0;
	if( int clusterNum = AiAasWorld::Instance()->FloorClusterNum( context->CurrGroundedAasAreaNum() ) ) {
		this->bestFloorClusterSoFar = clusterNum;
	}

	auto *const moveDirsState = &context->movementState->keyMoveDirsState;

	// The look dir gets reset in OnBeforePlanning() only once during planning.
	// We obviously should use the same look dir for every attempt
	// (for every action application sequence).
	// Only pressed buttons and their randomness vary during attempts.
	if( !lookDir ) {
		Assert( !context->topOfStackIndex );
		// There are currently 3 call sites where this movement action gets activated.
		// 1) MovementPredictionContext::SuggestAnyAction()
		// The action gets selected if there are valid "selected enemies"
		// and if the bot should attack and should keep crosshair on enemies.
		// 2) MovementPredictionContext::SuggestAnyAction()
		// If the previous condition does not hold but there is a valid "kept in fov point"
		// and the bot has a nav target and should not "rush headless"
		// (so a combat semi-random dodging keeping the "point" in fov
		// usually to be ready to fire is used for movement to nav target)
		// 3) WalkCarefullyAction::PlanPredictionStep()
		// That action checks whether a bot should "walk carefully"
		// and usually switches to a first bunnying action of proposed bunnying actions
		// if conditions of "walking carefully" action are not met.
		// But if the bot logic reports the bot should skip bunnying and favor combat movement
		// (e.g. to do an urgent dodge) this combat movement action gets activated.
		// There might be no predefined look dir in this case and thus we should keep existing look dir.

		// We try to select a look dir if it is available according to situation priority
		bool hasDefinedLookDir = false;
		if( bot->ShouldKeepXhairOnEnemy() && bot->GetSelectedEnemies().AreValid() ) {
			bot->GetSelectedEnemies().LastSeenOrigin().CopyTo( tmpDir );
			hasDefinedLookDir = true;
		} else if( const float *keptInFovPoint = bot->GetKeptInFovPoint() ) {
			VectorCopy( keptInFovPoint, tmpDir );
			hasDefinedLookDir = true;
		}

		const auto &entityPhysicsState = context->movementState->entityPhysicsState;
		if( hasDefinedLookDir ) {
			VectorSubtract( tmpDir, entityPhysicsState.Origin(), tmpDir );
			VectorNormalize( tmpDir );
		} else {
			// Just keep existing look dir
			entityPhysicsState.ForwardDir().CopyTo( tmpDir );
		}
		lookDir = tmpDir;

		// Check whether directions have timed out only once per planning frame
		if( !moveDirsState->IsActive() ) {
			UpdateKeyMoveDirs( context );
			hasUpdatedMoveDirsAtFirstAttempt = true;
		}
	}

	// We have failed the first attempt using the kept dirs.
	// Deactivate the dirs state and try all new dirs.
	if( attemptNum == 1 && !hasUpdatedMoveDirsAtFirstAttempt ) {
		Assert( moveDirsState->IsActive() );
		moveDirsState->Deactivate();
		UpdateKeyMoveDirs( context );
		// Consider the first attempt wasted
		maxAttempts += 1;
	}

	unsigned dirIndex = ~0u;
	if( hasUpdatedMoveDirsAtFirstAttempt ) {
		if( attemptNum != maxAttempts ) {
			dirIndex = dirIndices[attemptNum];
		}
	} else {
		if( attemptNum && attemptNum != maxAttempts ) {
			dirIndex = dirIndices[attemptNum - 1];
		}
	}

	if( dirIndex < std::size( kSideDirSigns ) ) {
		const auto *signs = kSideDirSigns[dirIndex];
		moveDirsState->forwardMove = (int8_t)signs[0];
		moveDirsState->rightMove = (int8_t)signs[1];
	}
}

void CombatDodgeSemiRandomlyToTargetAction::OnApplicationSequenceStopped( Context *context,
																		  SequenceStopReason stopReason,
																		  unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED ) {
		attemptNum = 0;
		return;
	}

	attemptNum++;
	Assert( attemptNum <= maxAttempts );
	// Could've been set on prediction stack overflow
	this->isDisabledForPlanning = false;
}

void CombatDodgeSemiRandomlyToTargetAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();

	const float skill = bot->Skill();

	this->lookDir = nullptr;
	this->attemptNum = 0;
	this->hasUpdatedMoveDirsAtFirstAttempt = false;
	this->isCombatDashingAllowed = ( skill >= 0.33f ) && bot->IsCombatDashingAllowed();
	this->isCompatCrouchingAllowed = bot->IsCombatCrouchingAllowed();
	this->maxAttempts = std::size( kSideDirSigns );
	this->allowFailureUsingThatAsNextAction = nullptr;

	constexpr float limit = 0.66f;
	this->dirsTimeout = 400u;
	// Make easy bots change directions slower
	if( skill < limit ) {
		const float frac = skill * ( 1.0f / limit );
		assert( frac >= 0.0f && frac <= 1.0f );
		this->dirsTimeout += (unsigned)( 300.0f * ( 1.0f - frac ) );
	}
}