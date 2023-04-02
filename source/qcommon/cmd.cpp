/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// cmd.c -- Quake script command processing module

#include "qcommon.h"
#include "cmdargssplitter.h"
#include "../qcommon/q_trie.h"
#include "../client/console.h"

#define MAX_ALIAS_NAME      64
#define ALIAS_LOOP_COUNT    16

typedef struct cmdalias_s {
	char *name;
	char *value;
	bool archive;
} cmd_alias_t;

static bool cmd_preinitialized = false;
static bool cmd_initialized = false;

static trie_t *cmd_alias_trie = NULL;
static const trie_casing_t CMD_ALIAS_TRIE_CASING = CON_CASE_SENSITIVE ? TRIE_CASE_SENSITIVE : TRIE_CASE_INSENSITIVE;;

static bool cmd_wait;
static int alias_count;    // for detecting runaway loops

static int Cmd_Archive( void *alias, void *ignored ) {
	assert( alias );
	return ( (cmd_alias_t *) alias )->archive;
}

//=============================================================================

/*
* Cmd_Wait_f
*
* Causes execution of the remainder of the command buffer to be delayed until
* next frame.  This allows commands like:
* bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
*/
static void Cmd_Wait_f( const CmdArgs & ) {
	cmd_wait = true;
}


/*
=============================================================================

COMMAND BUFFER

=============================================================================
*/

/*
* Command buffer is a cyclical dynamically allocated buffer
* It must never be totally full, since cbuf_text_head points to first free
* position in the buffer
*/

static bool cbuf_initialized = false;

#define MIN_CMD_TEXT_SIZE 1024

static size_t cbuf_text_size, cbuf_text_head, cbuf_text_tail;
static char *cbuf_text;

/*
* Cbuf_Init
*/
void Cbuf_Init( void ) {
	assert( !cbuf_initialized );

	cbuf_text_size = MIN_CMD_TEXT_SIZE;
	cbuf_text = (char *)Q_malloc( cbuf_text_size );
	cbuf_text_head = 0;
	cbuf_text_tail = 0;

	cbuf_initialized = true;
}

/*
* Cbuf_Shutdown
*/
void Cbuf_Shutdown( void ) {
	if( !cbuf_initialized ) {
		return;
	}

	Q_free( cbuf_text );
	cbuf_text = NULL;
	cbuf_text_size = 0;
	cbuf_text_head = 0;
	cbuf_text_tail = 0;

	cbuf_initialized = false;
}

/*
* Q_freeSpace
*
* Frees some space, if we have too big buffer in use
*/
static void Q_freeSpace( void ) {
	char *old;
	size_t used, old_size;

	if( cbuf_text_head >= cbuf_text_tail ) {
		used = cbuf_text_head - cbuf_text_tail;
	} else {
		used = cbuf_text_size - cbuf_text_tail + cbuf_text_head;
	}

	if( used < cbuf_text_size / 2 && used < cbuf_text_size - MIN_CMD_TEXT_SIZE ) {
		old = cbuf_text;
		old_size = cbuf_text_size;

		cbuf_text_size = used + MIN_CMD_TEXT_SIZE;
		cbuf_text = (char *)Q_malloc( cbuf_text_size );

		if( cbuf_text_head >= cbuf_text_tail ) {
			memcpy( cbuf_text, old + cbuf_text_tail, used );
		} else {
			memcpy( cbuf_text, old + cbuf_text_tail, old_size - cbuf_text_tail );
			memcpy( cbuf_text + ( old_size - cbuf_text_tail ), old, cbuf_text_head );
		}
		cbuf_text_tail = 0;
		cbuf_text_head = used;

		Q_free( old );
	}
}

