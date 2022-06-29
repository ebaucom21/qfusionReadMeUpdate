#include "chat.h"
#include "g_local.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstringview.h"

#include <cstdint>
#include <sstream>

using wsw::operator""_asView;

ChatPrintHelper::ChatPrintHelper( const edict_t *sender, uint64_t sendCommandNum, const char *format, ... )
	: m_sender( sender ), m_sendCommandNum( sendCommandNum ) {
	va_list va;
	va_start( va, format );
	formatTextFromVarargs( format, va );
	va_end( va );

	finishSetup();
}

ChatPrintHelper::ChatPrintHelper( const char *format, ... ) : m_sender( nullptr ), m_sendCommandNum( 0 ) {
	va_list va;
	va_start( va, format );
	formatTextFromVarargs( format, va );
	va_end( va );

	finishSetup();
}

ChatPrintHelper::ChatPrintHelper( const edict_t *source, uint64_t sendCommandNum, const wsw::StringView &message )
	: m_sender( source ), m_sendCommandNum( sendCommandNum ) {
	const wsw::StringView view( message.length() <= kMaxTextLength ? message : message.take( kMaxTextLength ) );
	view.copyTo( m_buffer + kTextOffset, view.length() + 1 );
	m_messageLength = (int)view.length();

	finishSetup();
}

void ChatPrintHelper::finishSetup() {
	// Replace double quotes in the message
	char *p = m_buffer + kTextOffset;
	while( ( p = strchr( p, '\"' ) ) ) {
		*p = '\'';
	}

	assert( !m_senderNum );
	if( m_sender ) {
		m_senderNum = PLAYERNUM( m_sender ) + 1;
	}

	// Wrap the message in double quotes
	m_buffer[kTextOffset - 1] = '\"';
	m_buffer[kTextOffset + m_messageLength] = '\"';
	m_buffer[kTextOffset + m_messageLength + 1] = '\0';
}

void ChatPrintHelper::formatTextFromVarargs( const char *format, va_list va ) {
	std::fill( m_buffer, m_buffer + kTextOffset, ' ' );
	m_messageLength = Q_vsnprintfz( m_buffer + kTextOffset, sizeof( m_buffer ) - kTextOffset, format, va );
	if( m_messageLength < 0 ) {
		m_messageLength = sizeof( m_buffer ) - kTextOffset - 1;
	}
}

void ChatPrintHelper::printToServerConsole( bool teamOnly ) {
	if( !dedicated->integer ) {
		return;
	}

	if( m_hasPrintedToServerConsole || m_skipServerConsole ) {
		return;
	}

	m_hasPrintedToServerConsole = true;

	const char *msg = m_buffer + kTextOffset;
	assert( m_buffer[kTextOffset + m_messageLength] == '"' );
	// Truncate the double quote at the end of the buffer
	m_buffer[kTextOffset + m_messageLength] = '\0';

	if( !m_sender ) {
		G_Printf( S_COLOR_GREEN "console: %s\n", m_buffer );     // admin console
	} else if( const auto *client = m_sender->r.client ) {
		if( teamOnly ) {
			const char *team = client->ps.stats[STAT_TEAM] == TEAM_SPECTATOR ? "SPEC" : "TEAM";
			G_Printf( S_COLOR_YELLOW "[%s]" S_COLOR_WHITE "%s" S_COLOR_YELLOW ": %s\n", team, client->netname.data(), msg );
		} else {
			G_Printf( "%s" S_COLOR_GREEN ": %s\n", client->netname.data(), msg );
		}
	}

	// Restore the double quote at the end of the buffer
	m_buffer[kTextOffset + m_messageLength] = '"';
}

void ChatPrintHelper::dispatchWithFilter( const ChatHandlersChain *filter, bool teamOnly ) {
	printToServerConsole( true );

	if( !m_sender ) {
		const char *cmd = setupPrefixForOthers( teamOnly );
		trap_GameCmd( nullptr, cmd );
		return;
	}

	const char *cmd = nullptr;
	for( int i = 0; i < gs.maxclients; i++ ) {
		edict_t *ent = game.edicts + 1 + i;
		if( !ent->r.inuse || !ent->r.client ) {
			continue;
		}
		if( teamOnly && ent->s.team != m_sender->s.team ) {
			continue;
		}
		if( trap_GetClientState( i ) < CS_CONNECTED ) {
			continue;
		}
		if( filter && filter->ignores( ent, m_sender ) ) {
			filter->notifyOfIgnoredMessage( ent, m_sender );
			continue;
		}
		// Set up the needed prefix
		const char *pendingCmd = cmd;
		if( m_sender == ent ) {
			cmd = setupPrefixForSender( teamOnly );
			// Force setting up the prefix for others on the next iteration
			pendingCmd = nullptr;
		} else if( !cmd ) {
			cmd = setupPrefixForOthers( teamOnly );
			pendingCmd = cmd;
		}
		trap_GameCmd( ent, cmd );
		cmd = pendingCmd;
	}
}

