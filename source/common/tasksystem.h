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

#ifndef WSW_1592e913_d661_4bb3_bab4_1e7b665d1c0c_H
#define WSW_1592e913_d661_4bb3_bab4_1e7b665d1c0c_H

#include "taskhandle.h"

#include <cassert>
#include <cstdlib>
#include <limits>
#include <span>
#include <utility>

class TaskSystem {
	friend struct TaskSystemImpl;

	struct TapeCallable {
		virtual ~TapeCallable() = default;
		virtual void call( unsigned workerIndex, unsigned entryInstanceArg ) = 0;
	};

	struct TapeStateGuard {
		struct TaskSystemImpl *const impl;
		explicit TapeStateGuard( TaskSystemImpl *impl ) : impl( impl ) {
			beginTapeModification( impl );
		}
		~TapeStateGuard() {
			endTapeModification( impl, succeeded );
		}
		bool succeeded { false };
	};
public:
	struct CtorArgs { size_t numExtraThreads; };
	explicit TaskSystem( CtorArgs &&args );
	~TaskSystem();
	// Returns a total number of threads which may execute, including the main (std::this_thread) thread
	[[nodiscard]]
	auto getNumberOfWorkers() const -> unsigned;

	static constexpr unsigned kMaxTaskEntries = std::numeric_limits<int16_t>::max();

	enum Affinity : uint8_t { AnyThread, OnlyThisThread };

	template <typename Callable>
	[[nodiscard]]
	auto add( std::initializer_list<TaskHandle> deps, Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		return add( std::forward<Callable>( callable ), deps.begin(), deps.end(), affinity );
	}

	template <typename Callable>
	[[nodiscard]]
	auto add( std::span<const TaskHandle> deps, Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		// This is a specialized replacement for std::function which should not have place in this omnipresent header
		struct TapeCallableImpl final : public TapeCallable {
			explicit TapeCallableImpl( Callable &&c ) : m_callable( std::forward<Callable>( c ) ) {}
			~TapeCallableImpl() override = default;
			void call( unsigned workerIndex, unsigned ) override {
				m_callable( workerIndex );
			}
			Callable m_callable;
		};

		[[maybe_unused]] volatile TapeStateGuard tapeStateGuard( m_impl );

		auto [mem, task] = addImpl( deps, alignof( TapeCallableImpl ), sizeof( TapeCallableImpl ), affinity );

		tapeStateGuard.succeeded = true;
		// Construct it last in nothrowing fashion
		static_assert( std::is_nothrow_move_constructible_v<Callable> );
		new( mem )TapeCallableImpl( std::forward<Callable>( callable ) );

		// The tape unlocks here
		return task;
	}

	template <typename Callable>
	[[nodiscard]]
	auto addForIndicesInRange( std::pair<unsigned, unsigned> indicesRange, std::initializer_list<TaskHandle> deps,
							   Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		return addForIndicesInRange( indicesRange, deps.begin(), deps.end(), std::forward<Callable>( callable ), affinity );
	}

	template <typename Callable>
	[[nodiscard]]
	auto addForIndicesInRange( std::pair<unsigned, unsigned> indicesRange,
							   std::span<const TaskHandle> deps,
							   Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		// This is a specialized replacement for std::function which should not have place in this omnipresent header
		struct TapeCallableImpl final : public TapeCallable {
			explicit TapeCallableImpl( Callable &&c ) : m_callable( std::forward<Callable>( c ) ) {}
			~TapeCallableImpl() override = default;
			void call( unsigned workerIndex, unsigned entryInstanceArg ) override {
				m_callable( workerIndex, entryInstanceArg );
			}
			Callable m_callable;
		};

		[[maybe_unused]] volatile TapeStateGuard tapeStateGuard( m_impl );

		auto [mem, task] = addForIndicesInRangeImpl( indicesRange, deps, sizeof( TapeCallableImpl ),
													 alignof( TapeCallableImpl ), affinity );

		tapeStateGuard.succeeded = true;
		// Construct it last in a nothrowing fashion
		static_assert( std::is_nothrow_move_constructible_v<Callable> );
		new( mem )TapeCallableImpl( std::forward<Callable>( callable ) );

		// The tape unlocks here
		return task;
	}