/*
* Cbuf_EnsureSpace
*/
static void Cbuf_EnsureSpace( size_t size ) {
	size_t free;
	size_t diff;

	if( cbuf_text_head >= cbuf_text_tail ) {
		free = cbuf_text_size - cbuf_text_head + cbuf_text_tail;
	} else {
		free = cbuf_text_tail - cbuf_text_head;
	}

	if( free >= size ) {
		return;
	}

	diff = ( size - free ) + MIN_CMD_TEXT_SIZE;
	cbuf_text_size += diff;
	cbuf_text = (char *)Q_realloc( cbuf_text, cbuf_text_size );

	if( cbuf_text_head < cbuf_text_tail ) {
		memmove( cbuf_text + cbuf_text_tail + diff, cbuf_text + cbuf_text_tail, cbuf_text_size - diff - cbuf_text_tail );
		cbuf_text_tail += diff;
	}
}

/*
* Cbuf_AddText
*
* Adds command text at the end of the buffer
*/
void Cbuf_AddText( const char *text ) {
	size_t textlen = strlen( text );

	Cbuf_EnsureSpace( textlen );

	if( cbuf_text_size - cbuf_text_head < textlen ) {
		size_t endsize = cbuf_text_size - cbuf_text_head;

		memcpy( cbuf_text + cbuf_text_head, text, endsize );
		memcpy( cbuf_text, text + endsize, textlen - endsize );
		cbuf_text_head = textlen - endsize;
	} else {
		memcpy( cbuf_text + cbuf_text_head, text, textlen );
		cbuf_text_head += textlen;
		if( cbuf_text_head == cbuf_text_size ) {
			cbuf_text_head = 0;
		}
	}
}

/*
* Cbuf_InsertText
*
* When a command wants to issue other commands immediately, the text is
* inserted at the beginning of the buffer, before any remaining unexecuted
* commands.
* Adds a \n to the text
*/
void Cbuf_InsertText( const char *text ) {
	size_t textlen = strlen( text );

	Cbuf_EnsureSpace( textlen );

	if( cbuf_text_tail < textlen ) {
		memcpy( cbuf_text + cbuf_text_size - ( textlen - cbuf_text_tail ), text, textlen - cbuf_text_tail );
		memcpy( cbuf_text, text + textlen - cbuf_text_tail, cbuf_text_tail );
		cbuf_text_tail = cbuf_text_size - ( textlen - cbuf_text_tail );
	} else {
		memcpy( cbuf_text + cbuf_text_tail - textlen, text, textlen );
		cbuf_text_tail -= textlen;
	}
}


/*
* Cbuf_ExecuteText
* This can be used in place of either Cbuf_AddText or Cbuf_InsertText
*/
void Cbuf_ExecuteText( int exec_when, const char *text ) {
	switch( exec_when ) {
		case EXEC_NOW:
			Cmd_ExecuteString( text );
			break;
		case EXEC_INSERT:
			Cbuf_InsertText( text );
			break;
		case EXEC_APPEND:
			Cbuf_AddText( text );
			break;
		default:
			Com_Error( ERR_FATAL, "Cbuf_ExecuteText: bad exec_when" );
	}
}