void ChatPrintHelper::printTo( const edict_t *target, bool teamOnly ) {
	if( !target->r.inuse ) {
		return;
	}

	if( !target->r.client ) {
		return;
	}

	if( teamOnly && m_sender && m_sender->s.team != target->s.team ) {
		return;
	}

	if( trap_GetClientState( PLAYERNUM( target ) ) < CS_SPAWNED ) {
		return;
	}

	printToServerConsole( teamOnly );
	const char *cmd = ( m_sender == target ) ? setupPrefixForSender( teamOnly ) : setupPrefixForOthers( teamOnly );
	trap_GameCmd( target, cmd );
}

auto ChatPrintHelper::setupPrefixForOthers( bool teamOnly ) -> const char * {
	const char *command = teamOnly ? "tch" : "ch";
	constexpr int limit = kTextOffset - 1;
	const int res = Q_snprintfz( m_buffer, limit, "%s %d", command, m_senderNum );
	assert( res > 0 && res < limit );
	std::fill( m_buffer + res, m_buffer + limit, ' ' );
	return m_buffer;
}

auto ChatPrintHelper::setupPrefixForSender( bool teamOnly ) -> const char * {
	const char *command = teamOnly ? "tcha" : "cha";
	constexpr const char *format = "%s %" PRIu64 " %d";
	constexpr int limit = kTextOffset - 1;
	const int res = Q_snprintfz( m_buffer, limit, format, command, m_sendCommandNum, m_senderNum );
	assert( res > 0 && res < limit );
	std::fill( m_buffer + res, m_buffer + limit, ' ' );
	return m_buffer;
}

void MuteFilter::mute( const edict_s *ent ) {
	m_muted[ENTNUM( ent ) - 1] = true;
}

void MuteFilter::unmute( const edict_s *ent ) {
	m_muted[ENTNUM( ent ) - 1] = false;
}

auto MuteFilter::handleMessage( const ChatMessage &message ) -> std::optional<MessageFault> {
	if( m_muted[message.clientNum] ) {
		return MessageFault { message.clientCommandNum, MessageFault::Muted, 0 };
	}
	return std::nullopt;
}

bool RespectHandler::skipStatsForClient( const edict_s *ent ) const {
	const auto &entry = m_entries[ENTNUM( ent ) - 1];
	return entry.hasViolatedCodex || entry.hasIgnoredCodex;
}

void RespectHandler::addToReportStats( const edict_s *ent, RespectStats *reported ) {
	m_entries[ENTNUM( ent ) - 1].addToReportStats( reported );
}

void RespectHandler::onClientDisconnected( const edict_s *ent ) {
	m_entries[ENTNUM( ent ) - 1].onClientDisconnected();
}

void RespectHandler::onClientJoinedTeam( const edict_s *ent, int newTeam ) {
	m_entries[ENTNUM( ent ) - 1].onClientJoinedTeam( newTeam );
}

bool IgnoreFilter::ignores( const edict_s *target, const edict_s *source ) const {
	if( target == source ) {
		return false;
	}
	const ClientEntry &e = m_entries[PLAYERNUM( target )];
	if( e.ignoresEverybody ) {
		return true;
	}
	if( e.ignoresNotTeammates && ( target->s.team != source->s.team ) ) {
		return true;
	}
	return e.GetClientBit( PLAYERNUM( source ) );
}

RespectHandler::RespectHandler() {
	for( int i = 0; i < MAX_CLIENTS; ++i ) {
		m_entries[i].ent = game.edicts + i + 1;
	}
	reset();
}

void RespectHandler::reset() {
	for( ClientEntry &e: m_entries ) {
		e.reset();
	}

	m_matchStartedAt = -1;
	m_lastFrameMatchState = MATCH_STATE_NONE;
}

void RespectHandler::frame() {
	const auto matchState = GS_MatchState();
	// This is not 100% correct but is sufficient for message checks
	if( matchState == MATCH_STATE_PLAYTIME ) {
		if( m_lastFrameMatchState != MATCH_STATE_PLAYTIME ) {
			m_matchStartedAt = level.time;
		}
	}

	if( !GS_RaceGametype() ) {
		for( int i = 0; i < gs.maxclients; ++i ) {
			m_entries[i].checkBehaviour( m_matchStartedAt );
		}
	}

	m_lastFrameMatchState = matchState;
}

