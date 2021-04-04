/*
Copyright (C) 2006 Pekka Lampila ("Medar"), Damien Deville ("Pb")
and German Garcia Fernandez ("Jal") for Chasseur de bots association.


This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "cg_local.h"
#include "../client/client.h"
#include "../ref/frontend.h"
#include "../ui/uisystem.h"

static cvar_t *cg_showHUD;
static cvar_t *cg_specHUD;
static cvar_t *cg_clientHUD;

cvar_t *cg_showminimap;
cvar_t *cg_showitemtimers;

void CG_SC_ResetObituaries() {}
void CG_SC_Obituary() {}
void CG_UpdateHUDPostDraw() {}
void CG_ClearHUDInputState() {}
void CG_ClearAwards() {}

void CG_InitHUD() {
	cg_showHUD =        Cvar_Get( "cg_showHUD", "1", CVAR_ARCHIVE );
	cg_clientHUD =      Cvar_Get( "cg_clientHUD", "", CVAR_ARCHIVE );
	cg_specHUD =        Cvar_Get( "cg_specHUD", "", CVAR_ARCHIVE );
}

void CG_ShutdownHUD() {
}

/*
* CG_DrawHUD
*/
void CG_DrawHUD() {
	if( !cg_showHUD->integer ) {
		return;
	}

	// if changed from or to spec, reload the HUD
	if( cg.specStateChanged ) {
		cg_specHUD->modified = cg_clientHUD->modified = true;
		cg.specStateChanged = false;
	}

	cvar_t *hud = ISREALSPECTATOR() ? cg_specHUD : cg_clientHUD;
	if( hud->modified ) {
		// TODO...
		hud->modified = false;
	}

	// TODO: Does it work for chasers?
	if( cg.predictedPlayerState.pmove.pm_type == PM_NORMAL ) {
		if( !wsw::ui::UISystem::instance()->isShown() ) {
			CG_DrawCrosshair();
		}
	}
}

bool CG_IsSpectator() {
	return ISREALSPECTATOR();
}

bool CG_HasActiveChasePov() {
	if( !ISREALSPECTATOR() ) {
		return true;
	}
	// TODO: Is this a correct condition for that?
	return cg.predictedPlayerState.POVnum >= 0 && cg.predictedPlayerState.POVnum != cg.predictedPlayerState.playerNum + 1;
}

bool CG_HasTwoTeams() {
	return GS_TeamBasedGametype() && !GS_InvidualGameType();
}

int CG_ActiveWeapon() {
	return cg.predictedPlayerState.stats[STAT_WEAPON];
}

bool CG_HasWeapon( int weapon ) {
	assert( (unsigned)weapon < (unsigned)WEAP_TOTAL );
	return cg.predictedPlayerState.inventory[weapon];
}

int CG_Health() {
	return cg.predictedPlayerState.stats[STAT_HEALTH];
}

int CG_Armor() {
	return cg.predictedPlayerState.stats[STAT_ARMOR];
}

int CG_TeamAlphaColor() {
	return COM_ReadColorRGBString( cg_teamALPHAcolor ? cg_teamALPHAcolor->string : "" );
}

int CG_TeamBetaColor() {
	return COM_ReadColorRGBString( cg_teamBETAcolor ? cg_teamBETAcolor->string : "" );
}

std::pair<int, int> CG_WeaponAmmo( int weapon ) {
	const auto *weaponDef = GS_GetWeaponDef( weapon );
	const int *inventory = cg.predictedPlayerState.inventory;
	return { inventory[weaponDef->firedef_weak.ammo_id], inventory[weaponDef->firedef.ammo_id] };
}

extern cvar_t *cg_showTimer;

auto CG_GetMatchClockTime() -> std::pair<int, int> {
	int64_t clocktime, startTime, duration, curtime;
	double seconds;
	int minutes;

	if( !cg_showTimer ) {
		return { 0, 0 };
	}

	if( !cg_showTimer->integer ) {
		return { 0, 0 };
	}

	if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		return { 0, 0 };
	}

	if( GS_RaceGametype() ) {
		if( cg.predictedPlayerState.stats[STAT_TIME_SELF] != STAT_NOTSET ) {
			clocktime = cg.predictedPlayerState.stats[STAT_TIME_SELF] * 100;
		} else {
			clocktime = 0;
		}
	} else if( GS_MatchClockOverride() ) {
		clocktime = GS_MatchClockOverride();
	} else {
		curtime = ( GS_MatchWaiting() || GS_MatchPaused() ) ? cg.frame.serverTime : cg.time;
		duration = GS_MatchDuration();
		startTime = GS_MatchStartTime();

		// count downwards when having a duration
		if( duration && ( cg_showTimer->integer != 3 ) ) {
			if( duration + startTime < curtime ) {
				duration = curtime - startTime; // avoid negative results

			}
			clocktime = startTime + duration - curtime;
		} else {
			if( curtime >= startTime ) { // avoid negative results
				clocktime = curtime - startTime;
			} else {
				clocktime = 0;
			}
		}
	}

	seconds = (double)clocktime * 0.001;
	minutes = (int)( seconds / 60 );
	seconds -= minutes * 60;

	return { minutes, seconds };
}