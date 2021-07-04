#ifndef WSW_8c488c22_5afd_4fad_9e25_9304dd129d10_H
#define WSW_8c488c22_5afd_4fad_9e25_9304dd129d10_H

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswstdtypes.h"

#include <QAbstractListModel>
#include <QQuickItem>
#include <QSizeF>
#include <QPointF>
#include <QRectF>
#include <QJsonArray>

namespace wsw::ui {

class HudLayoutModel : public QAbstractListModel {
	Q_OBJECT

	friend class HudEditorModel;
public:
	enum HorizontalAnchorBits {
		Left    = 0x1,
		HCenter = 0x2,
		Right   = 0x4
	};
	Q_ENUM( HorizontalAnchorBits );

	enum VerticalAnchorBits {
		Top      = 0x1 << 3,
		VCenter  = 0x2 << 3,
		Bottom   = 0x4 << 3
	};
	Q_ENUM( VerticalAnchorBits );

	enum Kind {
		HealthBar = 1,
		ArmorBar,
		InventoryBar,
		WeaponStatus,
		MatchTime,
		AlphaScore,
		BetaScore,
		Chat,
		TeamInfo,
		FragsFeed,
		MessageFeed,
		AwardsArea,
		StatusMessage,
	};
	Q_ENUM( Kind );

	// Could be useful for the editor model as well, so lift it here
	enum Flags {
		NoFlags       = 0x0,
		TeamBasedOnly = 0x1,
		PovOnly       = 0x2
	};
	Q_ENUM( Flags );

	static inline const unsigned kMaxHudNameLength = 16u;

	[[nodiscard]]
	Q_INVOKABLE bool load( const QByteArray &fileName );

	[[nodiscard]]
	bool load( const wsw::StringView &fileName );
protected:
	// Either this stuff is typed or we keep getting bugs
	class AnchorItem {
		int m_value { 0 };
	public:
		AnchorItem() = default;
		explicit AnchorItem( int value ): m_value( value ) {}

		[[nodiscard]]
		static auto forItem( unsigned item ) -> AnchorItem { return AnchorItem( (int)item + 1 ); }
		[[nodiscard]]
		static auto forField() -> AnchorItem { return AnchorItem( -1 ); }
		[[nodiscard]]
		static auto forToolbox() -> AnchorItem { return AnchorItem( 0 ); }

		[[nodiscard]]
		bool operator!=( const AnchorItem &that ) const { return m_value != that.m_value; }
		[[nodiscard]]
		bool operator==( const AnchorItem &that ) const { return m_value == that.m_value; }
		[[nodiscard]]
		bool isOtherItem() const { return m_value > 0; }
		[[nodiscard]]
		bool isField() const { return m_value < 0; }
		[[nodiscard]]
		bool isToolbox() const { return !m_value; }
		[[nodiscard]]
		auto toRawValue() const { return m_value; }

		[[nodiscard]]
		auto toItemIndex() const -> int {
			assert( m_value > 0 );
			return m_value - 1;
		}
	};

	struct FileEntry {
		Kind kind;
		int selfAnchors;
		int otherAnchors;
		AnchorItem anchorItem;
	};

	struct AnchorPair {
		int selfAnchors;
		int otherAnchors;
	};

	struct EditorProps {
		const wsw::StringView name;
		int kind;
		QSize size;
		QColor color;
		const std::optional<wsw::StringView> controllingCVar;
	};

	static const EditorProps kEditorPropsForKind[];

	static const AnchorPair kMatchingItemAndItemAnchorPairs[];
	static const AnchorPair kMatchingItemAndFieldAnchorPairs[];

	[[nodiscard]]
	static auto getFlagsForKind( Kind kind ) -> Flags;

	[[nodiscard]]
	static auto getAnchorNames( int anchors ) -> std::pair<wsw::StringView, wsw::StringView>;

	[[nodiscard]]
	auto makeFilePath( wsw::StaticString<MAX_QPATH> *buffer, const wsw::StringView &baseFileName ) const
		-> std::optional<wsw::StringView>;