auto RespectHandler::handleMessage( const ChatMessage &message ) -> std::optional<MessageFault> {
	// Race is another world...
	if( GS_RaceGametype() ) {
		return std::nullopt;
	}

	// Allow public chatting in timeouts
	if( GS_MatchPaused() ) {
		return std::nullopt;
	}

	const auto matchState = GS_MatchState();
	// Ignore until countdown
	if( matchState < MATCH_STATE_COUNTDOWN ) {
		return std::nullopt;
	}

	assert( message.clientNum < std::size( m_entries ) );
	(void)m_entries[message.clientNum].handleMessage( message );
	// TODO we don't really detect faults with this handler
	return std::nullopt;
}

void RespectHandler::ClientEntry::reset() {
	warnedAt = 0;
	firstJoinedGameAt = 0;
	std::fill( std::begin( firstSaidAt ), std::end( firstSaidAt ), 0 );
	std::fill( std::begin( lastSaidAt ), std::end( lastSaidAt ), 0 );
	std::fill( std::begin( numSaidTokens ), std::end( numSaidTokens ), 0 );
	saidBefore = false;
	saidAfter = false;
	hasTakenCountdownHint = false;
	hasTakenStartHint = false;
	hasTakenLastStartHint = false;
	hasTakenFinalHint = false;
	hasIgnoredCodex = false;
	hasViolatedCodex = false;
}

bool RespectHandler::ClientEntry::handleMessage( const ChatMessage &message ) {
	// If has already violated or ignored the Codex
	if( hasViolatedCodex || hasIgnoredCodex ) {
		return false;
	}

	const auto matchState = GS_MatchState();
	// Skip everything in warmup
	if( matchState < MATCH_STATE_COUNTDOWN ) {
		return false;
	}

	// Skip messages from spectators unless being post-match.
	if( matchState < MATCH_STATE_POSTMATCH && ( ent->s.team == TEAM_SPECTATOR ) ) {
		return false;
	}

	// Now check for RnS tokens...
	wsw::StaticString<MAX_CHAT_BYTES> text( message.text );
	// TODO: Make checkForTokens accept a StringView argument
	if( checkForTokens( text.data() ) ) {
		return false;
	}

	// Skip further tests for spectators (we might have saved post-match tokens that could be important)
	if( ent->s.team == TEAM_SPECTATOR ) {
		return false;
	}

	if( G_ISGHOSTING( ent ) ) {
		// This is primarily for round-based gametypes. Just print a warning.
		// Fragged players waiting for a next round start
		// are not considered spectators while really they are.
		// Other gametypes should follow this behaviour to avoid misunderstanding rules.
		displayCodexViolationWarning();
		return false;
	}

	// We do not intercept this condition in RespectHandler::HandleMessage()
	// as we still need to collect last said tokens for clients using CheckForTokens()
	if( matchState > MATCH_STATE_PLAYTIME ) {
		return false;
	}

	if( matchState < MATCH_STATE_PLAYTIME ) {
		displayCodexViolationWarning();
		return false;
	}

	// Never warned (at start of the level)
	if( !warnedAt ) {
		warnedAt = level.time;
		displayCodexViolationWarning();
		// Let the message be printed by default facilities
		return false;
	}

	const int64_t millisSinceLastWarn = level.time - warnedAt;
	// Don't warn again for occasional flood
	if( millisSinceLastWarn < 2000 ) {
		// Swallow messages silently
		return true;
	}

	// Allow speaking occasionally once per 5 minutes
	if( millisSinceLastWarn > 5 * 60 * 1000 ) {
		warnedAt = level.time;
		displayCodexViolationWarning();
		return false;
	}

	hasViolatedCodex = true;
	// Print the message first
	ChatPrintHelper chatPrintHelper( ent, message.clientCommandNum, message.text );
	chatPrintHelper.printToEverybody( ChatHandlersChain::instance() );
	// Then announce
	announceMisconductBehaviour( "violated" );
	// Interrupt handing of the message
	return true;
}

void RespectHandler::ClientEntry::announceMisconductBehaviour( const char *action ) {
	// Ignore bots.
	// We plan to add R&S bot behaviour but do not currently want to touch the game module
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		return;
	}

	// We can't actually figure out printing that in non-triggering fashion
	(void)action;

	char message[256] = S_COLOR_YELLOW "'" S_COLOR_CYAN "Fair play" S_COLOR_YELLOW "' award lost";
	if( !StatsowFacade::Instance()->IsMatchReportDiscarded() ) {
		Q_strncatz( message, ". No rating gain", sizeof( message ) );
	}

	G_PrintMsg( ent, "%s!\n", message );
}

void RespectHandler::ClientEntry::announceFairPlay() {
	G_PlayerAward( ent, S_COLOR_CYAN "Fair play!" );
	G_PrintMsg( ent, "Your stats and awards have been confirmed!\n" );

	char cmd[MAX_STRING_CHARS];
	Q_snprintfz( cmd, sizeof( cmd ), "ply \"%s\"", S_RESPECT_REWARD );
	trap_GameCmd( ent, cmd );
}

