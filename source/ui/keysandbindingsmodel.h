#ifndef WSW_531a9137_a4c6_43b4_961e_eda1109e7c80_H
#define WSW_531a9137_a4c6_43b4_961e_eda1109e7c80_H

#include <QObject>
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

#include <array>
#include "../qcommon/qcommon.h"
#include "../qcommon/wswstdtypes.h"

class QQuickItem;

namespace wsw::ui {

class QtUISystem;

class KeysAndBindingsModel : public QObject {
	Q_OBJECT

	friend class QtUISystem;
public:
	enum BindingGroup {
		MovementGroup = 1,
		ActionGroup,
		WeaponGroup,
		RespectGroup,
		UnknownGroup
	};
	Q_ENUM( BindingGroup )

	Q_INVOKABLE void reload();

	Q_INVOKABLE QColor colorForGroup( int group ) const;

	Q_INVOKABLE void registerKeyItem( QQuickItem *item, int quakeKey );
	Q_INVOKABLE void unregisterKeyItem( QQuickItem *item, int quakeKey );

	Q_INVOKABLE void onKeyItemContainsMouseChanged( QQuickItem *keyItem, int quakeKey, bool contains );
	Q_INVOKABLE void onCommandItemContainsMouseChanged( QQuickItem *commandItem, int commandNum, bool contains );

	Q_INVOKABLE void registerCommandItem( QQuickItem *item, int commandNum );
	Q_INVOKABLE void unregisterCommandItem( QQuickItem *item, int commandNum );
private:
	QJsonArray m_keyboardMainPadRowModel[6];
	QJsonArray m_keyboardArrowPadRowModel[5];
	QJsonArray m_keyboardNumPadRowModel[5];
	QJsonArray m_mouseKeysModel;

	QJsonArray m_commandsMovementColumnModel;
	QJsonArray m_commandsActionsColumnModel;
	QJsonArray m_commandsWeaponsColumnModel[2];
	QJsonArray m_commandsRespectColumnModel[2];

	// We split properties for every row to avoid a full reload of every row on model change

	Q_SIGNAL void keyboardMainPadRow1Changed();
	Q_SIGNAL void keyboardMainPadRow2Changed();
	Q_SIGNAL void keyboardMainPadRow3Changed();
	Q_SIGNAL void keyboardMainPadRow4Changed();
	Q_SIGNAL void keyboardMainPadRow5Changed();
	Q_SIGNAL void keyboardMainPadRow6Changed();

	Q_SIGNAL void keyboardArrowPadRow1Changed();
	Q_SIGNAL void keyboardArrowPadRow2Changed();
	Q_SIGNAL void keyboardArrowPadRow3Changed();
	Q_SIGNAL void keyboardArrowPadRow4Changed();
	Q_SIGNAL void keyboardArrowPadRow5Changed();

	Q_SIGNAL void keyboardNumPadRow1Changed();
	Q_SIGNAL void keyboardNumPadRow2Changed();
	Q_SIGNAL void keyboardNumPadRow3Changed();
	Q_SIGNAL void keyboardNumPadRow4Changed();
	Q_SIGNAL void keyboardNumPadRow5Changed();

	Q_SIGNAL void mouseKeysModelChanged();

	Q_SIGNAL void commandsMovementColumnChanged();
	Q_SIGNAL void commandsActionsColumnChanged();
	Q_SIGNAL void commandsWeaponsColumn1Changed();
	Q_SIGNAL void commandsWeaponsColumn2Changed();
	Q_SIGNAL void commandsRespectColumn1Changed();
	Q_SIGNAL void commandsRespectColumn2Changed();

	[[nodiscard]] auto getMainKeyboardPaneRow1() const -> const QJsonArray & { return m_keyboardMainPadRowModel[0]; }
	[[nodiscard]] auto getMainKeyboardPaneRow2() const -> const QJsonArray & { return m_keyboardMainPadRowModel[1]; }
	[[nodiscard]] auto getMainKeyboardPaneRow3() const -> const QJsonArray & { return m_keyboardMainPadRowModel[2]; }
	[[nodiscard]] auto getMainKeyboardPaneRow4() const -> const QJsonArray & { return m_keyboardMainPadRowModel[3]; }
	[[nodiscard]] auto getMainKeyboardPaneRow5() const -> const QJsonArray & { return m_keyboardMainPadRowModel[4]; }
	[[nodiscard]] auto getMainKeyboardPaneRow6() const -> const QJsonArray & { return m_keyboardMainPadRowModel[5]; }

