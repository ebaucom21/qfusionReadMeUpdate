/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2023 Chasseur de Bots

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

#include "cmdsystem.h"
#include "cmdcompat.h"
#include "../qcommon/links.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswexceptions.h"

using wsw::operator""_asView;

void CmdSystem::TextBuffer::prepend( const wsw::StringView &text ) {
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

void CmdSystem::TextBuffer::append( const wsw::StringView &text ) {
	m_data.insert( m_data.end(), text.begin(), text.end() );
	// TODO: Is this mandatory?
	if( !text.endsWith( '\n' ) && !text.endsWith( ';' ) ) {
		m_data.push_back( '\n' );
	}
}

auto CmdSystem::TextBuffer::fetchNextCmd() -> std::optional<wsw::StringView> {
	const unsigned oldHeadOffset = m_headOffset;

	bool isInsideQuotes       = false;
	bool hasPendingEscapeChar = false;
	unsigned numCmdChars      = 0;

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

		++numCmdChars;
	}

	if( m_headOffset > oldHeadOffset ) [[likely]] {
		return wsw::StringView( m_data.data() + oldHeadOffset, numCmdChars );
	}

	return std::nullopt;
}

void CmdSystem::TextBuffer::shrinkToFit() {
	const unsigned charsToClear = m_headOffset;
	constexpr unsigned limit    = 4 * 4096u;
	m_data.erase( m_data.begin(), m_data.begin() + charsToClear );
	if( charsToClear > limit || m_data.size() > limit ) [[unlikely]] {
		m_data.shrink_to_fit();
	}
	m_headOffset = 0;
}

bool CmdSystem::registerCommand( const wsw::StringView &name, CmdFunc cmdFunc ) {
	checkCallingThread();

	if( name.empty() ) {
		Com_Printf( S_COLOR_RED "Failed to register command: Empty command name\n" );
		return false;
	}

	assert( name.isZeroTerminated() );
	if( Cvar_String( name.data() )[0] ) {
		Com_Printf( S_COLOR_RED "Failed to register command: %s is already defined as a var\n", name.data() );
		return false;
	}

	const wsw::HashedStringView nameAndHash( name );
	if( CmdEntry *existing = m_cmdEntries.findByName( nameAndHash ) ) {
		Com_DPrintf( "The command %s is already registered, just updating the handler\n", name.data() );
		existing->m_cmdFunc = cmdFunc;
		return true;
	}

	void *const mem = ::malloc( sizeof( CmdEntry ) + nameAndHash.length() + 1 );
	if( !mem ) {
		Com_Printf( S_COLOR_RED "Failed to register command: allocation failure\n" );
		return false;
	}

	auto *entry = new( mem )CmdEntry;
	auto *chars = (char *)( entry + 1 );
	nameAndHash.copyTo( chars, nameAndHash.size() + 1 );

	entry->m_nameAndHash = wsw::HashedStringView( chars, name.length(), wsw::StringView::ZeroTerminated );
	entry->m_cmdFunc     = cmdFunc;

	m_cmdEntries.insertUniqueTakingOwneship( entry );

	return true;
}

bool CmdSystem::unregisterCommand( const wsw::StringView &name ) {
	checkCallingThread();

	if( !name.empty() ) {
		if( CmdEntry *entry = m_cmdEntries.findByName( wsw::HashedStringView( name ) ) ) {
			m_cmdEntries.remove( entry );
			return true;
		} else {
			assert( name.isZeroTerminated() );
			Com_Printf( S_COLOR_RED "Failed to unregister command: %s not registered\n", name.data() );
			return false;
		}
	} else {
		Com_Printf( S_COLOR_RED "Failed to unregister command: Empty command name\n" );
		return false;
	}
}

