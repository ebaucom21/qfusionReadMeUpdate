#ifndef WSW_16e4512a_b837_4c46_9141_bead40481a00_H
#define WSW_16e4512a_b837_4c46_9141_bead40481a00_H

#include "../ailocal.h"

class Bot;
class BotMovementModule;
class MovementPredictionContext;

class MovementScript {
public:
	enum Status {
		COMPLETED,
		PENDING,
		INVALID
	};

protected:
	const Bot *const bot;
	BotMovementModule *const module;
	int64_t activatedAt;
	Status status;
	int debugColor;

	void Activate() {
		activatedAt = level.time;
		status = PENDING;
	}

	// A convenient shorthand for returning from TryDeactivate()
	bool DeactivateWithStatus( Status status_ ) {
		assert( status_ != PENDING );
		this->status = status_;
		return true;
	}
public:
	MovementScript( const Bot *bot_, BotMovementModule *module_, int debugColor_ )
		: bot( bot_ ), module( module_ ), activatedAt( 0 ), status( COMPLETED ), debugColor( debugColor_ ) {}

	bool IsActive() const { return status == PENDING; }

	int DebugColor()  const { return debugColor; }

	virtual ~MovementScript() = default;

	virtual bool TryDeactivate( MovementPredictionContext *context = nullptr ) = 0;

	virtual void SetupMovement( MovementPredictionContext *context ) = 0;
};

#endif