/*
* Cbuf_Execute
* // Pulls off \n terminated lines of text from the command buffer and sends
* // them through Cmd_ExecuteString.  Stops when the buffer is empty.
* // Normally called once per frame, but may be explicitly invoked.
* // Do not call inside a command function!
*/
void Cbuf_Execute( void ) {
	size_t i;
	int c;
	char line[MAX_STRING_CHARS];
	bool quotes, quoteskip;

	alias_count = 0;    // don't allow infinite alias loops

	while( cbuf_text_tail != cbuf_text_head ) {
		// find a \n or ; line break
		i = 0;
		quotes = false;
		quoteskip = false;
		while( cbuf_text_tail != cbuf_text_head && i < sizeof( line ) - 1 ) {
			c = cbuf_text[cbuf_text_tail];

			if( !quoteskip && c == '"' ) {
				quotes = !quotes;
			}

			if( !quoteskip && c == '\\' ) {
				quoteskip = true;
			} else {
				quoteskip = false;
			}

			line[i] = c;

			cbuf_text_tail = ( cbuf_text_tail + 1 ) % cbuf_text_size;

			if( ( c == '\n' ) || ( !quotes && c == ';' ) ) {
				break;
			}

			i++;
		}
		line[i] = 0;

		// execute the command line
		Cmd_ExecuteString( line );

		if( cmd_wait ) {
			// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}

	Q_freeSpace();
}


/*
* Cbuf_AddEarlyCommands
*
* Adds all the +set commands from the command line:
*
* Adds command line parameters as script statements
* Commands lead with a +, and continue until another +
*
* Set and exec commands are added early, so they are guaranteed to be set before
* the client and server initialize for the first time.
*
* This command is first run before autoexec.cfg and config.cfg to allow changing
* fs_basepath etc. The second run is after those files has been execed in order
* to allow overwriting values set in them.
*
* Other commands are added late, after all initialization is complete.
*/
void Cbuf_AddEarlyCommands( bool second_run ) {
	int i;
	const char *s;

	for( i = 1; i < COM_Argc(); ++i ) {
		s = COM_Argv( i );
		if( !Q_strnicmp( s, "+set", 4 ) ) {
			if( strlen( s ) > 4 ) {
				Cbuf_AddText( va( "\"set%s\" \"%s\" \"%s\"\n", s + 4, COM_Argv( i + 1 ), COM_Argv( i + 2 ) ) );
			} else {
				Cbuf_AddText( va( "\"set\" \"%s\" \"%s\"\n", COM_Argv( i + 1 ), COM_Argv( i + 2 ) ) );
			}
			if( second_run ) {
				COM_ClearArgv( i );
				COM_ClearArgv( i + 1 );
				COM_ClearArgv( i + 2 );
			}
			i += 2;
		} else if( second_run && !Q_stricmp( s, "+exec" ) ) {
			Cbuf_AddText( va( "exec \"%s\"\n", COM_Argv( i + 1 ) ) );
			COM_ClearArgv( i );
			COM_ClearArgv( i + 1 );
			i += 1;
		}
	}
}

/*
* Cbuf_AddLateCommands
*
* Adds command line parameters as script statements
* Commands lead with a + and continue until another + or -
* quake +map amlev1
*
* Returns true if any late commands were added, which
* will keep the demoloop from immediately starting
*/
bool Cbuf_AddLateCommands( void ) {
	int i;
	size_t text_size;
	char *text;

	// build the combined string to parse from
	text_size = 0;
	for( i = 1; i < COM_Argc(); i++ ) {
		if( COM_Argv( i )[0] == 0 ) {
			continue;
		}
		text_size += strlen( COM_Argv( i ) ) + 2 /* quatation marks */ + 1 /* space */;
	}
	if( text_size == 0 ) {
		return false;
	}

	text_size += 2; // '\n' and '\0' at the end
	text = (char *)Q_malloc( (int)text_size );
	text[0] = 0;
	for( i = 1; i < COM_Argc(); i++ ) {
		if( COM_Argv( i )[0] == 0 ) {
			continue;
		}
		if( COM_Argv( i )[0] == '+' ) {
			Q_strncatz( text, "\n", text_size );
			Q_strncatz( text, va( "\"%s\"", COM_Argv( i ) + 1 ), text_size );
		} else {
			Q_strncatz( text, va( "\"%s\"", COM_Argv( i ) ), text_size );
		}
		Q_strncatz( text, " ", text_size );
	}
	Q_strncatz( text, "\n", text_size );

	Cbuf_AddText( text );
	Q_free( text );

	return true;
}


/*
==============================================================================

SCRIPT COMMANDS

==============================================================================
*/


/*
* Cmd_Exec_f
*/
static void Cmd_Exec_f( const CmdArgs &cmdArgs ) {
	char *f = NULL, *name;
	const char *arg = Cmd_Argv( 1 );
	bool silent = Cmd_Argc() >= 3 && !Q_stricmp( Cmd_Argv( 2 ), "silent" );
	int len = -1, name_size;
	const char *basename;

	if( Cmd_Argc() < 2 || !arg[0] ) {
		Com_Printf( "Usage: exec <filename>\n" );
		return;
	}

	name_size = sizeof( char ) * ( strlen( arg ) + strlen( ".cfg" ) + 1 );
	name = (char *)Q_malloc( name_size );

	Q_strncpyz( name, arg, name_size );
	COM_SanitizeFilePath( name );

	if( !COM_ValidateRelativeFilename( name ) ) {
		if( !silent ) {
			Com_Printf( "Invalid filename\n" );
		}
		Q_free( name );
		return;
	}

	COM_DefaultExtension( name, ".cfg", name_size );

	basename = FS_BaseNameForFile( name );
	if( basename ) {
		len = FS_LoadBaseFile( basename, (void **)&f, NULL, 0 );
	}

	if( !f ) {
		if( !silent ) {
			Com_Printf( "Couldn't execute: %s\n", name );
		}
		Q_free( name );
		return;
	}

	if( !silent ) {
		Com_Printf( "Executing: %s\n", name );
	}

	Cbuf_InsertText( "\n" );
	if( len >= 3 && ( (uint8_t)f[0] == 0xEF && (uint8_t)f[1] == 0xBB && (uint8_t)f[2] == 0xBF ) ) {
		Cbuf_InsertText( f + 3 ); // skip Windows UTF-8 marker
	} else {
		Cbuf_InsertText( f );
	}

	FS_FreeFile( f );
	Q_free( name );
}

/*
* Cmd_Echo_f
*
* Just prints the rest of the line to the console
*/
static void Cmd_Echo_f( const CmdArgs &cmdArgs ) {
	int i;
	for( i = 1; i < Cmd_Argc(); ++i )
		Com_Printf( "%s ", Cmd_Argv( i ) );
	Com_Printf( "\n" );
}

/*
* Cmd_Alias_f
*
* Creates a new command that executes a command string (possibly ; separated)
*/
static void Cmd_Alias_f_( bool archive, const CmdArgs &cmdArgs ) {
	cmd_alias_t *a;
	char cmd[1024];
	int i, c;
	size_t len;
	const char *s;

	if( Cmd_Argc() == 1 ) {
		Com_Printf( "usage: alias <name> <command>\n" );
		return;
	}

	s = Cmd_Argv( 1 );
	len = strlen( s );
	if( len >= MAX_ALIAS_NAME ) {
		Com_Printf( "Alias name is too long\n" );
		return;
	}

	assert( cmd_alias_trie );
	Trie_Find( cmd_alias_trie, s, TRIE_EXACT_MATCH, (void **)&a );
	if( a ) {
		if( Cmd_Argc() == 2 ) {
			if( archive ) {
				a->archive = true;
			}
			Com_Printf( "alias \"%s\" is \"%s" S_COLOR_WHITE "\"\n", a->name, a->value );
			return;
		}
		Q_free( a->value );
	} else {
		a = (cmd_alias_t *)Q_malloc( (int) ( sizeof( cmd_alias_t ) + len + 1 ) );
		a->name = (char *) ( (uint8_t *)a + sizeof( cmd_alias_t ) );
		strcpy( a->name, s );
		Trie_Insert( cmd_alias_trie, s, a );
	}

	if( archive ) {
		a->archive = true;
	}

	// copy the rest of the command line
	cmd[0] = 0; // start out with a null string
	c = Cmd_Argc();
	for( i = 2; i < c; i++ ) {
		Q_strncatz( cmd, Cmd_Argv( i ), sizeof( cmd ) );
		if( i != ( c - 1 ) ) {
			Q_strncatz( cmd, " ", sizeof( cmd ) );
		}
	}

	a->value = Q_strdup( cmd );
}

/*
* Cmd_Alias_f
*/
static void Cmd_Alias_f( const CmdArgs &cmdArgs ) {
	Cmd_Alias_f_( false, cmdArgs );
}

/*
* Cmd_Aliasa_f
*/
static void Cmd_Aliasa_f( const CmdArgs &cmdArgs ) {
	Cmd_Alias_f_( true, cmdArgs );
}

/*
* Cmd_Unalias_f
*
* Removes an alias command
*/
static void Cmd_Unalias_f( const CmdArgs &cmdArgs ) {
	const char *s;
	cmd_alias_t *a;

	if( Cmd_Argc() == 1 ) {
		Com_Printf( "usage: unalias <name>\n" );
		return;
	}

	s = Cmd_Argv( 1 );
	if( strlen( s ) >= MAX_ALIAS_NAME ) {
		Com_Printf( "Alias name is too long\n" );
		return;
	}

	assert( cmd_alias_trie );
	if( Trie_Remove( cmd_alias_trie, s, (void **)&a ) == TRIE_OK ) {
		Q_free( a->value );
		Q_free( a );
	} else {
		Com_Printf( "Cmd_Unalias_f: %s not added\n", s );
	}
}

/*
* Cmd_UnaliasAll_f
*
* Removes an alias command
*/
static void Cmd_UnaliasAll_f( const CmdArgs & ) {
	struct trie_dump_s *dump;
	unsigned int i;

	assert( cmd_alias_trie );
	Trie_Dump( cmd_alias_trie, "", TRIE_DUMP_VALUES, &dump );
	for( i = 0; i < dump->size; ++i ) {
		cmd_alias_t *const a = (cmd_alias_t *) dump->key_value_vector[i].value;
		Q_free( a->value );
		Q_free( a );
	}
	Trie_FreeDump( dump );
	Trie_Clear( cmd_alias_trie );
}

/*
* Cmd_WriteAliases
*
* Write lines containing "aliasa alias value" for all aliases
* with the archive flag set to true
*/
void Cmd_WriteAliases( int file ) {
	struct trie_dump_s *dump = NULL;
	unsigned int i;

	// Vic: Why on earth this line was written _below_ aliases?
	// It was kinda dumb, I think 'cause it undid everything above
	FS_Printf( file, "unaliasall\r\n" );

	assert( cmd_alias_trie );
	Trie_DumpIf( cmd_alias_trie, "", TRIE_DUMP_VALUES, Cmd_Archive, NULL, &dump );
	for( i = 0; i < dump->size; ++i ) {
		cmd_alias_t *const a = (cmd_alias_t *) dump->key_value_vector[i].value;
		FS_Printf( file, "aliasa %s \"%s\"\r\n", a->name, a->value );
	}
	Trie_FreeDump( dump );
}

/*
=============================================================================

COMMAND EXECUTION

=============================================================================
*/

typedef struct cmd_function_s {
	char *name;
	xcommand_t function;
	xcompletionf_t completion_func;
} cmd_function_t;

static trie_t *cmd_function_trie = NULL;
static const trie_casing_t CMD_FUNCTION_TRIE_CASING = CON_CASE_SENSITIVE ? TRIE_CASE_SENSITIVE : TRIE_CASE_INSENSITIVE;

/*
* Cmd_AddCommand
* // called by the init functions of other parts of the program to
* // register commands and functions to call for them.
* // The cmd_name is referenced later, so it should not be in temp memory
* // if function is NULL, the command will be forwarded to the server
* // as a clc_clientcommand instead of executed locally
*/
void Cmd_AddCommand( const char *cmd_name, xcommand_t function ) {
	cmd_function_t *cmd;

	if( !cmd_name || !cmd_name[0] ) {
		Com_DPrintf( "Cmd_AddCommand: empty name pass as an argument\n" );
		return;
	}

	// fail if the command is a variable name
	if( Cvar_String( cmd_name )[0] ) {
		Com_Printf( "Cmd_AddCommand: %s already defined as a var\n", cmd_name );
		return;
	}

	// fail if the command already exists
	assert( cmd_function_trie );
	assert( cmd_name );
	if( Trie_Find( cmd_function_trie, cmd_name, TRIE_EXACT_MATCH, (void **)&cmd ) == TRIE_OK ) {
		cmd->function = function;
		cmd->completion_func = NULL;
		Com_DPrintf( "Cmd_AddCommand: %s already defined\n", cmd_name );
		return;
	}

	cmd = (cmd_function_t *)Q_malloc( (int)( sizeof( cmd_function_t ) + strlen( cmd_name ) + 1 ) );
	cmd->name = (char *) ( (uint8_t *)cmd + sizeof( cmd_function_t ) );
	strcpy( cmd->name, cmd_name );
	cmd->function = function;
	cmd->completion_func = NULL;
	Trie_Insert( cmd_function_trie, cmd_name, cmd );
}

/*
* Cmd_RemoveCommand
*/
void Cmd_RemoveCommand( const char *cmd_name ) {
	cmd_function_t *cmd;

	if( !cmd_initialized ) {
		return;
	}

	assert( cmd_function_trie );
	assert( cmd_name );
	if( Trie_Remove( cmd_function_trie, cmd_name, (void **)&cmd ) == TRIE_OK ) {
		Q_free( cmd );
	} else {
		Com_Printf( "Cmd_RemoveCommand: %s not added\n", cmd_name );
	}
}

/*
* Cmd_Exists
* // used by the cvar code to check for cvar / command name overlap
*/
bool Cmd_Exists( const char *cmd_name ) {
	cmd_function_t *cmd;

	assert( cmd_function_trie );
	assert( cmd_name );
	return Trie_Find( cmd_function_trie, cmd_name, TRIE_EXACT_MATCH, (void **)&cmd ) == TRIE_OK;
}

/*
* Cmd_SetCompletionFunc
*/
void Cmd_SetCompletionFunc( const char *cmd_name, xcompletionf_t completion_func ) {
	cmd_function_t *cmd;

	if( !cmd_name || !cmd_name[0] ) {
		Com_DPrintf( "Cmd_SetCompletionFunc: empty name pass as an argument\n" );
		return;
	}

	if( Trie_Find( cmd_function_trie, cmd_name, TRIE_EXACT_MATCH, (void **)&cmd ) == TRIE_OK ) {
		cmd->completion_func = completion_func;
		return;
	}

	Com_DPrintf( "Cmd_SetCompletionFunc: %s already does not exist\n", cmd_name );
}

/*
* Cmd_VStr_f
*/
static void Cmd_VStr_f( const CmdArgs &cmdArgs ) {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "vstr <variable> : execute a variable command\n" );
	} else {
		Cbuf_InsertText( Cvar_String( Cmd_Argv( 1 ) ) );
	}
}

