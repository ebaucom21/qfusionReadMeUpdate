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
#include "../client/console.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/links.h"

static bool cmd_preinitialized = false;
static bool cmd_initialized = false;

class CmdTextBuffer {
public:
	void prepend( const wsw::StringView &text );
	void append( const wsw::StringView &text );
	[[nodiscard]]
	auto fetchNextCmd() -> std::optional<wsw::StringView>;
	void shrinkToFit();
private:
	wsw::Vector<char> m_data;
	unsigned m_headOffset { 0 };
};

void CmdTextBuffer::prepend( const wsw::StringView &text ) {
	if( !text.endsWith( '\n' ) && !text.endsWith( ';' ) ) {
		if( text.isZeroTerminated() ) {
			// Copy including the zero character just to overwrite it
			m_data.insert( m_data.begin() + m_headOffset, text.begin(), text.end() + 1 );
			m_data[m_headOffset + text.length()] = '\n';
		} else {
			m_data.insert( m_data.begin() + m_headOffset, text.begin(), text.end() );
			m_data.insert( m_data.begin() + m_headOffset + text.length(), '\n' );
		}
	} else {
		m_data.insert( m_data.begin() + m_headOffset, text.begin(), text.end() );
	}
}

void CmdTextBuffer::append( const wsw::StringView &text ) {
	m_data.insert( m_data.end(), text.begin(), text.end() );
	// TODO: Is this mandatory?
	if( !text.endsWith( '\n' ) && !text.endsWith( ';' ) ) {
		m_data.push_back( '\n' );
	}
}

auto CmdTextBuffer::fetchNextCmd() -> std::optional<wsw::StringView> {
	const unsigned oldHeadOffset = m_headOffset;

	bool isInsideQuotes       = false;
	bool hasPendingEscapeChar = false;

	while( m_headOffset < m_data.size() ) {
		const char ch = m_data[m_headOffset];

		if( hasPendingEscapeChar ) [[unlikely]] {
			hasPendingEscapeChar = false;
		} else {
			if( ch == '"' ) {
				isInsideQuotes = !isInsideQuotes;
			} else if( ch == '\\' ) {
				hasPendingEscapeChar = true;
			}
		}

		++m_headOffset;

		if( ch == '\n' || ( !isInsideQuotes && ch == ';' ) ) {
			break;
		}
	}

	if( m_headOffset > oldHeadOffset ) [[likely]] {
		return wsw::StringView( m_data.data() + oldHeadOffset, m_headOffset - oldHeadOffset );
	}

	return std::nullopt;
}

void CmdTextBuffer::shrinkToFit() {
	const unsigned charsToClear = m_headOffset;
	constexpr unsigned limit    = 4 * 4096u;
	m_data.erase( m_data.begin(), m_data.begin() + charsToClear );
	if( charsToClear > limit || m_data.size() > limit ) [[unlikely]] {
		m_data.shrink_to_fit();
	}
	m_headOffset = 0;
}

class CmdSystem {
public:
	~CmdSystem();
	// TODO: Check whether no command text is submitted partially, track submitted boundaries

	void prependCommand( const wsw::StringView &text ) {
		m_textBuffer.prepend( text );
	}
	void appendCommand( const wsw::StringView &text ) {
		m_textBuffer.append( text );
	}

	void executeNow( const wsw::StringView &text );

	void registerCommand( const wsw::StringView &name, void ( *handler )( const CmdArgs & ) );
	void unregisterCommand( const wsw::StringView &name );

	[[nodiscard]]
	bool isARegisteredCommand( const wsw::StringView &name ) const {
		return findCmdEntryByName( name ) != nullptr;
	}
	[[nodiscard]]
	bool isARegisteredAlias( const wsw::StringView &name ) const {
		return findAliasEntryByName( name ) != nullptr;
	}

	void executeBufferCommands();
	void interuptExecutionLoop() { m_interruptExecutionLoop = true; };
private:
	struct CmdEntry {
		CmdEntry *prev { nullptr }, *next { nullptr };
		wsw::HashedStringView nameAndHash;
		unsigned binIndex;
		void ( *handler )( const CmdArgs & );
	};

