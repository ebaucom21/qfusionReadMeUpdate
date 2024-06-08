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

#include "tasksystem.h"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
#include <deque>
#include <variant>
#include <span>
#include <mutex>
#include <barrier>

struct TaskSystemImpl {
	explicit TaskSystemImpl( unsigned numExtraThreads );
	~TaskSystemImpl();

	struct InlineDependentsArray {
		uint16_t values[11];
		uint16_t count {0};
	};

	struct TaskEntry {
		enum Status : unsigned { Pending, Busy, Completed };
		unsigned numSatisfiedDependencies { 0 };
		unsigned status { Pending };
		unsigned numDependencies { 0 };
		unsigned offsetOfCallable { 0 };
		TaskSystem::Affinity affinity { TaskSystem::AnyThread };
		std::variant<std::monostate, InlineDependentsArray, std::vector<uint16_t>> dependents {};

		[[nodiscard]]
		auto getDependents() const -> std::span<const uint16_t> {
			if( auto *inlineDependents = std::get_if<InlineDependentsArray>( &this->dependents ) ) {
				return { inlineDependents->values, inlineDependents->count };
			} else if( auto *dependentsVector = std::get_if<std::vector<uint16_t>>( &this->dependents ) ) {
				return *dependentsVector;
			} else {
				return {};
			}
		}
	};

	// A separate storage for large callables.
	// Callables and task entries may reside in the same buffer, but it (as a late addition) complicates existing code.
	// Preventing use of std::function<> in public API is primary reason of using custom allocation of callables.
	uint8_t *memOfCallables { nullptr };
	// In bytes
	unsigned sizeOfUsedMemOfCallables { 0 };
	unsigned capacityOfMemOfCallables { 0 };

	std::barrier<decltype( []() noexcept {} )> barrier;

	std::vector<TaskEntry> entries;
	std::vector<std::jthread> threads;
	std::deque<std::atomic_flag> signalingFlags;
	std::deque<std::atomic<unsigned>> completionStatuses;
	std::atomic<unsigned> startScanFrom { 0 };
	std::mutex globalMutex;
	std::atomic<bool> isExecuting { false };
	std::atomic<bool> isShuttingDown { false };
	std::vector<std::vector<uint16_t>> boxedDependentsCache;
	bool hasBoxedDependents { false };
};

TaskSystemImpl::TaskSystemImpl( unsigned numExtraThreads ) : barrier( numExtraThreads + 1 ) {
	signalingFlags.resize( numExtraThreads );
	completionStatuses.resize( numExtraThreads );

	// Spawn threads once we're done with vars
	for( unsigned threadNumber = 0; threadNumber < numExtraThreads; ++threadNumber ) {
		threads.emplace_back( std::jthread( TaskSystem::threadLoopFunc, this, threadNumber ) );
	}

	sizeOfUsedMemOfCallables = 0;
	capacityOfMemOfCallables = 8192;
	memOfCallables           = (uint8_t*)std::malloc( capacityOfMemOfCallables );
	if( !memOfCallables ) [[unlikely]] {
		throw std::bad_alloc();
	}
}

TaskSystemImpl::~TaskSystemImpl() {
	std::free( memOfCallables );
}

TaskSystem::TaskSystem( CtorArgs &&args ) {
	m_impl = new TaskSystemImpl( args.numExtraThreads );
}

TaskSystem::~TaskSystem() {
	m_impl->isShuttingDown.store( true, std::memory_order_seq_cst );
	// Awake all threads
	for( std::atomic_flag &flag: m_impl->signalingFlags ) {
		flag.test_and_set();
		flag.notify_one();
	}
	for( std::jthread &thread: m_impl->threads ) {
		thread.join();
	}
	delete m_impl;
}

auto TaskSystem::getNumberOfWorkers() const -> unsigned {
	return m_impl->threads.size() + 1;
}

