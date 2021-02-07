#ifndef WSW_7bec9edc_f137_4385_ad94_191ee7088622_H
#define WSW_7bec9edc_f137_4385_ad94_191ee7088622_H

#include <QAbstractListModel>
#include <QJsonArray>

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstdtypes.h"

namespace wsw::ui {

class CallvotesModelProxy;

class CallvotesModel : public QAbstractListModel {
	friend class CallvotesModelProxy;

	Q_OBJECT
public:
	enum Kind {
		NoArgs,
		Boolean,
		Number,
		Player,
		Minutes,
		MapList,
		Options,
	};
	Q_ENUM( Kind );

	enum Flags {
		Regular = 0x1,
		Operator = 0x2,
	};
	Q_ENUM( Flags );
private:
	enum Role {
		Name = Qt::UserRole + 1,
		Desc,
		Flags,
		ArgsKind,
		ArgsHandle,
		Current,
	};

	static inline const QVector<int> kRoleCurrentChangeset { Current };

	CallvotesModelProxy *const m_proxy;
	wsw::Vector<int> m_entryNums;
public:
	explicit CallvotesModel( CallvotesModelProxy *proxy ) : m_proxy( proxy ) {}

	void notifyOfChangesAtNum( int num );

	// Generic signals are not really usable in QML, add the our one
	Q_SIGNAL void currentChanged( int index, QVariant value );

	Q_INVOKABLE QJsonArray getOptionsList( int handle ) const;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;
};

class CallvotesModelProxy {
	friend class CallvotesModel;
public:
	struct Entry {
		QString name;
		QString desc;
		QString current;
		unsigned flags;
		CallvotesModel::Kind kind;
		int argsHandle;
	};
private:
	struct OptionTokens {
		wsw::String content;
		wsw::Vector<std::pair<uint16_t, uint16_t>> spans;
	};

	wsw::Vector<Entry> m_entries;
	wsw::Vector<std::pair<OptionTokens, int>> m_options;

	CallvotesModel m_regularModel {this };
	CallvotesModel m_operatorModel {this };

	[[nodiscard]]
	auto addArgs( const std::optional<wsw::StringView> &maybeArgs )
		-> std::optional<std::pair<CallvotesModel::Kind, std::optional<int>>>;

	[[nodiscard]]
	auto parseAndAddOptions( const wsw::StringView &encodedOptions ) -> std::optional<int>;
public:
	[[nodiscard]]
	auto getEntry( int entryNum ) const -> const Entry & { return m_entries[entryNum]; }

	[[nodiscard]]
	auto getRegularModel() -> CallvotesModel * { return &m_regularModel; }
	[[nodiscard]]
	auto getOperatorModel() -> CallvotesModel * { return &m_operatorModel; }

	void reload();

	void handleConfigString( unsigned configStringNum, const wsw::StringView &string );
};

}

#endif