	struct AliasEntry {
		AliasEntry *prev { nullptr }, *next { nullptr };
		wsw::HashedStringView nameAndHash;
		wsw::StringView text;
		unsigned binIndex;
		bool isArchive { false };
	};

	// TODO: We need a reusable hash map that relies on intrusive links

	[[nodiscard]]
	auto findCmdEntryByName( const wsw::StringView &name ) const -> const CmdEntry * {
		wsw::HashedStringView nameAndHash( name );
		return findEntryByName( nameAndHash, m_cmdEntryBins, nameAndHash.getHash() % std::size( m_cmdEntryBins ) );
	}

	[[nodiscard]]
	auto findAliasEntryByName( const wsw::StringView &name ) const -> const AliasEntry * {
		wsw::HashedStringView nameAndHash( name );
		return findEntryByName( nameAndHash, m_aliasEntryBins, nameAndHash.getHash() % std::size( m_aliasEntryBins ) );
	}

	template <typename T>
	[[nodiscard]]
	auto findEntryByName( const wsw::HashedStringView &nameAndHash, const T *const *bins, unsigned binIndex ) const -> const T *;

	CmdEntry *m_cmdEntryBins[37] {};
	AliasEntry *m_aliasEntryBins[37] {};

	CmdTextBuffer m_textBuffer;
	CmdArgsSplitter m_argsSplitter;

	unsigned m_aliasRecursionDepth { 0 };
	bool m_interruptExecutionLoop { false };
};

CmdSystem::~CmdSystem() {
	for( unsigned i = 0; i < std::size( m_cmdEntryBins ); ++i ) {
		for( CmdEntry *entry = m_cmdEntryBins[i], *next; entry; entry = next ) { next = entry->next;
			entry->~CmdEntry();
			::free( entry );
		}
	}
	for( unsigned i = 0; i < std::size( m_aliasEntryBins ); ++i ) {
		for( AliasEntry *entry = m_aliasEntryBins[i], *next = nullptr; entry; entry = next ) { next = entry->next;
			entry->~AliasEntry();
			::free( entry );
		}
	}
}

template <typename T>
auto CmdSystem::findEntryByName( const wsw::HashedStringView &nameAndHash, const T *const *bins,
								 unsigned binIndex ) const -> const T * {
	for( const T *entry = bins[binIndex]; entry; entry = entry->next ) {
		if( entry->nameAndHash.equalsIgnoreCase( nameAndHash ) ) {
			return entry;
		}
	}
	return nullptr;
}

void CmdSystem::registerCommand( const wsw::StringView &name, void (*handler)( const CmdArgs & ) ) {
	if( name.empty() ) {
		Com_Printf( S_COLOR_RED "Failed to register command: Empty command name\n" );
		return;
	}

	assert( name.isZeroTerminated() );
	if( Cvar_String( name.data() )[0] ) {
		Com_Printf( S_COLOR_RED "Failed to register command: %s is already defined as a var\n", name.data() );
		return;
	}

	const wsw::HashedStringView nameAndHash( name );
	const auto binIndex = nameAndHash.getHash() % std::size( m_cmdEntryBins );
	if( CmdEntry *existing = const_cast<CmdEntry *>( findEntryByName( nameAndHash, m_cmdEntryBins, binIndex ) ) ) {
		Com_DPrintf( "The command %s is already registered, just updating the handler\n", name.data() );
		existing->handler = handler;
		return;
	}

	void *const mem = ::malloc( sizeof( CmdEntry ) + nameAndHash.length() + 1 );
	if( !mem ) {
		Com_Printf( S_COLOR_RED "Failed to register command: allocation failure\n" );
		return;
	}

	auto *entry = new( mem )CmdEntry;
	auto *chars = (char *)( entry + 1 );
	nameAndHash.copyTo( chars, nameAndHash.size() + 1 );

	entry->nameAndHash = wsw::HashedStringView( chars, name.length(), wsw::StringView::ZeroTerminated );
	entry->handler     = handler;
	entry->binIndex    = binIndex;

	wsw::link( entry, &m_cmdEntryBins[binIndex] );
}