	[[nodiscard]]
	auto getKeyboardArrowPadRow1() const -> const QJsonArray & { return m_keyboardArrowPadRowModel[0]; }
	[[nodiscard]]
	auto getKeyboardArrowPadRow2() const -> const QJsonArray & { return m_keyboardArrowPadRowModel[1]; }
	[[nodiscard]]
	auto getKeyboardArrowPadRow3() const -> const QJsonArray & { return m_keyboardArrowPadRowModel[2]; }
	[[nodiscard]]
	auto getKeyboardArrowPadRow4() const -> const QJsonArray & { return m_keyboardArrowPadRowModel[3]; }
	[[nodiscard]]
	auto getKeyboardArrowPadRow5() const -> const QJsonArray & { return m_keyboardArrowPadRowModel[4]; }

	[[nodiscard]] 
	auto getKeyboardNumPadRow1() const -> const QJsonArray & { return m_keyboardNumPadRowModel[0]; }
	[[nodiscard]] 
	auto getKeyboardNumPadRow2() const -> const QJsonArray & { return m_keyboardNumPadRowModel[1]; }
	[[nodiscard]] 
	auto getKeyboardNumPadRow3() const -> const QJsonArray & { return m_keyboardNumPadRowModel[2]; }
	[[nodiscard]] 
	auto getKeyboardNumPadRow4() const -> const QJsonArray & { return m_keyboardNumPadRowModel[3]; }
	[[nodiscard]] 
	auto getKeyboardNumPadRow5() const -> const QJsonArray & { return m_keyboardNumPadRowModel[4]; }

	[[nodiscard]]
	auto getCommandsMovementColumn() const -> const QJsonArray & { return m_commandsMovementColumnModel; }
	[[nodiscard]]
	auto getCommandsActionsColumn() const -> const QJsonArray & { return m_commandsActionsColumnModel; }
	[[nodiscard]]
	auto getCommandsWeaponsColumn1() const -> const QJsonArray & { return m_commandsWeaponsColumnModel[0]; }
	[[nodiscard]]
	auto getCommandsWeaponsColumn2() const -> const QJsonArray & { return m_commandsWeaponsColumnModel[1]; }
	[[nodiscard]]
	auto getCommandsRespectColumn1() const -> const QJsonArray & { return m_commandsRespectColumnModel[0]; }
	[[nodiscard]]
	auto getCommandsRespectColumn2() const -> const QJsonArray & { return m_commandsRespectColumnModel[1]; }
	
	Q_PROPERTY( QJsonArray keyboardMainPadRow1 READ getMainKeyboardPaneRow1 NOTIFY keyboardMainPadRow1Changed );
	Q_PROPERTY( QJsonArray keyboardMainPadRow2 READ getMainKeyboardPaneRow2 NOTIFY keyboardMainPadRow2Changed );
	Q_PROPERTY( QJsonArray keyboardMainPadRow3 READ getMainKeyboardPaneRow3 NOTIFY keyboardMainPadRow3Changed );
	Q_PROPERTY( QJsonArray keyboardMainPadRow4 READ getMainKeyboardPaneRow4 NOTIFY keyboardMainPadRow4Changed );
	Q_PROPERTY( QJsonArray keyboardMainPadRow5 READ getMainKeyboardPaneRow5 NOTIFY keyboardMainPadRow5Changed );
	Q_PROPERTY( QJsonArray keyboardMainPadRow6 READ getMainKeyboardPaneRow6 NOTIFY keyboardMainPadRow6Changed );

	Q_PROPERTY( QJsonArray keyboardArrowPadRow1 READ getKeyboardArrowPadRow1 NOTIFY keyboardArrowPadRow1Changed );
	Q_PROPERTY( QJsonArray keyboardArrowPadRow2 READ getKeyboardArrowPadRow2 NOTIFY keyboardArrowPadRow2Changed );
	Q_PROPERTY( QJsonArray keyboardArrowPadRow3 READ getKeyboardArrowPadRow3 NOTIFY keyboardArrowPadRow3Changed );
	Q_PROPERTY( QJsonArray keyboardArrowPadRow4 READ getKeyboardArrowPadRow4 NOTIFY keyboardArrowPadRow4Changed );
	Q_PROPERTY( QJsonArray keyboardArrowPadRow5 READ getKeyboardArrowPadRow5 NOTIFY keyboardArrowPadRow5Changed );
	
