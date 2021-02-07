#include "callvotesmodel.h"
#include "../client/client.h"
#include "../qcommon/base64.h"
#include "../qcommon/compression.h"
#include "../qcommon/wswstringsplitter.h"

#include <QJsonObject>

using wsw::operator""_asView;

namespace wsw::ui {

auto CallvotesModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Name, "name" },
		{ Desc, "desc" },
		{ Flags, "flags" },
		{ ArgsKind, "argsKind" },
		{ ArgsHandle, "argsHandle" },
		{ Current, "current" }
	};
}

auto CallvotesModel::rowCount( const QModelIndex & ) const -> int  {
	return m_entryNums.size();
}

auto CallvotesModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( !index.isValid() ) {
		return QVariant();
	}

	const int row = index.row();
	if( (unsigned)row >= (unsigned)m_entryNums.size() ) {
		return QVariant();
	}

	switch( role ) {
		case Name: return m_proxy->getEntry( row ).name;
		case Desc: return m_proxy->getEntry( row ).desc;
		case Flags: return m_proxy->getEntry( row ).flags;
		case ArgsKind: return m_proxy->getEntry( row ).kind;
		case ArgsHandle: return m_proxy->getEntry( row ).argsHandle;
		case Current: return m_proxy->getEntry( row ).current;
		default: return QVariant();
	}
}

void CallvotesModel::notifyOfChangesAtNum( int num ) {
	const auto it = std::find( m_entryNums.begin(), m_entryNums.end(), num );
	if( it != m_entryNums.end() ) {
		const auto index = (int)( it - m_entryNums.begin() );
		QModelIndex modelIndex( createIndex( index, 0 ) );
		Q_EMIT dataChanged( modelIndex, modelIndex, kRoleCurrentChangeset );
		Q_EMIT currentChanged( index, m_proxy->getEntry( num ).current );
	}
}

auto CallvotesModel::getOptionsList( int handle ) const -> QJsonArray {
	assert( (unsigned)( handle - 1 ) < (unsigned)m_proxy->m_options.size() );
	[[maybe_unused]] const auto &[options, storedHandle] = m_proxy->m_options[handle - 1];
	assert( handle == storedHandle );

	QJsonArray result;
	for( const auto &[off, len]: options.spans ) {
		QString s( QString::fromUtf8( options.content.data() + off, len ) );
		result.append( QJsonObject {{"name", s}, {"value", s}});
	}

	return result;
}

static const std::pair<wsw::StringView, CallvotesModel::Kind> kArgKindNames[] {
	{ "boolean"_asView, CallvotesModel::Boolean },
	{ "number"_asView, CallvotesModel::Number },
	{ "player"_asView, CallvotesModel::Player },
	{ "minutes"_asView, CallvotesModel::Minutes },
	{ "maplist"_asView, CallvotesModel::MapList },
	{ "options"_asView, CallvotesModel::Options }
};

void CallvotesModelProxy::reload() {
	m_regularModel.beginResetModel();
	m_operatorModel.beginResetModel();

	m_regularModel.m_entryNums.clear();
	m_operatorModel.m_entryNums.clear();

	m_entries.clear();
	m_options.clear();

	using Storage = wsw::ConfigStringStorage;
	const Storage &storage = ::cl.configStrings;
	static_assert( Storage::kCallvoteFieldName == 0, "The name is assumed to come first" );

	unsigned num = 0;
	for(;; num ++ ) {
		const auto maybeName = storage.getCallvoteName( num );
		if( !maybeName ) {
			break;
		}

		const auto maybeDesc = storage.getCallvoteDesc( num );
		const auto maybeStatus = storage.getCallvoteStatus( num );

		// Very unlikely
		if( !maybeDesc || !maybeStatus ) {
			break;
		}

		wsw::StringSplitter splitter( *maybeStatus );
		const auto maybeAllowedOps = splitter.getNext();
		// Very unlikely
		if( !maybeAllowedOps ) {
			break;
		}

		const bool isVotingEnabled = maybeAllowedOps->contains( 'v' );
		const bool isOpcallEnabled = maybeAllowedOps->contains( 'o' );
		// Very unlikely
		if( !isVotingEnabled && !isOpcallEnabled ) {
			break;
		}

		const auto current = splitter.getNext().value_or( wsw::StringView() );

		const auto maybeArgKindAndHandle = addArgs( storage.getCallvoteArgs( num ) );
		// Very unlikely
		if( !maybeArgKindAndHandle ) {
			break;
		}

		const auto [kind, maybeArgsHandle] = *maybeArgKindAndHandle;
		assert( !maybeArgsHandle || *maybeArgsHandle > 0 );

		unsigned flags = 0;
		if( isVotingEnabled ) {
			flags |= CallvotesModel::Regular;
		}
		if( isOpcallEnabled ) {
			flags |= CallvotesModel::Operator;
		}

		m_operatorModel.m_entryNums.push_back( (int)m_entries.size() );
		if( isVotingEnabled ) {
			m_regularModel.m_entryNums.push_back( (int) m_entries.size() );
		}

		Entry entry {
			QString::fromUtf8( maybeName->data(), maybeName->size() ),
			QString::fromUtf8( maybeDesc->data(), maybeDesc->size() ),
			QString::fromUtf8( current.data(), current.size() ),
			flags,
			kind,
			maybeArgsHandle.value_or( 0 )
		};

		m_entries.emplace_back( std::move( entry ) );
	}

	m_regularModel.endResetModel();
	m_operatorModel.endResetModel();
}

