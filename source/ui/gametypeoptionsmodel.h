#ifndef WSW_290ddc6d_0a8e_448e_8285_9e54dd121bc2_H
#define WSW_290ddc6d_0a8e_448e_8285_9e54dd121bc2_H

#include <QAbstractListModel>
#include <QJsonArray>

#include <optional>

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/stringspanstorage.h"
#include "../qcommon/wswstdtypes.h"

namespace wsw::ui {

class GametypeOptionsModel : public QAbstractListModel {
	Q_OBJECT
public:
	enum Kind {
		Boolean,
		OneOfList,
	};
	Q_ENUM( Kind )

	Q_PROPERTY( bool available READ isAvailable NOTIFY availableChanged )
	Q_PROPERTY( QString tabTitle READ getTabTitle NOTIFY tabTitleChanged )

	Q_SIGNAL void availableChanged( bool available );
	Q_SIGNAL void tabTitleChanged( const QString &title );

	Q_INVOKABLE QByteArray getSelectorItemIcon( int optionIndex, int chosenIndex ) const;
	Q_INVOKABLE QByteArray getSelectorItemTitle( int optionIndex, int chosenIndex ) const;

	Q_INVOKABLE void select( int row, int chosen );
private:
	enum class Role {
		Title = Qt::UserRole + 1,
		Kind,
		Model,
		Current
	};

	struct OptionEntry {
		unsigned titleSpanIndex;
		unsigned commandSpanIndex;
		Kind kind;
		int model;
		std::pair<unsigned, unsigned> selectableItemsSpan;
	};

	struct SelectableItemEntry {
		unsigned titleSpanIndex;
		unsigned iconSpanIndex;
	};

	[[nodiscard]]
	bool isAvailable() const { return !m_optionEntries.empty(); };
	[[nodiscard]]
	auto getTabTitle() const -> QString { return m_tabTitle; }

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;

	[[nodiscard]]
	bool parseEntryParts( const wsw::StringView &string, wsw::StaticVector<wsw::StringView, 4> &parts );

	[[nodiscard]]
	auto addListItems( const wsw::StringView &string ) -> std::optional<std::pair<unsigned, unsigned>>;

	[[nodiscard]]
	auto addString( const wsw::StringView &string ) -> unsigned;
	[[nodiscard]]
	auto getString( unsigned stringSpanIndex ) const -> QByteArray;

	[[nodiscard]]
	auto getSelectableEntry( int row, int chosen ) const -> const SelectableItemEntry &;

	[[nodiscard]]
	bool doReload();

	void clear();

	QString m_tabTitle;

	static inline const QVector<int> kCurrentRoleAsVector { (int)Role::Current };

	static constexpr unsigned kMaxOptions = MAX_GAMETYPE_OPTIONS;

	wsw::StaticVector<OptionEntry, kMaxOptions> m_optionEntries;
	// A flattened list of selectable items of every option (that are not of Boolean kind)
	wsw::Vector<SelectableItemEntry> m_selectableItemEntries;
	wsw::StaticVector<int, kMaxOptions> m_allowedOptionIndices;
	wsw::StaticVector<int, kMaxOptions> m_currentSelectedItemNums;
	wsw::StringSpanStorage<unsigned, unsigned> m_stringDataStorage;
public:
	void reload();
	void handleOptionsStatusCommand( const wsw::StringView &status );
};

}

#endif