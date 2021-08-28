#include "baseteam.h"
#include "squadbasedteam.h"
#include "../bot.h"
#include "../../../qcommon/links.h"

AiBaseTeam *AiBaseTeam::teamsForNums[GS_MAX_TEAMS - 1];

AiBaseTeam::AiBaseTeam( int teamNum_ )
	: teamNum( teamNum_ ) {
	memset( botAffinityModulo, 0, sizeof( botAffinityModulo ) );
	memset( botAffinityOffsets, 0, sizeof( botAffinityOffsets ) );
}

void AiBaseTeam::Debug( const char *format, ... ) {
	// Cut it early to help optimizer to eliminate AI_Debugv call
#ifdef _DEBUG
	va_list va;
	va_start( va, format );
	AI_Debugv( GS_TeamName( teamNum ), format, va );
	va_end( va );
#endif
}

void AiBaseTeam::CheckTeamNum( int teamNum ) {
#ifndef PUBLIC_BUILD
	if( teamNum < TEAM_PLAYERS || teamNum >= GS_MAX_TEAMS ) {
		AI_FailWith( "AiBaseTeam", "GetTeamForNum(): Illegal team num %d\n", teamNum );
	}
#endif
}

AiBaseTeam **AiBaseTeam::TeamRefForNum( int teamNum ) {
	CheckTeamNum( teamNum );
	return &teamsForNums[teamNum - TEAM_PLAYERS];
}

AiBaseTeam *AiBaseTeam::GetTeamForNum( int teamNum ) {
	CheckTeamNum( teamNum );
	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	if( !*teamRef ) {
		AI_FailWith( "AiBaseTeam", "GetTeamForNum(): A team for num %d is not instantiated atm\n", teamNum );
	}
	return *teamRef;
}

AiBaseTeam *AiBaseTeam::ReplaceTeam( int teamNum, const std::type_info &desiredType ) {
	// Make sure this method is applied only for instantiation of descendants
	assert( typeid( AiBaseTeam ) != desiredType );

	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	if( !*teamRef ) {
		AI_FailWith( "AiBaseTeam::ReplaceTeam()", "A team for num %d has not been instantiated yet\n", teamNum );
	}

	// Destroy the existing AI team for the team slot
	AiBaseTeam *oldTeam = *teamRef;
	// Delegate further ops to the descendant factory
	*teamRef = AiSquadBasedTeam::InstantiateTeam( teamNum, desiredType );
	// Move an additional state (if any) from the old team
	( *teamRef )->TransferStateFrom( oldTeam );
	// Destroy the old team
	oldTeam->~AiBaseTeam();
	Q_free( oldTeam );

	return *teamRef;
}

unsigned AiBaseTeam::AffinityModulo() const {
	if( teamAffinityModulo == -1 ) {
		InitTeamAffinity();
	}
	return (unsigned)teamAffinityModulo;
}

unsigned AiBaseTeam::TeamAffinityOffset() const {
	if( teamAffinityOffset == -1 ) {
		InitTeamAffinity();
	}
	return (unsigned)teamAffinityOffset;
}

void AiBaseTeam::InitTeamAffinity() const {
	// We round frame time to integer milliseconds
	int frameTime = 1000 / ServerFps();
	// 4 for 60 fps or more, 1 for 16 fps or less
	teamAffinityModulo = std::min( 4, std::max( 1, 64 / frameTime ) );
	if( teamNum == TEAM_PLAYERS ) {
		teamAffinityOffset = 0;
		const_cast<AiBaseTeam*>( this )->SetFrameAffinity( teamAffinityModulo, teamAffinityOffset );
		return;
	}

	static_assert( TEAM_ALPHA == 2 && TEAM_BETA == 3, "Modify affinity offset computations" );
	switch( teamAffinityModulo ) {
		// The Alpha AI team thinks on frame 0, the Beta AI team thinks on frame 2
		case 4: teamAffinityOffset = ( teamNum - 2 ) * 2; break;
		// Both Alpha and Beta AI teams think on frame 0
		case 3: teamAffinityOffset = 0; break;
		// The Alpha AI team thinks on frame 0, the Beta AI team thinks on frame 1
		case 2: teamAffinityOffset = teamNum - 2; break;
		// All AI teams think in the same frame
		case 1: teamAffinityOffset = 0; break;
	}
	// Initialize superclass fields
	const_cast<AiBaseTeam*>( this )->SetFrameAffinity( teamAffinityModulo, teamAffinityOffset );
}

void AiBaseTeam::AddBot( Bot *bot ) {
	Debug( "new bot %s has been added\n", bot->Nick() );

	// Link first
	wsw::link( bot, &teamBotsHead, Bot::TEAM_LINKS );
	// Acquire affinity after linking
	AcquireBotFrameAffinity( ENTNUM( bot->self ) );
	// Call subtype method (if any) last
	OnBotAdded( bot );
}

void AiBaseTeam::RemoveBot( Bot *bot ) {
	Debug( "bot %s has been removed\n", bot->Nick() );

	// Call subtype method (if any) first
	OnBotRemoved( bot );
	// Release affinity before linking
	ReleaseBotFrameAffinity( ENTNUM( bot->self ) );
	// Unlink last
	wsw::unlink( bot, &teamBotsHead, Bot::TEAM_LINKS );
}

void AiBaseTeam::TransferStateFrom( AiBaseTeam *that ) {
	// Copy lazily-computed counterparts of frame affinity modulo and offset
	this->frameAffinityModulo = that->frameAffinityModulo;
	this->frameAffinityOffset = that->frameAffinityOffset;
	// Transfer bots list
	this->teamBotsHead = that->teamBotsHead;
	that->teamBotsHead = nullptr;
}

