#include "hudlayoutmodel.h"
#include "../qcommon/wswexceptions.h"
#include "../qcommon/wswfs.h"
#include "../qcommon/wswtonum.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstringsplitter.h"

#include <QPointF>
#include <QSizeF>
#include <QQmlEngine>

#include <array>

using wsw::operator""_asView;

namespace wsw::ui {

auto HudEditorLayoutModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ ItemKind, "kind" },
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
	return (int)m_entries.size();
}

auto HudEditorLayoutModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case ItemKind: return m_entries[row].kind;
				case Origin: return m_entries[row].rectangle.topLeft();
				case Size: return m_entries[row].rectangle.size();
				case Name: return QByteArray::fromRawData( m_entries[row].name.data(), m_entries[row].name.size() );
				case Color: return m_entries[row].color;
				case Draggable: return isDraggable( row );
				case DisplayedAnchors: return m_entries[row].displayedAnchors;
				case DisplayedAnchorItemIndex: return m_entries[row].getQmlAnchorItem();
				case SelfAnchors: return m_entries[row].selfAnchors;
				case AnchorItemAnchors: return m_entries[row].anchorItemAnchors;
				case AnchorItemIndex: return m_entries[row].realAnchorItem.toRawValue();
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
			if( entry.realAnchorItem.isOtherItem() && entry.realAnchorItem.toItemIndex() == index ) {
				return false;
			}
			if( const auto maybeDisplayedItem = entry.displayedAnchorItem ) {
				if( maybeDisplayedItem->isOtherItem() && maybeDisplayedItem->toItemIndex() == index ) {
					return false;
				}
			}
		}
	}
	return true;
}