	Q_PROPERTY( QJsonArray keyboardNumPadRow1 READ getKeyboardNumPadRow1 NOTIFY keyboardNumPadRow1Changed );
	Q_PROPERTY( QJsonArray keyboardNumPadRow2 READ getKeyboardNumPadRow2 NOTIFY keyboardNumPadRow2Changed );
	Q_PROPERTY( QJsonArray keyboardNumPadRow3 READ getKeyboardNumPadRow3 NOTIFY keyboardNumPadRow3Changed );
	Q_PROPERTY( QJsonArray keyboardNumPadRow4 READ getKeyboardNumPadRow4 NOTIFY keyboardNumPadRow4Changed );
	Q_PROPERTY( QJsonArray keyboardNumPadRow5 READ getKeyboardNumPadRow5 NOTIFY keyboardNumPadRow5Changed );

	Q_PROPERTY( QJsonArray commandsMovementColumn READ getCommandsMovementColumn NOTIFY commandsMovementColumnChanged );
	Q_PROPERTY( QJsonArray commandsActionsColumn READ getCommandsActionsColumn NOTIFY commandsActionsColumnChanged );
	Q_PROPERTY( QJsonArray commandsWeaponsColumn1 READ getCommandsWeaponsColumn1 NOTIFY commandsWeaponsColumn1Changed );
	Q_PROPERTY( QJsonArray commandsWeaponsColumn2 READ getCommandsWeaponsColumn2 NOTIFY commandsWeaponsColumn2Changed );
	Q_PROPERTY( QJsonArray commandsRespectColumn1 READ getCommandsRespectColumn1 NOTIFY commandsRespectColumn1Changed );
	Q_PROPERTY( QJsonArray commandsRespectColumn2 READ getCommandsRespectColumn2 NOTIFY commandsRespectColumn2Changed );

	template <typename Array>
	void reloadKeyBindings( Array &array, const wsw::StringView &changedSignalPrefix );

	void reloadKeyBindings( QJsonArray *rowsBegin, QJsonArray *rowsEnd, const wsw::StringView &changedSignalPrefix );

	[[nodiscard]]
	bool reloadRowKeyBindings( QJsonArray &row );

	void reloadCommandBindings( QJsonArray *columnsBegin, QJsonArray *columnsEnd, const wsw::StringView &changedSignalPrefix );

	[[nodiscard]]
	bool reloadColumnCommandBindings( QJsonArray &column );

	[[nodiscard]]
	auto registerKnownBindings( wsw::HashMap<wsw::String, int> &dest,
							    const wsw::StringView *begin,
							    const wsw::StringView *end,
							    BindingGroup bindingGroup,
							    int startFromNum ) -> int;

	template <typename Array>
	auto registerKnownBindings( wsw::HashMap<wsw::String, int> &dest,
								const Array &bindings,
								BindingGroup bindingGroup,
								int startFromNum ) -> int;

	void precacheKnownBindingProps();

	[[nodiscard]]
	auto commandsColumnToJsonArray( struct CommandsColumnEntry *begin, struct CommandsColumnEntry *end ) -> QJsonArray;

	template <typename Column>
	[[nodiscard]]
	auto commandsColumnToJsonArray( Column &column ) -> QJsonArray;

	[[nodiscard]]
	auto getCommandNum( const wsw::StringView &command ) const -> std::optional<int>;

	KeysAndBindingsModel();
private:
	static constexpr auto kMaxCommands = 48;
	std::array<BindingGroup, kMaxCommands> m_commandBindingGroups;

	wsw::HashMap<wsw::String, int> m_otherBindingNums;
	wsw::HashMap<wsw::String, int> m_weaponBindingNums;
	wsw::HashMap<wsw::String, int> m_respectBindingNums;

	// TODO: Optimize by avoiding allocations
	// (all known bindings/commands can use &'static lifetime)
	wsw::HashMap<int, wsw::String> m_oldKeyBindings;

	std::array<QQuickItem *, 256> m_keyItems {};
	std::array<QQuickItem *, kMaxCommands> m_commandItems {};
};

}

#endif