class RespectToken {
	const char *name { nullptr };
	wsw::StringView *aliases { nullptr };
	int tokenNum { -1 };
	int numAliases { -1 };

	// TODO: Can all this stuff be implemented using variadic templates?
	void InitFrom( const char *name_, int tokenNum_, int numAliases_, ... ) {
		aliases = (wsw::StringView *)::malloc( numAliases_ * sizeof( wsw::StringView ) );
		numAliases = numAliases_;
		name = name_;
		tokenNum = tokenNum_;

		va_list va;
		va_start( va, numAliases_ );
		for( int i = 0; i < numAliases_; ++i ) {
			const char *alias = va_arg( va, const char * );
			new( aliases + i )wsw::StringView( alias );
		}
		va_end( va );
	}

	int TryMatchingByAlias( const char *p, const wsw::StringView &alias ) const;
public:
	RespectToken( const char *name_, int tokenNum_, const char *alias1 ) {
		InitFrom( name_, tokenNum_, 1, alias1 );
	}

	RespectToken( const char *name_, int tokenNum_, const char *a1, const char *a2, const char *a3 ) {
		InitFrom( name_, tokenNum_, 3, a1, a2, a3 );
	}

	RespectToken( const char *name_, int tokenNum_, const char *a1, const char *a2, const char *a3, const char *a4 ) {
		InitFrom( name_, tokenNum_, 4, a1, a2, a3, a4 );
	}

	~RespectToken() {
		::free( aliases );
	}

	const char *Name() const { return name; }
	int TokenNum() const { return tokenNum; }

	int GetMatchedLength( const char *p ) const;
};

int RespectToken::GetMatchedLength( const char *p ) const {
	for( int i = 0; i < numAliases; ++i ) {
		const int len = TryMatchingByAlias( p, aliases[i] );
		if( len > 0 ) {
			return len;
		}
	}
	return -1;
}

int RespectToken::TryMatchingByAlias( const char *p, const wsw::StringView &alias ) const {
	const char *const start = p;
	for( const char aliasChar: alias ) {
		assert( !::isalpha( aliasChar ) || ::islower( aliasChar ) );
		// Try finding a first character that is not a part of a color token
		for(; ; ) {
			const char charToMatch = *p++;
			if( ::tolower( charToMatch ) == aliasChar ) {
				break;
			}
			if( charToMatch != '^' ) {
				return -1;
			}
			const char nextCharToMatch = *p++;
			if( nextCharToMatch && !::isdigit( nextCharToMatch ) ) {
				return -1;
			}
		}
	}
	return (int)( p - start );
}

class RespectTokensRegistry {
	static const std::array<RespectToken, 10> TOKENS;

	static_assert( RespectHandler::kNumTokens == 10, "" );
public:
	// For players staying in game during the match
	static const int SAY_AT_START_TOKEN_NUM;
	static const int SAY_AT_END_TOKEN_NUM;

	/**
	 * Finds a number of a token (a number of a token aliases group) the supplied string matches.
	 * @param p a pointer to a string data. Should not point to a white-space. A successful match advances this token.
	 * @return a number of token (of a token aliases group), a negative value on failure.
	 */
	static int MatchByToken( const char **p );

	static const RespectToken &TokenByName( const char *name ) {
		for( const RespectToken &token: TOKENS ) {
			if( !Q_stricmp( token.Name(), name ) ) {
				return token;
			}
		}
		abort();
	}

	static const RespectToken &TokenForNum( int num ) {
		assert( (int)num < TOKENS.size() );
		assert( TOKENS[num].TokenNum() == num );
		return TOKENS[num];
	}
};

const std::array<RespectToken, 10> RespectTokensRegistry::TOKENS = {{
	RespectToken( "hi", 0, "hi" ),
	RespectToken( "bb", 1, "bb" ),
	RespectToken( "glhf", 2, "glhf", "gl", "hf" ),
	RespectToken( "gg", 3, "ggs", "gg", "bgs", "bg" ),
	RespectToken( "plz", 4, "plz" ),
	RespectToken( "tks", 5, "tks" ),
	RespectToken( "soz", 6, "soz" ),
	RespectToken( "n1", 7, "n1" ),
	RespectToken( "nt", 8, "nt" ),
	RespectToken( "lol", 9, "lol" ),
}};

const int RespectTokensRegistry::SAY_AT_START_TOKEN_NUM = RespectTokensRegistry::TokenByName( "glhf" ).TokenNum();
const int RespectTokensRegistry::SAY_AT_END_TOKEN_NUM = RespectTokensRegistry::TokenByName( "gg" ).TokenNum();

int RespectTokensRegistry::MatchByToken( const char **p ) {
	for( const RespectToken &token: TOKENS ) {
		int len = token.GetMatchedLength( *p );
		if( len < 0 ) {
			continue;
		}
		*p += len;
		return token.TokenNum();
	}
	return -1;
}

