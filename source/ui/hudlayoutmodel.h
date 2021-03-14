#ifndef WSW_8c488c22_5afd_4fad_9e25_9304dd129d10_H
#define WSW_8c488c22_5afd_4fad_9e25_9304dd129d10_H

#include "../qcommon/qcommon.h"
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
	Q_INVOKABLE void trackDragging( int index, qreal x, qreal y );
	Q_INVOKABLE void finishDragging( int index );
	Q_INVOKABLE void setFieldSize( qreal width, qreal height );
	Q_INVOKABLE void updatePosition( int index, qreal x, qreal y );
	Q_INVOKABLE void updateAnchors( int index );

	Q_SIGNAL void displayedFieldAnchorsChanged( int displayedFieldAnchors );
	Q_PROPERTY( int displayedFieldAnchors READ getDisplayedFieldAnchors NOTIFY displayedFieldAnchorsChanged );

	HudLayoutModel();

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
private:
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
		std::optional<int> displayedAnchorItem;
	};

	static inline const QVector<int> kDisplayedAnchorsAsRole { DisplayedAnchors, Draggable };
	static inline const QVector<int> kAllAnchorsAsRole { DisplayedAnchors, SelfAnchors, AnchorItemAnchors, Draggable };
	static inline const QVector<int> kOriginRoleAsVector { Origin };

	QSizeF m_fieldSize;
	int m_displayedFieldAnchors { 0 };

	wsw::Vector<Entry> m_entries;

	void updateMarkers( int draggedIndex );

	struct AnchorPair {
		int selfAnchors;
		int otherAnchors;
	};

	static const AnchorPair kMatchingEntryAnchorPairs[];

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

	[[nodiscard]]
	static auto getAnchorNames( int anchors ) -> std::pair<wsw::StringView, wsw::StringView>;

	[[nodiscard]]
	auto getDisplayedFieldAnchors() const -> int { return m_displayedFieldAnchors; }

	void setDisplayedFieldAnchors( int anchors ) {
		if( m_displayedFieldAnchors != anchors ) {
			m_displayedFieldAnchors = anchors;
			Q_EMIT displayedFieldAnchorsChanged( anchors );
		}
	}

	void updateAnchors( int index, int newAnchorItem, const AnchorPair &newAnchorPair );

	void notifyOfUpdatesAtIndex( int index, const QVector<int> &changedRoles );

	[[nodiscard]]
	bool isDraggable( int index ) const;

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &, int role ) const -> QVariant override;
};

}

#endif