/*
* Cmd_CheckForCommand
*
* Used by console code to check if text typed is a command/cvar/alias or chat
*/
bool Cmd_CheckForCommand( char *text ) {
	char cmd[MAX_STRING_CHARS];
	cmd_alias_t *a;
	int i;

	// this is not exactly what cbuf does when extracting lines
	// for execution, but it works unless you do weird things like
	// putting the command in quotes
	for( i = 0; i < MAX_STRING_CHARS - 1; i++ )
		if( (unsigned char)text[i] <= ' ' || text[i] == ';' ) {
			break;
		} else {
			cmd[i] = text[i];
		}
	cmd[i] = 0;

	if( Cmd_Exists( cmd ) ) {
		return true;
	}
	if( Cvar_Find( cmd ) ) {
		return true;
	}
	if( Trie_Find( cmd_alias_trie, cmd, TRIE_EXACT_MATCH, (void **)&a ) == TRIE_OK ) {
		return true;
	}

	return false;
}

/*
* Cmd_ExecuteString
* // Parses a single line of text into arguments and tries to execute it
* // as if it was typed at the console
* FIXME: lookupnoadd the token to speed search?
*/
void Cmd_ExecuteString( const char *text ) {
	cmd_function_t *cmd;
	cmd_alias_t *a;

	static CmdArgsSplitter argsSplitter;
	const CmdArgs &cmdArgs = argsSplitter.exec( wsw::StringView( text ) );

	if( cmdArgs.allArgs.empty() ) {
		return; // no tokens
	}

	const char *str = cmdArgs[0].data();

	// FIXME: This routine defines the order in which identifiers are looked-up, but
	// there are no checks for name-clashes. If a user sets a cvar with the name of
	// an existing command, alias, or dynvar, that cvar becomes shadowed!
	// We need a global namespace data-structure and a way to check for name-clashes
	// that does not break seperation of concerns.
	// Aiwa, 07-14-2006

	assert( cmd_function_trie );
	assert( cmd_alias_trie );
	if( Trie_Find( cmd_function_trie, str, TRIE_EXACT_MATCH, (void **)&cmd ) == TRIE_OK ) {
		// check functions
		if( !cmd->function ) {
			// forward to server command
			Cmd_ExecuteString( va( "cmd %s", text ) );
		} else {
			cmd->function( cmdArgs );
		}
	} else if( Trie_Find( cmd_alias_trie, str, TRIE_EXACT_MATCH, (void **)&a ) == TRIE_OK ) {
		// check alias
		if( ++alias_count == ALIAS_LOOP_COUNT ) {
			Com_Printf( "ALIAS_LOOP_COUNT\n" );
			alias_count = 0;
			return;
		}
		Cbuf_InsertText( "\n" );
		Cbuf_InsertText( a->value );
	} else if( Cvar_Command( cmdArgs ) ) {
		// check cvars
		;
	} else {
		Com_Printf( "Unknown command \"%s" S_COLOR_WHITE "\"\n", str );
	}
}

