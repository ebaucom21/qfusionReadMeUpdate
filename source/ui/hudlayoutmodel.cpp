#include "hudlayoutmodel.h"

#include <QPointF>
#include <QSizeF>

using wsw::operator""_asView;

namespace wsw::ui {

auto HudLayoutModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Origin, "origin" },
		{ Size, "size" },
		{ Draggable, "draggable" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ DisplayedAnchors, "displayedAnchors" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ AnchorItemIndex, "anchorItemIndex" }
	};
}

auto HudLayoutModel::rowCount( const QModelIndex & ) const -> int {
	return m_fieldSize.isValid() ? (int)m_entries.size() : 0;
}

auto HudLayoutModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case Origin: return m_entries[row].rectangle.topLeft();
				case Size: return m_entries[row].rectangle.size();
				case Draggable: return isDraggable( row );
				case DisplayedAnchors: return m_entries[row].displayedAnchors;
				case DisplayedAnchorItemIndex: return m_entries[row].displayedAnchorItem.value_or( 0 );
				case SelfAnchors: return m_entries[row].selfAnchors;
				case AnchorItemAnchors: return m_entries[row].anchorItemAnchors;
				case AnchorItemIndex: return m_entries[row].realAnchorItem;
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

bool HudLayoutModel::isDraggable( int index ) const {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	// This is not that bad as properties are cached/retrieved on demand
	// and maintaining a bidirectional mapping is extremely error-prone.
	for( unsigned i = 0; i < m_entries.size(); ++i ) {
		if( i != (unsigned)index ) {
			const Entry &entry = m_entries[i];
			if( entry.realAnchorItem == index ) {
				return false;
			}
			if( const auto maybeDisplayedItem = entry.displayedAnchorItem ) {
				if( *maybeDisplayedItem == index ) {
					return false;
				}
			}
		}
	}
	return true;
}