	template <typename Callable>
	[[nodiscard]]
	auto addForSubrangesInRange( std::pair<unsigned, unsigned> indicesRange, unsigned subrangeLength,
								 std::initializer_list<TaskHandle> deps,
								 Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		return addForSubrangesInRange( indicesRange, subrangeLength, { deps.begin(), deps.end() },
									   std::forward<Callable>( callable ), affinity );
	}

	template <typename Callable>
	[[nodiscard]]
	auto addForSubrangesInRange( std::pair<unsigned, unsigned> indicesRange,
								 unsigned subrangeLength, std::span<const TaskHandle> deps,
								 Callable &&callable, Affinity affinity = AnyThread ) -> TaskHandle {
		// This is a specialized replacement for std::function which should not have place in this omnipresent header
		struct TapeCallableImpl final : public TapeCallable {
			explicit TapeCallableImpl( Callable &&c ) : m_callable( std::forward<Callable>( c ) ) {}
			~TapeCallableImpl() override = default;
			void call( unsigned workerIndex, unsigned entryInstanceArg ) override {
				const unsigned beginIndex = entryInstanceArg >> 16;
				const unsigned endIndex   = entryInstanceArg & 0xFFFF;
				m_callable( workerIndex, beginIndex, endIndex );
			}
			Callable m_callable;
		};

		[[maybe_unused]] volatile TapeStateGuard tapeStateGuard( m_impl );

		auto [mem, task] = addForSubrangesInRangeImpl( indicesRange, subrangeLength, deps,
													   alignof( TapeCallableImpl ), sizeof( TapeCallableImpl ), affinity );

		tapeStateGuard.succeeded = true;
		// Construct it last in a nothrowing fashion
		static_assert( std::is_nothrow_move_constructible_v<Callable> );
		new( mem )TapeCallableImpl( std::forward<Callable>( callable ) );

		// The tape unlocks here
		return task;
	}

	void startExecution();
	[[nodiscard]] bool awaitCompletion();
private:
	enum CompletionStatus : unsigned { CompletionPending, CompletionSuccess, CompletionFailure };
	enum DependencyAddressMode : uint8_t { RangeOfEntries, RangeOfEntryIndices };

	void clear();
	void awakeWorkers();

	[[nodiscard]]
	auto addImpl( std::span<const TaskHandle> deps, size_t alignment, size_t size, Affinity affinity ) -> std::pair<void *, TaskHandle>;

	[[nodiscard]]
	auto addForIndicesInRangeImpl( std::pair<unsigned, unsigned> indicesRange, std::span<const TaskHandle> deps,
								   size_t alignment, size_t size, Affinity affinity ) -> std::pair<void *, TaskHandle>;

	[[nodiscard]]
	auto addForSubrangesInRangeImpl( std::pair<unsigned, unsigned> indicesRange,
									 unsigned subrangeLength, std::span<const TaskHandle> deps,
									 size_t alignment, size_t size, Affinity affinity ) -> std::pair<void *, TaskHandle>;

	[[nodiscard]]
	auto allocMemForCallable( size_t alignment, size_t size ) -> std::pair<void *, unsigned>;

	[[nodiscard]]
	auto addEntry( Affinity affinity, unsigned offsetOfCallable ) -> TaskHandle;

	void addPolledDependenciesToEntry( TaskHandle taskHandle, const TaskHandle *depsBegin, const TaskHandle *depsEnd );
	void addPushedDependentsToEntry( TaskHandle taskHandle, unsigned taskRangeBegin, unsigned taskRangeEnd );

	[[nodiscard]]
	auto addParallelAndJoinEntries( Affinity affinity, unsigned offsetOfCallable, unsigned numTasks )
		-> std::pair<std::pair<unsigned, unsigned>, TaskHandle>;

	void setupIotaInstanceArgs( std::pair<unsigned, unsigned> taskRange, unsigned startValue );
	void setupRangeInstanceArgs( std::pair<unsigned, unsigned> taskRange, unsigned startValue,
								 unsigned subrangeLength, unsigned totalWorkload );

	static void beginTapeModification( struct TaskSystemImpl * );
	static void endTapeModification( struct TaskSystemImpl *, bool succeeded );

	static void threadLoopFunc( struct TaskSystemImpl *impl, unsigned threadNumber );
	[[nodiscard]]
	static bool threadExecTasks( struct TaskSystemImpl *impl, unsigned threadNumber );

	struct TaskSystemImpl *m_impl;
};

#endif