/*
* Cmd_PreInit
*/
void Cmd_PreInit( void ) {
	assert( !cmd_preinitialized );
	assert( !cmd_initialized );

	assert( !cmd_alias_trie );
	assert( !cmd_function_trie );

	Trie_Create( CMD_ALIAS_TRIE_CASING, &cmd_alias_trie );
	Trie_Create( CMD_FUNCTION_TRIE_CASING, &cmd_function_trie );

	cmd_preinitialized = true;
}

/*
* Cmd_Init
*/
void Cmd_Init( void ) {
	assert( !cmd_initialized );
	assert( cmd_preinitialized );

	assert( cmd_alias_trie );
	assert( cmd_function_trie );

	//
	// register our commands
	//
	Cmd_AddCommand( "exec", Cmd_Exec_f );
	Cmd_AddCommand( "echo", Cmd_Echo_f );
	Cmd_AddCommand( "aliasa", Cmd_Aliasa_f );
	Cmd_AddCommand( "unalias", Cmd_Unalias_f );
	Cmd_AddCommand( "unaliasall", Cmd_UnaliasAll_f );
	Cmd_AddCommand( "alias", Cmd_Alias_f );
	Cmd_AddCommand( "wait", Cmd_Wait_f );
	Cmd_AddCommand( "vstr", Cmd_VStr_f );

	cmd_initialized = true;
}

