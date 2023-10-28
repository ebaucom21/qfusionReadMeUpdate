#ifndef WSW_1468775f_9a19_4e35_9daf_d703cb9288ef_H
#define WSW_1468775f_9a19_4e35_9daf_d703cb9288ef_H

#include "../qcommon/wswstringview.h"

struct BasicObjectiveIndicatorState {
public:
	uint8_t color[3] { 0, 0, 0 };
	uint8_t anim { 0 };
	int8_t progress { 0 };
	uint8_t iconNum { 0 };
	uint8_t stringNum { 0 };
	bool enabled { false };

	[[nodiscard]]
	bool operator!=( const BasicObjectiveIndicatorState &that ) const {
		return color[0] == that.color[0] && color[1] == that.color[1] && color[2] == that.color[2] &&
			anim == that.anim && progress == that.progress && iconNum == that.iconNum &&
			stringNum == that.stringNum && enabled == that.enabled;
	}
};

BasicObjectiveIndicatorState CG_HudIndicatorState( int );

int CG_RealClientTeam();
bool CG_HasTwoTeams();
int CG_ActiveWeapon();
bool CG_HasWeapon( int weapon );
int CG_Health();
int CG_Armor();
int CG_TeamAlphaDisplayedColor();
int CG_TeamBetaDisplayedColor();
int CG_TeamToForcedTeam( int team );
bool CG_IsOperator();
bool CG_IsChallenger();
bool CG_CanBeReady();
bool CG_IsReady();
int CG_MyRealTeam();

std::optional<wsw::StringView> CG_HudIndicatorIconPath( int );
std::optional<wsw::StringView> CG_HudIndicatorStatusString( int );
auto CG_GetMatchClockTime() -> std::pair<int, int>;
auto CG_WeaponAmmo( int weapon ) -> std::pair<int, int>;
std::optional<unsigned> CG_ActiveChasePov();
bool CG_IsPovAlive();
wsw::StringView CG_PlayerName( unsigned playerNum );
wsw::StringView CG_PlayerClan( unsigned playerClan );
wsw::StringView CG_LocationName( unsigned location );

extern struct cvar_s *cg_showChasers;

#endif