/**
 * Tries to match a sequence like this: {@code ( Whitespace-Char* Color-Token? )* }
 * @param s an address of the supplied string. Gets modified on success.
 * @param numWhitespaceChars an address to write a number of whitespace characters met.
 * @return false if there were malformed color tokens (the only kind of failure possible).
 */
static bool StripUpToMaybeToken( const char **s, int *numWhitespaceChars ) {
	*numWhitespaceChars = 0;
	const char *p = *s;
	for(; ; ) {
		const char *const oldp = p;
		// Strip whitespace and punctuation except the circumflex that requires a special handling
		while( ::ispunct( *p ) && *p != '^' ) {
			p++;
		}
		*numWhitespaceChars += (int)( oldp - p );
		// Interrupt at the string end
		if( !*p ) {
			break;
		}
		// Try matching a single color token
		const char ch = *p;
		if( ch != '^' ) {
			break;
		}
		p++;
		const char nextCh = *p++;
		// Interrupt at an incomplete color token at the end with success
		if( !nextCh ) {
			break;
		}
		// A next character (if any) must be a digit
		if( !::isdigit( nextCh ) ) {
			return false;
		}
		// Go to a next color token (if any)
	}

	*s = p;
	return true;
}

bool RespectHandler::ClientEntry::checkForTokens( const char *message ) {
	// Do not modify tokens count immediately
	// Either this routine fails completely or stats for all tokens get updated
	int numFoundTokens[kNumTokens];
	std::fill( std::begin( numFoundTokens ), std::end( numFoundTokens ), 0 );

	const int64_t levelTime = level.time;

	bool expectPunctOrSpace = false;
	const char *p = message;
	for(;; ) {
		int numWhitespaceChars = 0;
		// If there were malformed color tokens
		if( !StripUpToMaybeToken( &p, &numWhitespaceChars ) ) {
			return false;
		}
		// If we've reached the string end
		if( !*p ) {
			break;
		}
		// If we didn't advance a displayed position after a previously matched token
		if( expectPunctOrSpace && !numWhitespaceChars ) {
			return false;
		}
		int tokenNum = RespectTokensRegistry::MatchByToken( &p );
		if( tokenNum < 0 ) {
			return false;
		}
		numFoundTokens[tokenNum]++;
		// Expect a whitespace after just matched token
		// (punctuation characters are actually allowed as well).
		expectPunctOrSpace = true;
	}

	for( int tokenNum = 0; tokenNum < kNumTokens; ++tokenNum ) {
		int numTokens = numFoundTokens[tokenNum];
		if( !numTokens ) {
			continue;
		}
		this->numSaidTokens[tokenNum] += numTokens;
		this->lastSaidAt[tokenNum] = levelTime;
	}

	return true;
}

void RespectHandler::ClientEntry::checkBehaviour( const int64_t matchStartTime ) {
	if( !ent->r.inuse ) {
		return;
	}

	if( !ent->r.client->stats.had_playtime ) {
		return;
	}

	if( saidBefore && saidAfter ) {
		return;
	}

	const auto levelTime = level.time;
	const auto matchState = GS_MatchState();
	const int startTokenNum = RespectTokensRegistry::SAY_AT_START_TOKEN_NUM;

	if( matchState == MATCH_STATE_COUNTDOWN ) {
		// If has just said "glhf"
		if( levelTime - lastSaidAt[startTokenNum] < 64 ) {
			saidBefore = true;
		}
		if( !hasTakenCountdownHint ) {
			requestClientRespectAction( startTokenNum );
			hasTakenCountdownHint = true;
		}
		return;
	}

	if( matchState == MATCH_STATE_PLAYTIME ) {
		if( saidBefore || hasViolatedCodex ) {
			return;
		}

		if( firstJoinedGameAt > matchStartTime ) {
			saidBefore = true;
			return;
		}

		// If a client has joined a spectators team
		if( ent->s.team == TEAM_SPECTATOR ) {
			saidBefore = true;
			return;
		}

		const auto lastActivityAt = ent->r.client->last_activity;
		// Skip inactive clients considering their behaviour respectful
		if( !lastActivityAt || levelTime - lastActivityAt > 10000 ) {
			saidBefore = true;
			return;
		}

		if( levelTime - lastSaidAt[startTokenNum] < 64 ) {
			saidBefore = true;
			return;
		}

		const int64_t countdownStartTime = matchStartTime;
		if( levelTime - countdownStartTime < 1500 ) {
			return;
		}

		if( !hasTakenStartHint && !hasViolatedCodex ) {
			requestClientRespectAction( startTokenNum );
			hasTakenStartHint = true;
			return;
		}

		// Wait for making a second hint
		if( levelTime - countdownStartTime < 6500 ) {
			return;
		}

		if( !hasTakenLastStartHint && !hasViolatedCodex ) {
			requestClientRespectAction( startTokenNum );
			hasTakenLastStartHint = true;
			return;
		}

		if( !hasIgnoredCodex && levelTime - countdownStartTime > 10000 ) {
			// The misconduct behaviour is going to be detected inevitably.
			// This is just to prevent massive console spam at the same time.
			if( random() > 0.97f ) {
				hasIgnoredCodex = true;
				announceMisconductBehaviour( "ignored" );
			}
			return;
		}
	}

	if( matchState != MATCH_STATE_POSTMATCH ) {
		return;
	}

	if( hasViolatedCodex || hasIgnoredCodex ) {
		return;
	}

	// A note: we do not distinguish players that became spectators mid-game
	// and players that have played till the match end.
	// They still have to say the mandatory token at the end with the single exception of becoming inactive.

	const auto lastActivityAt = ent->r.client->last_activity;
	if( !lastActivityAt || levelTime - lastActivityAt > 10000 ) {
		saidAfter = true;
		return;
	}

	const int endTokenNum = RespectTokensRegistry::SAY_AT_END_TOKEN_NUM;
	if( levelTime - lastSaidAt[endTokenNum] < 64 ) {
		if( !saidAfter ) {
			announceFairPlay();
			saidAfter = true;
		}
	}

	if( saidAfter || hasTakenFinalHint ) {
		return;
	}

	requestClientRespectAction( endTokenNum );
	hasTakenFinalHint = true;
}

