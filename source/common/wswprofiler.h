/*
Copyright (C) 2024-2025 Chasseur de bots

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

#ifndef WSW_462961de_963c_4da6_b80b_b0ff67392202_H
#define WSW_462961de_963c_4da6_b80b_b0ff67392202_H

#include "wswstringview.h"

namespace wsw {

class ProfilerScope {
public:
	explicit ProfilerScope( const wsw::HashedStringView &name );
	~ProfilerScope();

	ProfilerScope( const ProfilerScope & ) = delete;
	auto operator=( const ProfilerScope & ) -> ProfilerScope & = delete;
	ProfilerScope( ProfilerScope && ) = delete;
	auto operator=( ProfilerScope && ) -> ProfilerScope & = delete;
private:
	wsw::HashedStringView m_name;
};

class ProfilerThreadInstance;

class ProfilingSystem {
public:
	friend class ProfilerScope;
	friend class ThreadProfilingAttachment;

	enum FrameGroup { ClientGroup, ServerGroup };

	static void attachToThisThread( FrameGroup group );
	static void detachFromThisThread( FrameGroup group );

	static void beginFrame( FrameGroup group, const wsw::StringView &targetName );
	static void endFrame( FrameGroup group );
private:
	// Per-group
	static class ProfilerThreadInstance *s_instances[2];
	static volatile unsigned s_isProfilingEnabled[2];
};

class ThreadProfilingAttachment {
public:
	ThreadProfilingAttachment( ProfilingSystem::FrameGroup group ) : m_group( group ) {
		ProfilingSystem::attachToThisThread( group );
	}
	~ThreadProfilingAttachment() {
		ProfilingSystem::detachFromThisThread( m_group );
	}

	ThreadProfilingAttachment( const ThreadProfilingAttachment & ) = delete;
	auto operator=( const ThreadProfilingAttachment & ) -> ThreadProfilingAttachment & = delete;
	ThreadProfilingAttachment( ThreadProfilingAttachment && ) = delete;
	auto operator=( ThreadProfilingAttachment && ) -> ThreadProfilingAttachment & = delete;
private:
	const ProfilingSystem::FrameGroup m_group;
};

}

#endif