void AiBaseTeam::AcquireBotFrameAffinity( int entNum ) {
	// Always specify offset as zero for affinity module that = 1 (any % 1 == 0)
	if( AffinityModulo() == 1 ) {
		SetBotFrameAffinity( entNum, AffinityModulo(), 0 );
		return;
	}

	// Just distribute bot think frames evenly for non-team gametypes
	if( !GS_TeamBasedGametype() ) {
		unsigned chosenOffset = 0;
		for( unsigned i = 1; i < MAX_AFFINITY_OFFSET; ++i ) {
			if( affinityOffsetsInUse[chosenOffset] > affinityOffsetsInUse[i] ) {
				chosenOffset = i;
			}
		}
		affinityOffsetsInUse[chosenOffset]++;
		SetBotFrameAffinity( entNum, AffinityModulo(), chosenOffset );
		return;
	}

	const unsigned modulo = AffinityModulo();
	assert( modulo >= 1 && modulo <= 4 );

	// This is the value that gets actually used.
	// Older versions used to set think frames of every team bot one frame after the team thinks.
	// It was expected that the team logic is going to be quite computational expensive.
	// Actually it is not more expensive than logic of a single bot.
	// Lets distribute frames evenly (giving the team the same weight as for a bot).
	if( modulo == 4 ) {
		// 0 for ALPHA, 2 for BETA
		const auto teamOffset = 2 * (unsigned)( teamNum - TEAM_ALPHA );
		assert( teamOffset == 0 || teamOffset == 2 );
		unsigned chosenOffset = teamOffset;
		// If more bots think at teamOffset frames (counting the team as a "bot" too)
		if( affinityOffsetsInUse[teamOffset] + 1 > affinityOffsetsInUse[teamOffset + 1] ) {
			chosenOffset = teamOffset + 1;
		}
		affinityOffsetsInUse[chosenOffset]++;
		SetBotFrameAffinity( entNum, modulo, chosenOffset );
		return;
	}

	// These modulo values are not really used but let's keep this code for various reasons.
	// Just do not complicate things by trying distribute think frames evenly.

	// If the think cycle consist of 3 frames:
	if( modulo == 3 ) {
		// AI teams think on frame 0, bots of team Alpha think on frame 1, bots of team Beta think on frame 2
		SetBotFrameAffinity( entNum, modulo, 1 + (unsigned)teamNum - TEAM_ALPHA );
		return;
	}

	// If the think cycle consist of 2 frames:
	if( modulo == 2 ) {
		// the Alpha AI team and team Alpha bots think on frame 0,
		// the Beta AI team and team Beta bots think on frame 1
		SetBotFrameAffinity( entNum, modulo, (unsigned)teamNum - TEAM_ALPHA );
		return;
	}
}

void AiBaseTeam::ReleaseBotFrameAffinity( int entNum ) {
	unsigned offset = botAffinityOffsets[entNum];
	botAffinityOffsets[entNum] = 0;
	botAffinityModulo[entNum] = 0;
	affinityOffsetsInUse[offset]--;
}

void AiBaseTeam::SetBotFrameAffinity( int entNum, unsigned modulo, unsigned offset ) {
	botAffinityModulo[entNum] = (unsigned char)modulo;
	botAffinityOffsets[entNum] = (unsigned char)offset;
	game.edicts[entNum].bot->SetFrameAffinity( modulo, offset );
}

void AiBaseTeam::Init() {
#ifndef PUBLIC_BUILD
	for( int team = TEAM_PLAYERS; team < GS_MAX_TEAMS; ++team ) {
		if( *TeamRefForNum( team ) ) {
			AI_FailWith( "AiBaseTeam::Init()", "A team for num %d is already present / was not released", team );
		}
	}
#endif

	if( GS_TeamBasedGametype() ) {
		for( int team = TEAM_ALPHA; team < GS_MAX_TEAMS; ++team ) {
			CreateTeam( team );
		}
	} else {
		CreateTeam( TEAM_PLAYERS );
	}
}

void AiBaseTeam::Shutdown() {
	// Destroy all current teams (if any)
	for( int team = TEAM_PLAYERS; team < GS_MAX_TEAMS; ++team ) {
		ReleaseTeam( team );
	}
}

void AiBaseTeam::CreateTeam( int teamNum ) {
	AiBaseTeam **teamRef = TeamRefForNum( teamNum );
	// If there was an existing
	if( *teamRef ) {
		ReleaseTeam( teamNum );
	}
	// Set team pointer
	*teamRef = InstantiateTeam( teamNum );
	// TODO: Should we nofify bots? They should not use a cached team reference and always use GetTeamForNum()
}

void AiBaseTeam::ReleaseTeam( int teamNum ) {
	// Get the static cell that maybe holds the address of the team
	AiBaseTeam **teamToRef = TeamRefForNum( teamNum );
	// If there is no team address in this memory cell
	if( !*teamToRef ) {
		return;
	}

	// Destruct the team
	( *teamToRef )->~AiBaseTeam();
	// Free team memory
	Q_free( *teamToRef );
	// Nullify the static memory cell holding no longer valid address
	*teamToRef = nullptr;
}

AiBaseTeam *AiBaseTeam::InstantiateTeam( int teamNum ) {
	// Delegate construction to AiSquadBasedTeam
	if( GS_TeamBasedGametype() && !GS_IndividualGameType() ) {
		return AiSquadBasedTeam::InstantiateTeam( teamNum );
	}

	void *mem = Q_malloc( sizeof( AiBaseTeam ) );
	return new(mem)AiBaseTeam( teamNum );
}
