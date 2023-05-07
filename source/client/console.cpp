/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// console.c

#include "client.h"
#include "../qcommon/cmdargs.h"
#include "../qcommon/cmdcompat.h"
#include "../qcommon/cmdsystem.h"
#include "../qcommon/freelistallocator.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswvector.h"
#include "../qcommon/wswfs.h"

#include <mutex>

using wsw::operator""_asView;

CmdSystem *CL_GetCmdSystem();
void CL_Cmd_SubmitCompletionRequest( const wsw::StringView &name, unsigned requestId, const wsw::StringView &partial );
CompletionResult CL_GetPossibleCommands( const wsw::StringView &partial );
CompletionResult CL_GetPossibleAliases( const wsw::StringView &partial );
CompletionResult Cvar_CompleteBuildList( const wsw::StringView &partial );

// TODO: Generalize, lift to the top level
// We didn't initially lean to this design as it has a poor compatibility with non-POD types

template <typename T>
[[maybe_unused]]
static inline auto link( T *item, T *head ) -> T * {
	assert( item != head );

	T *const oldNext = head->next;

	item->prev = head;
	head->next = item;

	oldNext->prev = item;
	item->next    = oldNext;

	return item;
}

template <typename T>
[[maybe_unused]]
static inline auto unlink( T *item ) -> T * {
	item->prev->next = item->next;
	item->next->prev = item->prev;
	return item;
}

volatile bool con_initialized;

static bool con_hasKeyboardFocus;

static cvar_t *con_maxNotificationTime;
static cvar_t *con_maxNotificationLines;
static cvar_t *con_chatmode;

class Console {
public:
	Console();
	~Console();

	void clearLines();
	void clearNotifications();
	void clearInput();

	void drawPane( unsigned width, unsigned height );
	void drawNotifications( unsigned width, unsigned height );

	enum NotificationBehaviour : uint8_t {
		DrawNotification,
		SuppressNotification,
	};

	void addText( wsw::StringView text, NotificationBehaviour notificationBehaviour );

	void handleKeyDownEvent( int quakeKey );
	void handleCharInputEvent( int key );

	void acceptCommandCompletionResult( unsigned requestId, const CompletionResult &result );

	[[nodiscard]]
	auto dumpLinesToBuffer() const -> wsw::Vector<char>;
private:
	static constexpr unsigned kMaxLines                = 512u;
	static constexpr unsigned kUseDefaultHeapLineLimit = 256u;
	static constexpr unsigned kInputLengthLimit        = 256u;

	struct LineEntry {
		int64_t timestamp { 0 };
		LineEntry *prev { nullptr }, *next { nullptr };
		const char *data { nullptr };
		unsigned dataSize { 0 };
		NotificationBehaviour notificationBehaviour { DrawNotification };
	};

	struct HistoryEntry {
		HistoryEntry *prev { nullptr }, *next { nullptr };
		char *data { nullptr };
		unsigned dataSize { 0 };
	};

	void addLine( wsw::StringView line, NotificationBehaviour notificationBehaviour );
	void destroyLineEntry( LineEntry *entry );
	void destroyAllLines();

	void addToHistory( wsw::StringView line );
	void setCurrHistoryEntry( HistoryEntry *historyEntry );

	[[nodiscard]]
	bool isAwaitingAsyncCompletion() const;

	void printCompletionResultAsList( const char *color, const char *itemSingular, const char *itemPlural,
									  const CompletionResult &completionResult );

	void handleSubmitKeyAction();
	void handleCompleteKeyAction();
	void handleBackspaceKeyAction();
	void handleStepLeftKeyAction();
	void handleStepRightKeyAction();
	void handlePositionCursorAtStartAction();
	void handlePositionCursorAtEndAction();
	void handleDeleteKeyAction();
	void handleHistoryUpKeyAction();
	void handleHistoryDownKeyAction();
	void handleClipboardCopyKeyAction();
	void handleClipboardPasteKeyAction();
	void handleScrollUpKeyAction();
	void handleScrollDownKeyAction();
	void handlePositionPageAtStartAction();
	void handlePositionPageAtEndAction();

	wsw::HeapBasedFreelistAllocator m_linesAllocator { sizeof( LineEntry ) + kUseDefaultHeapLineLimit, kMaxLines };
	wsw::HeapBasedFreelistAllocator m_historyAllocator { sizeof( HistoryEntry ) + kInputLengthLimit + 1, 32 };

	mutable std::mutex m_mutex;

	LineEntry m_lineEntriesHeadnode {};
	HistoryEntry m_historyEntriesHeadnode {};

	HistoryEntry *m_currHistoryEntry { nullptr };

	unsigned m_numLines { 0 };
	unsigned m_requestedLineNumOffset { 0 };
	size_t m_totalNumChars { 0 };

	wsw::StaticString<kInputLengthLimit> m_inputLine;
	unsigned m_inputPos { 0 };

	unsigned m_completionRequestIdsCounter { 0 };
	unsigned m_lastAsyncCompletionRequestId { 0 };
	unsigned m_lastAsyncCompletionKeepLength { 0 };
	unsigned m_lastAsyncCompletionFullLength { 0 };
	int64_t m_lastAsyncCompletionRequestAt { 0 };
};

static SingletonHolder<Console> g_console;

Console::Console() {
	m_lineEntriesHeadnode.prev = &m_lineEntriesHeadnode;
	m_lineEntriesHeadnode.next = &m_lineEntriesHeadnode;

	m_historyEntriesHeadnode.prev = &m_historyEntriesHeadnode;
	m_historyEntriesHeadnode.next = &m_historyEntriesHeadnode;
}