auto TaskSystem::addEntryAndAllocCallableMem( Affinity affinity, size_t alignment, size_t size ) -> std::pair<void *, TaskHandle> {
	assert( !m_impl->isExecuting );
	assert( m_impl->entries.size() < std::numeric_limits<uint16_t>::max() );
	assert( alignment && size );

	uintptr_t alignmentBytes   = 0;
	uintptr_t actualTopAddress = (uintptr_t)m_impl->memOfCallables + m_impl->sizeOfUsedMemOfCallables;
	if( !( alignment & ( alignment - 1 ) ) ) [[likely]] {
		// TODO: Can be branchless
		if( auto rem = actualTopAddress & ( alignment - 1 ) ) {
			alignmentBytes += alignment - rem;
		}
	} else {
		if( auto rem = actualTopAddress % alignment ) {
			alignmentBytes += alignment - rem;
		}
	}

	const size_t requiredMemSize = size + alignmentBytes;
	if( m_impl->sizeOfUsedMemOfCallables + requiredMemSize > m_impl->capacityOfMemOfCallables ) [[unlikely]] {
		// We cannot use realloc() as callables are not guaranteed to be trivially relocatable
		// TODO: Check growth policy
		const size_t newCapacity = ( 3 * ( m_impl->sizeOfUsedMemOfCallables + requiredMemSize ) ) / 2;
		auto *const newMem       = (uint8_t *)std::malloc( newCapacity );
		if( !newMem ) [[unlikely]] {
			throw std::bad_alloc();
		}
		for( TaskSystemImpl::TaskEntry &entry: m_impl->entries ) {
			auto *oldStorage = ( CallableStorage *)( m_impl->memOfCallables + entry.offsetOfCallable );
			oldStorage->moveSelfTo( newMem + entry.offsetOfCallable );
		}
		std::free( m_impl->memOfCallables );
		m_impl->memOfCallables           = newMem;
		m_impl->capacityOfMemOfCallables = newCapacity;
	}

	const unsigned offsetOfCallable = m_impl->sizeOfUsedMemOfCallables + alignmentBytes;
	m_impl->sizeOfUsedMemOfCallables += requiredMemSize;

	m_impl->entries.emplace_back( TaskSystemImpl::TaskEntry {
		.offsetOfCallable = offsetOfCallable,
		.affinity         = affinity,
	});

	TaskHandle resultHandle;
	resultHandle.m_opaque = m_impl->entries.size();
	return { (void *)( m_impl->memOfCallables + offsetOfCallable ), resultHandle };
}

void TaskSystem::scheduleInOrder( TaskHandle former, TaskHandle latter ) {
	assert( former.m_opaque > 0 && former.m_opaque <= (uintptr_t)std::numeric_limits<uint16_t>::max() + 1 );
	assert( latter.m_opaque > 0 && latter.m_opaque <= (uintptr_t)std::numeric_limits<uint16_t>::max() + 1 );
	assert( former.m_opaque != latter.m_opaque );

	TaskSystemImpl::TaskEntry &entry = m_impl->entries[former.m_opaque - 1];
	const auto latterIndex           = (uint16_t)( latter.m_opaque - 1 );

	if( auto *const inlineDependents = std::get_if<TaskSystemImpl::InlineDependentsArray>( &entry.dependents ) ) {
		if( inlineDependents->count < std::size( inlineDependents->values ) ) {
			inlineDependents->values[inlineDependents->count++] = latterIndex;
		} else {
			std::vector<uint16_t> boxed;
			if( !m_impl->boxedDependentsCache.empty() ) {
				boxed = std::move( m_impl->boxedDependentsCache.back() );
				m_impl->boxedDependentsCache.pop_back();
				boxed.clear();
			}
			boxed.assign( inlineDependents->values, inlineDependents->values + inlineDependents->count );
			boxed.push_back( latterIndex );
			entry.dependents           = std::move( boxed );
			m_impl->hasBoxedDependents = true;
		}
	} else if( auto *const dependentsVector = std::get_if<std::vector<uint16_t>>( &entry.dependents ) ) {
		dependentsVector->push_back( latterIndex );
	} else {
		TaskSystemImpl::InlineDependentsArray dependents;
		dependents.values[0] = latterIndex;
		dependents.count     = 1;
		entry.dependents     = dependents;
	}
}

void TaskSystem::clear() {
	assert( !m_impl->isExecuting );
	if( m_impl->hasBoxedDependents ) {
		for( TaskSystemImpl::TaskEntry &entry: m_impl->entries ) {
			if( auto *boxed = std::get_if<std::vector<uint16_t>>( &entry.dependents ) ) {
				m_impl->boxedDependentsCache.emplace_back( std::move( *boxed ) );
			}
			( ( CallableStorage *)( m_impl->memOfCallables + entry.offsetOfCallable ) )->~CallableStorage();
		}
		m_impl->hasBoxedDependents = false;
	} else {
		for( TaskSystemImpl::TaskEntry &entry: m_impl->entries ) {
			( ( CallableStorage *)( m_impl->memOfCallables + entry.offsetOfCallable ) )->~CallableStorage();
		}
	}
	m_impl->entries.clear();
	m_impl->sizeOfUsedMemOfCallables = 0;
}