	[[nodiscard]]
	auto deserialize( const wsw::StringView &data ) -> std::optional<wsw::Vector<FileEntry>>;

	[[nodiscard]]
	auto parseEntry( const wsw::StringView &line ) -> std::optional<std::pair<FileEntry, unsigned>>;

	[[nodiscard]]
	virtual bool acceptDeserializedEntries( wsw::Vector<FileEntry> &&entries ) = 0;

	[[nodiscard]]
	auto parseHorizontalAnchors( const wsw::StringView &keyword ) -> std::optional<int>;
	[[nodiscard]]
	auto parseVerticalAnchors( const wsw::StringView &keyword ) -> std::optional<int>;
	[[nodiscard]]
	auto parseAnchors( const wsw::StringView &first, const wsw::StringView &second ) -> std::optional<int>;
	[[nodiscard]]
	auto parseAnchors( const wsw::StringView &token ) -> std::optional<int>;
	[[nodiscard]]
	auto parseKind( const wsw::StringView &token ) -> std::optional<Kind>;
};

class HudEditorLayoutModel : public HudLayoutModel {
	Q_OBJECT

	friend class HudEditorModel;

	enum Role {
		ItemKind = Qt::UserRole + 1,
		Origin,
		Size,
		Name,
		Color,
		Draggable,
		DisplayedAnchors,
		DisplayedAnchorItemIndex,
		SelfAnchors,
		AnchorItemAnchors,
		AnchorItemIndex
	};

	struct Entry {
		wsw::StringView name;
		QRectF rectangle;
		QPointF pendingOrigin;
		QColor color;
		int displayedAnchors { 0 };
		int selfAnchors { 0 };
		int anchorItemAnchors { 0 };
		AnchorItem realAnchorItem;
		Kind kind { (Kind)0 };
		std::optional<AnchorItem> displayedAnchorItem;
		[[nodiscard]]
		auto getQmlAnchorItem() const -> QVariant {
			return displayedAnchorItem ? QVariant( displayedAnchorItem->toRawValue() ) : QVariant();
		}
	};

	[[nodiscard]]
	bool serialize( wsw::StaticString<4096> *buffer );
	void writeAnchor( wsw::StaticString<32> *tmp, int anchor );

	[[nodiscard]]
	bool acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) override;

	[[nodiscard]]
	bool isDraggable( int index ) const;

	void notifyOfDisplayedAnchorsUpdateAtIndex( int index );
	void notifyOfOriginUpdateAtIndex( int index );
	void notifyOfFullUpdateAtIndex( int index );

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &, int role ) const -> QVariant override;

	wsw::Vector<Entry> m_entries;

	static inline const QVector<int> kDisplayedAnchorRoles { DisplayedAnchors, DisplayedAnchorItemIndex, Draggable };
	static inline const QVector<int> kOriginRoleAsVector { Origin };
};

class HudEditorToolboxModel : public QAbstractListModel {
	friend class HudEditorModel;

	enum Role {
		Kind = Qt::UserRole + 1,
		Name,
		Size,
		Color,
		DisplayedAnchors,
	};

	struct Entry {
		QRectF rectangle;
		wsw::StringView name;
		QColor color;
		int kind { 0 };
		int displayedAnchors { false };
	};

	wsw::Vector<Entry> m_entries;

	static inline QVector<int> kMutableRoles { DisplayedAnchors };

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &, int role ) const -> QVariant override;

	void setDisplayedAnchorsForKind( int kind, int anchors );
};

class HudEditorModel : public QObject {
	Q_OBJECT

	using AnchorItem = HudEditorLayoutModel::AnchorItem;
	using AnchorPair = HudEditorLayoutModel::AnchorPair;

	HudEditorLayoutModel m_layoutModel;
	HudEditorToolboxModel m_toolboxModel;

	QSizeF m_fieldAreaSize;
	QSizeF m_dragAreaSize;
	int m_displayedFieldAnchors { 0 };

	QJsonArray m_existingHuds;

	bool m_hasSetLayoutModelOwnership { false };
	bool m_hasSetToolboxModelOwnership { false };

	[[nodiscard]]
	auto getDisplayedFieldAnchors() const -> int { return m_displayedFieldAnchors; }

