#ifndef WSW_bff08f97_8303_4bed_89a7_5a30a048a164_H
#define WSW_bff08f97_8303_4bed_89a7_5a30a048a164_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <memory>

#include "qthreads.h"
#include "wswstaticvector.h"

class PipeAdapter {
public:
	[[nodiscard]]
	auto handlePipeCmd( unsigned cmd, uint8_t *data ) -> size_t {
		assert( cmd < std::size( m_calls ) );
		assert( m_calls[cmd] );
		// Com_Printf( "Should handle %s\n", m_calls[cmd]->m_name );
		return m_calls[cmd]->handlePipeCmd( data );
	}
protected:
	class InterThreadCall {
		friend class PipeAdapter;
	public:
		InterThreadCall( PipeAdapter *adapter, const char *name ) : m_adapter( adapter ), m_name( name ) {
			adapter->registerCall( this );
		}

		[[nodiscard]]
		virtual auto handlePipeCmd( uint8_t *data ) -> size_t = 0;
	protected:
		PipeAdapter *const m_adapter;
		const char *m_name;
	private:
		unsigned m_id { 0 };
	};

	friend class PipeAdapter::InterThreadCall;

	void registerCall( InterThreadCall *call ) {
		assert( m_callIdsCounter < std::size( m_calls ) );
		assert( !call->m_id );
		call->m_id = m_callIdsCounter++;
		assert( !m_calls[call->m_id] );
		m_calls[call->m_id] = call;
	}

	class TerminatePipeCall : public InterThreadCall {
	public:
		explicit TerminatePipeCall( PipeAdapter *adapter ) : InterThreadCall( adapter, "<terminate pipe>" ) {}

		void exec() {
			if( !m_adapter->m_sync ) {
				const Cmd cmd { .id = m_id };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, pad( sizeof( Cmd ) ), sizeof( Cmd ) );
			}
		}
	private:
		struct Cmd { unsigned id; };

