/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "g_local.h"

game_import_t GAME_IMPORT;

static clientRating_t *G_AddDefaultRating( edict_t * ent, const char *gametype ) {
	return StatsowFacade::Instance()->AddDefaultRating( ent, gametype );
}

static clientRating_t *G_AddRating( edict_t * ent, const char *gametype, float rating, float deviation ) {
	return StatsowFacade::Instance()->AddRating( ent, gametype, rating, deviation );
}

static void G_RemoveRating( edict_t *ent ) {
	StatsowFacade::Instance()->RemoveRating( ent );
}

static void ClientCommand( edict_t *ent ) {
	ClientCommandsHandler::instance()->handleClientCommand( ent );
}

/*
* GetGameAPI
*
* Returns a pointer to the structure with all entry points
*/
extern "C" QF_DLL_EXPORT game_export_t * GetGameAPI( game_import_t * import )
{
	static game_export_t globals;

	GAME_IMPORT = *import;

	globals.API = G_API;

	globals.Init = G_Init;
	globals.Shutdown = G_Shutdown;

	globals.InitLevel = G_InitLevel;

	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientMultiviewChanged = ClientMultiviewChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;
	globals.SnapFrame = G_SnapFrame;
	globals.ClearSnap = G_ClearSnap;

	globals.GetGameState = G_GetGameState;
	globals.GetRawScoreboardData = G_GetScoreboardData;

	globals.AllowDownload = G_AllowDownload;

	globals.AddDefaultRating = G_AddDefaultRating;
	globals.AddRating = G_AddRating;
	globals.RemoveRating = G_RemoveRating;

	return &globals;
}

#if defined ( HAVE_DLLMAIN ) && !defined ( GAME_HARD_LINKED )
int WINAPI DLLMain( void *hinstDll, unsigned long dwReason, void *reserved ) {
	return 1;
}
#endif