void CmdSystem::unregisterCommand( const wsw::StringView &name ) {
	if( !name.empty() ) {
		assert( name.isZeroTerminated() );
		auto *entry = const_cast<CmdEntry *>( findCmdEntryByName( name ) );
		if( !entry ) {
			Com_Printf( S_COLOR_RED "Failed to unregister command: %s not registered\n", name.data() );
		} else {
			wsw::unlink( entry, &m_cmdEntryBins[entry->binIndex] );
			entry->~CmdEntry();
			::free( entry );
		}
	} else {
		Com_Printf( S_COLOR_RED "Failed to unregister command: Empty command name\n" );
	}
}

void CmdSystem::executeBufferCommands() {
	m_aliasRecursionDepth = 0;

	while( const std::optional<wsw::StringView> maybeNextCmd = m_textBuffer.fetchNextCmd() ) {
		executeNow( *maybeNextCmd );
		if( m_interruptExecutionLoop ) {
			m_interruptExecutionLoop = false;
			break;
		}
	}

	m_textBuffer.shrinkToFit();
}

static SingletonHolder<CmdSystem> g_cmdSystemHolder;

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

static void Cmd_Echo_f( const CmdArgs &cmdArgs ) {
	for( int i = 1; i < Cmd_Argc(); ++i )
		Com_Printf( "%s ", Cmd_Argv( i ) );
	Com_Printf( "\n" );
}

static void Cmd_Alias_f_( bool archive, const CmdArgs &cmdArgs ) {
}

static void Cmd_Alias_f( const CmdArgs &cmdArgs ) {
	Cmd_Alias_f_( false, cmdArgs );
}

static void Cmd_Aliasa_f( const CmdArgs &cmdArgs ) {
	Cmd_Alias_f_( true, cmdArgs );
}

static void Cmd_Unalias_f( const CmdArgs &cmdArgs ) {
}

static void Cmd_UnaliasAll_f( const CmdArgs & ) {
}

void Cmd_WriteAliases( int file ) {
}

void Cbuf_AddText( const char *text ) {
	g_cmdSystemHolder.instance()->appendCommand( wsw::StringView( text ) );
}