		[[nodiscard]]
		auto handlePipeCmd( uint8_t * ) -> size_t override {
			return 0;
		}
	};

	static constexpr size_t kAlignment = 16;

	[[nodiscard]]
	static constexpr auto pad( size_t value ) -> size_t {
		return value + ( kAlignment - value % kAlignment ) % kAlignment;
	}

	template <typename T>
	[[nodiscard]]
	static auto getCheckingAlignment( void *p ) -> T * {
		assert( !( (uintptr_t)p & ( kAlignment - 1 ) ) );
		return (T *)p;
	}

	template <typename Endpoint>
	class InterThreadCall0 final: public InterThreadCall {
	public:
		using Method = void (Endpoint::*)();

		InterThreadCall0( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec() {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )();
			}
		}
	private:
		struct Cmd { unsigned id; };
		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t * ) -> size_t override {
			( m_endpoint->*m_method )();
			return kPaddedCmdSize;
		}

		Endpoint *m_endpoint;
		const Method m_method;
	};

	template <typename Endpoint, typename Arg1>
	class InterThreadCall1 final: public InterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 & );

		InterThreadCall1( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1 ) {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id, .arg1 = arg1 };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )( arg1 );
			}
		}
	private:
		struct Cmd {
			unsigned id;
			Arg1 arg1;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			auto *cmd = getCheckingAlignment<Cmd>( data );
			( m_endpoint->*m_method )( cmd->arg1 );
			return kPaddedCmdSize;
		}

		Endpoint *const m_endpoint;
		const Method m_method;
	};

	template <typename Endpoint, typename Arg1, typename Arg2>
	class InterThreadCall2 final: public InterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 &, const Arg2 & );

		InterThreadCall2( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1, const Arg2 &arg2 ) {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id, .arg1 = arg1, .arg2 = arg2 };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )( arg1, arg2 );
			}
		}
	private:
		struct Cmd {
			unsigned id;
			Arg1 arg1;
			Arg2 arg2;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			auto *cmd = getCheckingAlignment<Cmd>( data );
			( m_endpoint->*m_method )( cmd->arg1, cmd->arg2 );
			return kPaddedCmdSize;
		}

		Endpoint *const m_endpoint;
		const Method m_method;
	};

	template <typename Endpoint, typename Arg1, typename Arg2, typename Arg3>
	class InterThreadCall3 final: public InterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 &, const Arg2 &, const Arg3 & );

		InterThreadCall3( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3 ) {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id, .arg1 = arg1, .arg2 = arg2, .arg3 = arg3 };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )( arg1, arg2, arg3 );
			}
		}
	private:
		struct Cmd {
			unsigned id;
			Arg1 arg1;
			Arg2 arg2;
			Arg3 arg3;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			auto *cmd = getCheckingAlignment<Cmd>( data );
			( m_endpoint->*m_method )( cmd->arg1, cmd->arg2, cmd->arg3 );
			return kPaddedCmdSize;
		}

		Endpoint *const m_endpoint;
		const Method m_method;
	};

	template <typename Endpoint, typename Arg1, typename Arg2, typename Arg3, typename Arg4>
	class InterThreadCall4 final : public InterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 &, const Arg2 &, const Arg3 &, const Arg4 & );

		InterThreadCall4( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3, const Arg4 &arg4 ) {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id, .arg1 = arg1, .arg2 = arg2, .arg3 = arg3, .arg4 = arg4 };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )( arg1, arg2, arg3, arg4 );
			}
		}
	private:
		struct Cmd {
			unsigned id;
			Arg1 arg1;
			Arg2 arg2;
			Arg3 arg3;
			Arg4 arg4;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			auto *cmd = getCheckingAlignment<Cmd>( data );
			( m_endpoint->*m_method )( cmd->arg1, cmd->arg2, cmd->arg3, cmd->arg4 );
			return kPaddedCmdSize;
		}

		Endpoint *const m_endpoint;
		const Method m_method;
	};

	template <typename Endpoint, typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
	class InterThreadCall5 final : public InterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 &, const Arg2 &, const Arg3 &, const Arg4 &, const Arg5 & );

		InterThreadCall5( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: InterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3, const Arg4 &arg4, const Arg5 &arg5 ) {
			if( !m_adapter->m_sync ) [[likely]] {
				const Cmd cmd { .id = m_id, .arg1 = arg1, .arg2 = arg2, .arg3 = arg3, .arg4 = arg4, .arg5 = arg5 };
				QBufPipe_WriteCmd( m_adapter->m_pipe, &cmd, kPaddedCmdSize, sizeof( Cmd ) );
			} else {
				( m_endpoint->*m_method )( arg1, arg2, arg3, arg4, arg5 );
			}
		}
	private:
		struct Cmd {
			unsigned id;
			Arg1 arg1;
			Arg2 arg2;
			Arg3 arg3;
			Arg4 arg4;
			Arg5 arg5;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			auto *cmd = getCheckingAlignment<Cmd>( data );
			( m_endpoint->*m_method )( cmd->arg1, cmd->arg2, cmd->arg3, cmd->arg4, cmd->arg5 );
			return kPaddedCmdSize;
		}

		Endpoint *const m_endpoint;
		const Method m_method;
	};

	class BatchedInterThreadCall : public InterThreadCall {
	public:
		explicit BatchedInterThreadCall( PipeAdapter *adapter, const char *name )
			: InterThreadCall( adapter, name ) {}

		virtual void flush() = 0;
	};

	// Only this specialization actually gets used
	template <typename Endpoint, typename Arg1, typename Arg2, typename Arg3, size_t BufferSize>
	class BatchedInterThreadCall3 : public BatchedInterThreadCall {
	public:
		using Method = void (Endpoint::*)( const Arg1 &, const Arg2 &, const Arg3 & );

		BatchedInterThreadCall3( PipeAdapter *adapter, const char *name, Endpoint *endpoint, Method method )
			: BatchedInterThreadCall( adapter, name ), m_endpoint( endpoint ), m_method( method ) {}

		void exec( const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3 ) {
			if( m_pendingCmd.entries.full() ) [[unlikely]] {
				flush();
			}
			m_pendingCmd.entries.emplace_back( { .arg1 = arg1, .arg2 = arg2, .arg3 = arg3 } );
		}

		void flush() override {
			if( !m_pendingCmd.entries.empty() ) {
				if( !m_adapter->m_sync ) [[likely]] {
					m_pendingCmd.id = m_id;
					QBufPipe_WriteCmd( m_adapter->m_pipe, &m_pendingCmd, kPaddedCmdSize, sizeof( Cmd ) );
				} else {
					performEndpointMethodCalls( m_pendingCmd );
				}
				m_pendingCmd.entries.clear();
			}
		}
	private:
		struct Cmd {
			struct Entry {
				Arg1 arg1;
				Arg2 arg2;
				Arg3 arg3;
			};
			unsigned id;
			wsw::StaticVector<Entry, BufferSize> entries;
		};

		static constexpr size_t kPaddedCmdSize = pad( sizeof( Cmd ) );

		[[nodiscard]]
		auto handlePipeCmd( uint8_t *data ) -> size_t override {
			performEndpointMethodCalls( *getCheckingAlignment<Cmd>( data ) );
			return kPaddedCmdSize;
		}

		void performEndpointMethodCalls( const Cmd &cmd ) {
			for( const typename Cmd::Entry &entry: cmd.entries ) {
				( m_endpoint->*m_method )( entry.arg1, entry.arg2, entry.arg3 );
			}
		}

		Endpoint *const m_endpoint;
		const Method m_method;
		Cmd m_pendingCmd {};
	};

	qbufPipe_s *m_pipe { nullptr };
	// TODO: Allow specifying number of calls
	InterThreadCall *m_calls[32] {};
	unsigned m_callIdsCounter { 0 };
	bool m_sync { false };
};

#endif