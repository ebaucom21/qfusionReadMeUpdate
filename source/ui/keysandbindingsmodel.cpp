#include <QQuickItem>

#include "keysandbindingsmodel.h"
#include "../client/keys.h"

using wsw::operator""_asView;

namespace wsw::ui {

struct KeyboardRowEntry {
	const char *text { "" };
	int quakeKey { -1 };
	float layoutWeight { 1.0 };
	bool hidden { false };
	bool enabled { true };
	uint8_t rowSpan { 1 };

	static constexpr auto spacer() noexcept -> KeyboardRowEntry {
		return { "", -1, 1.0, true, false, 1 };
	}
};

static const KeyboardRowEntry kMainPadRow1[] {
	{ "Esc", -1, 1.0, false, false },
	KeyboardRowEntry::spacer(),
	{ "F1", K_F1 },
	{ "F2", K_F2 },
	{ "F3", K_F3 },
	{ "F4", K_F4 },
	KeyboardRowEntry::spacer(),
	{ "F5", K_F5 },
	{ "F6", K_F6 },
	{ "F7", K_F7 },
	{ "F8", K_F8 },
	KeyboardRowEntry::spacer(),
	{ "F9", K_F9 },
	{ "F10", K_F10 },
	{ "F11", K_F11 },
	{ "F12", K_F12 }
};

static const KeyboardRowEntry kMainPadRow2[] {
	{ "~" },
	{ "1", (int)'1' },
	{ "2", (int)'2' },
	{ "3", (int)'3' },
	{ "4", (int)'4' },
	{ "5", (int)'5' },
	{ "6", (int)'6' },
	{ "7", (int)'7' },
	{ "8", (int)'8' },
	{ "9", (int)'9' },
	{ "0", (int)'0' },
	{ "-", (int)'-' },
	{ "+", (int)'=' },
	{ "\u232B", K_BACKSPACE, 2.0 }
};

static const KeyboardRowEntry kMainPadRow3[] {
	{ "\u21C4", K_TAB, 1.5 },
	{ "Q", (int)'q' },
	{ "W", (int)'w' },
	{ "E", (int)'e' },
	{ "R", (int)'r' },
	{ "T", (int)'t' },
	{ "Y", (int)'y' },
	{ "U", (int)'u' },
	{ "I", (int)'i' },
	{ "O", (int)'o' },
	{ "P", (int)'p' },
	{ "[", (int)'[' },
	{ "]", (int)']' },
	{ "\\", (int)'\\', 1.5 }
};

static KeyboardRowEntry kMainPadRow4[] {
	{ "CAPS", K_CAPSLOCK, 2.0 },
	{ "A", (int)'a' },
	{ "S", (int)'s' },
	{ "D", (int)'d' },
	{ "F", (int)'f' },
	{ "G", (int)'g' },
	{ "H", (int)'h' },
	{ "J", (int)'j' },
	{ "K", (int)'k' },
	{ "L", (int)'l' },
	{ ":", (int)';' },
	{ "\"", (int)'\'' },
	{ "ENTER", K_ENTER, 2.0 }
};

static const KeyboardRowEntry kMainPadRow5[] {
	{ "SHIFT", K_LSHIFT, 2.5 },
	{ "Z", (int)'z' },
	{ "X", (int)'x' },
	{ "C", (int)'c' },
	{ "V", (int)'v' },
	{ "B", (int)'b' },
	{ "N", (int)'n' },
	{ "M", (int)'m' },
	{ "<", (int)',' },
	{ ">", (int)'.' },
	{ "?", (int)'/' },
	{ "SHIFT", K_RSHIFT, 2.5 }
};

static const KeyboardRowEntry kMainPadRow6[] {
	{ "CTRL", K_LCTRL, 1.5 },
	{ "\u2756", K_WIN },
	{ "ALT", K_LALT, 1.5 },
	{ "SPACE", K_SPACE, 6 },
	{ "ALT", K_RALT, 1.5 },
	{ "\u25A4", K_MENU },
	{ "CTRL", K_RCTRL, 1.5 }
};

static const KeyboardRowEntry kArrowPadRow1[] {
	{ "INS", K_INS },
	{ "HM",  K_HOME },
	{ "PU",  K_PGUP },
};

static const KeyboardRowEntry kNumPadRow1[] {
	{ "NUM", K_NUMLOCK },
	{ "/", KP_SLASH },
	{ "*", KP_MULT },
	{ "-", KP_MINUS }
};

static const KeyboardRowEntry kArrowPadRow2[] {
	{ "DEL", K_DEL },
	{ "END", K_END },
	{ "PD",  K_PGDN }
};

static const KeyboardRowEntry kNumPadRow2[] {
	{ "7", 0 },
	{ "8", 0 },
	{ "9", 0 },
	{ "+", KP_PLUS, 1.0, false, true, 2 }
};

static const KeyboardRowEntry kArrowPadRow3[] {
	KeyboardRowEntry::spacer()
};

static const KeyboardRowEntry kNumPadRow3[] {
	{ "4", 0 },
	{ "5", 0 },
	{ "6", 0 },
	KeyboardRowEntry::spacer()
};

static const KeyboardRowEntry kArrowPadRow4[] {
	KeyboardRowEntry::spacer(),
	{ "\u2B06", K_UPARROW },
	KeyboardRowEntry::spacer()
};

static const KeyboardRowEntry kNumPadRow4[] {
	{ "1", 0 },
	{ "2", 0 },
	{ "3", 0 },
	{ "\u23CE", KP_ENTER, 1.0, false, true, 2 }
};

static const KeyboardRowEntry kArrowPadRow5[] {
	{ "\u2B05", K_LEFTARROW },
	{ "\u2B07", K_DOWNARROW },
	{ "\u27A1", K_RIGHTARROW }
};

static const KeyboardRowEntry kNumPadRow5[] {
	{ "0", 0, 2.0 },
	{ ".", 0 },
	KeyboardRowEntry::spacer()
};

static auto keyboardRowToJsonArray( const KeyboardRowEntry *begin, const KeyboardRowEntry *end ) -> QJsonArray {
	QJsonArray result;
	for( const KeyboardRowEntry *e = begin; e != end; ++e ) {
		QJsonObject obj {
			{ "text", e->text },
			{ "quakeKey", e->quakeKey },
			{ "layoutWeight", e->layoutWeight },
			{ "enabled", e->enabled },
			{ "hidden", e->hidden },
			{ "rowSpan", e->rowSpan },
			{ "group", 0 }
		};
		result.append( obj );
	}
	return result;
}

template <typename T>
static auto keyboardRowToJsonArray( const T &row ) -> QJsonArray {
	return keyboardRowToJsonArray( std::begin( row ), std::end( row ) );
}

void KeysAndBindingsModel::checkUpdates() {
	if( m_isTrackingUpdates ) {
		// Like a "CAS" atomic operation
		if( wsw::cl::KeyBindingsSystem::instance()->getAndResetModifiedStatus() ) {
			reload();
		}
	}
}

void KeysAndBindingsModel::reload() {
	for( auto &keys : m_boundKeysForCommand ) {
		keys.clear();
	}

	reloadKeyBindings( m_keyboardMainPadRowModel, "keyboardMainPadRow"_asView );
	reloadKeyBindings( m_keyboardArrowPadRowModel, "keyboardArrowPadRow"_asView );
	reloadKeyBindings( m_keyboardNumPadRowModel, "keyboardNumPadRow"_asView );

	reloadColumnCommandBindings( m_commandsMovementColumnModel, "commandsMovementColumnChanged"_asView );
	reloadColumnCommandBindings( m_commandsWeaponsColumnModel[0], "commandsWeaponsColumn1Changed"_asView );
	reloadColumnCommandBindings( m_commandsWeaponsColumnModel[1], "commandsWeaponsColumn2Changed"_asView );
	reloadColumnCommandBindings( m_commandsActionsColumnModel, "commandsActionsColumnChanged"_asView );
	reloadColumnCommandBindings( m_commandsRespectColumnModel[0], "commandsRespectColumn1Changed"_asView );
	reloadColumnCommandBindings( m_commandsRespectColumnModel[1], "commandsRespectColumn2Changed"_asView );
}

static_assert( KeysAndBindingsModel::MovementGroup == 1 );
static_assert( KeysAndBindingsModel::ActionGroup == 2 );
static_assert( KeysAndBindingsModel::WeaponGroup == 3 );
static_assert( KeysAndBindingsModel::RespectGroup == 4 );
static_assert( KeysAndBindingsModel::UnknownGroup == 5 );

static const QColor kColorForGroup[] = {
	QColor::fromRgbF( colorGreen[0], colorGreen[1], colorGreen[2], colorGreen[3] ),
	QColor::fromRgbF( colorCyan[0], colorCyan[1], colorCyan[2], colorCyan[3] ),
	QColor::fromRgbF( colorRed[0], colorRed[1], colorRed[2], colorRed[3] ),
	QColor::fromRgbF( colorMagenta[0], colorMagenta[1], colorMagenta[2], colorMagenta[3] ),
	QColor::fromRgbF( colorLtGrey[0], colorLtGrey[1], colorLtGrey[2], colorLtGrey[3] )
};

static const QColor kTransparentColor( QColor::fromRgb( 0, 0, 0, 0 ) );

auto KeysAndBindingsModel::colorForGroup( int group ) const -> QColor {
	if( group >= MovementGroup && group <= UnknownGroup ) {
		return kColorForGroup[group - MovementGroup];
	}
	return kTransparentColor;
}

template <typename Array>
void KeysAndBindingsModel::reloadKeyBindings( Array &array, const wsw::StringView &changedSignalPrefix ) {
	reloadKeyBindings( std::begin( array ), std::end( array ), changedSignalPrefix );
}

static const wsw::StringView kChangedSuffix( "Changed" );

struct ChangedSignalNameComposer {
	const wsw::StringView m_prefix;
	char m_buffer[64];
	bool m_initialized { false };

