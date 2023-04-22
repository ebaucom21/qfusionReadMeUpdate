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
				executeNow( forwardingBuffer.asView() );
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

void CmdSystem::unregisterSystemCommands() {
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
	assert( args.size() % 3 == 0 );
	for( size_t i = 0; i < args.size(); i += 3 ) {
		wsw::StaticString<MAX_TOKEN_CHARS> text;
		text << args[i + 0] << " \""_asView << args[i + 1] << "\" \""_asView << args[i + 2] << "\"\n"_asView;
		appendCommand( text.asView() );
	}
}

void CmdSystem::appendEarlySetAndExecCommands( std::span<const wsw::StringView> args ) {
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
	for( size_t i = 1; i < cmdArgs.allArgs.size(); ++i ) {
		Com_Printf( "%s", cmdArgs.allArgs[i].data() );
	}
	Com_Printf( "\n" );
}

void CmdSystem::helperForHandlerOfVstr( const CmdArgs &cmdArgs ) {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "vstr <variable> : execute a variable command\n" );
	} else {
		prependCommand( wsw::StringView( Cvar_String( Cmd_Argv( 1 ) ) ) );
	};
}

void CmdSystem::helperForHandlerOfWait( const CmdArgs & ) {
	m_interruptExecutionLoop = true;
}

void CmdSystem::helperForHandlerOfAlias( bool archive, const CmdArgs & ) {
	;
}

void CmdSystem::helperForHandlerOfUnalias( const CmdArgs & ) {
	;
}

void CmdSystem::helperForHandlerOfUnaliasall( const CmdArgs &cmdArgs ) {
	;
}