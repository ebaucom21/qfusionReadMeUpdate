#include "snd_computation_host.h"
#include "snd_local.h"

#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"

#include <thread>

static SingletonHolder<ParallelComputationHost> instanceHolder;

ParallelComputationHost *ParallelComputationHost::Instance() {
	return instanceHolder.instance();
};

void ParallelComputationHost::Init() {
	instanceHolder.init();
}

void ParallelComputationHost::Shutdown() {
	instanceHolder.shutdown();
}

int ParallelComputationHost::SuggestNumberOfTasks() {
	unsigned numPhysicalProcessors, numLogicalProcessors;
	if( !Sys_GetNumberOfProcessors( &numPhysicalProcessors, &numLogicalProcessors ) ) {
		return 2;
	}
	if( numLogicalProcessors > numPhysicalProcessors * 2 ) {
		// We're running it at some weird machine, let's try
		return numLogicalProcessors;
	}
	if( numLogicalProcessors == numPhysicalProcessors * 2 ) {
		// A widely available implementation of HT.
		// Let's use all logical cores except a single one.
		// There's going to be lots of cache misses so utilizing HT can be beneficial.
		return wsw::max( 2u, numLogicalProcessors - 1 );
	}
	return numPhysicalProcessors;
}

void *TaskThreadFunc( void *param ) {
	auto *const task = (ParallelComputationHost::PartialTask *)param;
	// TODO: This is not that bad as it gets started almost immediately but still...
	while( !task->hasStarted.load( std::memory_order_relaxed ) ) {
		QThread_Yield();
	}

	task->Exec();

	task->isCompleted.store( true, std::memory_order_relaxed );
	return nullptr;
}

bool ParallelComputationHost::TryAddTask( PartialTask *task ) {
	assert( !isRunning );

	// If there is no tasks, just link the task.
	// It will be executed in a caller thread even if other tasks are added.
	if( !tasksHead ) {
		wsw::link( task, &tasksHead, 0 );
		return true;
	}

	assert( !task->hasStarted && !task->isCompleted );

	auto *threadHandle = QThread_Create( &TaskThreadFunc, task );
	if( !threadHandle ) {
		// Destroy the task immediately as we always acquire an ownership over the task lifetime.
		DestroyTask( task );
		return false;
	}

	task->threadHandle = threadHandle;
	wsw::link( task, &tasksHead, 0 );
	return true;
}

void ParallelComputationHost::Exec() {
	assert( !isRunning );

	if( !tasksHead ) {
		return;
	}

	isRunning = true;

	PartialTask *thisThreadTask = nullptr;
	// Launch all tasks if their own
	for( auto *task = tasksHead; task; task = task->Next() ) {
		if( task->threadHandle ) {
			task->hasStarted.store( true, std::memory_order_relaxed );
			continue;
		}
		assert( !thisThreadTask );
		thisThreadTask = task;
	}

	assert( thisThreadTask );
	// Execute the single task without its thread in the caller thread
	thisThreadTask->Exec();

	// Wait for completion of other tasks.
	// As a CPU workload of tasks is assumed to be +/- equal, this should not take long.
	WaitForTasksCompletion( thisThreadTask );
	DestroyHeldTasks();
	// We're ready for another batch of tasks
	isRunning = false;
}

void ParallelComputationHost::WaitForTasksCompletion( const PartialTask *thisThreadTask ) {
	for(;; ) {
		if( AreAllTasksCompleted( thisThreadTask ) ) {
			return;
		}
		// Should not really be bad as all tasks are going to be complete on the first iteration
		QThread_Yield();
	}
}

bool ParallelComputationHost::AreAllTasksCompleted( const PartialTask *thisThreadTask ) {
	for( auto *task = tasksHead; task; task = task->Next() ) {
		if( task != thisThreadTask && !task->isCompleted.load( std::memory_order_relaxed ) ) {
			return false;
		}
	}

	return true;
}

inline void ParallelComputationHost::DestroyTask( PartialTask *task ) {
	assert( task );
	task->~PartialTask();
	Q_free( task );
}

void ParallelComputationHost::DestroyHeldTasks() {
	PartialTask *nextTask;
	for( auto *task = tasksHead; task; task = nextTask ) {
		nextTask = task->Next();
		DestroyTask( task );
	}
	tasksHead = nullptr;
}