void HudEditorLayoutModel::notifyOfDisplayedAnchorsUpdateAtIndex( int index ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	const QModelIndex modelIndex( createIndex( index, 0 ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, kDisplayedAnchorRoles );
}

void HudEditorLayoutModel::notifyOfOriginUpdateAtIndex( int index ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	const QModelIndex modelIndex( createIndex( index, 0 ) );
	Q_EMIT dataChanged( modelIndex, modelIndex, kOriginRoleAsVector );
}

void HudEditorLayoutModel::notifyOfFullUpdateAtIndex( int index ) {
	assert( (unsigned)index < (unsigned)m_entries.size() );
	const QModelIndex modelIndex( createIndex( index, 0 ) );
	Q_EMIT dataChanged( modelIndex, modelIndex );
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

void HudEditorModel::reloadExistingHuds() {
	wsw::fs::SearchResultHolder searchResultHolder;
	if( const auto maybeResult = searchResultHolder.findDirFiles( kHudsDirectory, kHudsExtension ) ) {
		QJsonArray huds;
		for( const wsw::StringView &fileName : *maybeResult ) {
			if( const auto maybeName = wsw::fs::stripExtension( fileName ) ) {
				if( maybeName->length() <= HudLayoutModel::kMaxHudNameLength ) {
					// Check whether the hud file really contains a valid hud
					// TODO: Don't modify the global mutable state
					if( m_layoutModel.load( *maybeName ) ) {
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

bool HudEditorModel::load( const QByteArray &fileName ) {
	return m_layoutModel.load( fileName );
}

bool HudEditorModel::save( const QByteArray &fileName ) {
	const wsw::StringView fileNameView( fileName.data(), fileName.size() );
	wsw::StaticString<MAX_QPATH> pathBuffer;
	bool hasWrittenSuccessfully = false;
	if( const auto maybePath = m_layoutModel.makeFilePath( &pathBuffer, fileNameView ) ) {
		if( auto maybeHandle = wsw::fs::openAsWriteHandle( *maybePath ) ) {
			wsw::StaticString<4096> dataBuffer;
			if( m_layoutModel.serialize( &dataBuffer ) ) {
				if( maybeHandle->write( dataBuffer.data(), dataBuffer.size() ) ) {
					hasWrittenSuccessfully = true;
				}
			}
		}
	}
	// Make sure that the file handle has been closed to the moment of reloading
	if( hasWrittenSuccessfully ) {
		reloadExistingHuds();
		Q_EMIT hudUpdated( fileName );
	}
	return hasWrittenSuccessfully;
}

auto HudLayoutModel::makeFilePath( wsw::StaticString<MAX_QPATH> *buffer,
								   const wsw::StringView &baseFileName ) const -> std::optional<wsw::StringView> {
	if( baseFileName.length() > kMaxHudNameLength ) {
		return std::nullopt;
	}
	if( baseFileName.length() > wsw::min( 16u, buffer->capacity() ) ) {
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
	if( m_entries.size() >= 32 ) {
		wsw::failWithLogicError( "Too many entries, this shouldn't happen" );
	}

	// Prune toolbox-attached items. This requires remapping indices.
	alignas( 16 ) std::array<int, 32> entryIndexToFileIndex {};
	entryIndexToFileIndex.fill( 0 );

	constexpr int kMinFileItemIndex = 1;
	int remappedIndicesCounter = kMinFileItemIndex;
	for( unsigned i = 0; i < m_entries.size(); ++i ) {
		const Entry &entry = m_entries[i];
		// Disallow serializing dragged items
		if( entry.displayedAnchorItem ) {
			return false;
		}
		// Don't serialize toolbox-attached items
		if( entry.realAnchorItem.isToolbox() ) {
			continue;
		}
		entryIndexToFileIndex[i] = remappedIndicesCounter++;
	}

	// Disallow serializing empty huds
	if( remappedIndicesCounter == kMinFileItemIndex ) {
		return false;
	}

	wsw::StaticString<32> selfAnchorsString, otherAnchorsString;
	auto &buffer = *buffer_;

	const QMetaEnum kindMeta( QMetaEnum::fromType<Kind>() );
	// `<<` throws on overflow
	try {
		buffer.clear();
		for( unsigned i = 0; i < m_entries.size(); ++i ) {
			// Remap the entry index
			if( const int entryFileIndex = entryIndexToFileIndex[i] ) {
				const Entry &entry = m_entries[i];

				// Remap the anchor item index
				int anchorItemFileIndex = -1;
				assert( !entry.realAnchorItem.isToolbox() );
				assert( !entry.realAnchorItem.isField() || entry.realAnchorItem.toRawValue() == -1 );
				if( entry.realAnchorItem.isOtherItem() ) {
					const int anchorItemIndex = entry.realAnchorItem.toItemIndex();
					assert( entryIndexToFileIndex[anchorItemIndex] );
					anchorItemFileIndex = entryIndexToFileIndex[anchorItemIndex];
				}

				assert( entryFileIndex && anchorItemFileIndex );
				assert( entryFileIndex != anchorItemFileIndex );

				writeAnchor( &selfAnchorsString, entry.selfAnchors );
				writeAnchor( &otherAnchorsString, entry.anchorItemAnchors );

				buffer << entryFileIndex << ' ';
				buffer << kItem << ' ' << anchorItemFileIndex << ' ';
				buffer << kSelf << ' ' << selfAnchorsString << ' ';
				buffer << kOther << ' ' << otherAnchorsString << ' ';
				buffer << kKind << ' ' << wsw::StringView( kindMeta.valueToKey( entry.kind ) ) << ' ';
				buffer << "\r\n"_asView;
			}
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

// Pairs are arranged in their priority order

const HudLayoutModel::AnchorPair HudLayoutModel::kMatchingItemAndItemAnchorPairs[] {
	// Sides
	{ Top | HCenter, Bottom | HCenter },
	{ Bottom | HCenter, Top | HCenter },
	{ VCenter | Left, VCenter | Right },
	{ VCenter | Right, VCenter | Left },

	// Pairs of opposite corners
	{ Top | Left, Bottom | Left }, { Top | Right, Bottom | Left },
	{ Bottom | Left, Top | Right }, { Bottom | Right, Top | Left },

	// Corner-corner pairs that don't lead to overlapping
	{ Top | Left, Bottom | Left }, { Top | Left, Top | Right },
	{ Top | Right, Bottom | Right }, { Top | Right, Top | Left },
	{ Bottom | Left, Top | Left }, { Bottom | Left, Bottom | Right },
	{ Bottom | Right, Top | Right }, { Bottom | Right, Bottom | Left },

	// Corners to mid points of sides
	{ Top | Left, Bottom | HCenter }, { Top | Right, Bottom | HCenter },
	{ Bottom | Left, Top | HCenter }, { Bottom | Right, Top | HCenter },

	// Mid points of sides to corners
	{ Top | HCenter, Bottom | Left }, { Top | HCenter, Bottom | Right },
	{ Bottom | HCenter, Top | Left }, { Bottom | HCenter, Top | Right }
};

const HudLayoutModel::AnchorPair HudLayoutModel::kMatchingItemAndFieldAnchorPairs[] {
	{ VCenter | HCenter, VCenter | HCenter },

	// Centers of same contacting sides
	{ Top | HCenter, Top | HCenter },
	{ Bottom | HCenter, Bottom | HCenter },
	{ VCenter | Left, VCenter | Left },
	{ VCenter | Right, VCenter | Right },

	// Same corners
	{ Top | Left, Top | Left },
	{ Top | Right, Top | Right },
	{ Bottom | Left, Bottom | Left },
	{ Bottom | Right, Bottom | Right },

	// Top or bottom to the vertical center field line
	{ Top | Left, VCenter | Left },
	{ Top | Right, VCenter | Right },
	{ Bottom | Left, VCenter | Left },
	{ Bottom | Right, VCenter | Right },

	// Left or right to the horizontal center field line
	{ Top | Left, Top | HCenter },
	{ Top | Right, Top | HCenter },
	{ Bottom | Left, Bottom | HCenter },
	{ Bottom | Right, Bottom | HCenter },

	// A side mid point to the central point
	{ Top | HCenter, VCenter | HCenter },
	{ Bottom | HCenter, VCenter | HCenter },
	{ VCenter | Left, VCenter | HCenter },
	{ VCenter | Right, VCenter | HCenter },

	// A corner to the central point
	{ Top | Left, VCenter | HCenter },
	{ Top | Right, VCenter | HCenter },
	{ Bottom | Left, VCenter | HCenter },
	{ Bottom | Right, VCenter | HCenter }
};

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
		if( index != entries.size() + 1 ) {
			return std::nullopt;
		}
		entries.push_back( entry );
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

	// Reject illegal anchors
	// TODO: Check mutual intersections of items (this requires applying a layout manually)
	// TODO: Check indirect anchor loops (graph cycles)
	for( unsigned i = 0; i < entries.size(); ++i ) {
		const FileEntry &entry = entries[i];
		const AnchorPair *validAnchorsBegin = std::begin( kMatchingItemAndFieldAnchorPairs );
		const AnchorPair *validAnchorsEnd = std::end( kMatchingItemAndFieldAnchorPairs );
		// Disallow non-existing anchor items
		if( entry.anchorItem.isOtherItem() ) {
			const int itemIndex = entry.anchorItem.toItemIndex();
			if( (unsigned)itemIndex >= (unsigned)entries.size() ) {
				return std::nullopt;
			}
			// Disallow anchoring to self
			if( itemIndex == (int)i ) {
				return std::nullopt;
			}
			// Try detecting direct (1-1) anchor loops'
			const auto anchorItemAnchorItem = entries[itemIndex].anchorItem;
			if( anchorItemAnchorItem.isOtherItem() && anchorItemAnchorItem.toItemIndex() == (int)i ) {
				return std::nullopt;
			}
			validAnchorsBegin = std::begin( kMatchingItemAndItemAnchorPairs );
			validAnchorsEnd = std::end( kMatchingItemAndItemAnchorPairs );
		} else if( entry.anchorItem.isToolbox() ) {
			// Protect against loading toolbox stuff from files
			return std::nullopt;
		}
		bool hasValidAnchorPair = false;
		for( const AnchorPair *it = validAnchorsBegin; it != validAnchorsEnd; ++it ) {
			if( it->selfAnchors == entry.selfAnchors && it->otherAnchors == entry.otherAnchors ) {
				hasValidAnchorPair = true;
				break;
			}
		}
		if( !hasValidAnchorPair ) {
			return std::nullopt;
		}
	}

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
		FileEntry entry {
			.kind = kind.value(),
			.selfAnchors = anchors[0].value(),
			.otherAnchors = anchors[1].value(),
			.anchorItem = AnchorItem( values[1].value() ),
		};
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

auto HudEditorToolboxModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Kind, "kind" }, { Name, "name" }, { Size, "size" },
		{ Color, "color" }, { DisplayedAnchors, "displayedAnchors" },
	};
}

auto HudEditorToolboxModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto HudEditorToolboxModel::data( const QModelIndex &modelIndex, int role ) const -> QVariant {
	if( modelIndex.isValid() ) {
		if( const int row = modelIndex.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case Kind: return m_entries[row].kind;
				case Name: return m_entries[row].name.data();
				case Size: return m_entries[row].rectangle.size();
				case Color: return m_entries[row].color;
				case DisplayedAnchors: return m_entries[row].displayedAnchors;
				default:;
			}
		}
	}
	return QVariant();
}

void HudEditorToolboxModel::setDisplayedAnchorsForKind( int kind, int anchors ) {
	assert( (unsigned)( kind - 1 ) < m_entries.size() );
	m_entries[kind - 1].kind = kind;
	const QModelIndex modelIndex( createIndex( kind - 1, 0 ) );
	// TODO: Use a better granularity for supplied roles
	Q_EMIT dataChanged( modelIndex, modelIndex, kMutableRoles );
}

bool HudEditorLayoutModel::acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) {
	const QMetaEnum metaKinds( QMetaEnum::fromType<Kind>() );

	// Discover what kinds are missing from the layout
	unsigned presentKindsMask = 0;
	assert( metaKinds.keyCount() < 32 );

	wsw::Vector<Entry> entries;
	for( FileEntry &fileEntry: fileEntries ) {
		const auto &props = kEditorPropsForKind[fileEntry.kind - 1];
		assert( fileEntry.kind == props.kind );
		Entry entry;
		entry.kind = fileEntry.kind;
		entry.selfAnchors = fileEntry.selfAnchors;
		entry.anchorItemAnchors = fileEntry.otherAnchors;
		assert( !fileEntry.anchorItem.isToolbox() );
		entry.realAnchorItem = fileEntry.anchorItem;
		entry.rectangle.setSize( props.size );
		entry.color = props.color;
		entry.name = props.name;
		entries.push_back( entry );

		// These assertions hold for valid deserialized entries
		const auto kind = (unsigned)entry.kind;
		assert( kind && kind < 32 );
		assert( metaKinds.valueToKey( kind ) );
		assert( !( presentKindsMask & ( 1u << kind ) ) );
		presentKindsMask |= ( 1u << kind );
	}

	// Add toolbox entries for missing item kinds
	for( int i = 1; i < metaKinds.keyCount() + 1; ++i ) {
		if( !( presentKindsMask & ( 1u << (unsigned)i ) ) ) {
			const auto kind = (Kind)i;
			const auto props = kEditorPropsForKind[kind - 1];
			Entry entry;
			entry.kind = kind;
			entry.selfAnchors = VCenter | HCenter;
			entry.anchorItemAnchors = VCenter | HCenter;
			entry.realAnchorItem = AnchorItem::forToolbox();
			entry.rectangle.setSize( props.size );
			entry.color = props.color;
			entry.name = props.name;
			entries.push_back( entry );
		}
	}

	beginResetModel();
	std::swap( m_entries, entries );
	endResetModel();
	return true;
}

const HudLayoutModel::EditorProps HudLayoutModel::kEditorPropsForKind[] {
	{ "Health"_asView, HealthBar, QSize( 144, 32 ), QColor::fromRgbF( 1.0, 0.5, 1.0 ), std::nullopt },
	{ "Armor"_asView, ArmorBar, QSize( 144, 32 ), QColor::fromRgbF( 1.0, 0.3, 0.0 ), std::nullopt },
	{ "Inventory"_asView, InventoryBar, QSize( 256, 48 ), QColor::fromRgbF( 1.0, 0.8, 0.0 ), std::nullopt },
	{ "Weapon status"_asView, WeaponStatus, QSize( 96, 96 ), QColor::fromRgbF( 1.0, 0.5, 0.0 ), std::nullopt },
	{ "Match time"_asView, MatchTime, QSize( 128, 64 ), QColor::fromRgbF( 0.7, 0.7, 0.7 ), std::nullopt },
	{ "Alpha score"_asView, AlphaScore, QSize( 128, 56 ), QColor::fromRgbF( 1.0, 0.0, 0.0 ), std::nullopt },
	{ "Beta score"_asView, BetaScore, QSize( 128, 56 ), QColor::fromRgbF( 0.0, 1.0, 0.0 ), std::nullopt },
	{ "Chat"_asView, Chat, QSize( 256, 72 ), QColor::fromRgbF( 0.7, 1.0, 0.3 ), std::nullopt },
	{ "Team info"_asView, TeamInfo, QSize( 256, 128 ), QColor::fromRgbF( 0.0, 0.3, 0.7 ), "cg_showTeamInfo"_asView },
	{ "Frags feed"_asView, FragsFeed, QSize( 144, 108 ), QColor::fromRgbF( 0.3, 0.0, 0.7 ), "cg_showFragsFeed"_asView },
	{ "Message feed"_asView, MessageFeed, QSize( 256, 64 ), QColor::fromRgbF( 0.0, 0.7, 0.7 ), "cg_showMessageFeed"_asView },
	{ "Awards area"_asView, AwardsArea, QSize( 256, 64 ), QColor::fromRgbF( 0.0, 0.7, 0.9 ), "cg_showAwards"_asView },
	{ "Status message"_asView, StatusMessage, QSize( 192, 32 ), QColor::fromRgbF( 0.3, 0.9, 0.7 ), std::nullopt },
	{ "Objective status"_asView, ObjectiveStatus, QSize( 96, 64 ), QColor::fromRgbF( 0.9, 0.6, 0.3 ), std::nullopt }
};

void HudEditorModel::setFieldAreaSize( qreal width, qreal height ) {
	if( const QSizeF size( width, height); size != m_fieldAreaSize ) {
		m_layoutModel.beginResetModel();
		m_toolboxModel.beginResetModel();
		m_fieldAreaSize = size;
		m_toolboxModel.endResetModel();
		m_layoutModel.endResetModel();
	}
}

void HudEditorModel::setDragAreaSize( qreal width, qreal height ) {
	if( const QSizeF size( width, height ); size != m_dragAreaSize ) {
		m_layoutModel.beginResetModel();
		m_toolboxModel.beginResetModel();
		m_dragAreaSize = size;
		m_toolboxModel.endResetModel();
		m_layoutModel.endResetModel();
	}
}

auto HudEditorModel::getLayoutModel() -> QObject * {
	if( !m_hasSetLayoutModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_layoutModel, QQmlEngine::CppOwnership );
		m_hasSetLayoutModelOwnership = true;
	}
	return &m_layoutModel;
}

auto HudEditorModel::getToolboxModel() -> QObject * {
	if( !m_hasSetToolboxModelOwnership ) {
		QQmlEngine::setObjectOwnership( &m_toolboxModel, QQmlEngine::CppOwnership );
		m_hasSetToolboxModelOwnership = true;
	}
	return &m_toolboxModel;
}

void HudEditorModel::trackDragging( int index, qreal x, qreal y ) {
	auto &entries = m_layoutModel.m_entries;
	assert( (unsigned)index < (unsigned)entries.size() );
	entries[index].pendingOrigin = QPointF( x, y );
	updateMarkers( index );
}

void HudEditorModel::finishDragging( int index ) {
	auto &entries = m_layoutModel.m_entries;
	assert( (unsigned)index < (unsigned)entries.size() );
	auto &dragged = entries[index];

	if( dragged.rectangle.topLeft() != dragged.pendingOrigin ) {
		dragged.rectangle.moveTopLeft( dragged.pendingOrigin );
	}

	if( dragged.displayedAnchorItem ) {
		const AnchorItem anchorItem = *dragged.displayedAnchorItem;
		assert( dragged.displayedAnchors );
		dragged.realAnchorItem = anchorItem;
		dragged.selfAnchors = dragged.displayedAnchors;
		if( anchorItem.isOtherItem() ) {
			dragged.anchorItemAnchors = entries[anchorItem.toItemIndex()].displayedAnchors;
		} else if( anchorItem.isField() ) {
			dragged.anchorItemAnchors = m_displayedFieldAnchors;
		} else {
			dragged.anchorItemAnchors = HudLayoutModel::VCenter | HudLayoutModel::HCenter;
		}
	}

	m_layoutModel.notifyOfFullUpdateAtIndex( index );
}

void HudEditorModel::clearDisplayedMarkers( int index ) {
	updateMarkers( index );

	auto &entries = m_layoutModel.m_entries;
	auto &dragged = entries[index];
	if( dragged.displayedAnchorItem ) {
		const auto oldAnchorItem = *dragged.displayedAnchorItem;
		if( oldAnchorItem.isOtherItem() ) {
			const int itemIndex = oldAnchorItem.toItemIndex();
			entries[itemIndex].displayedAnchors = 0;
			assert( entries[itemIndex].displayedAnchorItem == std::nullopt );
			m_layoutModel.notifyOfDisplayedAnchorsUpdateAtIndex( itemIndex );
		} else if( oldAnchorItem.isField() ) {
			setDisplayedFieldAnchors( 0 );
		} else {
			assert( oldAnchorItem.isToolbox() && dragged.kind );
			m_toolboxModel.setDisplayedAnchorsForKind( dragged.kind, 0 );
		}
	}

	entries[index].displayedAnchors = 0;
	entries[index].displayedAnchorItem = std::nullopt;
	m_layoutModel.notifyOfDisplayedAnchorsUpdateAtIndex( index );
}

void HudEditorModel::updateElementPosition( int index, qreal x, qreal y ) {
	auto &entries = m_layoutModel.m_entries;
	assert( (unsigned)index < (unsigned)entries.size() );
	auto &entry = entries[index];
	if( const QPointF point( x, y ); entry.rectangle.topLeft() != point ) {
		entry.rectangle.moveTopLeft( point );
		m_layoutModel.notifyOfOriginUpdateAtIndex( index );
	}
}

Q_INVOKABLE void HudEditorModel::updatePlaceholderPosition( int index, qreal x, qreal y ) {
	auto &entries = m_toolboxModel.m_entries;
	assert( (unsigned)index < (unsigned)entries.size() );
	auto &entry = entries[index];
	if( const QPointF point( x, y ); entry.rectangle.topLeft() != point ) {
		entry.rectangle.moveTopLeft( point );
		// There's no need to dispatch updates in this case
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

void HudEditorModel::updateMarkers( int draggedIndex ) {
	// It's more convenient to decompose results
	std::optional<AnchorItem> anchorItem;
	std::optional<AnchorPair> anchorsPair;
	if( const auto maybeItemAndAnchors = getMatchingAnchorItem( draggedIndex ) ) {
		anchorItem = maybeItemAndAnchors->first;
		anchorsPair = maybeItemAndAnchors->second;
	}

	SmallIntSet modifiedRows;
	std::optional<int> updatedToolboxAnchors;

	auto &entries = m_layoutModel.m_entries;
	HudEditorLayoutModel::Entry &dragged = entries[draggedIndex];
	if( dragged.displayedAnchorItem != anchorItem ) {
		// Clear flags of a (maybe) old item
		if( dragged.displayedAnchorItem ) {
			const AnchorItem oldDisplayedAnchorItem = *dragged.displayedAnchorItem;
			if( oldDisplayedAnchorItem.isOtherItem() ) {
				const int itemIndex = oldDisplayedAnchorItem.toItemIndex();
				entries[itemIndex].displayedAnchors = 0;
				entries[itemIndex].displayedAnchorItem = std::nullopt;
				modifiedRows.add( itemIndex );
			} else if( oldDisplayedAnchorItem.isField() ) {
				setDisplayedFieldAnchors( 0 );
			} else {
				assert( oldDisplayedAnchorItem.isToolbox() );
				updatedToolboxAnchors = 0;
			}
		}
		dragged.displayedAnchorItem = anchorItem;
		modifiedRows.add( draggedIndex );
	}

	if( dragged.displayedAnchorItem ) {
		assert( anchorsPair );
		const AnchorItem currDisplayedAnchorItem = *dragged.displayedAnchorItem;
		if( currDisplayedAnchorItem.isOtherItem() ) {
			const int currItemIndex = currDisplayedAnchorItem.toItemIndex();
			if( entries[currItemIndex].displayedAnchors != anchorsPair->otherAnchors ) {
				entries[currItemIndex].displayedAnchors = anchorsPair->otherAnchors;
				modifiedRows.add( currItemIndex );
			}
		} else if( currDisplayedAnchorItem.isField() ) {
			setDisplayedFieldAnchors( anchorsPair->otherAnchors );
		} else {
			assert( currDisplayedAnchorItem.isToolbox() );
			assert( anchorsPair->otherAnchors == ( HudLayoutModel::VCenter | HudLayoutModel::HCenter ) );
			updatedToolboxAnchors = anchorsPair->otherAnchors;
		}
	}

	const int selfAnchors = anchorsPair ? anchorsPair->selfAnchors : 0;
	if( dragged.displayedAnchors != selfAnchors ) {
		dragged.displayedAnchors = selfAnchors;
		modifiedRows.add( draggedIndex );
	}

	for( int row: modifiedRows ) {
		m_layoutModel.notifyOfDisplayedAnchorsUpdateAtIndex( row );
	}

	if( updatedToolboxAnchors != std::nullopt ) {
		m_toolboxModel.setDisplayedAnchorsForKind( dragged.kind, *updatedToolboxAnchors );
	}
}

[[nodiscard]]
static inline bool isClose( const QPointF &pt1, const QPointF &pt2 ) {
	const QPointF diff( pt1 - pt2 );
	// Units are device-independent so this is correct
	return diff.x() * diff.x() + diff.y() * diff.y() < 8.0 * 8.0;
}

template <typename Range>
auto HudEditorModel::getMatchingAnchors( const QRectF &draggedRectangle, const QRectF &otherRectangle,
										 const Range &range )
	-> std::optional<AnchorPair> {
	for( auto it = std::begin( range ); it != std::end( range ); ++it ) {
		const auto &[selfAnchors, otherAnchors] = *it;
		const QPointF selfPoint( getPointForAnchors( draggedRectangle, selfAnchors ) );
		const QPointF otherPoint( getPointForAnchors( otherRectangle, otherAnchors ) );
		if( isClose( selfPoint, otherPoint ) ) {
			return AnchorPair { selfAnchors, otherAnchors };
		}
	}
	return std::nullopt;
}

auto HudEditorModel::getMatchingEntryAnchors( const QRectF &draggedRectangle, const QRectF &otherEntryRectangle )
	-> std::optional<AnchorPair> {
	return getMatchingAnchors( draggedRectangle, otherEntryRectangle, HudLayoutModel::kMatchingItemAndItemAnchorPairs );
}

auto HudEditorModel::getMatchingFieldAnchors( const QRectF &draggedRectangle, const QRectF &fieldRectangle )
	-> std::optional<AnchorPair> {
	return getMatchingAnchors( draggedRectangle, fieldRectangle, HudLayoutModel::kMatchingItemAndFieldAnchorPairs );
}

auto HudEditorModel::getMatchingAnchorItem( int draggedIndex ) const
	-> std::optional<std::pair<AnchorItem, AnchorPair>> {
	const auto &itemEntries = m_layoutModel.m_entries;
	assert( (unsigned)draggedIndex < (unsigned)itemEntries.size() );
	QRectF draggedRectangle( itemEntries[draggedIndex].rectangle );
	draggedRectangle.moveTopLeft( itemEntries[draggedIndex].pendingOrigin );
	const QRectF validAreaRectangle( 0, 0, m_fieldAreaSize.width(), m_fieldAreaSize.height() );
	if( !draggedRectangle.intersects( validAreaRectangle ) ) {
		const auto draggedItemKind = itemEntries[draggedIndex].kind;
		assert( draggedItemKind > 0 );
		const auto &toolboxEntryRectangle = m_toolboxModel.m_entries[draggedItemKind - 1].rectangle;
		const auto centerAnchors = HudLayoutModel::VCenter | HudLayoutModel::HCenter;
		const QPointF draggedItemCenter( getPointForAnchors( draggedRectangle, centerAnchors ) );
		const QPointF toolboxPlaceholderCenter( getPointForAnchors( toolboxEntryRectangle, centerAnchors ) );
		if( isClose( draggedItemCenter, toolboxPlaceholderCenter ) ) {
			return std::make_pair( AnchorItem::forToolbox(), AnchorPair { centerAnchors, centerAnchors } );
		}
		return std::nullopt;
	}

	for( unsigned i = 0; i < itemEntries.size(); ++i ) {
		if( i != (unsigned)draggedIndex ) {
			if( const auto maybeAnchors = getMatchingEntryAnchors( draggedRectangle, itemEntries[i].rectangle ) ) {
				if( isAnchorDefinedPositionValid( draggedIndex, (int)i, *maybeAnchors ) ) {
					const auto entryAnchorItem = itemEntries[i].realAnchorItem;
					if( !entryAnchorItem.isOtherItem() || entryAnchorItem.toItemIndex() != draggedIndex ) {
						return std::make_pair( AnchorItem::forItem( i ), *maybeAnchors );
					}
				}
			}
		}
	}

	assert( m_fieldAreaSize.isValid() );
	const QRectF fieldRectangle( 0, 0, m_fieldAreaSize.width(), m_fieldAreaSize.height() );
	if( const auto maybeAnchors = getMatchingFieldAnchors( draggedRectangle, fieldRectangle ) ) {
		if( isAnchorDefinedPositionValid( draggedIndex, std::nullopt, *maybeAnchors ) ) {
			return std::make_pair( AnchorItem::forField(), *maybeAnchors );
		}
	}

	return std::nullopt;
}

bool HudEditorModel::isAnchorDefinedPositionValid( int draggedIndex, const std::optional<int> &otherIndex,
												   const AnchorPair &anchors ) const {
	const auto &entries = m_layoutModel.m_entries;
	assert( (unsigned)draggedIndex < (unsigned)entries.size() );
	const auto &dragged = entries[draggedIndex];

	QPointF anchorPoint;
	if( otherIndex ) {
		assert( (unsigned)*otherIndex < (unsigned)entries.size() && *otherIndex != draggedIndex );
		anchorPoint = getPointForAnchors( entries[*otherIndex].rectangle, anchors.otherAnchors );
	} else {
		const QRectF fieldRectangle( 0, 0, m_fieldAreaSize.width(), m_fieldAreaSize.height() );
		anchorPoint = getPointForAnchors( fieldRectangle, anchors.otherAnchors );
	}

	// Apply an anchor-defined position to the dragged item rectangle
	QRectF predictedRectangle( dragged.rectangle );
	// Align center first as this moves along both axes
	if( anchors.selfAnchors & ( HudLayoutModel::VCenter | HudLayoutModel::HCenter ) ) {
		predictedRectangle.moveCenter( anchorPoint );
	}

	if( anchors.selfAnchors & HudLayoutModel::Left ) {
		predictedRectangle.moveLeft( anchorPoint.x() );
	} else if( anchors.selfAnchors & HudLayoutModel::Right ) {
		predictedRectangle.moveRight( anchorPoint.x() );
	}

	if( anchors.selfAnchors & HudLayoutModel::Top ) {
		predictedRectangle.moveTop( anchorPoint.y() );
	} else if( anchors.selfAnchors & HudLayoutModel::Bottom ) {
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
	for( unsigned i = 0; i < entries.size(); ++i ) {
		if( i != (unsigned)draggedIndex && i != (unsigned)secondCmpIndex ) {
			if( entries[i].rectangle.intersects( predictedRectangle ) ) {
				return false;
			}
		}
	}

	return true;
}

static constexpr auto kHorizontalBitsMask = 0x7;
static constexpr auto kVerticalBitsMask = 0x7 << 3;

auto HudEditorModel::getPointForAnchors( const QRectF &r, int anchors ) -> QPointF {
	qreal x;
	switch( anchors & kHorizontalBitsMask ) {
		case HudLayoutModel::Left: x = r.left(); break;
		case HudLayoutModel::HCenter: x = 0.5 * ( r.left() + r.right() ); break;
		case HudLayoutModel::Right: x = r.right(); break;
		default: wsw::failWithInvalidArgument( "Invalid X anchor bits" );
	}
	qreal y;
	switch( anchors & kVerticalBitsMask ) {
		case HudLayoutModel::Top: y = r.top(); break;
		case HudLayoutModel::VCenter: y = 0.5 * ( r.top() + r.bottom() ); break;
		case HudLayoutModel::Bottom: y = r.bottom(); break;
		default: wsw::failWithInvalidArgument( "Invalid Y anchor bits" );
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
		case HealthBar: return AlivePovOnly;
		case ArmorBar: return AlivePovOnly;
		case InventoryBar: return AlivePovOnly;
		case WeaponStatus: return AlivePovOnly;
		case MatchTime: return NoFlags;
		case AlphaScore: return TeamBasedOnly;
		case BetaScore: return TeamBasedOnly;
		case Chat: return NoFlags;
		case TeamInfo: return (Flags)( TeamBasedOnly | PovOnly );
		case FragsFeed: return NoFlags;
		case MessageFeed: return NoFlags;
		case AwardsArea: return NoFlags;
		case StatusMessage: return NoFlags;
		case ObjectiveStatus: return NoFlags;
		default: wsw::failWithLogicError( "unreachable" );
	}
}

HudEditorModel::HudEditorModel() {
	reloadExistingHuds();

	m_toolboxModel.beginResetModel();

	for( const auto &props: HudEditorLayoutModel::kEditorPropsForKind ) {
		HudEditorToolboxModel::Entry entry;
		entry.name = props.name;
		entry.color = props.color;
		entry.kind = props.kind;
		entry.rectangle = QRectF( 0, 0, props.size.width(), props.size.height() );
		m_toolboxModel.m_entries.push_back( entry );
	}

	m_toolboxModel.endResetModel();
}

auto InGameHudLayoutModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ ItemKind, "kind" },
		{ Flags, "flags" },
		{ SelfAnchors, "selfAnchors" },
		{ AnchorItemAnchors, "anchorItemAnchors" },
		{ AnchorItemIndex, "anchorItemIndex" },
		{ ControllingCVar, "controllingCVar" }
	};
}

auto InGameHudLayoutModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_entries.size();
}

auto InGameHudLayoutModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( index.isValid() ) {
		if( int row = index.row(); (unsigned)row < (unsigned)m_entries.size() ) {
			switch( role ) {
				case ItemKind: return m_entries[row].kind;
				case Flags: return getFlagsForKind( m_entries[row].kind );
				case SelfAnchors: return m_entries[row].selfAnchors;
				case AnchorItemAnchors: return m_entries[row].otherAnchors;
				case AnchorItemIndex: return m_entries[row].anchorItem.toRawValue();
				case ControllingCVar: return m_entries[row].getControllingCVarAsQVariant();
				default: return QVariant();
			}
		}
	}
	return QVariant();
}

bool InGameHudLayoutModel::acceptDeserializedEntries( wsw::Vector<FileEntry> &&fileEntries ) {
	beginResetModel();
	m_entries.clear();
	m_entries.reserve( fileEntries.size() );
	for( const FileEntry &fileEntry: fileEntries ) {
		assert( fileEntry.kind && fileEntry.kind < std::size( kEditorPropsForKind ) + 1 );
		m_entries.emplace_back( Entry {
			.kind            = fileEntry.kind,
			.selfAnchors     = fileEntry.selfAnchors,
			.otherAnchors    = fileEntry.otherAnchors,
			.anchorItem      = fileEntry.anchorItem,
			.controllingCVar = kEditorPropsForKind[fileEntry.kind - 1].controllingCVar,
		});
	}
	endResetModel();
	return true;
}

}