Console::~Console() {
	destroyAllLines();

	for( HistoryEntry *entry = m_historyEntriesHeadnode.next, *next; entry != &m_historyEntriesHeadnode; entry = next ) {
		next = entry->next;
		entry->~HistoryEntry();
		m_historyAllocator.free( entry );
	}
}

void Console::destroyAllLines() {
	[[maybe_unused]] volatile std::scoped_lock<std::mutex> lock( m_mutex );

	for( LineEntry *entry = m_lineEntriesHeadnode.next, *next; entry != &m_lineEntriesHeadnode; entry = next ) {
		next = entry->next;
		destroyLineEntry( entry );
	}

	assert( m_lineEntriesHeadnode.prev = &m_lineEntriesHeadnode );
	assert( m_lineEntriesHeadnode.next = &m_lineEntriesHeadnode );
}

void Console::clearLines() {
	destroyAllLines();
	m_requestedLineNumOffset = 0;
}

void Console::clearNotifications() {
	[[maybe_unused]] volatile std::scoped_lock<std::mutex> lock( m_mutex );

	for( LineEntry *entry = m_lineEntriesHeadnode.next; entry != &m_lineEntriesHeadnode; entry = entry->next ) {
		if( entry->notificationBehaviour == DrawNotification ) {
			entry->timestamp = std::numeric_limits<decltype( entry->timestamp )>::min();
		}
	}
}

void Console::clearInput() {
	m_inputLine.clear();
	m_inputPos = 0;

	// Discard pending completion results, if any
	m_lastAsyncCompletionRequestId  = 0;
	m_lastAsyncCompletionRequestAt  = 0;
	m_lastAsyncCompletionKeepLength = 0;
	m_lastAsyncCompletionFullLength = 0;
}

void Console::addText( wsw::StringView text, NotificationBehaviour notificationBehaviour ) {
	if( text.empty() ) [[unlikely]] {
		return;
	}

	[[maybe_unused]] volatile std::scoped_lock<std::mutex> mutexLock( m_mutex );

	wsw::StringSplitter splitter( text );
	while( const std::optional<wsw::StringView> maybeLine = splitter.getNext( wsw::StringView( "\r\n" ) ) ) {
		addLine( *maybeLine, notificationBehaviour );
	}
}

void Console::addLine( wsw::StringView line, NotificationBehaviour notificationBehaviour ) {
	line = line.trimRight();
	if( line.empty() ) [[unlikely]] {
		return;
	}

	if( m_numLines == kMaxLines ) {
		assert( m_lineEntriesHeadnode.prev != &m_lineEntriesHeadnode );
		LineEntry *oldestEntry = unlink( m_lineEntriesHeadnode.prev );
		assert( oldestEntry );
		destroyLineEntry( oldestEntry );
	}

	void *mem = nullptr;
	if( line.size() >= kUseDefaultHeapLineLimit ) {
		mem = std::malloc( sizeof( LineEntry ) + line.size() + 1 );
		if( !mem ) {
			// TODO: Truncate it correctly
			line = line.take( kUseDefaultHeapLineLimit );
		}
	}

	if( !mem ) {
		mem = m_linesAllocator.allocOrNull();
		assert( mem );
	}

	auto *const newEntry = new( mem )LineEntry;
	auto *const textData = (char *)( newEntry + 1 );
	line.copyTo( textData, line.size() + 1 );

	newEntry->data                  = textData;
	newEntry->dataSize              = line.size();
	newEntry->timestamp             = cls.realtime;
	newEntry->notificationBehaviour = notificationBehaviour;

	link( newEntry, &m_lineEntriesHeadnode );
	m_numLines++;
	m_totalNumChars += line.size();

	if( m_requestedLineNumOffset ) {
		if( m_requestedLineNumOffset < kMaxLines ) {
			m_requestedLineNumOffset++;
		}
	}
}

void Console::destroyLineEntry( LineEntry *entry ) {
	unlink( entry );

	m_totalNumChars -= entry->dataSize;
	m_numLines--;

	entry->~LineEntry();
	if( m_linesAllocator.mayOwn( entry ) ) {
		m_linesAllocator.free( entry );
	} else {
		std::free( entry );
	}
}