void Cmd_Shutdown( void ) {
	if( cmd_initialized ) {
		unsigned int i;
		struct trie_dump_s *dump;

		assert( cmd_alias_trie );
		assert( cmd_function_trie );

		Cmd_RemoveCommand( "exec" );
		Cmd_RemoveCommand( "echo" );
		Cmd_RemoveCommand( "aliasa" );
		Cmd_RemoveCommand( "unalias" );
		Cmd_RemoveCommand( "unaliasall" );
		Cmd_RemoveCommand( "alias" );
		Cmd_RemoveCommand( "wait" );
		Cmd_RemoveCommand( "vstr" );

		Trie_Dump( cmd_function_trie, "", TRIE_DUMP_VALUES, &dump );
		for( i = 0; i < dump->size; ++i ) {
#ifndef PUBLIC_BUILD
			Com_Printf( "Warning: Command %s was never removed\n",
						( (cmd_function_t *)dump->key_value_vector[i].value )->name );
#endif
			Cmd_RemoveCommand( ( (cmd_function_t *)dump->key_value_vector[i].value )->name );
		}
		Trie_FreeDump( dump );

		cmd_initialized = false;
	}

	if( cmd_preinitialized ) {
		assert( cmd_alias_trie );
		assert( cmd_function_trie );

		Trie_Destroy( cmd_alias_trie );
		cmd_alias_trie = NULL;
		Trie_Destroy( cmd_function_trie );
		cmd_function_trie = NULL;

		cmd_preinitialized = false;
	}
}