bool TaskSystem::exec() {
	assert( !m_impl->isExecuting );
	assert( m_impl->entries.size() <= std::numeric_limits<uint16_t>::max() );

	bool succeeded = true;
	if( !m_impl->entries.empty() ) {
		m_impl->isExecuting   = true;
		m_impl->startScanFrom = 0;

		// TODO: Validate the graph

		for( TaskSystemImpl::TaskEntry &entry: m_impl->entries ) {
			for( uint16_t dependentIndex: entry.getDependents() ) {
				assert( m_impl->entries[dependentIndex].numSatisfiedDependencies == 0 );
			}
			for( uint16_t dependentIndex: entry.getDependents() ) {
				m_impl->entries[dependentIndex].numDependencies++;
			}
		}

		std::fill( m_impl->completionStatuses.begin(), m_impl->completionStatuses.end(), CompletionPending );

		// Awake all threads
		// TODO: Is there something smarter
		for( std::atomic_flag &flag: m_impl->signalingFlags ) {
			flag.test_and_set();
			flag.notify_one();
		}

		// Run in this thread as well
		succeeded = threadExecTasks( m_impl, ~0u );

		m_impl->barrier.arrive_and_wait();

		if( succeeded ) {
			auto it = m_impl->completionStatuses.begin();
			for(; it != m_impl->completionStatuses.end(); ++it ) {
				const unsigned status = (*it).load( std::memory_order_relaxed );
				assert( status != CompletionPending );
				if( status != CompletionSuccess ) {
					succeeded = false;
					break;
				}
			}
		}

		m_impl->isExecuting.store( false, std::memory_order_seq_cst );

		clear();
	}

	return succeeded;
}

void TaskSystem::threadLoopFunc( TaskSystemImpl *__restrict impl, unsigned threadNumber ) {
	for(;; ) {
		impl->signalingFlags[threadNumber].wait( false );
		impl->signalingFlags[threadNumber].clear();

		if( impl->isShuttingDown.load( std::memory_order_seq_cst ) ) {
			break;
		}

		const auto status = threadExecTasks( impl, threadNumber ) ? CompletionSuccess : CompletionFailure;
		impl->completionStatuses[threadNumber].store( status, std::memory_order_seq_cst );
		(void)impl->barrier.arrive( 1 );
	}
}

bool TaskSystem::threadExecTasks( TaskSystemImpl *__restrict impl, unsigned threadNumber ) {
	try {
		TaskSystemImpl::TaskEntry *const __restrict entries = impl->entries.data();
		const size_t numEntries                             = impl->entries.size();

		constexpr auto kIndexUnset    = (size_t)-1;
		size_t pendingCompletionIndex = kIndexUnset;
		for(;; ) {
			size_t chosenIndex      = kIndexUnset;
			bool isInNonPendingSpan = true;
			do {
				[[maybe_unused]] volatile std::scoped_lock<std::mutex> lock( impl->globalMutex );

				if( pendingCompletionIndex != kIndexUnset ) {
					TaskSystemImpl::TaskEntry &entry = entries[pendingCompletionIndex];
					// Safe under mutex
					entry.status = TaskSystemImpl::TaskEntry::Completed;
					for( uint16_t dependentIndex: entry.getDependents() ) {
						TaskSystemImpl::TaskEntry &dependent = entries[dependentIndex];
						// Safe under mutex
						dependent.numSatisfiedDependencies++;
					}
					pendingCompletionIndex = kIndexUnset;
				}

				// This is similar to scanning the monotonic array-based heap in the AAS routing subsystem
				size_t entryIndex = impl->startScanFrom.load( std::memory_order_relaxed );
				for(; entryIndex < numEntries; ++entryIndex ) {
					// Safe under mutex
					TaskSystemImpl::TaskEntry &entry = entries[entryIndex];
					const unsigned status            = entry.status;
					if( status == TaskSystemImpl::TaskEntry::Pending ) {
						if( isInNonPendingSpan ) {
							impl->startScanFrom.store( entryIndex, std::memory_order_relaxed );
							isInNonPendingSpan = false;
						}
						if( entry.affinity == AnyThread || threadNumber == ~0u ) {
							const unsigned numSatisfiedDeps = entry.numSatisfiedDependencies;
							assert( numSatisfiedDeps <= entry.numDependencies );
							if( numSatisfiedDeps == entry.numDependencies ) {
								// Safe under mutex
								entry.status = TaskSystemImpl::TaskEntry::Busy;
								chosenIndex  = entryIndex;
								break;
							}
						}
					}
				}
				// The global mutex unlocks here
			} while( false );

			if( chosenIndex != kIndexUnset ) {
				auto *callable = (CallableStorage *)( impl->memOfCallables + entries[chosenIndex].offsetOfCallable );
				const unsigned workerIndex = threadNumber + 1;
				callable->call( workerIndex );
				// Postpone completion so we don't have to lock twice
				pendingCompletionIndex = chosenIndex;
			} else {
				// If all tasks are completed or busy
				if( isInNonPendingSpan ) {
					break;
				}
			}
		}

		assert( pendingCompletionIndex == kIndexUnset );
		return true;
	} catch( ... ) {
		return false;
	}
}