	explicit ChangedSignalNameComposer( const wsw::StringView &signalPrefix )
		: m_prefix( signalPrefix ) {}

	void initBuffer();

	auto getSignalNameForNum( int num ) -> const char *;
};

void ChangedSignalNameComposer::initBuffer() {
	const size_t prefixLen = m_prefix.length();
	const size_t totalLen = prefixLen + kChangedSuffix.length() + 1;
	assert( totalLen < sizeof( m_buffer ) );
	m_prefix.copyTo( m_buffer, sizeof( m_buffer ) );
	kChangedSuffix.copyTo( m_buffer + prefixLen + 1, sizeof( m_buffer ) - prefixLen - 1 );
	m_buffer[totalLen] = '\0';
}

auto ChangedSignalNameComposer::getSignalNameForNum( int num ) -> const char * {
	assert( num >= 1 && num <= 9 );
	if( !m_initialized ) {
		initBuffer();
		m_initialized = true;
	}

	m_buffer[m_prefix.length()] = (char)( num + '0' );
	return m_buffer;
}

void KeysAndBindingsModel::reloadKeyBindings( QJsonArray *rowsBegin, QJsonArray *rowsEnd,
											  const wsw::StringView &changedSignalPrefix ) {
	ChangedSignalNameComposer signalNameComposer( changedSignalPrefix );
	for( QJsonArray *row = rowsBegin; row != rowsEnd; ++row ) {
		if( !reloadRowKeyBindings( *row ) ) {
			continue;
		}

		const auto signalNum = (int)( row - rowsBegin );
		QMetaObject::invokeMethod( this, signalNameComposer.getSignalNameForNum( signalNum ) );
	}
}


bool KeysAndBindingsModel::reloadRowKeyBindings( QJsonArray &row ) {
	bool wasRowModified = false;
	for( QJsonValueRef ref: row ) {
		if( reloadRowKeyEntry( ref ) ) {
			wasRowModified = true;
		}
	}
	return wasRowModified;
}

static const QString kGroup( "group" );
static const QString kQuakeKey( "quakeKey" );

bool KeysAndBindingsModel::reloadRowKeyEntry( QJsonValueRef ref ) {
	assert( ref.isObject() );
	// We have to modify an object and assign the ref back to modify a field
	QJsonObject obj( ref.toObject() );
	const int quakeKey = obj[kQuakeKey].toInt();
	wsw::String &lastBinding = m_lastKeyBindings[quakeKey];

	if( const auto maybeCurrBinding = wsw::cl::KeyBindingsSystem::instance()->getBindingForKey( quakeKey ) ) {
		const wsw::StringView currBinding( *maybeCurrBinding );
		const wsw::StringView lastBindingView( lastBinding.data(), lastBinding.size(), wsw::StringView::ZeroTerminated );
		const auto maybeCommand = getCommandNum( currBinding );
		if( maybeCommand ) {
			m_boundKeysForCommand[*maybeCommand].push_back( quakeKey );
		}
		if( lastBindingView != currBinding ) {
			lastBinding.assign( currBinding.data(), currBinding.size() );
			obj[kGroup] = maybeCommand ? m_commandBindingGroups[*maybeCommand] : (int) UnknownGroup;
			ref = obj;
			return true;
		}
		return false;
	}

	if( !lastBinding.empty() ) {
		lastBinding.clear();
		static_assert( UnknownGroup != 0, "An unknown group is a group for an unknown but present binding" );
		obj[kGroup] = 0;
		ref = obj;
		return true;
	}

	return false;
}

void KeysAndBindingsModel::reloadColumnCommandBindings( QJsonArray &column, const wsw::StringView &changedSignal ) {
	bool wasColumnModified = false;
	for( QJsonValueRef ref: column ) {
		QJsonObject obj( ref.toObject() );
		const int commandNum = obj["commandNum"].toInt();
		assert( (unsigned)commandNum < (unsigned)kMaxCommands );
		const wsw::Vector<int> &boundKeys = m_boundKeysForCommand[commandNum];
		const bool isActuallyBound = !boundKeys.empty();
		if( obj["isBound"].toBool() != isActuallyBound ) {
			obj["isBound"] = isActuallyBound;
			wasColumnModified = true;
			ref = obj;
		}
	}

	assert( changedSignal.isZeroTerminated() );
	if( wasColumnModified ) {
		QMetaObject::invokeMethod( this, changedSignal.data() );
	}
}

static const wsw::StringView kMovementCommands[] = {
	"+forward"_asView, "+back"_asView, "+moveleft"_asView, "+moveright"_asView,
	"+moveup"_asView, "+movedown"_asView, "+special"_asView
};

static const wsw::StringView kActionCommands[] = {
	"+attack"_asView, "+zoom"_asView, "weapnext"_asView, "weapprev"_asView,
	"messagemode"_asView, "messagemode2"_asView, "+scores"_asView
};

static const wsw::StringView kWeaponCommands[] = {
	"gb"_asView, "mg"_asView, "rg"_asView, "gl"_asView, "pg"_asView,
	"rl"_asView, "lg"_asView, "eb"_asView, "sw"_asView, "ig"_asView
};

static const wsw::StringView kRespectCommands[] = {
	"hi"_asView, "bb"_asView, "glhf"_asView, "gg"_asView, "plz"_asView,
	"tks"_asView, "soz"_asView, "n1"_asView, "nt"_asView, "lol"_asView
};

static const wsw::StringView kUsePrefix( "use" );
static const wsw::StringView kSayPrefix( "say" );

auto KeysAndBindingsModel::getCommandNum( const wsw::StringView &bindingView ) const -> std::optional<int> {
	// TODO: Eliminate this copy...
	wsw::String binding( bindingView.data(), bindingView.size() );
	if( auto it = m_otherBindingNums.find( binding ); it != m_otherBindingNums.end() ) {
		return it->second;
	}

	const wsw::StringView prefixes[2] = { kUsePrefix, kSayPrefix };
	const wsw::HashMap<wsw::String, int> *mapsOfNums[2] = { &m_weaponBindingNums, &m_respectBindingNums };
	for( int i = 0; i < 2; ++i ) {
		if( !bindingView.startsWith( prefixes[i] ) ) {
			continue;
		}
		wsw::StringView v( bindingView );
		v = v.drop( prefixes[i].length() ).trimLeft();
		// TODO: Eliminate this copy...
		wsw::String s( v.data(), v.size() );
		if( auto it = mapsOfNums[i]->find( s ); it != mapsOfNums[i]->end() ) {
			return it->second;
		}
		return std::nullopt;
	}

	return std::nullopt;
}

void KeysAndBindingsModel::registerKeyItem( QQuickItem *item, int quakeKey ) {
	if( (size_t)( quakeKey - 1 ) < (size_t)( m_keyItems.size() - 1 ) ) {
		assert( !m_keyItems[quakeKey] );
		m_keyItems[quakeKey] = item;
	}
}

void KeysAndBindingsModel::unregisterKeyItem( QQuickItem *item, int quakeKey ) {
	if( (size_t)( quakeKey - 1 ) < (size_t)( m_keyItems.size() - 1 ) ) {
		assert( m_keyItems[quakeKey] == item );
		m_keyItems[quakeKey] = nullptr;
	}
}

void KeysAndBindingsModel::registerCommandItem( QQuickItem *item, int commandNum ) {
	assert( (size_t)commandNum < m_commandItems.size() );
	assert( !m_commandItems[commandNum] );
	m_commandItems[commandNum] = item;
}

void KeysAndBindingsModel::unregisterCommandItem( QQuickItem *item, int commandNum ) {
	assert( (size_t)commandNum < m_commandItems.size() );
	assert( item == m_commandItems[commandNum] );
	m_commandItems[commandNum] = nullptr;
}

auto KeysAndBindingsModel::registerKnownBindings( wsw::HashMap<wsw::String, int> &dest,
												  const wsw::StringView *begin,
												  const wsw::StringView *end,
												  BindingGroup bindingGroup,
												  int startFromNum ) -> int {
	for( const wsw::StringView *view = begin; view != end; ++view ) {
		int num = startFromNum + (int)( view - begin );
		dest.insert( std::make_pair( wsw::String( view->data(), view->size() ), num ) );
		assert( (size_t)num < sizeof( m_commandBindingGroups ) );
		assert( m_commandBindingGroups[num] == UnknownGroup );
		m_commandBindingGroups[num] = bindingGroup;
	}
	return startFromNum + (int)( end - begin );
}

template <typename Array>
auto KeysAndBindingsModel::registerKnownBindings( wsw::HashMap<wsw::String, int> &dest,
												  const Array &bindings,
												  BindingGroup bindingGroup,
												  int startFromNum ) -> int {
	return registerKnownBindings( dest, std::begin( bindings ), std::end( bindings ), bindingGroup, startFromNum );
}

void KeysAndBindingsModel::precacheKnownBindingProps() {
	std::fill( std::begin( m_commandBindingGroups ), std::end( m_commandBindingGroups ), UnknownGroup );

	// Start from 1 so it less error-prone regarding to coercion to booleans at JS side
	int numBindings = registerKnownBindings( m_otherBindingNums, kMovementCommands, MovementGroup, 1 );
	numBindings = registerKnownBindings( m_otherBindingNums, kActionCommands, ActionGroup, numBindings );
	numBindings = registerKnownBindings( m_weaponBindingNums, kWeaponCommands, WeaponGroup, numBindings );
	registerKnownBindings( m_respectBindingNums, kRespectCommands, RespectGroup, numBindings );
}

struct CommandsColumnEntry {
	const char *text;
	const char *command;
};

static CommandsColumnEntry kMovementCommandsColumn[] {
	{ "Forward", "+forward" },
	{ "Back", "+back" },
	{ "Left", "+moveleft" },
	{ "Right", "+moveright" },
	{ "Jump/Up", "+moveup" },
	{ "Crouch/Down", "+movedown" },
	{ "Dash/Walljump", "+special" }
};

static CommandsColumnEntry kActionCommandsColumn[] {
	{ "Attack", "+attack" },
	{ "Zoom", "+zoom" },
	{ "Next weapon", "weapnext" },
	{ "Previous weapon", "weapprev" },
	{ "Chat", "messagemode" },
	{ "Team chat", "messagemode2" },
	{ "Scoreboard", "+scores" }
};

static CommandsColumnEntry kWeaponCommandsColumn1[] {
	{ "Gunblade", "use gb" },
	{ "Machinegun", "use mg" },
	{ "Riotgun", "use rg" },
	{ "Grenade launcher", "use gl" },
	{ "Plasmagun", "use pg" }
};

static CommandsColumnEntry kWeaponCommandsColumn2[] {
	{ "Rocket launcher", "use rl" },
	{ "Lasergun", "use lg" },
	{ "Electrobolt", "use eb" },
	{ "Shockwave", "use sw" },
	{ "Instagun", "use ig" }
};

static CommandsColumnEntry kRespectCommandsColumn1[] {
	{ "Say hi!", "say hi" },
	{ "Say bb!", "say bb" },
	{ "Say glhf!", "say glhf" },
	{ "Say gg!", "say gg" },
	{ "Say plz!", "say plz" }
};

static CommandsColumnEntry kRespectCommandsColumn2[] {
	{ "Say tks!", "say tks" },
	{ "Say soz!", "say soz" },
	{ "Say n1!", "say n1" },
	{ "Say nt!", "say nt" },
	{ "Say lol!", "say lol" }
};

auto KeysAndBindingsModel::commandsColumnToJsonArray( CommandsColumnEntry *begin,
													  CommandsColumnEntry *end )
													  -> QJsonArray {
	QJsonArray result;
	for( const CommandsColumnEntry *entry = begin; entry != end; ++entry ) {
		auto maybeNum = getCommandNum( wsw::StringView( entry->command ) );
		assert( maybeNum );
		QJsonObject obj {
			{ "text", entry->text },
			{ "command", entry->command },
			{ "commandNum", *maybeNum },
			{ "isBound", false }
		};
		result.append( obj );
	}
	return result;
}

template <typename Column>
auto KeysAndBindingsModel::commandsColumnToJsonArray( Column &column ) -> QJsonArray {
	return commandsColumnToJsonArray( std::begin( column ), std::end( column ) );
}

KeysAndBindingsModel::KeysAndBindingsModel() {
	precacheKnownBindingProps();

	m_commandsMovementColumnModel = commandsColumnToJsonArray( kMovementCommandsColumn );
	m_commandsActionsColumnModel = commandsColumnToJsonArray( kActionCommandsColumn );
	m_commandsWeaponsColumnModel[0] = commandsColumnToJsonArray( kWeaponCommandsColumn1 );
	m_commandsWeaponsColumnModel[1] = commandsColumnToJsonArray( kWeaponCommandsColumn2 );
	m_commandsRespectColumnModel[0] = commandsColumnToJsonArray( kRespectCommandsColumn1 );
	m_commandsRespectColumnModel[1] = commandsColumnToJsonArray( kRespectCommandsColumn2 );

	m_keyboardMainPadRowModel[0] = keyboardRowToJsonArray( kMainPadRow1 );
	m_keyboardMainPadRowModel[1] = keyboardRowToJsonArray( kMainPadRow2 );
	m_keyboardMainPadRowModel[2] = keyboardRowToJsonArray( kMainPadRow3 );
	m_keyboardMainPadRowModel[3] = keyboardRowToJsonArray( kMainPadRow4 );
	m_keyboardMainPadRowModel[4] = keyboardRowToJsonArray( kMainPadRow5 );
	m_keyboardMainPadRowModel[5] = keyboardRowToJsonArray( kMainPadRow6 );

	m_keyboardArrowPadRowModel[0] = keyboardRowToJsonArray( kArrowPadRow1 );
	m_keyboardArrowPadRowModel[1] = keyboardRowToJsonArray( kArrowPadRow2 );
	m_keyboardArrowPadRowModel[2] = keyboardRowToJsonArray( kArrowPadRow3 );
	m_keyboardArrowPadRowModel[3] = keyboardRowToJsonArray( kArrowPadRow4 );
	m_keyboardArrowPadRowModel[4] = keyboardRowToJsonArray( kArrowPadRow5 );

	m_keyboardNumPadRowModel[0] = keyboardRowToJsonArray( kNumPadRow1 );
	m_keyboardNumPadRowModel[1] = keyboardRowToJsonArray( kNumPadRow2 );
	m_keyboardNumPadRowModel[2] = keyboardRowToJsonArray( kNumPadRow3 );
	m_keyboardNumPadRowModel[3] = keyboardRowToJsonArray( kNumPadRow4 );
	m_keyboardNumPadRowModel[4] = keyboardRowToJsonArray( kNumPadRow5 );
}

void KeysAndBindingsModel::onKeyItemContainsMouseChanged( QQuickItem *keyItem, int quakeKey, bool contains ) {
	// Find a command with the respective binding
	if( const auto &command = m_lastKeyBindings[quakeKey]; !command.empty() ) {
		if( const auto maybeCommandNum = getCommandNum( wsw::StringView( command.data(), command.size() ) ) ) {
			if ( QQuickItem *commandItem = m_commandItems[*maybeCommandNum] ) {
				commandItem->setProperty( "externallyHighlighted", contains );
			}
		}
	}
}

void KeysAndBindingsModel::onCommandItemContainsMouseChanged( QQuickItem *commandItem, int commandNum, bool contains ) {
	assert( (unsigned)commandNum < (unsigned)kMaxCommands );
	for( int key : m_boundKeysForCommand[commandNum] ) {
		if( QQuickItem *keyItem = m_keyItems[key] ) {
			keyItem->setProperty( "externallyHighlighted", contains );
		}
	}
}

}