void RespectHandler::ClientEntry::requestClientRespectAction( int tokenNum ) {
	wsw::StringView rawTokenView( RespectTokensRegistry::TokenForNum( tokenNum ).Name() );
	wsw::StaticString<8> token;
	token << rawTokenView;
	for( char &ch: token ) {
		ch = (char)std::toupper( ch );
	}

	const wsw::StringView yellow( S_COLOR_YELLOW ), white( S_COLOR_WHITE );

	wsw::StaticString<32> title;
	title << "Say "_asView << yellow << token << white << ", please!"_asView;

	// Client bindings for a 1st token start at 0-th offset in the numeric keys row.
	const int keyNum = ( tokenNum + 1 ) % 10;

	wsw::StaticString<32> desc;
	desc << "Press "_asView << yellow << keyNum << white << " for that"_asView;

	wsw::StaticString<16> key( "%d", keyNum );

	wsw::StaticString<16> command;
	command << "say "_asView << rawTokenView;

	const std::pair<wsw::StringView, wsw::StringView> actions[1] { { key.asView(), command.asView() } };
	G_SendActionRequest( ent, "respectAction"_asView, 4000, title.asView(), desc.asView(), actions, actions + 1 );
}

void RespectHandler::ClientEntry::displayCodexViolationWarning() {
	const wsw::StringView title( "Less talk, let's play!"_asView );
	const wsw::StringView desc( ""_asView );
	// Zero-size arrays are illegal for MSVC. We should switch to passing std::span eventually.
	const std::pair<wsw::StringView, wsw::StringView> actions[1];
	G_SendActionRequest( ent, "respectWarning"_asView, 3000, title, desc, actions, actions + 0 );
}

void RespectHandler::ClientEntry::onClientDisconnected() {
	if( GS_MatchState() != MATCH_STATE_PLAYTIME ) {
		return;
	}

	if( !ent->r.client->stats.had_playtime ) {
		return;
	}

	// Skip bots currently
	if( ent->r.svflags & SVF_FAKECLIENT ) {
		return;
	}

	if( hasIgnoredCodex || hasViolatedCodex ) {
		return;
	}

	// We have decided to consider the behaviour respectful in this case
	saidAfter = true;
}

void RespectHandler::ClientEntry::onClientJoinedTeam( int newTeam ) {
	if( GS_MatchState() > MATCH_STATE_PLAYTIME ) {
		return;
	}

	// Invalidate the "saidAfter" flag possible set on a disconnection during a match
	if( newTeam == TEAM_SPECTATOR ) {
		saidAfter = false;
		return;
	}

	if( !firstJoinedGameAt && ( GS_MatchState() == MATCH_STATE_PLAYTIME ) ) {
		firstJoinedGameAt = level.time;
	}

	// Check whether there is already Codex violation recorded for the player during this match
	mm_uuid_t clientSessionId = this->ent->r.client->mm_session;
	if( !clientSessionId.IsValidSessionId() ) {
		return;
	}

	auto *respectStats = StatsowFacade::Instance()->FindRespectStatsById( clientSessionId );
	if( !respectStats ) {
		return;
	}

	this->hasViolatedCodex = respectStats->hasViolatedCodex;
	this->hasIgnoredCodex = respectStats->hasIgnoredCodex;
}

