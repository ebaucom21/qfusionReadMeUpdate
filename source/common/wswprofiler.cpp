/*
Copyright (C) 2024 Chasseur de bots

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

#include "wswprofiler.h"
#include "wswpodvector.h"
#include "wswexceptions.h"
#include "links.h"
#include "local.h"
#include "common.h"

#include <unordered_map>

template <>
struct std::hash<wsw::HashedStringView> {
	[[nodiscard]]
	auto operator()( const wsw::HashedStringView &view ) const noexcept -> std::size_t {
		return view.getHash();
	}
};

namespace wsw {

class ProfilerThreadInstance {
public:
	friend class ProfilerScope;
	friend class ProfilingSystem;

	ProfilerThreadInstance *prev { nullptr }, *next { nullptr };

	void enterScope( ProfilerScope *scope, const wsw::HashedStringView &name );
	void leaveScope( ProfilerScope *scope, const wsw::HashedStringView &name );

	void resetFrameStats();

	struct DescendantEntry {
		uint64_t enterTimestamp { 0 };
		uint64_t accumTime { 0 };
		int enterCount { 0 };
	};

	// Reset each frame
	wsw::PodVector<char> m_targetName;

	uint64_t m_enterTimestamp { 0 };
	uint64_t m_accumTime { 0 };
	int m_enterCount { 0 };

	int m_targetScopeReentrancyCounter { 0 };
	int m_scopeDepthFromTheTargetScope { 0 };

	std::unordered_map<wsw::HashedStringView, DescendantEntry> m_statsOfDescendantScopes;
};

void ProfilerThreadInstance::resetFrameStats() {
	if( m_enterCount > 0 ) {
		comNotice() << "(*)" << m_targetName << ": enter count" << m_enterCount << "accum time" << m_accumTime;
		for( const auto &[k, v] : m_statsOfDescendantScopes ) {
			comNotice() << " |-" << k << ": enter count" << v.enterCount << "accum time" << v.accumTime;
		}
	}

	m_targetName.clear();

	m_enterTimestamp = 0;
	m_accumTime      = 0;
	m_enterCount     = 0;

	m_targetScopeReentrancyCounter = 0;
	m_scopeDepthFromTheTargetScope = 0;

	m_statsOfDescendantScopes.clear();
}

static thread_local ProfilerThreadInstance *tl_profilerThreadInstance;

ProfilerScope::ProfilerScope( const wsw::HashedStringView &name ) : m_name( name ) {
	if( ProfilingSystem::s_isProfilingEnabled ) {
		if( ProfilerThreadInstance *instance = tl_profilerThreadInstance ) {
			instance->enterScope( this, name );
		}
	}
}

ProfilerScope::~ProfilerScope() {
	if( ProfilingSystem::s_isProfilingEnabled ) {
		if( ProfilerThreadInstance *instance = tl_profilerThreadInstance ) {
			instance->leaveScope( this, m_name );
		}
	}
}

ProfilerThreadInstance *ProfilingSystem::s_instances { nullptr };
volatile bool ProfilingSystem::s_isProfilingEnabled { false };

void ProfilingSystem::attachToThisThread() {
	if( tl_profilerThreadInstance ) {
		wsw::failWithLogicError( "Already attached to this thread" );
	}
	auto *newInstance = new ProfilerThreadInstance;
	wsw::link( newInstance, &s_instances );
	tl_profilerThreadInstance = newInstance;
}

void ProfilingSystem::detachFromThisThread() {
	if( auto *instance = tl_profilerThreadInstance ) {
		wsw::unlink( instance, &s_instances );
		delete instance;
		tl_profilerThreadInstance = nullptr;
	}
}

void ProfilingSystem::beginFrame( const wsw::StringView &targetName ) {
	if( !targetName.empty() ) {
		for( ProfilerThreadInstance *instance = s_instances; instance; instance = instance->next ) {
			instance->m_targetName.assign( targetName );
		}
		s_isProfilingEnabled = true;
	} else {
		s_isProfilingEnabled = false;
	}
}

void ProfilingSystem::endFrame() {
	for( ProfilerThreadInstance *instance = s_instances; instance; instance = instance->next ) {
		instance->resetFrameStats();
	}
}

[[nodiscard]]
static inline bool matchesName( const wsw::PodVector<char> &expected, const wsw::StringView &given ) {
	return expected.size() == given.size() && std::memcmp( expected.data(), given.data(), expected.size() ) == 0;
}

void ProfilerThreadInstance::enterScope( ProfilerScope *, const wsw::HashedStringView &name ) {
	// If we are in the target scope
	if( m_targetScopeReentrancyCounter > 0 ) {
		m_scopeDepthFromTheTargetScope++;
	}
	if( matchesName( m_targetName, name ) ) {
		m_targetScopeReentrancyCounter++;
		if( m_targetScopeReentrancyCounter == 1 ) {
			m_enterTimestamp = Sys_Microseconds();
			m_enterCount++;
		}
	} else {
		// If it's a direct call from the target scope
		if( m_scopeDepthFromTheTargetScope == 1 ) {
			DescendantEntry &entry = m_statsOfDescendantScopes[name];
			entry.enterTimestamp = Sys_Microseconds();
			entry.enterCount++;
		}
	}
}

void ProfilerThreadInstance::leaveScope( ProfilerScope *, const wsw::HashedStringView &name ) {
	// If we are in the target scope
	const bool isInTargetScope = m_targetScopeReentrancyCounter > 0;
	if( isInTargetScope ) {
		m_scopeDepthFromTheTargetScope--;
	}
	if( matchesName( m_targetName, name ) ) {
		m_targetScopeReentrancyCounter--;
		if( m_targetScopeReentrancyCounter == 0 ) {
			m_accumTime += ( Sys_Microseconds() - m_enterTimestamp );
		}
	} else {
		// If it's a direct call from the target scope
		if( isInTargetScope && m_scopeDepthFromTheTargetScope == 0 ) {
			DescendantEntry &entry = m_statsOfDescendantScopes[name];
			entry.accumTime += ( Sys_Microseconds() - entry.enterTimestamp );
		}
	}
}

}