void CmdSystem::executeBufferCommands() {
	checkCallingThread();

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

void CmdSystem::executeNow( const wsw::StringView &text ) {
	checkCallingThread();

	if( const CmdArgs &cmdArgs = m_argsSplitter.exec( text ); !cmdArgs.allArgs.empty() ) {
		// FIXME: This routine defines the order in which identifiers are looked-up, but
		// there are no checks for name-clashes. If a user sets a cvar with the name of
		// an existing command, alias, or dynvar, that cvar becomes shadowed!
		// We need a global namespace data-structure and a way to check for name-clashes
		// that does not break seperation of concerns.
		// Aiwa, 07-14-2006

		const wsw::HashedStringView testedName( cmdArgs[0] );
		if( const CmdEntry *cmdEntry = m_cmdEntries.findByName( testedName ) ) {
			if( cmdEntry->m_cmdFunc ) {
				cmdEntry->m_cmdFunc( cmdArgs );
			} else {
				// forward to server command
				wsw::StaticString<MAX_TOKEN_CHARS> forwardingBuffer;
				forwardingBuffer << wsw::StringView( "cmd " ) << text;
				executeNow( forwardingBuffer.asView() );
			}
		} else if( const AliasEntry *aliasEntry = m_aliasEntries.findByName( testedName ) ) {
			m_aliasRecursionDepth++;
			if( m_aliasRecursionDepth < 16 ) {
				prependCommand( wsw::StringView( "\n" ) );
				prependCommand( aliasEntry->m_text );
			} else {
				Com_Printf( S_COLOR_RED "Alias recursion depth has reached its limit\n" );
				m_aliasRecursionDepth = 0;
			}
		} else if( !Cvar_Command( cmdArgs ) ) {
			Com_Printf( "Unknown command: \"%s\"\n", cmdArgs[0].data() );
		}
	}
}

void CmdSystem::unregisterSystemCommands() {
	checkCallingThread();

	unregisterCommand( "exec"_asView );
	unregisterCommand( "echo"_asView );
	unregisterCommand( "alias"_asView );
	unregisterCommand( "aliasa"_asView );
	unregisterCommand( "unalias"_asView );
	unregisterCommand( "unaliasall"_asView );
	unregisterCommand( "wait"_asView );
	unregisterCommand( "vstr"_asView );
}

void CmdSystem::classifyExecutableCmdArgs( int argc, char **argv,
										   wsw::Vector<wsw::StringView> *setArgs,
										   wsw::Vector<wsw::StringView> *setAndExecArgs,
										   wsw::Vector<std::optional<wsw::StringView>> *otherArgs ) {
	for( int i = 1; i < argc; ++i ) {
		const wsw::StringView arg( argv[i] );
		if( arg.startsWith( "+set"_asView ) ) {
			if( i + 2 < argc ) {
				const wsw::StringView args[] { arg.drop( 1 ), wsw::StringView( argv[i + 1] ), wsw::StringView( argv[i + 2] ) };
				setArgs->insert( setArgs->end(), std::begin( args ), std::end( args ) );
				setAndExecArgs->insert( setAndExecArgs->end(), std::begin( args ), std::end( args ) );
				i += 2;
			}
		} else if( arg.startsWith( "+exec"_asView ) ) {
			if( i + 1 < argc ) {
				setAndExecArgs->push_back( arg.drop( 1 ) );
				setAndExecArgs->push_back( wsw::StringView( argv[i + 1 ] ) );
				i += 1;
			}
		} else {
			if( arg.startsWith( '+' ) ) {
				if( arg.length() > 1 ) {
					otherArgs->push_back( std::nullopt );
					otherArgs->push_back( arg.drop( 1 ) );
				}
			} else {
				otherArgs->push_back( arg );
			}
		}
	}
}

void CmdSystem::appendEarlySetCommands( std::span<const wsw::StringView> args ) {
	checkCallingThread();

	assert( args.size() % 3 == 0 );
	for( size_t i = 0; i < args.size(); i += 3 ) {
		wsw::StaticString<MAX_TOKEN_CHARS> text;
		text << args[i + 0] << " \""_asView << args[i + 1] << "\" \""_asView << args[i + 2] << "\"\n"_asView;
		appendCommand( text.asView() );
	}
}

void CmdSystem::appendEarlySetAndExecCommands( std::span<const wsw::StringView> args ) {
	checkCallingThread();

	size_t i = 0;
	while( i < args.size() ) {
		const wsw::StringView &cmdName = args[0];
		if( cmdName.startsWith( "set"_asView ) ) {
			wsw::StaticString<MAX_TOKEN_CHARS> text;
			text << cmdName << " \""_asView << args[i + 1] << "\" \""_asView << args[i + 2] << "\"\n"_asView;
			appendCommand( text.asView() );
			i += 3;
		} else if( cmdName.startsWith( "exec"_asView ) ) {
			wsw::StaticString<MAX_TOKEN_CHARS> text;
			text << cmdName << " \""_asView << args[i + 1] << "\"\n"_asView;
			appendCommand( text.asView() );
			i += 2;
		} else {
			wsw::failWithLogicError( "Unexpected command name" );
		}
	}
}

void CmdSystem::appendLateCommands( std::span<const std::optional<wsw::StringView>> args ) {
	checkCallingThread();

	assert( !args.empty() );

	wsw::String text;
	for( size_t i = 0; i < args.size(); ++i ) {
		const std::optional<wsw::StringView> &argOrNewCmdSeparator = args[i];
		if( argOrNewCmdSeparator.has_value() ) {
			text.push_back( '"' );
			text.append( argOrNewCmdSeparator->data(), argOrNewCmdSeparator->size() );
			text.push_back( '"' );
		} else {
			text.push_back( '\n' );
		}
	}
	if( !text.ends_with( '\n' ) ) {
		text.push_back( '\n' );
	}

	appendCommand( wsw::StringView( text.data(), text.size() ) );
}

void CmdSystem::helperForHandlerOfExec( const CmdArgs &cmdArgs ) {
	checkCallingThread();

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

	prependCommand( "\n"_asView );
	if( len >= 3 && ( (uint8_t)f[0] == 0xEF && (uint8_t)f[1] == 0xBB && (uint8_t)f[2] == 0xBF ) ) {
		prependCommand( wsw::StringView( f + 3 ) ); // skip Windows UTF-8 marker
	} else {
		prependCommand( wsw::StringView( f ) );
	}

	FS_FreeFile( f );
	Q_free( name );
}

void CmdSystem::helperForHandlerOfEcho( const CmdArgs &cmdArgs ) {
	checkCallingThread();
	for( size_t i = 1; i < cmdArgs.allArgs.size(); ++i ) {
		Com_Printf( "%s", cmdArgs.allArgs[i].data() );
	}
	Com_Printf( "\n" );
}

void CmdSystem::helperForHandlerOfVstr( const CmdArgs &cmdArgs ) {
	checkCallingThread();

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "vstr <variable> : execute a variable command\n" );
	} else {
		prependCommand( wsw::StringView( Cvar_String( Cmd_Argv( 1 ) ) ) );
	};
}