void RespectHandler::ClientEntry::addToReportStats( RespectStats *reportedStats ) {
	if( reportedStats->hasViolatedCodex ) {
		return;
	}

	if( hasViolatedCodex ) {
		reportedStats->Clear();
		reportedStats->hasViolatedCodex = true;
		reportedStats->hasIgnoredCodex = hasIgnoredCodex;
		return;
	}

	if( hasIgnoredCodex ) {
		reportedStats->Clear();
		reportedStats->hasIgnoredCodex = true;
		return;
	}

	if( reportedStats->hasIgnoredCodex ) {
		return;
	}

	for( int i = 0; i < kNumTokens; ++i ) {
		if( !numSaidTokens[i] ) {
			continue;
		}
		const auto &token = RespectTokensRegistry::TokenForNum( i );
		reportedStats->AddToEntry( token.Name(), numSaidTokens[i] );
	}
}

void IgnoreFilter::handleIgnoreCommand( const edict_t *ent, bool ignore ) {
	const int numArgs = wsw::min( trap_Cmd_Argc(), MAX_CLIENTS );
	if( numArgs < 2 ) {
		printIgnoreCommandUsage( ent, ignore );
		return;
	}

	// Resetting of global flags should also unset individual bits

	ClientEntry &e = m_entries[PLAYERNUM( ent )];
	const char *prefix = trap_Cmd_Argv( 1 );
	if( !Q_stricmp( prefix, "everybody" ) ) {
		if( e.ignoresEverybody == ignore ) {
			return;
		}
		e.ignoresEverybody = ignore;
		e.ignoresNotTeammates = false;
		if( !ignore ) {
			e.ignoredClientsMask = 0;
		}
		sendChangeFilterVarCommand( ent );
		return;
	}

	if( !Q_stricmp( prefix, "notteam" ) ) {
		if( e.ignoresNotTeammates == ignore ) {
			return;
		}
		e.ignoresNotTeammates = ignore;
		e.ignoresEverybody = false;
		if( !ignore ) {
			for( int i = 0; i <= gs.maxclients; ++i ) {
				const edict_t *player = game.edicts + i + 1;
				if( player->r.inuse && player->s.team != ent->s.team ) {
					e.SetClientBit( i, false );
				}
			}
		}
		sendChangeFilterVarCommand( ent );
		return;
	}

	if( Q_stricmp( prefix, "players" ) != 0 ) {
		printIgnoreCommandUsage( ent, ignore );
		return;
	}

	uint64_t requestedMask = 0;
	static_assert( MAX_CLIENTS <= 64, "" );
	bool wereOnlyTeammates = true;
	// Convert player numbers first before applying a modification (don't apply changes partially)
	for( int i = 2; i < numArgs; ++i ) {
		const char *arg = trap_Cmd_Argv( i );
		const edict_t *player = G_PlayerForText( arg );
		if( !player ) {
			G_PrintMsg( ent, "Failed to get a player for `%s`\n", arg );
			return;
		}
		if( player == ent ) {
			G_PrintMsg( ent, "You can't ignore yourself\n" );
			return;
		}
		wereOnlyTeammates &= ( player->s.team == ent->s.team );
		requestedMask |= ( ( (uint64_t)1 ) << PLAYERNUM( player ) );
	}

	// We think we should no longer keep these global flags
	// if a player affected by these flags was mentioned in "unignore" command
	if( !ignore && requestedMask ) {
		if( e.ignoresEverybody ) {
			e.ignoresEverybody = false;
			// Convert the global flag to the per-client mask
			e.ignoredClientsMask = ~( (uint64_t)0 );
			sendChangeFilterVarCommand( ent );
		} else if( e.ignoresNotTeammates && !wereOnlyTeammates ) {
			e.ignoresNotTeammates = false;
			// Convert the global flag to the per-client mask
			for( int i = 0; i < gs.maxclients; ++i ) {
				const edict_t *clientEnt = game.edicts + i + 1;
				if( clientEnt->r.inuse && clientEnt->s.team != ent->s.team ) {
					e.SetClientBit( i, true );
				}
			}
			sendChangeFilterVarCommand( ent );
		}
	}

	if( ignore ) {
		e.ignoredClientsMask |= requestedMask;
	} else {
		e.ignoredClientsMask &= ~requestedMask;
	}
}

void IgnoreFilter::printIgnoreCommandUsage( const edict_t *ent, bool ignore ) {
	const char *usageFormat = "Usage: %s players <player1> [, <player2> ...] or %s everybody or %s notteam\n";
	const char *verb = ignore ? "ignore" : "unignore";
	G_PrintMsg( ent, usageFormat, verb, verb, verb );
}

void IgnoreFilter::sendChangeFilterVarCommand( const edict_t *ent ) {
	ClientEntry &e = m_entries[PLAYERNUM( ent )];
	int value = 0;
	if( e.ignoresEverybody ) {
		value = 1;
	} else if( e.ignoresNotTeammates ) {
		value = 2;
	}
	trap_GameCmd( ent, va( "ign setVar %d", value ) );
}