void HudLayoutModel::notifyOfUpdatesAtIndex( int index, const QVector<int> &changedRoles ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	const QModelIndex modelIndex( createIndex( index, 0 ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, changedRoles );
}

HudLayoutModel::HudLayoutModel() {
	// Just for debugging
	Entry entry;
	entry.rectangle = QRectF( 0, 0, 32, 96 );
	entry.selfAnchors = Top | Left;
	entry.anchorItemAnchors = Top | Left;
	m_entries.push_back( entry );
	entry.rectangle = QRectF( 0, 0, 96, 32 );
	entry.selfAnchors = Bottom | Right;
	entry.anchorItemAnchors = Bottom | Right;
	m_entries.push_back( entry );
	entry.rectangle = QRectF( 0, 0, 64, 64 );
	entry.selfAnchors = VCenter | HCenter;
	entry.anchorItemAnchors = VCenter | HCenter;
	m_entries.push_back( entry );
}

void HudLayoutModel::setFieldSize( qreal width, qreal height ) {
	QSize size( width, height );
	if( size != m_fieldSize ) {
		beginResetModel();
		m_fieldSize = size;
		endResetModel();
	}
}

void HudLayoutModel::trackDragging( int index, qreal x, qreal y ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	m_entries[index].pendingOrigin = QPointF( x, y );
	updateMarkers( index );
}

void HudLayoutModel::finishDragging( int index ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	Entry &dragged = m_entries[index];
	if( dragged.rectangle.topLeft() != dragged.pendingOrigin ) {
		dragged.rectangle.moveTopLeft( dragged.pendingOrigin );
		notifyOfUpdatesAtIndex( index, kOriginRoleAsVector );
	}
}

void HudLayoutModel::updateAnchors( int index ) {
	if( const auto maybeItemAndAnchors = getMatchingAnchorItem( index ) ) {
		updateAnchors( index, maybeItemAndAnchors->first, maybeItemAndAnchors->second );
	}
}

void HudLayoutModel::updateAnchors( int index, int newAnchorItem, const AnchorPair &newAnchorPair ) {
	Entry &dragged = m_entries[index];
	const int oldAnchorItem = dragged.realAnchorItem;
	if( oldAnchorItem != newAnchorItem ) {
		if( oldAnchorItem >= 0 ) {
			m_entries[oldAnchorItem].displayedAnchors = 0;
			notifyOfUpdatesAtIndex( oldAnchorItem, kDisplayedAnchorsAsRole );
		} else {
			setDisplayedFieldAnchors( 0 );
		}
	}

	dragged.realAnchorItem = newAnchorItem;
	dragged.displayedAnchors = 0;
	dragged.selfAnchors = newAnchorPair.selfAnchors;
	dragged.anchorItemAnchors = newAnchorPair.otherAnchors;
	notifyOfUpdatesAtIndex( index, kAllAnchorsAsRole );

	if( newAnchorItem >= 0 ) {
		m_entries[newAnchorItem].displayedAnchors = 0;
		notifyOfUpdatesAtIndex( newAnchorItem, kDisplayedAnchorsAsRole );
	} else {
		setDisplayedFieldAnchors( 0 );
	}
}

void HudLayoutModel::updatePosition( int index, qreal x, qreal y ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	QPointF point( x, y );
	Entry &entry = m_entries[index];
	if( entry.rectangle.topLeft() != point ) {
		entry.rectangle.moveTopLeft( point );
		notifyOfUpdatesAtIndex( index, kOriginRoleAsVector );
	}
}

class SmallIntSet {
	wsw::StaticVector<int, 3> m_nums;
public:
	[[nodiscard]]
	auto begin() const { return m_nums.begin(); }
	[[nodiscard]]
	auto end() const { return m_nums.end(); }
	void add( int num ) {
		if( std::find( m_nums.begin(), m_nums.end(), num ) == m_nums.end() ) {
			m_nums.push_back( num );
		}
	}
};

void HudLayoutModel::updateMarkers( int draggedIndex ) {
	// It's more convenient to decompose results
	std::optional<int> anchorItem;
	std::optional<AnchorPair> anchorsPair;
	if( const auto maybeItemAndAnchors = getMatchingAnchorItem( draggedIndex ) ) {
		anchorItem = maybeItemAndAnchors->first;
		anchorsPair = maybeItemAndAnchors->second;
	}

	SmallIntSet modifiedRows;

	Entry &dragged = m_entries[draggedIndex];
	if( dragged.displayedAnchorItem != anchorItem ) {
		// Clear flags of a (maybe) old item
		if( dragged.displayedAnchorItem ) {
			if( const int oldItemIndex = *dragged.displayedAnchorItem; oldItemIndex >= 0 ) {
				m_entries[oldItemIndex].displayedAnchors = 0;
				modifiedRows.add( oldItemIndex );
			} else {
				setDisplayedFieldAnchors( 0 );
			}
		}
		dragged.displayedAnchorItem = anchorItem;
		modifiedRows.add( draggedIndex );
	}

	if( dragged.displayedAnchorItem ) {
		assert( anchorsPair );
		if( const int currItemIndex = *dragged.displayedAnchorItem; currItemIndex >= 0 ) {
			if( m_entries[currItemIndex].displayedAnchors != anchorsPair->otherAnchors ) {
				m_entries[currItemIndex].displayedAnchors = anchorsPair->otherAnchors;
				modifiedRows.add( currItemIndex );
			}
		} else {
			setDisplayedFieldAnchors( anchorsPair->otherAnchors );
		}
	}

	const int selfAnchors = anchorsPair ? anchorsPair->selfAnchors : 0;
	if( dragged.displayedAnchors != selfAnchors ) {
		dragged.displayedAnchors = selfAnchors;
		modifiedRows.add( draggedIndex );
	}

	for( int row: modifiedRows ) {
		notifyOfUpdatesAtIndex( row, kDisplayedAnchorsAsRole );
	}
}

// Pairs are arranged in their priority order
const HudLayoutModel::AnchorPair HudLayoutModel::kMatchingEntryAnchorPairs[] {
	{ Top | HCenter, Bottom | HCenter },
	{ Bottom | HCenter, Top | HCenter },
	{ VCenter | Left, VCenter | Right },
	{ VCenter | Right, VCenter | Left },

	{ Top | Left, Bottom | Left }, { Top | Left, Bottom | Right },
	{ Top | Right, Bottom | Left }, { Top | Right, Bottom | Right },
	{ Bottom | Left, Top | Left }, { Bottom | Left, Top | Right },
	{ Bottom | Right, Top | Left }, { Bottom | Right, Top | Right }
};

[[nodiscard]]
static inline bool isClose( const QPointF &pt1, const QPointF &pt2 ) {
	QPointF diff( pt1 - pt2 );
	// Units are device-independent so this is correct
	return diff.x() * diff.x() + diff.y() * diff.y() < 12 * 12;
}

auto HudLayoutModel::getMatchingEntryAnchors( const QRectF &draggedRectangle, const QRectF &otherEntryRectangle )
	-> std::optional<AnchorPair> {
	for( const auto &[selfAnchors, otherAnchors] : kMatchingEntryAnchorPairs ) {
		const QPointF selfPoint( getPointForAnchors( draggedRectangle, selfAnchors ) );
		const QPointF otherPoint( getPointForAnchors( otherEntryRectangle, otherAnchors ) );
		if( isClose( selfPoint, otherPoint ) ) {
			return AnchorPair { selfAnchors, otherAnchors };
		}
	}
	return std::nullopt;
}

auto HudLayoutModel::getMatchingFieldAnchors( const QRectF &draggedRectangle, const QRectF &fieldRectangle )
	-> std::optional<AnchorPair> {
	// Arrange in their priority order
	for( const int horizontalBits : { Left, Right, HCenter } ) {
		for( const int verticalBits : { Top, Bottom, VCenter } ) {
			const int anchors = horizontalBits | verticalBits;
			const QPointF selfPoint( getPointForAnchors( draggedRectangle, anchors ) );
			const QPointF otherPoint( getPointForAnchors( fieldRectangle, anchors ) );
			if( isClose( selfPoint, otherPoint ) ) {
				return AnchorPair { anchors, anchors };
			}
		}
	}
	return std::nullopt;
}

auto HudLayoutModel::getMatchingAnchorItem( int draggedIndex ) const -> std::optional<std::pair<int, AnchorPair>> {
	assert( (unsigned)draggedIndex < (unsigned)m_entries.size() );
	QRectF draggedRectangle( m_entries[draggedIndex].rectangle );
	draggedRectangle.moveTopLeft( m_entries[draggedIndex].pendingOrigin );

	for( unsigned i = 0; i < m_entries.size(); ++i ) {
		if( i != (unsigned)draggedIndex ) {
			if( const auto maybeAnchors = getMatchingEntryAnchors( draggedRectangle, m_entries[i].rectangle ) ) {
				if( isAnchorDefinedPositionValid( draggedIndex, (int)i, *maybeAnchors ) ) {
					if( m_entries[i].realAnchorItem != draggedIndex ) {
						return std::make_pair( (int)i, *maybeAnchors );
					}
				}
			}
		}
	}

	assert( m_fieldSize.isValid() );
	QRectF fieldRectangle( 0, 0, m_fieldSize.width(), m_fieldSize.height() );
	if( const auto maybeAnchors = getMatchingFieldAnchors( draggedRectangle, fieldRectangle ) ) {
		if( isAnchorDefinedPositionValid( draggedIndex, std::nullopt, *maybeAnchors ) ) {
			// TODO: It would be nice to have a proper ADT language-level support
			return std::make_pair( -1, *maybeAnchors );
		}
	}

	return std::nullopt;
}

bool HudLayoutModel::isAnchorDefinedPositionValid( int draggedIndex, const std::optional<int> &otherIndex,
												   const AnchorPair &anchors ) const {
	assert( (unsigned)draggedIndex < (unsigned)m_entries.size() );
	const Entry &dragged = m_entries[draggedIndex];

	QPointF anchorPoint;
	if( otherIndex ) {
		assert( (unsigned)*otherIndex < (unsigned)m_entries.size() && *otherIndex != draggedIndex );
		anchorPoint = getPointForAnchors( m_entries[*otherIndex].rectangle, anchors.otherAnchors );
	} else {
		const QRectF fieldRectangle( 0, 0, m_fieldSize.width(), m_fieldSize.height() );
		anchorPoint = getPointForAnchors( fieldRectangle, anchors.otherAnchors );
	}

	// Apply an anchor-defined position to the dragged item rectangle
	QRectF predictedRectangle( dragged.rectangle );
	// Align center first as this moves along both axes
	if( anchors.selfAnchors & ( VCenter | HCenter ) ) {
		predictedRectangle.moveCenter( anchorPoint );
	}

	if( anchors.selfAnchors & Left ) {
		predictedRectangle.moveLeft( anchorPoint.x() );
	} else if( anchors.selfAnchors & Right ) {
		predictedRectangle.moveRight( anchorPoint.x() );
	}

	if( anchors.selfAnchors & Top ) {
		predictedRectangle.moveTop( anchorPoint.y() );
	} else if( anchors.selfAnchors & Bottom ) {
		predictedRectangle.moveBottom( anchorPoint.y() );
	}

	// Try shrinking a bit to mitigate f.p. comparison issues
	if( predictedRectangle.width() > 2.0 && predictedRectangle.height() > 2.0 ) {
		const QPointF oldCenter( predictedRectangle.center() );
		predictedRectangle.setWidth( predictedRectangle.width() - 0.5 );
		predictedRectangle.setHeight( predictedRectangle.height() - 0.5 );
		predictedRectangle.moveCenter( oldCenter );
	}

	const int secondCmpIndex = otherIndex ? *otherIndex : draggedIndex;
	for( unsigned i = 0; i < m_entries.size(); ++i ) {
		if( i != (unsigned)draggedIndex && i != (unsigned)secondCmpIndex ) {
			if( m_entries[i].rectangle.intersects( predictedRectangle ) ) {
				return false;
			}
		}
	}

	return true;
}

static constexpr auto kHorizontalBitsMask = 0x7;
static constexpr auto kVerticalBitsMask = 0x7 << 3;

auto HudLayoutModel::getPointForAnchors( const QRectF &r, int anchors ) -> QPointF {
	qreal x;
	switch( anchors & kHorizontalBitsMask ) {
		case Left: x = r.left(); break;
		case HCenter: x = 0.5 * ( r.left() + r.right() ); break;
		case Right: x = r.right(); break;
		default: throw std::invalid_argument( "Invalid X anchor bits" );
	}
	qreal y;
	switch( anchors & kVerticalBitsMask ) {
		case Top: y = r.top(); break;
		case VCenter: y = 0.5 * ( r.top() + r.bottom() ); break;
		case Bottom: y = r.bottom(); break;
		default: throw std::invalid_argument( "Invalid Y anchor bits" );
	}
	return QPointF( x, y );
}

[[nodiscard]]
static inline auto getHorizontalAnchorName( int bits ) -> wsw::StringView {
	switch( bits & kHorizontalBitsMask ) {
		case HudLayoutModel::Left: return "Left"_asView;
		case HudLayoutModel::HCenter: return "HCenter"_asView;
		case HudLayoutModel::Right: return "Right"_asView;
		default: return "None"_asView;
	}
}

[[nodiscard]]
static inline auto getVerticalAnchorName( int bits ) -> wsw::StringView {
	switch( bits & kVerticalBitsMask ) {
		case HudLayoutModel::Top: return "Top"_asView;
		case HudLayoutModel::VCenter: return "VCenter"_asView;
		case HudLayoutModel::Bottom: return "Bottom"_asView;
		default: return "None"_asView;
	}
}

auto HudLayoutModel::getAnchorNames( int anchors ) -> std::pair<wsw::StringView, wsw::StringView> {
	return { getVerticalAnchorName( anchors ), getHorizontalAnchorName( anchors ) };
}

}