	[[nodiscard]]
	auto getExistingHuds() const -> QJsonArray { return m_existingHuds; }

	void setDisplayedFieldAnchors( int anchors ) {
		if( m_displayedFieldAnchors != anchors ) {
			m_displayedFieldAnchors = anchors;
			Q_EMIT displayedFieldAnchorsChanged( anchors );
		}
	}

	void reloadExistingHuds();

	void updateMarkers( int draggedIndex );

	template <typename Range>
	[[nodiscard]]
	static auto getMatchingAnchors( const QRectF &draggedRectangle, const QRectF &otherRectangle,
									const Range &range )
		-> std::optional<AnchorPair>;

	[[nodiscard]]
	static auto getMatchingEntryAnchors( const QRectF &draggedRectangle, const QRectF &otherEntryRectangle )
		-> std::optional<AnchorPair>;

	[[nodiscard]]
	static auto getMatchingFieldAnchors( const QRectF &draggedRectangle, const QRectF &fieldRectangle )
		-> std::optional<AnchorPair>;

	[[nodiscard]]
	auto getMatchingAnchorItem( int draggedIndex ) const -> std::optional<std::pair<AnchorItem, AnchorPair>>;

	[[nodiscard]]
	bool isAnchorDefinedPositionValid( int draggedIndex, const std::optional<int> &otherIndex,
									   const AnchorPair &anchors ) const;

	[[nodiscard]]
	static auto getPointForAnchors( const QRectF &r, int anchors ) -> QPointF;

	[[nodiscard]]
	static auto getMaxHudNameLength() -> int { return HudLayoutModel::kMaxHudNameLength; }
public:
	HudEditorModel();

	Q_SIGNAL void displayedFieldAnchorsChanged( int displayedFieldAnchors );
	Q_PROPERTY( int displayedFieldAnchors READ getDisplayedFieldAnchors NOTIFY displayedFieldAnchorsChanged );

	Q_SIGNAL void existingHudsChanged( const QJsonArray &existingHuds );
	Q_PROPERTY( const QJsonArray existingHuds READ getExistingHuds NOTIFY existingHudsChanged );

	Q_PROPERTY( unsigned maxHudNameLength READ getMaxHudNameLength CONSTANT );

	[[nodiscard]]
	Q_INVOKABLE QAbstractListModel *getLayoutModel();
	[[nodiscard]]
	Q_INVOKABLE QAbstractListModel *getToolboxModel();

	Q_INVOKABLE void trackDragging( int index, qreal x, qreal y );
	Q_INVOKABLE void finishDragging( int index );
	Q_INVOKABLE void setFieldAreaSize( qreal width, qreal height );
	Q_INVOKABLE void setDragAreaSize( qreal width, qreal height );
	Q_INVOKABLE void updateElementPosition( int index, qreal x, qreal y );
	Q_INVOKABLE void updatePlaceholderPosition( int index, qreal x, qreal y );
	Q_INVOKABLE void clearDisplayedMarkers( int index );

	[[nodiscard]]
	Q_INVOKABLE bool load( const QByteArray &fileName );
	[[nodiscard]]
	Q_INVOKABLE bool save( const QByteArray &fileName );
};

class InGameHudLayoutModel : public HudLayoutModel {
	enum Role {
		ItemKind = Qt::UserRole + 1,
		Flags,
		SelfAnchors,
		AnchorItemIndex,
		AnchorItemAnchors,
		ControllingCVar
	};

	struct Entry {
		Kind kind;
		int selfAnchors;
		int otherAnchors;
		AnchorItem anchorItem;
		std::optional<wsw::StringView> controllingCVar;
		[[nodiscard]]
		auto getControllingCVarAsQVariant() const -> QVariant {
			if( controllingCVar ) {
				return QByteArray::fromRawData( controllingCVar->data(), controllingCVar->size() );
			}
			return QVariant();
		}
	};

	wsw::Vector<Entry> m_entries;

	[[nodiscard]]
	bool acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) override;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &, int role ) const -> QVariant override;
};

}

#endif
