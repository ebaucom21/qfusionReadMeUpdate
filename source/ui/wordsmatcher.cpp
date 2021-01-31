/**
 * Distance computations are derived from this work
 * Copyright (C) 2019 Frederik Hertzum
 * SPDX-License-Identifier: GPL-3.0
 */

#include "wordsmatcher.h"

#include <numeric>
#include <vector>

namespace wsw::ui {

auto WordsMatcher::distance( wsw::StringView a, wsw::StringView b, unsigned maxDist ) -> std::optional<Match> {
	if( a.length() > b.length() ) {
		std::swap( a, b );
	}

	unsigned prefixLen = 0;
	for( unsigned i = 0, len = std::min( a.length(), b.length() ); i < len; ++i ) {
		if( a[i] != b[i] ) {
			prefixLen = i;
			break;
		}
	}

	a = a.drop( prefixLen );
	b = b.drop( prefixLen );

	unsigned suffixLen = 0;
	for( unsigned i = 0, len = std::min( a.length(), b.length() ); i < len; ++i ) {
		if( a[a.length() - 1 - i] != b[b.length() - 1 - i] ) {
			suffixLen = i;
			break;
		}
	}

	a = a.dropRight( suffixLen );
	b = b.dropRight( suffixLen );

	assert( a.length() <= b.length() );

	const unsigned bufferSize = b.length() + 1;
	m_distanceBuffer.resize( bufferSize );

	auto *const buffer = m_distanceBuffer.data();
	std::iota( buffer, buffer + bufferSize, 0 );

	for( unsigned i = 1; i < a.length() + 1; ++i) {
		unsigned temp = buffer[0]++;
		for( unsigned j = 1; j < bufferSize; ++j ) {
			unsigned p = buffer[j - 1];
			unsigned r = buffer[j];
			temp = std::min( std::min( r, p ) + 1, temp + ( a[i - 1] == b[j - 1] ? 0 : 1 ) );
			std::swap( buffer[j], temp );
		}
	}

	// TODO: Cut off early
	if( const auto dist = buffer[bufferSize - 1]; dist < maxDist ) {
		return Match { dist, prefixLen + suffixLen };
	}

	return std::nullopt;
}

auto WordsMatcher::prepareInput( const wsw::StringView &rawInput ) -> wsw::StringView {
	m_inputBuffer.clear();
	m_inputBuffer.reserve( rawInput.size() );
	for( char ch: rawInput ) {
		m_inputBuffer.push_back( std::tolower( ch, std::locale::classic() ) );
	}
	return wsw::StringView( m_inputBuffer.data(), m_inputBuffer.size() );
}

auto WordsMatcher::matchByDistance( const wsw::StringView &input, const wsw::StringView &word, unsigned maxDist )
	-> std::optional<Match> {
	if( input.length() <= word.size() ) {
		return distance( word, input, maxDist );
	}

	unsigned bestMismatch = maxDist;
	unsigned resultLen = 0;
	const unsigned numMatchingAttempts = input.length() - word.length();
	for( unsigned i = 0; i < numMatchingAttempts; ++i ) {
		const wsw::StringView inputWindow( input.takeMid( i, word.length() ) );
		// Make sure we handle last windows correctly
		assert( inputWindow.length() == word.length() );
		if( const auto maybeMatch = distance( word, inputWindow, bestMismatch ) ) {
			auto [dist, len] = *maybeMatch;
			bestMismatch = dist;
			resultLen = len;
			if( !bestMismatch ) {
				return Match { 0u, resultLen };
			}
		}
	}

	if( bestMismatch != maxDist ) {
		return Match { bestMismatch, resultLen };
	}

	return std::nullopt;
}

WordsMatcher::WordsMatcher( const wsw::StringView &word ) {
	if( word.empty() ) {
		throw std::logic_error( "Should not be used for empty words" );
	}

	m_stringDataBuffer.reserve( word.size() );
	for( char ch: word ) {
		m_stringDataBuffer.push_back( std::tolower( ch, std::locale::classic() ) );
	}

	const char *begin = m_stringDataBuffer.data();
	const char *end = m_stringDataBuffer.data() + m_stringDataBuffer.size();
	new( m_exactMatcherHolder.unsafe_grow_back() )std::boyer_moore_searcher( begin, end );
}

auto WordsMatcher::match( const wsw::StringView &rawInput, unsigned maxDist ) -> std::optional<Match> {
	const wsw::StringView input( prepareInput( rawInput ) );
	if( input.empty() ) {
		return std::nullopt;
	}

	std::boyer_moore_searcher<const char *> &matcher = m_exactMatcherHolder.front();
	if( matcher( input.begin(), input.end() ).first != input.end() ) {
		return Match { 0u, (unsigned)m_stringDataBuffer.length() };
	}

	// Only if a fuzzy match is allowed
	if( maxDist ) {
		const wsw::StringView word( m_stringDataBuffer.data(), m_stringDataBuffer.size() );
		if( auto maybeMatch = matchByDistance( input, word, maxDist ) ) {
			return *maybeMatch;
		}
	}

	return std::nullopt;
}

}