auto CallvotesModelProxy::addArgs( const std::optional<wsw::StringView> &maybeArgs )
	-> std::optional<std::pair<CallvotesModel::Kind, std::optional<int>>> {
	if( !maybeArgs ) {
		return std::make_pair( CallvotesModel::NoArgs, std::nullopt );
	}

	wsw::StringSplitter splitter( *maybeArgs );

	const auto maybeHeadToken = splitter.getNext();
	// Malformed args, should not happen
	if( !maybeHeadToken ) {
		return std::nullopt;
	}

	auto foundKind = CallvotesModel::NoArgs;
	for( const auto &[name, kind] : kArgKindNames ) {
		assert( kind != CallvotesModel::NoArgs );
		if( maybeHeadToken->equalsIgnoreCase( name ) ) {
			foundKind = kind;
			break;
		}
	}

	if( foundKind == CallvotesModel::NoArgs ) {
		return std::nullopt;
	}
	if( foundKind != CallvotesModel::Options && foundKind != CallvotesModel::MapList ) {
		return std::make_pair( foundKind, std::nullopt );
	}

	const auto maybeDataToken = splitter.getNext();
	if( !maybeDataToken ) {
		return std::nullopt;
	}

	// Check for illegal trailing tokens
	if( splitter.getNext() ) {
		return std::nullopt;
	}

	if( const auto maybeArgsHandle = parseAndAddOptions( *maybeDataToken ) ) {
		return std::make_pair( foundKind, maybeArgsHandle );
	}

	return std::nullopt;
}

auto CallvotesModelProxy::parseAndAddOptions( const wsw::StringView &encodedOptions ) -> std::optional<int> {
	size_t zippedDataLen = 0;
	auto *decoded = base64_decode( (const unsigned char *)encodedOptions.data(), encodedOptions.size(), &zippedDataLen );
	if( !decoded ) {
		return std::nullopt;
	}

	wsw::String content;
	content.resize( 1u << 15u );

	uLong unpackedDataLen = content.size();
	const auto zlibResult = qzuncompress( (Bytef *)content.data(), &unpackedDataLen, decoded, zippedDataLen );
	Q_free( decoded );
	if( zlibResult != Z_OK ) {
		return std::nullopt;
	}

	content.resize( unpackedDataLen );

	wsw::Vector<std::pair<uint16_t, uint16_t>> spans;
	wsw::StringSplitter splitter( wsw::StringView( content.data(), content.size() ) );
	while( auto maybeToken = splitter.getNext() ) {
		const auto token = *maybeToken;
		const auto rawOffset = token.data() - content.data();

		// This allows us a further retrieval of zero-terminated tokens
		if( const auto tokenEndIndex = rawOffset + token.length(); tokenEndIndex < content.size() ) {
			content[tokenEndIndex] = '\0';
		}

		assert( rawOffset < (int)std::numeric_limits<uint16_t>::max() );
		assert( token.size() < (size_t)std::numeric_limits<uint16_t>::max() );
		spans.emplace_back( std::make_pair( (uint16_t)rawOffset, (uint16_t)token.size() ) );
	}

	const int handle = (int)m_options.size() + 1;
	OptionTokens tokens { std::move( content ), std::move( spans ) };
	m_options.emplace_back( std::make_pair( std::move( tokens ), handle ) );
	return handle;
}

void CallvotesModelProxy::handleConfigString( unsigned configStringNum, const wsw::StringView &string ) {
	// Should not happen
	if( configStringNum < CS_CALLVOTEINFOS || configStringNum >= CS_CALLVOTEINFOS + MAX_CALLVOTEINFOS ) {
		return;
	}

	if( configStringNum % 4 != wsw::ConfigStringStorage::kCallvoteFieldStatus ) {
		return;
	}

	const auto entryNum = (int)( configStringNum - CS_CALLVOTEINFOS ) / 4;
	// Should not happen
	if( (unsigned)entryNum >= (unsigned)m_entries.size() ) {
		return;
	}

	wsw::StringSplitter splitter( string );
	// Skip flags
	if( !splitter.getNext() ) {
		return;
	}

	const auto maybeCurrent = splitter.getNext();
	if( !maybeCurrent ) {
		return;
	}

	// TODO: Avoid reallocation?
	m_entries[entryNum].current = QString::fromUtf8( maybeCurrent->data(), maybeCurrent->size() );

	// TODO: Also emit some global signal that should be useful for updating values in an open popup
	m_regularModel.notifyOfChangesAtNum( entryNum );
	m_operatorModel.notifyOfChangesAtNum( entryNum );
}

}