void IgnoreFilter::handleIgnoreListCommand( const edict_t *ent ) {
	const edict_t *player = G_PlayerForText( trap_Cmd_Argv( 1 ) );
	if( !player ) {
		if( trap_Cmd_Argc() >= 2 ) {
			G_PrintMsg( ent, "Usage: ignorelist [<player>]\n" );
			return;
		}
		player = ent;
	}

	const char *action = S_COLOR_WHITE "You ignore";
	const char *pronoun = S_COLOR_WHITE "your";
	char buffer[64];
	if( player != ent ) {
		Q_snprintfz( buffer, sizeof( buffer ), S_COLOR_WHITE "%s" S_COLOR_WHITE " ignores", player->r.client->netname.data() );
		action = buffer;
		pronoun = "their";
	}

	const ClientEntry &e = m_entries[PLAYERNUM( player )];
	if( e.ignoresEverybody ) {
		G_PrintMsg( ent, "%s everybody\n", action );
		return;
	}

	if( !e.ignoredClientsMask ) {
		if( e.ignoresNotTeammates ) {
			G_PrintMsg( ent, "%s players not in %s team\n", action, pronoun );
		} else {
			G_PrintMsg( ent, "%s nobody\n", action );
		}
		return;
	}

	std::stringstream ss;
	ss << action;
	const char *separator = " ";
	bool wereTeammatesMet = false;
	for( int i = 0; i < gs.maxclients; ++i ) {
		const auto *clientEnt = game.edicts + i + 1;
		if( clientEnt == player ) {
			continue;
		}
		if( !clientEnt->r.inuse ) {
			continue;
		}
		if( trap_GetClientState( i ) < CS_SPAWNED ) {
			continue;
		}
		if( !e.GetClientBit( i ) ) {
			continue;
		}
		if( e.ignoresNotTeammates ) {
			if( clientEnt->s.team != player->s.team ) {
				continue;
			}
			wereTeammatesMet = true;
		}
		ss << S_COLOR_WHITE << separator << clientEnt->r.client->netname.data();
		separator = ", ";
	}

	if( e.ignoresNotTeammates ) {
		if( wereTeammatesMet ) {
			ss << S_COLOR_WHITE << " and";
		}
		ss << S_COLOR_WHITE " players not in " << pronoun << " team";
	}

	const auto str( ss.str() );
	G_PrintMsg( ent, "%s\n", str.c_str() );
}

void IgnoreFilter::reset() {
	for( ClientEntry &e: m_entries ) {
		e.Reset();
	}
}

void IgnoreFilter::notifyOfIgnoredMessage( const edict_t *target, const edict_t *source ) const {
	trap_GameCmd( target, va( "ign %d", PLAYERNUM( source ) + 1 ) );
}

void IgnoreFilter::onUserInfoChanged( const edict_t *user ) {
	ClientEntry &e = m_entries[PLAYERNUM( user )];
	const int flags = user->r.client->getChatFilterFlags();
	e.ignoresEverybody = ( flags & 1 ) != 0;
	e.ignoresNotTeammates = ( flags & 2 ) != 0;
}

static SingletonHolder<ChatHandlersChain> chatHandlersChainHolder;

void ChatHandlersChain::init() {
	::chatHandlersChainHolder.init();
}

void ChatHandlersChain::shutdown() {
	::chatHandlersChainHolder.shutdown();
}

ChatHandlersChain *ChatHandlersChain::instance() {
	return ::chatHandlersChainHolder.instance();
}

void ChatHandlersChain::reset() {
	m_muteFilter.reset();
	m_floodFilter.reset();
	m_respectHandler.reset();
	m_ignoreFilter.reset();
}

void ChatHandlersChain::resetForClient( int clientNum ) {
	m_muteFilter.resetForClient( clientNum );
	m_floodFilter.resetForClient( clientNum );
	m_respectHandler.resetForClient( clientNum );
	m_ignoreFilter.resetForClient( clientNum );
}

auto ChatHandlersChain::handleMessage( const ChatMessage &message )
	-> std::optional<MessageFault> {
	// We want to call overridden methods directly just to avoid pointless virtual invocations.
	// Filters are applied in order of their priority.
	if( const auto maybeFault = m_muteFilter.handleMessage( message ) ) {
		return maybeFault;
	}
	if( const auto maybeFault = m_floodFilter.handleMessage( message ) ) {
		return maybeFault;
	}
	if( const auto maybeFault = m_respectHandler.handleMessage( message ) ) {
		return maybeFault;
	}

	ChatPrintHelper chatPrintHelper( PLAYERENT( message.clientNum ), message.clientCommandNum, message.text );
	chatPrintHelper.printToEverybody( ChatHandlersChain::instance() );
	return std::nullopt;
}