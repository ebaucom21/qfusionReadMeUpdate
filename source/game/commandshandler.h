#ifndef WSW_09180ffb_50be_4694_b83a_1e336d950fea_H
#define WSW_09180ffb_50be_4694_b83a_1e336d950fea_H

#include "../qcommon/freelistallocator.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstring.h"
#include "../qcommon/commandshandler.h"

class ClientCommandsHandler : public wsw::CommandsHandler<wsw::VarArgCommandCallback<bool, edict_t *, uint64_t>> {
	static inline const wsw::StringView kScriptTag { "script" };
	static inline const wsw::StringView kBuiltinTag { "builtin" };

	using Callback = wsw::VarArgCommandCallback<bool, edict_t *, uint64_t>;

	class ScriptCommandCallback final : public Callback {
	public:
		explicit ScriptCommandCallback( wsw::String &&name ): Callback( kScriptTag, std::move( name ) ) {}
		[[nodiscard]]
		bool operator()( edict_t *arg, uint64_t ) override;
	};

	class Builtin1ArgCallback final : public Callback {
		void (*m_fn)( edict_t * );
	public:
		Builtin1ArgCallback( const wsw::HashedStringView &name, void (*fn)( edict_t * ) )
			: Callback( kBuiltinTag, name ), m_fn( fn ) {}
		[[nodiscard]]
		bool operator()( edict_t *ent, uint64_t ) override {
			m_fn( ent ); return true;
		}
	};

	class Builtin2ArgsCallback final : public Callback {
		void (*m_fn)( edict_t *, uint64_t );
	public:
		Builtin2ArgsCallback( const wsw::HashedStringView &name, void (*fn)( edict_t *, uint64_t ) )
			: Callback( kBuiltinTag, name ), m_fn( fn ) {}
		[[nodiscard]]
		bool operator()( edict_t *ent, uint64_t clientCommandNum ) override {
			m_fn( ent, clientCommandNum ); return true;
		}
	};

	static constexpr size_t kMaxCallbackSize = wsw::max( sizeof( ScriptCommandCallback ),
														 wsw::max( sizeof( Builtin1ArgCallback ), sizeof( Builtin2ArgsCallback ) ) );

	// Make sure we can always construct a new argument for addOrReplace()
	wsw::MemberBasedFreelistAllocator<kMaxCallbackSize, MAX_GAMECOMMANDS + 1> m_allocator;

	[[nodiscard]]
	static bool checkNotWriteProtected( const wsw::StringView &name );

	void addAndNotify( Callback *callback, bool allowToReplace );
public:
	static void init();
	static void shutdown();
	static ClientCommandsHandler *instance();

	ClientCommandsHandler();

	void precacheCommands();

	void handleClientCommand( edict_t *ent, uint64_t clientCommandNum );
	void addScriptCommand( const wsw::StringView &name );

	void addBuiltin( const wsw::HashedStringView &name, void (*handler)( edict_t * ) );
	void addBuiltin( const wsw::HashedStringView &name, void (*handler)( edict_t *, uint64_t ) );
};

#endif
