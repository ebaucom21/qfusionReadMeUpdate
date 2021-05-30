#include "hudlayoutmodel.h"
#include "../qcommon/wswfs.h"
#include "../qcommon/wswtonum.h"
#include "../qcommon/wswstringsplitter.h"

#include <QPointF>
#include <QSizeF>

#include <array>

using wsw::operator""_asView;

namespace wsw::ui {

auto HudEditorLayoutModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Origin, "origin" },
		{ Size, "size" },
		{ Name, "name" },
		{ Color, "color" },
		{ Draggable, "draggable" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ DisplayedAnchors, "displayedAnchors" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ AnchorItemIndex, "anchorItemIndex" }
	};
}

auto HudEditorLayoutModel::rowCount( const QModelIndex & ) const -> int {
	return m_fieldSize.isValid() ? (int)m_entries.size() : 0;
}

auto HudEditorLayoutModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case Origin: return m_entries[row].rectangle.topLeft();
				case Size: return m_entries[row].rectangle.size();
				case Name: return m_entries[row].name;
				case Color: return m_entries[row].color;
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

bool HudEditorLayoutModel::isDraggable( int index ) const {
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

void HudEditorLayoutModel::notifyOfUpdatesAtIndex( int index, const QVector<int> &changedRoles ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	const QModelIndex modelIndex( createIndex( index, 0 ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, changedRoles );
}

bool HudLayoutModel::load( const QByteArray &fileName ) {
	return load( wsw::StringView( fileName.data(), fileName.size() ) );
}

bool HudLayoutModel::load( const wsw::StringView &fileName ) {
	wsw::StaticString<MAX_QPATH> pathBuffer;
	if( const auto maybePath = makeFilePath( &pathBuffer, fileName ) ) {
		if( auto maybeHandle = wsw::fs::openAsReadHandle( *maybePath ) ) {
			char dataBuffer[4096];
			if( const size_t size = maybeHandle->getInitialFileSize(); size && size < sizeof( dataBuffer ) ) {
				if( maybeHandle->readExact( dataBuffer, size ) ) {
					if( auto maybeLoadedEntries = deserialize( wsw::StringView( dataBuffer, size ) ) ) {
						if( !maybeLoadedEntries->empty() ) {
							return acceptDeserializedEntries( std::move( *maybeLoadedEntries ) );
						}
					}
				}
			}
		}
	}
	return false;
}

static const wsw::StringView kHudsDirectory( "huds"_asView );
static const wsw::StringView kHudsExtension( ".wswhud"_asView );

void HudEditorLayoutModel::reloadExistingHuds() {
	wsw::fs::SearchResultHolder searchResultHolder;
	if( const auto maybeResult = searchResultHolder.findDirFiles( kHudsDirectory, kHudsExtension ) ) {
		QJsonArray huds;
		for( const wsw::StringView &fileName : *maybeResult ) {
			if( const auto maybeName = wsw::fs::stripExtension( fileName ) ) {
				if( maybeName->length() <= kMaxHudNameLength ) {
					// Check whether the hud file really contains a valid hud
					// TODO: Don't modify the global mutable state
					if( load( *maybeName ) ) {
						huds.append( QString::fromLatin1( maybeName->data(), (int)maybeName->size() ).toLower() );
					}
				}
			}
		}
		if( huds != m_existingHuds ) {
			m_existingHuds = huds;
			Q_EMIT existingHudsChanged( huds );
		}
	}
}

bool HudEditorLayoutModel::save( const QByteArray &fileName ) {
	wsw::StaticString<MAX_QPATH> pathBuffer;
	if( const auto maybePath = makeFilePath( &pathBuffer, wsw::StringView( fileName.data(), fileName.size() ) ) ) {
		if( auto maybeHandle = wsw::fs::openAsWriteHandle( *maybePath ) ) {
			wsw::StaticString<4096> dataBuffer;
			if( serialize( &dataBuffer ) ) {
				if( maybeHandle->write( dataBuffer.data(), dataBuffer.size() ) ) {
					reloadExistingHuds();
					return true;
				}
			}
		}
	}
	return false;
}

auto HudLayoutModel::makeFilePath( wsw::StaticString<MAX_QPATH> *buffer,
								   const wsw::StringView &baseFileName ) const -> std::optional<wsw::StringView> {
	if( baseFileName.length() > kMaxHudNameLength ) {
		return std::nullopt;
	}
	if( baseFileName.length() > std::min( 16u, buffer->capacity() ) ) {
		return std::nullopt;
	}
	if( baseFileName.contains( '/' ) || baseFileName.contains( '\\' ) || baseFileName.contains( '.' ) ) {
		return std::nullopt;
	}
	for( char ch: baseFileName ) {
		if( !::isalnum( ch ) ) {
			return std::nullopt;
		}
	}
	buffer->clear();
	( *buffer ) << kHudsDirectory << '/' << baseFileName << kHudsExtension;
	return buffer->asView();
}

static const wsw::StringView kItem( "Item"_asView );
static const wsw::StringView kSelf( "Self"_asView );
static const wsw::StringView kOther( "Other"_asView );
static const wsw::StringView kKind( "Kind"_asView );

static const wsw::StringView *const kOrderedKeywords[] { &kItem, &kSelf, &kOther, &kKind };

bool HudEditorLayoutModel::serialize( wsw::StaticString<4096> *buffer_ ) {
	wsw::StaticString<32> selfAnchorsString, otherAnchorsString;
	auto &buffer = *buffer_;

	const QMetaEnum kindMeta( QMetaEnum::fromType<Kind>() );
	// `<<` throws on overflow
	try {
		buffer.clear();
		for( unsigned i = 0; i < m_entries.size(); ++i ) {
			const Entry &entry = m_entries[i];
			// Disallow serializing dragged items
			if( entry.displayedAnchorItem ) {
				return false;
			}

			writeAnchor( &selfAnchorsString, entry.selfAnchors );
			writeAnchor( &otherAnchorsString, entry.anchorItemAnchors );

			buffer << i << ' ';
			buffer << kItem << ' ' << entry.realAnchorItem << ' ';
			buffer << kSelf << ' ' << selfAnchorsString << ' ';
			buffer << kOther << ' ' << otherAnchorsString << ' ';
			buffer << kKind << ' ' << wsw::StringView( kindMeta.valueToKey( entry.kind ) ) << ' ';
			buffer << "\r\n"_asView;
		}
		return true;
	} catch( ... ) {
		return false;
	}
}

void HudEditorLayoutModel::writeAnchor( wsw::StaticString<32> *buffer, int anchor ) {
	const auto [first, second] = getAnchorNames( anchor );
	buffer->clear();
	( *buffer ) << first << '|' << second;
}

// TODO: Share the static instance over the codebase
static const wsw::CharLookup kNewlineChars( "\r\n"_asView );

auto HudLayoutModel::deserialize( const wsw::StringView &data ) -> std::optional<wsw::Vector<FileEntry>> {
	wsw::Vector<FileEntry> entries;
	wsw::StringSplitter lineSplitter( data );
	while( const auto maybeNextLine = lineSplitter.getNext( kNewlineChars ) ) {
		auto maybeEntryAndIndex = parseEntry( *maybeNextLine );
		if( !maybeEntryAndIndex ) {
			return std::nullopt;
		}
		auto &&[entry, index] = std::move( *maybeEntryAndIndex );
		if( index != entries.size() ) {
			return std::nullopt;
		}
		entries.push_back( entry );
	}

	for( const FileEntry &entry: entries ) {
		// Disallow non-existing anchor items
		if( entry.anchorItem >= 0 ) {
			if( (unsigned)entry.anchorItem >= (unsigned)entries.size() ) {
				return std::nullopt;
			}
		}
	}

	// Validate kinds
	const QMetaEnum metaKinds( QMetaEnum::fromType<Kind>() );
	std::array<bool, 32> presentKinds;
	assert( presentKinds.size() > (size_t)metaKinds.keyCount() );
	presentKinds.fill( false );
	for( const FileEntry &entry: entries ) {
		const int kind = entry.kind;
		if( kind && (unsigned)kind < (unsigned)presentKinds.max_size() ) {
			if( metaKinds.valueToKey( kind ) != nullptr ) {
				if ( !presentKinds[kind] ) {
					presentKinds[kind] = true;
					continue;
				}
			}
		}
		return std::nullopt;
	}

	// Try detecting direct (1-1) anchor loops
	for( unsigned i = 0; i < entries.size(); ++i ) {
		if( int anchorItem = entries[i].anchorItem; anchorItem >= 0 ) {
			if( entries[anchorItem].anchorItem == (int)i ) {
				return std::nullopt;
			}
		}
	}

	// TODO: Check mutual intersections of items
	// TODO: Check indirect anchor loops (graph cycles)

	return entries;
}

auto HudLayoutModel::parseEntry( const wsw::StringView &line ) -> std::optional<std::pair<FileEntry, unsigned>> {
	wsw::StringSplitter splitter( line );
	std::optional<int> values[2];
	std::optional<int> anchors[2];
	std::optional<Kind> kind;

	unsigned lastTokenNum = 0;
	while( const auto maybeNextTokenAndNum = splitter.getNextWithNum() ) {
		const auto [token, num] = *maybeNextTokenAndNum;
		lastTokenNum = num;
		if( num % 2 ) {
			const unsigned keywordIndex = num / 2;
			if( keywordIndex >= std::size( kOrderedKeywords ) ) {
				return std::nullopt;
			}
			if( !kOrderedKeywords[keywordIndex]->equalsIgnoreCase( token ) ) {
				return std::nullopt;
			}
		} else {
			const unsigned valueIndex = num / 2;
			if( valueIndex < 2 ) {
				if( const auto maybeValue = wsw::toNum<int>( token ) ) {
					values[valueIndex] = maybeValue;
				} else {
					return std::nullopt;
				}
			} else if( valueIndex >= 2 && valueIndex <= 3 ) {
				if( const auto maybeAnchors = parseAnchors( token ) ) {
					anchors[valueIndex - 2] = maybeAnchors;
				} else {
					return std::nullopt;
				}
			} else if( valueIndex == 4 ) {
				if( const auto maybeKind = parseKind( token ) ) {
					kind = maybeKind;
				} else {
					return std::nullopt;
				}
			} else {
				return std::nullopt;
			}
		}
	}

	if( lastTokenNum == 2 * std::size( kOrderedKeywords ) ) {
		// TODO: Use designated initializers
		FileEntry entry;
		entry.anchorItem = values[1].value();
		entry.selfAnchors = anchors[0].value();
		entry.otherAnchors = anchors[1].value();
		entry.kind = kind.value();
		return std::make_pair( entry, values[0].value() );
	}

	return std::nullopt;
}

auto HudLayoutModel::parseAnchors( const wsw::StringView &token ) -> std::optional<int> {
	wsw::StringSplitter splitter( token );
	// Expect exactly 2 tokens
	if( const auto maybeFirst = splitter.getNext( '|' ) ) {
		if( const auto maybeSecond = splitter.getNext( '|' ) ) {
			if( !splitter.getNext() ) {
				return parseAnchors( maybeFirst->trim(), maybeSecond->trim() );
			}
		}
	}
	return std::nullopt;
}

auto HudLayoutModel::parseAnchors( const wsw::StringView &first, const wsw::StringView &second ) -> std::optional<int> {
	std::optional<int> horizontal, vertical;
	if( !( vertical = parseVerticalAnchors( first ) ) ) {
		if( !( horizontal = parseHorizontalAnchors( first ) ) ) {
			return std::nullopt;
		}
	}
	if( vertical ) {
		if( !( horizontal = parseHorizontalAnchors( second ) ) ) {
			return std::nullopt;
		}
	} else {
		if( !( vertical = parseVerticalAnchors( second ) ) )  {
			return std::nullopt;
		}
	}
	return horizontal.value() | vertical.value();
}

static const wsw::StringView kTop( "Top"_asView );
static const wsw::StringView kBottom( "Bottom"_asView );
static const wsw::StringView kVCenter( "VCenter"_asView );

static const wsw::StringView kLeft( "Left"_asView );
static const wsw::StringView kRight( "Right"_asView );
static const wsw::StringView kHCenter( "HCenter"_asView );

auto HudLayoutModel::parseHorizontalAnchors( const wsw::StringView &keyword ) -> std::optional<int> {
	const std::pair<wsw::StringView, int> mapping[] { { kTop, Top }, { kBottom, Bottom }, { kVCenter, VCenter } };
	for( const auto &[word, value] : mapping ) {
		if( word.equalsIgnoreCase( keyword ) ) {
			return value;
		}
	}
	return std::nullopt;
}

auto HudLayoutModel::parseVerticalAnchors( const wsw::StringView &keyword ) -> std::optional<int> {
	const std::pair<wsw::StringView, int> mapping[] { { kLeft, Left }, { kRight, Right }, { kHCenter, HCenter } };
	for( const auto &[word, value] : mapping ) {
		if( word.equalsIgnoreCase( keyword ) ) {
			return value;
		}
	}
	return std::nullopt;
}

auto HudLayoutModel::parseKind( const wsw::StringView &token ) -> std::optional<Kind> {
	const QMetaEnum kindMeta( QMetaEnum::fromType<Kind>() );
	// Implement a case-insensitive comparison manually
	for( int i = 0, end = kindMeta.keyCount(); i < end; ++i ) {
		if( token.equalsIgnoreCase( wsw::StringView( kindMeta.key( i ) ) ) ) {
			return (Kind)kindMeta.value( i );
		}
	}
	return std::nullopt;
}

HudEditorLayoutModel::HudEditorLayoutModel() {
	reloadExistingHuds();
}

bool HudEditorLayoutModel::acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) {
	wsw::Vector<Entry> entries;
	for( FileEntry &fileEntry: fileEntries ) {
		const auto &props = kEditorPropsForKind[fileEntry.kind - 1];
		assert( fileEntry.kind == props.kind );
		Entry entry;
		entry.kind = fileEntry.kind;
		entry.selfAnchors = fileEntry.selfAnchors;
		entry.anchorItemAnchors = fileEntry.otherAnchors;
		entry.realAnchorItem = fileEntry.anchorItem;
		entry.rectangle.setSize( props.size );
		entry.color = props.color;
		entry.name = props.name;
		entries.push_back( entry );
	}
	assert( entries.size() == fileEntries.size() );
	beginResetModel();
	std::swap( m_entries, entries );
	endResetModel();
	return true;
}

const HudEditorLayoutModel::EditorProps HudEditorLayoutModel::kEditorPropsForKind[] {
	{ "Health", HealthBar, QSize( 144, 32 ), QColor::fromRgbF( 1.0, 0.5, 1.0 ) },
	{ "Armor", ArmorBar, QSize( 144, 32 ), QColor::fromRgbF( 1.0, 0.3, 0.0 ) },
	{ "Inventory", InventoryBar, QSize( 256, 48 ), QColor::fromRgbF( 1.0, 0.8, 0.0 ) },
	{ "Weapon status", WeaponStatus, QSize( 96, 96 ), QColor::fromRgbF( 1.0, 0.5, 0.0 ) },
	{ "Match time", MatchTime, QSize( 128, 64 ), QColor::fromRgbF( 0.7, 0.7, 0.7 ) },
	{ "Alpha score", AlphaScore, QSize( 128, 56 ), QColor::fromRgbF( 1.0, 0.0, 0.0 ) },
	{ "Beta score", BetaScore, QSize( 128, 56 ), QColor::fromRgbF( 0.0, 1.0, 0.0 ) },
	{ "Chat", Chat, QSize( 256, 72 ), QColor::fromRgbF( 0.7, 1.0, 0.3 ) },
	{ "Team list", TeamList, QSize( 256, 128 ), QColor::fromRgbF( 0.0, 0.3, 0.7 ) },
	{ "Frags feed", Obituaries, QSize( 144, 108 ), QColor::fromRgbF( 0.3, 0.0, 0.7 ) },
	{ "Message feed", MessageFeed, QSize( 256, 72 ), QColor::fromRgbF( 0.0, 0.7, 0.7 ) }
};

void HudEditorLayoutModel::setFieldSize( qreal width, qreal height ) {
	QSize size( width, height );
	if( size != m_fieldSize ) {
		beginResetModel();
		m_fieldSize = size;
		endResetModel();
	}
}

void HudEditorLayoutModel::trackDragging( int index, qreal x, qreal y ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	m_entries[index].pendingOrigin = QPointF( x, y );
	updateMarkers( index );
}

void HudEditorLayoutModel::finishDragging( int index ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	Entry &dragged = m_entries[index];
	if( dragged.rectangle.topLeft() != dragged.pendingOrigin ) {
		dragged.rectangle.moveTopLeft( dragged.pendingOrigin );
		notifyOfUpdatesAtIndex( index, kOriginRoleAsVector );
	}
}

void HudEditorLayoutModel::updateAnchors( int index ) {
	if( const auto maybeItemAndAnchors = getMatchingAnchorItem( index ) ) {
		updateAnchors( index, maybeItemAndAnchors->first, maybeItemAndAnchors->second );
	}
}

void HudEditorLayoutModel::updateAnchors( int index, int newAnchorItem, const AnchorPair &newAnchorPair ) {
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

void HudEditorLayoutModel::updatePosition( int index, qreal x, qreal y ) {
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

void HudEditorLayoutModel::updateMarkers( int draggedIndex ) {
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
const HudLayoutModel::AnchorPair HudEditorLayoutModel::kMatchingEntryAnchorPairs[] {
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

auto HudEditorLayoutModel::getMatchingEntryAnchors( const QRectF &draggedRectangle, const QRectF &otherEntryRectangle )
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

auto HudEditorLayoutModel::getMatchingFieldAnchors( const QRectF &draggedRectangle, const QRectF &fieldRectangle )
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

auto HudEditorLayoutModel::getMatchingAnchorItem( int draggedIndex ) const
	-> std::optional<std::pair<int, AnchorPair>> {
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

bool HudEditorLayoutModel::isAnchorDefinedPositionValid( int draggedIndex, const std::optional<int> &otherIndex,
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

auto HudEditorLayoutModel::getPointForAnchors( const QRectF &r, int anchors ) -> QPointF {
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
		case HudLayoutModel::Left: return kLeft;
		case HudLayoutModel::HCenter: return kHCenter;
		case HudLayoutModel::Right: return kRight;
		default: return "None"_asView;
	}
}

[[nodiscard]]
static inline auto getVerticalAnchorName( int bits ) -> wsw::StringView {
	switch( bits & kVerticalBitsMask ) {
		case HudLayoutModel::Top: return kTop;
		case HudLayoutModel::VCenter: return kVCenter;
		case HudLayoutModel::Bottom: return kBottom;
		default: return "None"_asView;
	}
}

auto HudLayoutModel::getAnchorNames( int anchors ) -> std::pair<wsw::StringView, wsw::StringView> {
	return { getVerticalAnchorName( anchors ), getHorizontalAnchorName( anchors ) };
}

auto HudLayoutModel::getFlagsForKind( Kind kind ) -> Flags {
	switch( kind ) {
		case HealthBar: return PovOnly;
		case ArmorBar: return PovOnly;
		case InventoryBar: return PovOnly;
		case WeaponStatus: return PovOnly;
		case MatchTime: return NoFlags;
		case AlphaScore: return TeamBasedOnly;
		case BetaScore: return TeamBasedOnly;
		case Chat: return NoFlags;
		case TeamList: return (Flags)( TeamBasedOnly | PovOnly );
		case Obituaries: return NoFlags;
		case MessageFeed: return NoFlags;
		default: throw std::logic_error( "unreachable" );
	}
}

auto InGameHudLayoutModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Kind, "kind" },
		{ Flags, "flags" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ AnchorItemIndex, "anchorItemIndex" }
	};
}

auto InGameHudLayoutModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto InGameHudLayoutModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case Kind: return m_entries[row].kind;
				case Flags: return getFlagsForKind( m_entries[row].kind );
				case SelfAnchors: return m_entries[row].selfAnchors;
				case AnchorItemAnchors: return m_entries[row].otherAnchors;
				case AnchorItemIndex: return m_entries[row].anchorItem;
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

bool InGameHudLayoutModel::acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) {
	beginResetModel();
	std::swap( m_entries, fileEntries );
	endResetModel();
	return true;
}

}