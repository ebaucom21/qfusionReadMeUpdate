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

namespace wsw::ui {

class HudLayoutModel : public QAbstractListModel {
	Q_OBJECT
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
	};
	Q_ENUM( Kind );

	// Could be useful for the editor model as well, so lift it here
	enum Flags {
		NoFlags       = 0x0,
		TeamBasedOnly = 0x1,
		PovOnly       = 0x2
	};
	Q_ENUM( Flags );

	[[nodiscard]]
	Q_INVOKABLE bool load( const QByteArray &fileName );
protected:
	struct FileEntry {
		Kind kind;
		int selfAnchors;
		int otherAnchors;
		int anchorItem;
	};

	struct AnchorPair {
		int selfAnchors;
		int otherAnchors;
	};

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

	enum Role {
		Origin = Qt::UserRole + 1,
		Size,
		Draggable,
		DisplayedAnchors,
		DisplayedAnchorItemIndex,
		SelfAnchors,
		AnchorItemAnchors,
		AnchorItemIndex
	};

	struct Entry {
		QRectF rectangle;
		QPointF pendingOrigin;
		int displayedAnchors { 0 };
		int selfAnchors { 0 };
		int anchorItemAnchors { 0 };
		int realAnchorItem { -1 };
		Kind kind { (Kind)0 };
		std::optional<int> displayedAnchorItem;
	};

	[[nodiscard]]
	bool serialize( wsw::StaticString<4096> *buffer );
	void writeAnchor( wsw::StaticString<32> *tmp, int anchor );

	[[nodiscard]]
	bool acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) override;

	[[nodiscard]]
	static auto getEditorSizeForKind( Kind kind ) -> std::optional<QSize>;

	[[nodiscard]]
	bool isDraggable( int index ) const;

	[[nodiscard]]
	auto getDisplayedFieldAnchors() const -> int { return m_displayedFieldAnchors; }

	void setDisplayedFieldAnchors( int anchors ) {
		if( m_displayedFieldAnchors != anchors ) {
			m_displayedFieldAnchors = anchors;
			Q_EMIT displayedFieldAnchorsChanged( anchors );
		}
	}

	void updateMarkers( int draggedIndex );

	void updateAnchors( int index, int newAnchorItem, const AnchorPair &newAnchorPair );

	void notifyOfUpdatesAtIndex( int index, const QVector<int> &changedRoles );

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &, int role ) const -> QVariant override;

	[[nodiscard]]
	static auto getMatchingEntryAnchors( const QRectF &draggedRectangle, const QRectF &otherEntryRectangle )
		-> std::optional<AnchorPair>;

	[[nodiscard]]
	static auto getMatchingFieldAnchors( const QRectF &draggedRectangle, const QRectF &fieldRectangle )
		-> std::optional<AnchorPair>;

	[[nodiscard]]
	auto getMatchingAnchorItem( int draggedIndex ) const -> std::optional<std::pair<int, AnchorPair>>;

	[[nodiscard]]
	bool isAnchorDefinedPositionValid( int draggedIndex, const std::optional<int> &otherIndex,
									   const AnchorPair &anchors ) const;

	[[nodiscard]]
	static auto getPointForAnchors( const QRectF &r, int anchors ) -> QPointF;

	wsw::Vector<Entry> m_entries;

	QSizeF m_fieldSize;
	int m_displayedFieldAnchors { 0 };

	static inline const QVector<int> kDisplayedAnchorsAsRole { DisplayedAnchors, Draggable };
	static inline const QVector<int> kAllAnchorsAsRole { DisplayedAnchors, SelfAnchors, AnchorItemAnchors, Draggable };
	static inline const QVector<int> kOriginRoleAsVector { Origin };

	static const AnchorPair kMatchingEntryAnchorPairs[];
public:
	Q_SIGNAL void displayedFieldAnchorsChanged( int displayedFieldAnchors );
	Q_PROPERTY( int displayedFieldAnchors READ getDisplayedFieldAnchors NOTIFY displayedFieldAnchorsChanged );

	Q_INVOKABLE void trackDragging( int index, qreal x, qreal y );
	Q_INVOKABLE void finishDragging( int index );
	Q_INVOKABLE void setFieldSize( qreal width, qreal height );
	Q_INVOKABLE void updatePosition( int index, qreal x, qreal y );
	Q_INVOKABLE void updateAnchors( int index );

	[[nodiscard]]
	Q_INVOKABLE bool save( const QByteArray &fileName );
};

class InGameHudLayoutModel : public HudLayoutModel {
	enum Role {
		Kind = Qt::UserRole + 1,
		Flags,
		SelfAnchors,
		AnchorItemIndex,
		AnchorItemAnchors
	};

	// Use entries as-is
	wsw::Vector<FileEntry> m_entries;

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
