#ifndef WSW_WSWSTRINGSPLITTER_H
#define WSW_WSWSTRINGSPLITTER_H

#include "wswstringview.h"
#include <type_traits>

namespace wsw {

class StringSplitter {
	wsw::StringView m_data;
	size_t m_tokenNum { 0 };

	template <typename Separator>
	[[nodiscard]]
	static auto lenOf( Separator separator ) -> unsigned {
		if constexpr( std::is_same_v<wsw::StringView, std::remove_cvref_t<Separator>> ) {
			return separator.length();
		}
		return 1;
	}

	template <typename Separator>
	[[nodiscard]]
	auto getNext_( Separator separator ) -> std::optional<wsw::StringView> {
		for(;; ) {
			if( auto maybeIndex = m_data.indexOf( separator ) ) {
				auto index = *maybeIndex;
				// Disallow empty tokens
				if( index ) {
					auto result = m_data.take( index );
					m_data = m_data.drop( index + lenOf( separator ) );
					m_tokenNum++;
					return result;
				}
				m_data = m_data.drop( lenOf( separator ) );
			} else if( !m_data.empty() ) {
				auto result = m_data;
				// Preserve the underlying pointer (may be useful in future)
				m_data = m_data.drop( m_data.length() );
				m_tokenNum++;
				assert( m_data.empty() );
				return result;
			} else {
				return std::nullopt;
			}
		}
	}

	template <typename Separator>
	[[nodiscard]]
	auto getNextWithNum_( Separator separator ) -> std::optional<std::pair<wsw::StringView, size_t>> {
		for(;; ) {
			if( auto maybeIndex = m_data.indexOf( separator ) ) {
				auto index = *maybeIndex;
				if( index ) {
					auto view = m_data.take( index );
					m_data = m_data.drop( index + lenOf( separator ) );
					auto num = m_tokenNum;
					m_tokenNum++;
					return std::make_pair( view, num );
				}
				m_data = m_data.drop( lenOf( separator ) );
			} else if( !m_data.empty() ) {
				auto view = m_data;
				m_data = m_data.drop( m_data.length() );
				auto num = m_tokenNum;
				m_tokenNum++;
				assert( m_data.empty() );
				return std::make_pair( view, num );
			} else {
				return std::nullopt;
			}
		}
	}
public:
	explicit StringSplitter( wsw::StringView data )
		: m_data( data ) {}

	[[nodiscard]]
	auto getLastTokenNum() const -> size_t {
		assert( m_tokenNum );
		return m_tokenNum - 1;
	}

	[[nodiscard]]
	auto getNext( char separator = ' ' ) -> std::optional<wsw::StringView> {
		return getNext_( separator );
	}

	[[nodiscard]]
	auto getNext( const wsw::CharLookup &separatorChars ) -> std::optional<wsw::StringView> {
		return getNext_( separatorChars );
	}

	[[nodiscard]]
	auto getNext( const wsw::StringView &separatorString ) -> std::optional<wsw::StringView> {
		assert( !separatorString.empty() );
		return getNext_( separatorString );
	}

	[[nodiscard]]
	auto getNextWithNum( char separator = ' ' ) -> std::optional<std::pair<wsw::StringView, size_t>> {
		return getNextWithNum_( separator );
	}

	[[nodiscard]]
	auto getNextWithNum( const wsw::CharLookup &separatorChars ) -> std::optional<std::pair<wsw::StringView, size_t>> {
		return getNextWithNum_( separatorChars );
	}

	[[nodiscard]]
	auto getNextWithNum( const wsw::StringView &separatorString ) -> std::optional<std::pair<wsw::StringView, size_t>> {
		assert( !separatorString.empty() );
		return getNextWithNum_( separatorString );
	}
};

}

#endif
