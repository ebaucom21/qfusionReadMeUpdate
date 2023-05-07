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

#ifndef WSW_a84d2200_4f2e_4b79_bc26_b210c74cbdb6_H
#define WSW_a84d2200_4f2e_4b79_bc26_b210c74cbdb6_H

#include "cmdargssplitter.h"
#include "cmdcompat.h"
#include "wswstringview.h"

class CmdSystem {
public:
	virtual ~CmdSystem();

	// TODO: Check whether no command text is submitted partially, track submitted boundaries

	void prependCommand( const wsw::StringView &text ) {
		m_textBuffer.prepend( text );
	}
	void appendCommand( const wsw::StringView &text ) {
		m_textBuffer.append( text );
	}

	void executeNow( const wsw::StringView &text );

	[[maybe_unused]]
	virtual bool registerCommand( const wsw::StringView &name, CmdFunc cmdFunc );
	[[maybe_unused]]
	virtual bool unregisterCommand( const wsw::StringView &name );

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

	virtual void registerSystemCommands() = 0;
	virtual void unregisterSystemCommands();

	static void classifyExecutableCmdArgs( int argc, char **argv,
										   wsw::Vector<wsw::StringView> *setArgs,
										   wsw::Vector<wsw::StringView> *setAndExecArgs,
										   wsw::Vector<std::optional<wsw::StringView>> *otherArgs );

	void appendEarlySetCommands( std::span<const wsw::StringView> args );
	void appendEarlySetAndExecCommands( std::span<const wsw::StringView> args );
	void appendLateCommands( std::span<const std::optional<wsw::StringView>> args );
protected:
	void helperForHandlerOfExec( const CmdArgs &cmdArgs );
	void helperForHandlerOfEcho( const CmdArgs &cmdArgs );
	void helperForHandlerOfAlias( bool archive, const CmdArgs &cmdArgs );
	void helperForHandlerOfUnalias( const CmdArgs &cmdArgs );
	void helperForHandlerOfUnaliasall( const CmdArgs &cmdArgs );
	void helperForHandlerOfWait( const CmdArgs &cmdArgs );
	void helperForHandlerOfVstr( const CmdArgs &cmdArgs );

	struct CmdEntry {
		CmdEntry *prev { nullptr }, *next { nullptr };
		wsw::HashedStringView nameAndHash;
		unsigned binIndex;
		CmdFunc cmdFunc { nullptr };
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

	class TextBuffer {
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

	CmdEntry *m_cmdEntryBins[37] {};
	AliasEntry *m_aliasEntryBins[37] {};

	TextBuffer m_textBuffer;
	CmdArgsSplitter m_argsSplitter;

	unsigned m_aliasRecursionDepth { 0 };
	bool m_interruptExecutionLoop { false };
};

#endif