void CmdSystem::helperForHandlerOfWait( const CmdArgs & ) {
	checkCallingThread();
	m_interruptExecutionLoop = true;
}

[[maybe_unused]]
static auto writeAliasText( char *buffer, const CmdArgs &cmdArgs ) -> unsigned {
	unsigned totalTextSize = 0;
	for( int i = 2; i < cmdArgs.size(); ++i ) {
		const wsw::StringView &arg = cmdArgs[i];
		if( buffer ) {
			arg.copyTo( buffer + totalTextSize, arg.size() + 1 );
		}
		totalTextSize += arg.size();
		if( i + 1 < cmdArgs.size() ) {
			if( buffer ) {
				buffer[totalTextSize] = ' ';
			}
			totalTextSize++;
		}
	}
	if( buffer ) {
		buffer[totalTextSize] = '\0';
	}
	return totalTextSize;
}

void CmdSystem::helperForHandlerOfAlias( bool archive, const CmdArgs &cmdArgs ) {
	checkCallingThread();

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: alias <name> <command>\n" );
		return;
	}

	constexpr auto kMaxNameLength = 32u;
	constexpr auto kMaxTextSize   = 1024u;

	const wsw::StringView &name = cmdArgs[1];
	assert( name.isZeroTerminated() );
	if( name.length() > kMaxNameLength ) {
		Com_Printf( "Failed to register alias: the alias name is too long\n" );
		return;
	}

	if( Cvar_String( name.data() )[0] ) {
		Com_Printf( S_COLOR_RED "Failed to register alias: %s is already defined as a var\n", name.data() );
		return;
	}

	const wsw::HashedStringView nameAndHash( name );
	assert( nameAndHash.isZeroTerminated() );

	if( m_cmdEntries.findByName( nameAndHash ) ) {
		Com_Printf( S_COLOR_RED "Failed to register alias: %s is already defined as a command\n", nameAndHash.data() );
		return;
	}

	[[maybe_unused]] unsigned requiredTextSize = 0;
	if( Cmd_Argc() > 2 ) {
		requiredTextSize = writeAliasText( nullptr, cmdArgs );
		if( requiredTextSize > kMaxTextSize ) {
			Com_Printf( S_COLOR_RED "Failed to register alias: the alias text is too long\n" );
		}
	}

	AliasEntry *existing = m_aliasEntries.findByName( nameAndHash );
	if( existing ) {
		if( Cmd_Argc() == 2 ) {
			assert( existing->m_text.isZeroTerminated() );
			Com_Printf( "alias \"%s\" is \"%s\"\n", nameAndHash.data(), existing->m_text.data() );
			if( archive ) {
				existing->m_isArchive = true;
			}
			return;
		}
		existing = m_aliasEntries.releaseOwnership( existing );
	} else {
		if( Cmd_Argc() == 2 ) {
			Com_Printf( "Failed to register alias: empty text\n" );
			return;
		}
	}

	// Try reusing the same entry, as the reallocation could fail
	if( existing && existing->m_text.size() <= requiredTextSize ) {
		const auto textDataOffset  = (uintptr_t)existing->m_text.data() - (uintptr_t)existing;
		char *const writableText   = (char *)existing + textDataOffset;
		writeAliasText( writableText, cmdArgs );
		existing->m_text       = wsw::StringView( writableText, requiredTextSize, wsw::StringView::ZeroTerminated );
		existing->m_isArchive = archive;
		m_aliasEntries.insertUniqueTakingOwneship( existing );
		return;
	}

	// TODO: Try reallocating the old memory block?

	const size_t allocationSize = sizeof( AliasEntry ) + name.size() + 1 + requiredTextSize + 1;
	void *const mem = std::malloc( allocationSize );
	if( mem ) {
		if( existing ) {
			existing->~AliasEntry();
			std::free( existing );
		}
	} else {
		Com_Printf( "Failed to register alias: failed to allocate memory\n" );
		if( existing ) {
			// TODO: Should we keep it in this case?
			m_aliasEntries.insertUniqueTakingOwneship( existing );
		}
		return;
	}

	auto *const newEntry     = new( mem )AliasEntry;
	auto *const writableName = (char *)( newEntry + 1 );
	auto *const writableText = (char *)( newEntry + 1 ) + name.size() + 1;

	name.copyTo( writableName, name.size() + 1 );
	const unsigned actualTextSize = writeAliasText( writableText, cmdArgs );
	assert( actualTextSize == requiredTextSize );

	newEntry->m_nameAndHash = wsw::HashedStringView( writableName, name.size(), wsw::StringView::ZeroTerminated );
	newEntry->m_text        = wsw::StringView( writableText, actualTextSize, wsw::StringView::ZeroTerminated );
	newEntry->m_isArchive   = archive;

	m_aliasEntries.insertUniqueTakingOwneship( newEntry );
}

void CmdSystem::helperForHandlerOfUnalias( const CmdArgs &cmdArgs ) {
	checkCallingThread();

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: unalias <name>\n" );
		return;
	}

	for( int i = 1; i < cmdArgs.size(); ++i ) {
		const wsw::StringView &arg = cmdArgs[i];
		if( AliasEntry *entry = m_aliasEntries.findByName( wsw::HashedStringView( arg ) ) ) {
			m_aliasEntries.remove( entry );
		} else {
			assert( arg.isZeroTerminated() );
			Com_Printf( "Failed to unalias \"%s\": not found\n", arg.data() );
		}
	}
}

void CmdSystem::helperForHandlerOfUnaliasall( const CmdArgs &cmdArgs ) {
	checkCallingThread();
	m_aliasEntries.clear();
}