[[nodiscard]]
static auto calcNumVisibleCodePoints( std::span<const char> utf8Chars ) -> unsigned {
	const char *s = utf8Chars.data();
	unsigned result = 0;

	while( s < utf8Chars.data() + utf8Chars.size() ) {
		wchar_t c;
		const int gc = Q_GrabWCharFromColorString( &s, &c, NULL );
		if( gc == GRABCHAR_CHAR ) {
			result++;
		} else if( gc == GRABCHAR_COLOR ) {
			;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}

	return result;
}

[[nodiscard]]
static auto calcPrefixLenForNumVisibleCodePoints( const char *utf8Chars, unsigned numCodePoints ) -> unsigned {
	const char *start = utf8Chars;

	while( *utf8Chars && numCodePoints ) {
		wchar_t c;
		const int gc = Q_GrabWCharFromColorString( &utf8Chars, &c, NULL );
		if( gc == GRABCHAR_CHAR ) {
			numCodePoints--;
		} else if( gc == GRABCHAR_COLOR ) {
			;
		} else if( gc == GRABCHAR_END ) {
			break;
		} else {
			assert( 0 );
		}
	}

	return (unsigned)( utf8Chars - start );
}

void Con_DrawNotify( unsigned width, unsigned height ) {
	if( con_initialized ) {
		g_console.instance()->drawNotifications( width, height );
	}
}

void Con_DrawConsole( unsigned width, unsigned height ) {
	if( con_initialized ) {
		g_console.instance()->drawPane( width, height );
	}
}

void Console::drawPane( unsigned width, unsigned height ) {
	const int smallCharHeight = SCR_FontHeight( cls.consoleFont );
	if( !smallCharHeight ) {
		return;
	}

	// get date from system
	time_t long_time;
	time( &long_time );
	struct tm *newtime = localtime( &long_time );

	char version[256];
	Q_snprintfz( version, sizeof( version ), "%02d:%02d %s v%4.2f", newtime->tm_hour, newtime->tm_min, APPLICATION, APP_VERSION );

	if( strlen( APP_VERSION_STAGE ) > 0 ) {
		Q_strncatz( version, " ", sizeof( version ) );
		Q_strncatz( version, APP_VERSION_STAGE, sizeof( version ) );
	}

	const float pixelRatio = Con_GetPixelRatio();
	const int sideMargin   = 8 * pixelRatio;

	// draw the background
	R_DrawStretchPic( 0, 0, width, height, 0, 0, 1, 1, colorWhite, cls.consoleShader );

	const int bottomLineHeight = 2 * pixelRatio;
	SCR_DrawFillRect( 0, height - bottomLineHeight, width, bottomLineHeight, colorOrange );

	const int versionMargin = 4 * pixelRatio;
	const int versionX      = width - SCR_strWidth( version, cls.consoleFont, 0, 0 ) - versionMargin;
	const int versionY      = height - SCR_FontHeight( cls.consoleFont ) - versionMargin;
	SCR_DrawString( versionX, versionY, ALIGN_LEFT_TOP, version, cls.consoleFont, colorOrange, 0 );

	// Draw the input prompt, user text, and cursor if desired

	const int promptWidth         = SCR_strWidth( "]", cls.consoleFont, 1, 0 );
	const int inputWidth          = width - sideMargin * 2 - promptWidth - SCR_strWidth( "_", cls.consoleFont, 1, 0 );
	const int inputReservedHeight = 14 * pixelRatio + smallCharHeight;

	const int inputX = sideMargin + promptWidth;
	const int inputY = height - inputReservedHeight;

	const int inputTextWidth = SCR_strWidth( m_inputLine.data(), cls.consoleFont, 0, 0 );
	const int preWidth = m_inputPos ? SCR_strWidth( m_inputLine.data(), cls.consoleFont, m_inputPos, 0 ) : 0;

	int inputPrestep = 0;
	if( inputTextWidth > inputWidth ) {
		// don't let the cursor go beyond the left screen edge
		clamp_high( inputPrestep, preWidth );
		// don't let it go beyond the right screen edge
		clamp_low( inputPrestep, preWidth - inputWidth );
		// don't leave an empty space after the string when deleting a character
		if( ( inputTextWidth - inputPrestep ) < inputWidth ) {
			inputPrestep = inputTextWidth - inputWidth;
		}
	}

	SCR_DrawRawChar( inputX - promptWidth, inputY, ']', cls.consoleFont, colorWhite );

	SCR_DrawClampString( inputX - inputPrestep, inputY, m_inputLine.data(), inputX, inputY,
						 inputX + inputWidth, inputY + 2 * smallCharHeight, cls.consoleFont, colorWhite, 0 );

	if( (int)( cls.realtime >> 8 ) & 1 ) {
		SCR_DrawRawChar( inputX + preWidth - inputPrestep, inputY, '_', cls.consoleFont, colorWhite );
	}

	int lineY = height - smallCharHeight - inputReservedHeight;

	// Lock during drawing lines

	[[maybe_unused]] volatile std::lock_guard lock( m_mutex );

	if( m_requestedLineNumOffset ) {
		// Patch it each frame
		const unsigned numFittingLines = lineY / smallCharHeight;
		if( m_numLines > numFittingLines ) {
			if( m_requestedLineNumOffset > m_numLines - numFittingLines ) {
				m_requestedLineNumOffset = m_numLines - numFittingLines;
			}
		} else {
			m_requestedLineNumOffset = 0;
		}
	}

	if( m_requestedLineNumOffset ) {
		const int arrowWidth   = SCR_strWidth( "^", cls.consoleFont, 0, 0 );
		const int arrowSpacing = 3 * arrowWidth;

		// draw arrows to show the buffer is backscrolled
		for( unsigned x = arrowSpacing; x + arrowSpacing <= width; x += arrowSpacing ) {
			SCR_DrawRawChar( x, lineY, '^', cls.consoleFont, colorOrange );
		}

		// the arrows obscure one line of scrollback
		lineY -= smallCharHeight;
	}

	// draw from the bottom up
	unsigned numSkippedEntries = 0;
	for( const LineEntry *entry = m_lineEntriesHeadnode.next; entry != &m_lineEntriesHeadnode; entry = entry->next ) {
		if( numSkippedEntries < m_requestedLineNumOffset ) {
			numSkippedEntries++;
		} else {
			SCR_DrawString( sideMargin, lineY, ALIGN_LEFT_TOP, entry->data, cls.consoleFont, colorWhite, 0 );
			lineY -= smallCharHeight;
			if( lineY < -smallCharHeight ) {
				break;
			}
		}
	}
}

void Console::drawNotifications( unsigned width, unsigned height ) {
	const auto maxLines = (unsigned)con_maxNotificationLines->integer;
	if( maxLines <= 0 ) {
		return;
	}

	[[maybe_unused]] volatile std::lock_guard lock( m_mutex );

	const int64_t minTimestamp = cls.realtime - 1000 * wsw::max( 0, con_maxNotificationTime->integer );

	// TODO: Allow specifying a filter to match?
	wsw::StaticVector<const LineEntry *, kMaxLines> matchingLines;
	for( const LineEntry *entry = m_lineEntriesHeadnode.next; entry != &m_lineEntriesHeadnode; entry = entry->next ) {
		if( entry->notificationBehaviour == Console::DrawNotification ) {
			if( entry->timestamp >= minTimestamp && entry->dataSize > 0 ) {
				matchingLines.push_back( entry );
				if( matchingLines.size() == maxLines ) {
					break;
				}
			}
		}
	}

	if( matchingLines.empty() ) {
		return;
	}

	const float pixelRatio  = Con_GetPixelRatio();
	const size_t fontHeight = SCR_FontHeight( cls.consoleFont );

	if( const size_t totalHeight = matchingLines.size() * fontHeight; totalHeight > height ) {
		const size_t extraLines = totalHeight / fontHeight;
		matchingLines.erase( matchingLines.begin(), matchingLines.begin() + extraLines );
	}

	int textY = 0;
	const int textX = 8 * pixelRatio;
	for( const LineEntry *line: matchingLines ) {
		SCR_DrawString( textX, textY, ALIGN_LEFT_TOP, line->data, cls.consoleFont, colorWhite, 0 );
		textY += fontHeight;
	}
}

void Console::addToHistory( wsw::StringView line ) {
	line = line.trim();

	assert( line.size() <= kInputLengthLimit );
	if( line.empty() ) [[unlikely]] {
		return;
	}

	void *mem;
	if( m_historyAllocator.isFull() ) {
		assert( m_historyEntriesHeadnode.prev != &m_historyEntriesHeadnode );
		HistoryEntry *const oldestEntry = m_historyEntriesHeadnode.prev;
		oldestEntry->~HistoryEntry();
		mem = oldestEntry;
	} else {
		mem = m_historyAllocator.allocOrNull();
	}

	auto *const newEntry = new( mem )HistoryEntry;
	auto *const chars    = (char *)( newEntry + 1 );

	line.copyTo( chars, kInputLengthLimit );

	newEntry->data     = chars;
	newEntry->dataSize = line.size();

	link( newEntry, &m_historyEntriesHeadnode );
}

void Console::setCurrHistoryEntry( HistoryEntry *historyEntry ) {
	m_currHistoryEntry = historyEntry;
	if( historyEntry ) {
		m_inputLine.assign( historyEntry->data, historyEntry->dataSize );
	} else {
		m_inputLine.clear();
	}
	m_inputPos = m_inputLine.size();
}

void Console::printCompletionResultAsList( const char *color, const char *itemSingular, const char *itemPlural,
										   const CompletionResult &completionResult ) {
	// TODO: Add column-wise formatting as a custom line wrap function for a line entry
	wsw::String lineBuffer;
	for( const wsw::StringView &view: completionResult ) {
		lineBuffer.append( view.data(), view.size() );
		lineBuffer.push_back( ' ' );
	}

	const char *itemNoun = completionResult.size() == 1 ? itemSingular : itemPlural;
	Com_Printf( "%s%d %s possible\n", color, completionResult.size(), itemNoun );
	Com_Printf( "%s\n", lineBuffer.data() );
}

[[nodiscard]]
static bool isExactlyACommand( const wsw::StringView &text ) {
	return CL_GetCmdSystem()->isARegisteredCommand( text );
}

[[nodiscard]]
static bool isExactlyAVar( const wsw::StringView &text ) {
	if( text.size() <= MAX_STRING_CHARS ) {
		if( text.isZeroTerminated() ) {
			return Cvar_Find( text.data() );
		}
		return Cvar_Find( wsw::StaticString<MAX_STRING_CHARS>( text ).data() );
	}
	return false;
}

[[nodiscard]]
static bool isExactlyAnAlias( const wsw::StringView &text ) {
	return CL_GetCmdSystem()->isARegisteredAlias( text );
}

[[nodiscard]]
static auto takeCommandLikePrefix( const wsw::StringView &text ) -> wsw::StringView {
	return text.takeWhile( []( char ch ) { return (size_t)ch > (size_t)' ' && ch != ';'; } );
}

bool Console::isAwaitingAsyncCompletion() const {
	return m_lastAsyncCompletionRequestAt + 500 > cls.realtime;
}

void Console::acceptCommandCompletionResult( unsigned requestId, const CompletionResult &result ) {
	// Timed out/obsolete?
	if( m_lastAsyncCompletionRequestId != requestId ) {
		return;
	}

	if( !result.empty() ) {
		// Sanity checks
		if( m_lastAsyncCompletionFullLength == m_inputLine.length() && m_inputLine.length() < kInputLengthLimit ) {
			// Sanity checks
			if( m_lastAsyncCompletionKeepLength <= m_lastAsyncCompletionFullLength ) {
				const unsigned maxCharsToAdd = kInputLengthLimit - m_inputLine.size();
				m_inputLine.erase( m_lastAsyncCompletionKeepLength );
				m_inputLine.append( ' ' );
				if( result.size() == 1 ) {
					m_inputLine.append( result.front().take( maxCharsToAdd ) );
				} else {
					m_inputLine.append( result.getLongestCommonPrefix().take( maxCharsToAdd ) );
					printCompletionResultAsList( S_COLOR_GREEN, "argument", "arguments", result );
				}
				m_inputPos = m_inputLine.size();
			}
		}
	}

	m_lastAsyncCompletionRequestId  = 0;
	m_lastAsyncCompletionRequestAt  = 0;
	m_lastAsyncCompletionKeepLength = 0;
	m_lastAsyncCompletionFullLength = 0;
}

void Console::handleCompleteKeyAction() {
	if( isAwaitingAsyncCompletion() ) {
		return;
	}

	// Don't make a completion attempt while the cursor is in the middle of the line (should we?)
	if( m_inputPos != m_inputLine.size() ) {
		return;
	}
	if( m_inputLine.full() ) [[unlikely]] {
		return;
	}

	bool droppedFirstChar     = false;
	wsw::StringView inputText = m_inputLine.asView();
	if( inputText.startsWith( '/' ) || inputText.startsWith( '\\' ) ) {
		inputText        = inputText.drop( 1 );
		droppedFirstChar = true;
	}

	const wsw::StringView &maybeCommandLikePrefix = takeCommandLikePrefix( inputText );
	if( maybeCommandLikePrefix.empty() ) [[unlikely]] {
		return;
	}

	if( isExactlyACommand( maybeCommandLikePrefix ) ) {
		const wsw::StringView &partial = inputText.drop( maybeCommandLikePrefix.size() ).trim();

		m_lastAsyncCompletionRequestId = ++m_completionRequestIdsCounter;
		if( !m_lastAsyncCompletionRequestId ) {
			// Don't let the resulting id be zero TODO: Use std::optional<> ?
			m_lastAsyncCompletionRequestId = ++m_completionRequestIdsCounter;
		}

		m_lastAsyncCompletionRequestAt  = cls.realtime;
		m_lastAsyncCompletionKeepLength = maybeCommandLikePrefix.size() + ( droppedFirstChar ? 1 : 0 );
		m_lastAsyncCompletionFullLength = m_inputLine.size();

		CL_Cmd_SubmitCompletionRequest( maybeCommandLikePrefix, m_lastAsyncCompletionRequestId, partial );
	} else if( isExactlyAVar( maybeCommandLikePrefix ) ) {
		// TODO: Implement completion for command values?
		;
	} else if( isExactlyAnAlias( maybeCommandLikePrefix ) ) {
		// TODO: Could aliases accept args?
		;
	} else if( maybeCommandLikePrefix.size() == inputText.size() ) {
		const CompletionResult &cmdCompletionResult   = CL_GetPossibleCommands( maybeCommandLikePrefix );
		const CompletionResult &varCompletionResult   = Cvar_CompleteBuildList( maybeCommandLikePrefix );
		const CompletionResult &aliasCompletionResult = CL_GetPossibleAliases( maybeCommandLikePrefix );

		const size_t totalSize = cmdCompletionResult.size() + varCompletionResult.size() + aliasCompletionResult.size();
		if( totalSize == 0 ) {
			Com_Printf( "No matching aliases, commands or vars were found\n" );
		} else if( totalSize == 1 ) {
			wsw::StringView chosenCompletion;
			if( !cmdCompletionResult.empty() ) {
				chosenCompletion = cmdCompletionResult.front();
			} else if( !varCompletionResult.empty() ) {
				chosenCompletion = varCompletionResult.front();
			} else if( !aliasCompletionResult.empty() ) {
				chosenCompletion = aliasCompletionResult.front();
			} else {
				wsw::failWithLogicError( "Unreachable" );
			}
			assert( !chosenCompletion.empty() );
			m_inputLine.erase( droppedFirstChar ? 1 : 0 );
			m_inputLine.append( chosenCompletion.take( kInputLengthLimit - m_inputLine.size() ) );
			m_inputPos = m_inputLine.size();
		} else {
			if( !cmdCompletionResult.empty() ) {
				printCompletionResultAsList( S_COLOR_RED, "command", "commands", cmdCompletionResult );
			}
			if( !varCompletionResult.empty() ) {
				printCompletionResultAsList( S_COLOR_CYAN, "var", "vars", varCompletionResult );
			}
			if( !aliasCompletionResult.empty() ) {
				printCompletionResultAsList( S_COLOR_MAGENTA, "alias", "aliases", aliasCompletionResult );
			}
		}
	}
}

void Console::handleBackspaceKeyAction() {
	if( m_inputPos > 0 ) {
		// TODO: Restore the old behaviour that allows breaking in the middle of a color token?
		handleStepLeftKeyAction();
		handleDeleteKeyAction();
	}
}

void Console::handleStepLeftKeyAction() {
	if( m_inputPos > 0 ) {
		if( const unsigned numCodePoints = calcNumVisibleCodePoints( { m_inputLine.data(), m_inputPos } ) ) {
			m_inputPos = calcPrefixLenForNumVisibleCodePoints( m_inputLine.data(), numCodePoints - 1 );
		}
	}
}

void Console::handleStepRightKeyAction() {
	if( m_inputPos < m_inputLine.size() ) {
		const unsigned numCodePoints = calcNumVisibleCodePoints( { m_inputLine.data(), m_inputPos } );
		m_inputPos = calcPrefixLenForNumVisibleCodePoints( m_inputLine.data(), numCodePoints + 1 );
	}
}

void Console::handlePositionCursorAtStartAction() {
	m_inputPos = 0;
}

void Console::handlePositionCursorAtEndAction() {
	m_inputPos = m_inputLine.size();
}

void Console::handleDeleteKeyAction() {
	if( m_inputPos < m_inputLine.size() ) {
		char *s = m_inputLine.data() + m_inputPos;
		if( Q_GrabWCharFromUtf8String( (const char **)&s ) ) {
			assert( s && s > m_inputLine.data() + m_inputPos );
			const ptrdiff_t utf8SpanLength = s - ( m_inputLine.data() + m_inputPos );
			m_inputLine.erase( m_inputPos, (unsigned)( utf8SpanLength ) );
		}
	}
}

void Console::handleHistoryUpKeyAction() {
	HistoryEntry *historyEntry = nullptr;
	if( m_currHistoryEntry ) {
		if( m_currHistoryEntry->next != &m_historyEntriesHeadnode ) {
			historyEntry = m_currHistoryEntry->next;
		}
	} else {
		if( m_historyEntriesHeadnode.next != &m_historyEntriesHeadnode ) {
			historyEntry = m_historyEntriesHeadnode.next;
		}
	}
	setCurrHistoryEntry( historyEntry );
}

void Console::handleHistoryDownKeyAction() {
	HistoryEntry *historyEntry = nullptr;
	if( m_currHistoryEntry ) {
		if( m_currHistoryEntry->prev != &m_historyEntriesHeadnode ) {
			historyEntry = m_currHistoryEntry->prev;
		}
	} else {
		if( m_historyEntriesHeadnode.prev != &m_historyEntriesHeadnode ) {
			historyEntry = m_historyEntriesHeadnode.prev;
		}
	}
	setCurrHistoryEntry( historyEntry );
}

void Console::handleScrollUpKeyAction() {
	if( m_requestedLineNumOffset < kMaxLines ) {
		m_requestedLineNumOffset++;
	}
}

void Console::handleScrollDownKeyAction() {
	if( m_requestedLineNumOffset ) {
		m_requestedLineNumOffset--;
	}
}

void Console::handlePositionPageAtStartAction() {
	m_requestedLineNumOffset = kMaxLines;
}

void Console::handlePositionPageAtEndAction() {
	m_requestedLineNumOffset = 0;
}

void Console::handleClipboardCopyKeyAction() {
	const wsw::Vector<char> &lines = dumpLinesToBuffer();
	CL_SetClipboardData( lines.data() );
}

void Console::handleClipboardPasteKeyAction() {
	assert( m_inputPos <= m_inputLine.size() );

	bool shouldFlushLine  = false;
	char *const clipboardData = CL_GetClipboardData();

	const wsw::CharLookup separators { wsw::StringView( "\r\n" ) };
	wsw::StringSplitter splitter { wsw::StringView( clipboardData ) };
	while( const std::optional<wsw::StringView> maybeLine = splitter.getNext( separators ) ) {
		if( const wsw::StringView &trimmedLine = maybeLine->trim(); !trimmedLine.empty() ) {
			if( shouldFlushLine ) {
				// Flush it TODO make this more clear
				handleSubmitKeyAction();
				assert( m_inputLine.empty() && m_inputPos == 0 );
			}
			size_t maxCharsToInsert = m_inputLine.capacity() - m_inputLine.size();
			if( !maxCharsToInsert ) {
				// Flush if we can't insert even a single character
				handleSubmitKeyAction();
				assert( m_inputLine.empty() && m_inputPos == 0 );
				maxCharsToInsert = m_inputLine.capacity();
			}
			const size_t numCharsToInsert = wsw::min( maxCharsToInsert, trimmedLine.size() );
			m_inputLine.insert( m_inputLine.begin() + m_inputPos, trimmedLine.take( numCharsToInsert ) );
			m_inputPos = m_inputLine.size();
			// Allow the last line stay in the input buffer
			shouldFlushLine = true;
		}
	}
	CL_FreeClipboardData( clipboardData );
}

[[nodiscard]]
static inline bool isKeyDown( int key ) {
	return wsw::cl::KeyHandlingSystem::instance()->isKeyDown( key );
}

[[nodiscard]]
static bool isCtrlKeyDown() { return isKeyDown( K_LCTRL ) || isKeyDown( K_RCTRL ); }

void Console::handleSubmitKeyAction() {
	enum { Command, Chat, Teamchat } type;

	if( cls.state <= CA_HANDSHAKE || cls.demoPlayer.playing ) {
		type = Command;
	} else if( m_inputLine.startsWith( '\\') || m_inputLine.startsWith( '/' ) ) {
		type = Command;
	} else if( isCtrlKeyDown() ) {
		type = Teamchat;
	} else if( con_chatmode && con_chatmode->integer == 1 ) {
		type = Chat;
	} else {
		const wsw::StringView &prefix = takeCommandLikePrefix( m_inputLine.asView() );
		if( isExactlyACommand( prefix ) || isExactlyAVar( prefix ) || isExactlyAnAlias( prefix ) ) {
			type = Command;
		} else {
			type = Chat;
		}
	}

	// do appropriate action
	if( type != Command ) {
		if( !m_inputLine.asView().trimLeft().empty() ) {
			if( type == Teamchat ) {
				Con_SendTeamChatMessage( m_inputLine.asView() );
			} else {
				Con_SendCommonChatMessage( m_inputLine.asView() );
			}
		}
	} else {
		const unsigned skipLen = ( m_inputLine.startsWith( '\\' ) || m_inputLine.startsWith( '/' ) ) ? 1 : 0;
		if( skipLen < m_inputLine.size() ) {
			CL_Cbuf_AppendCommand( m_inputLine.data() + skipLen );
		}
	}

	// echo to the console and cycle command history
	Com_Printf( "]%s\n", m_inputLine.data() );

	addToHistory( m_inputLine.asView() );

	setCurrHistoryEntry( nullptr );
}

void Console::handleKeyDownEvent( int key ) {
	if( isAwaitingAsyncCompletion() ) {
		return;
	}

	if( ( key == K_INS || key == KP_INS ) && ( isKeyDown( K_LSHIFT ) || isKeyDown( K_RSHIFT ) ) ) {
		return handleClipboardPasteKeyAction();
	}
	if( key == K_ENTER || key == KP_ENTER ) {
		return handleSubmitKeyAction();
	}
	if( key == K_TAB ) {
		return handleCompleteKeyAction();
	}
	if( key == K_LEFTARROW || key == KP_LEFTARROW ) {
		return handleStepLeftKeyAction();
	}
	if( key == K_BACKSPACE ) {
		return handleBackspaceKeyAction();
	}
	if( key == K_DEL ) {
		return handleDeleteKeyAction();
	}
	if( key == K_RIGHTARROW || key == KP_RIGHTARROW ) {
		return handleStepRightKeyAction();
	}
	if( key == K_UPARROW || key == KP_UPARROW ) {
		return handleHistoryUpKeyAction();
	}
	if( key == K_DOWNARROW || key == KP_DOWNARROW ) {
		return handleHistoryDownKeyAction();
	}
	if( key == K_PGUP || key == KP_PGUP || key == K_MWHEELUP ) { // wsw : pb : support mwheel in console
		return handleScrollUpKeyAction();
	}
	if( key == K_PGDN || key == KP_PGDN || key == K_MWHEELDOWN ) { // wsw : pb : support mwheel in console
		return handleScrollDownKeyAction();
	}
	if( key == K_HOME || key == KP_HOME ) {
		return isCtrlKeyDown() ? handlePositionPageAtStartAction() : handlePositionCursorAtStartAction();
	}
	if( key == K_END || key == KP_END ) {
		return isCtrlKeyDown() ? handlePositionPageAtEndAction() : handlePositionPageAtEndAction();
	}

	// key is a normal printable key normal which wil be HANDLE later in response to WM_CHAR event
}

void Console::handleCharInputEvent( int key ) {
	if( isAwaitingAsyncCompletion() ) {
		return;
	}

	switch( key ) {
		case 22: // CTRL+V
			return handleClipboardPasteKeyAction();
		case 12: // CTRL+L
			return CL_Cbuf_AppendCommand( "clear" );
		case 16: // CTRL+P
			return handleHistoryUpKeyAction();
		case 14: // CTRL+N
			return handleHistoryDownKeyAction();
		case 3: // CTRL+C
			return handleClipboardCopyKeyAction();
		case 1: // CTRL+A
			return handlePositionCursorAtStartAction();
		case 5: // CTRL+E
			return handlePositionCursorAtEndAction();
		default:
			break;
	}

	if( ( key >= ' ' && key < 0xFFFF ) && m_inputPos < m_inputLine.capacity() ) {
		const char *utf = Q_WCharToUtf8Char( key );
		const size_t utflen = std::strlen( utf );

		if( m_inputLine.size() + utflen < m_inputLine.capacity() ) {
			m_inputLine.insert( m_inputLine.data() + m_inputPos, wsw::StringView( utf, utflen ) );
			m_inputPos += utflen;
		}
	}
}

auto Console::dumpLinesToBuffer() const -> wsw::Vector<char> {
	[[maybe_unused]] volatile std::lock_guard lock( m_mutex );

	const size_t bufferSize = m_totalNumChars + 2 * m_numLines + 1;
	wsw::Vector<char> result;
	result.reserve( bufferSize );

	assert( m_totalNumChars + 2 * m_numLines < bufferSize );

	for( LineEntry *entry = m_lineEntriesHeadnode.prev; entry != &m_lineEntriesHeadnode; entry = entry->prev ) {
		result.insert( result.end(), entry->data, entry->data + entry->dataSize );
		result.push_back( '\r' );
		result.push_back( '\n' );
	}

	result.push_back( '\0' );
	return result;
}

[[nodiscard]]
static auto translateNumPadKey( int key ) -> int {
	switch( key ) {
		case KP_SLASH: return '/';
		case KP_STAR: return '*';
		case KP_MINUS: return '-';
		case KP_PLUS: return '+';
		case KP_HOME: return '7';
		case KP_UPARROW: return '8';
		case KP_PGUP: return '9';
		case KP_LEFTARROW: return '4';
		case KP_5: return '5';
		case KP_RIGHTARROW: return '6';
		case KP_END: return '1';
		case KP_DOWNARROW: return '2';
		case KP_PGDN: return '3';
		case KP_INS: return '0';
		case KP_DEL: return '.';
		default: return key;
	}
}

[[nodiscard]]
static bool canManipulateConsoleInThisState() {
	return con_initialized && cls.state != CA_GETTING_TICKET && cls.state != CA_CONNECTING && cls.state != CA_CONNECTED;
}

bool Con_HasKeyboardFocus() {
	return con_hasKeyboardFocus;
}

bool Con_HandleCharEvent( wchar_t key ) {
	if( Con_HasKeyboardFocus() ) {
		if( canManipulateConsoleInThisState() ) {
			g_console.instance()->handleCharInputEvent( translateNumPadKey( key ) );
		}
		return true;
	}
	return false;
}

bool Con_HandleKeyEvent( int key, bool down ) {
	if( Con_HasKeyboardFocus() ) {
		// TODO: Check whether the key is a "consolekey" (see old keys.cpp)
		if( down ) {
			if( canManipulateConsoleInThisState() ) {
				g_console.instance()->handleKeyDownEvent( key );
			}
		}
		return true;
	}
	return false;
}

void Con_AcceptCompletionResult( unsigned requestId, const CompletionResult &result ) {
	if( con_initialized ) {
		g_console.instance()->acceptCommandCompletionResult( requestId, result );
	}
}

void Con_Close( void ) {
	if( con_initialized ) {
		g_console.instance()->clearInput();
		g_console.instance()->clearNotifications();
	}

	CL_ClearInputState();
}

void Con_ToggleConsole_f( const CmdArgs & ) {
	if( canManipulateConsoleInThisState() ) {
		g_console.instance()->clearInput();
		g_console.instance()->clearNotifications();
		con_hasKeyboardFocus = !con_hasKeyboardFocus;
	}
}

static void Con_Clear_f( const CmdArgs & ) {
	if( con_initialized ) {
		g_console.instance()->clearLines();
		g_console.instance()->clearInput();
	}
}

static void Con_Dump_f( const CmdArgs &cmdArgs ) {
	if( !con_initialized ) {
		return;
	}

	if( cmdArgs.size() < 2 ) {
		Com_Printf( "Usage: condump <filename>\n" );
		return;
	}

	wsw::String name;
	name.resize( 16, '\0' );
	name.insert( name.begin(), cmdArgs[1].begin(), cmdArgs[1].end() );
	COM_DefaultExtension( name.data(), ".txt", name.size() );
	COM_SanitizeFilePath( name.data() );
	name.erase( std::strlen( name.data() ) );

	if( !COM_ValidateRelativeFilename( name.data() ) ) {
		Com_Printf( "Invalid filename\n" );
	} else {
		const wsw::StringView nameView( name.data(), name.size(), wsw::StringView::ZeroTerminated );
		if( auto maybeHandle = wsw::fs::openAsWriteHandle( nameView ) ) {
			const wsw::Vector<char> &dump = g_console.instance()->dumpLinesToBuffer();
			if( !maybeHandle->write( dump.data(), dump.size() - 1 ) ) {
				Com_Printf( "Failed to write %s\n", name.data() );
			} else {
				Com_Printf( "Dumped console text to %s\n", name.data() );
			}
		} else {
			Com_Printf( "Failed to open %s\n", name.data() );
		}
	}
}

void Con_ClearNotify( void ) {
	if( con_initialized ) {
		g_console.instance()->clearNotifications();
	}
}

void Con_CheckResize( void ) {
}

float Con_GetPixelRatio( void ) {
	float pixelRatio = VID_GetPixelRatio();
	clamp_low( pixelRatio, 0.5f );
	return pixelRatio;
}

void Con_Init( void ) {
	if( con_initialized ) {
		return;
	}

	g_console.init();

	Com_Printf( "Console initialized.\n" );

	con_maxNotificationTime  = Cvar_Get( "con_maxNotificationTime", "3", CVAR_ARCHIVE );
	con_maxNotificationLines = Cvar_Get( "con_maxNotificationLines", "4", CVAR_ARCHIVE );
	con_chatmode             = Cvar_Get( "con_chatmode", "3", CVAR_ARCHIVE );

	CL_Cmd_Register( "toggleconsole"_asView, Con_ToggleConsole_f );
	CL_Cmd_Register( "clear"_asView, Con_Clear_f );
	CL_Cmd_Register( "condump"_asView, Con_Dump_f );
	con_initialized = true;
}

void Con_Shutdown( void ) {
	if( !con_initialized ) {
		return;
	}

	CL_Cmd_Unregister( "toggleconsole"_asView );
	CL_Cmd_Unregister( "clear"_asView );
	CL_Cmd_Unregister( "condump"_asView );

	g_console.shutdown();

	con_initialized = false;
}

void Con_Print( const char *txt ) {
	if( con_initialized ) {
		g_console.instance()->addText( wsw::StringView( txt ), Console::DrawNotification );
	}
}

void Con_PrintSilent( const char *txt ) {
	if( con_initialized ) {
		g_console.instance()->addText( wsw::StringView( txt ), Console::SuppressNotification );
	}
}

[[nodiscard]]
static uint64_t Con_SendChatMessage( const wsw::StringView &text, bool team ) {
	// MAX_CHAT_BYTES includes trailing '\0'
	wsw::StaticString<MAX_CHAT_BYTES - 1> sanitizedText;
	for( const char ch: text ) {
		sanitizedText.append( ( ch != '"' ) ? ch : '\'' );
		if( sanitizedText.full() ) [[unlikely]] {
			break;
		}
	}

	const char *cmd;
	if( team && CL_Cmd_Exists( wsw::StringView( "say_team" ) ) ) {
		cmd = "say_team";
	} else if( CL_Cmd_Exists( wsw::StringView( "say" ) ) ) {
		cmd = "say";
	} else {
		cmd = "cmd say";
	}

	const auto oldCmdNum = cls.reliableSequence;
	CL_Cmd_ExecuteNow( va( "%s \"%s\"\n", cmd, sanitizedText.data() ) );
	assert( oldCmdNum + 1 == cls.reliableSequence );
	return oldCmdNum;
}

uint64_t Con_SendCommonChatMessage( const wsw::StringView &text ) {
	return Con_SendChatMessage( text, false );
}

uint64_t Con_SendTeamChatMessage( const wsw::StringView &text ) {
	return Con_SendChatMessage( text, true );
}