void Cbuf_InsertText( const char *text ) {
	g_cmdSystemHolder.instance()->prependCommand( wsw::StringView( text ) );
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
	for( int i = 1; i < COM_Argc(); ++i ) {
		const char *s = COM_Argv( i );
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
	wsw::String text;

	for( int i = 1; i < COM_Argc(); i++ ) {
		const char *arg = COM_Argv( i );
		if( arg[0] != '\0' ) {
			if( arg[0] == '+' ) {
				text.push_back( '\n' );
				arg = arg + 1;
			}
			text.push_back( '"' );
			text.append( arg );
			text.push_back( '"' );
			text.push_back( ' ' );
		}
	}

	if( text.empty() ) {
		return false;
	}

	text.push_back( '\n' );
	Cbuf_AddText( text.data() );
	return true;
}

/*
* Cmd_Wait_f
*
* Causes execution of the remainder of the command buffer to be delayed until
* next frame.  This allows commands like:
* bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
*/
static void Cmd_Wait_f( const CmdArgs & ) {
	g_cmdSystemHolder.instance()->interuptExecutionLoop();
}

/*
* Cmd_AddCommand
* // called by the init functions of other parts of the program to
* // register commands and functions to call for them.
* // The cmd_name is referenced later, so it should not be in temp memory
* // if function is NULL, the command will be forwarded to the server
* // as a clc_clientcommand instead of executed locally
*/
void Cmd_AddCommand( const char *cmd_name, xcommand_t function ) {
	g_cmdSystemHolder.instance()->registerCommand( wsw::StringView( cmd_name ), function );
}

/*
* Cmd_RemoveCommand
*/
void Cmd_RemoveCommand( const char *cmd_name ) {
	g_cmdSystemHolder.instance()->unregisterCommand( wsw::StringView( cmd_name ) );
}

/*
* Cmd_Exists
* // used by the cvar code to check for cvar / command name overlap
*/
bool Cmd_Exists( const char *cmd_name ) {
	return g_cmdSystemHolder.instance()->isARegisteredCommand( wsw::StringView( cmd_name ) );
}

/*
* Cmd_SetCompletionFunc
*/
void Cmd_SetCompletionFunc( const char *cmd_name, xcompletionf_t completion_func ) {
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
	unsigned i;

	// this is not exactly what cbuf does when extracting lines
	// for execution, but it works unless you do weird things like
	// putting the command in quotes
	for( i = 0; i < MAX_STRING_CHARS - 1; i++ ) {
		if( (unsigned char)text[i] <= ' ' || text[i] == ';' ) {
			break;
		} else {
			cmd[i] = text[i];
		}
	}
	cmd[i] = 0;

	if( g_cmdSystemHolder.instance()->isARegisteredCommand( wsw::StringView( cmd, i ) ) ) {
		return true;
	}
	if( Cvar_Find( cmd ) ) {
		return true;
	}
	if( g_cmdSystemHolder.instance()->isARegisteredAlias( wsw::StringView( cmd, i ) ) ) {
		return true;
	}

	return false;
}

/*
* Cmd_ExecuteString
* // Parses a single line of text into arguments and tries to execute it
* // as if it was typed at the console
*/
void Cmd_ExecuteString( const wsw::StringView &text ) {
	g_cmdSystemHolder.instance()->executeNow( text );
}

void CmdSystem::executeNow( const wsw::StringView &text ) {
	const CmdArgs &cmdArgs = m_argsSplitter.exec( text );

	if( !cmdArgs.allArgs.empty() ) {
		// FIXME: This routine defines the order in which identifiers are looked-up, but
		// there are no checks for name-clashes. If a user sets a cvar with the name of
		// an existing command, alias, or dynvar, that cvar becomes shadowed!
		// We need a global namespace data-structure and a way to check for name-clashes
		// that does not break seperation of concerns.
		// Aiwa, 07-14-2006

		if( const CmdEntry *cmdEntry = findCmdEntryByName( cmdArgs[0] ) ) {
			if( cmdEntry->handler ) {
				cmdEntry->handler( cmdArgs );
			} else {
				// forward to server command
				wsw::StaticString<MAX_TOKEN_CHARS> forwardingBuffer;
				forwardingBuffer << wsw::StringView( "cmd " ) << text;
				Cmd_ExecuteString( forwardingBuffer.asView());
			}
		} else if( const AliasEntry *aliasEntry = findAliasEntryByName( cmdArgs[0] ) ) {
			m_aliasRecursionDepth++;
			if( m_aliasRecursionDepth < 16 ) {
				prependCommand( wsw::StringView( "\n" ) );
				prependCommand( aliasEntry->text );
			} else {
				Com_Printf( S_COLOR_RED "Alias recursion depth has reached its limit\n" );
				m_aliasRecursionDepth = 0;
			}
		} else if( !Cvar_Command( cmdArgs ) ) {
			Com_Printf( "Unknown command: \"%s\"\n", cmdArgs[0].data() );
		}
	}
}

void Cmd_ExecuteString( const char *cmd ) {
	Cmd_ExecuteString( wsw::StringView( cmd ) );
}

/*
* Cbuf_Execute
* // Pulls off \n terminated lines of text from the command buffer and sends
* // them through Cmd_ExecuteString.  Stops when the buffer is empty.
* // Normally called once per frame, but may be explicitly invoked.
* // Do not call inside a command function!
*/
void Cbuf_Execute( void ) {
	g_cmdSystemHolder.instance()->executeBufferCommands();
}

/*
* Cmd_PreInit
*/
void Cmd_PreInit( void ) {
	assert( !cmd_preinitialized );
	assert( !cmd_initialized );

	g_cmdSystemHolder.init();

	cmd_preinitialized = true;
}

/*
* Cmd_Init
*/
void Cmd_Init( void ) {
	assert( !cmd_initialized );
	assert( cmd_preinitialized );

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
		Cmd_RemoveCommand( "exec" );
		Cmd_RemoveCommand( "echo" );
		Cmd_RemoveCommand( "aliasa" );
		Cmd_RemoveCommand( "unalias" );
		Cmd_RemoveCommand( "unaliasall" );
		Cmd_RemoveCommand( "alias" );
		Cmd_RemoveCommand( "wait" );
		Cmd_RemoveCommand( "vstr" );

		cmd_initialized = false;
	}

	if( cmd_preinitialized ) {
		g_cmdSystemHolder.shutdown();

		cmd_preinitialized = false;
	}
}
