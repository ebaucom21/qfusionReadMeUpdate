#include "wswpodvector.h"
#include "wswexceptions.h"

namespace wsw {

void BasePodVector::ensureCapacityInElems( size_t newCapacityInElems, size_t elemSize, AllocGrowthPolicy growthPolicy ) noexcept( false ) {
	// Make sure we don't misuse this call. Let checks whether ensuring capacity is really needed be inlined.
	assert( newCapacityInElems > m_capacityInElems );

	size_t capacityToActuallyUse;
	if( growthPolicy == Optimal ) {
		size_t nextCapacity;
		if( m_capacityInElems * elemSize < 2048 ) {
			if( m_capacityInElems ) [[likely]] {
				nextCapacity = 2 * m_capacityInElems;
			} else {
				nextCapacity = 2 * ( elemSize < 16 ? ( 16 / (uint8_t)elemSize ) : 1 );
			}
		} else {
			nextCapacity = m_capacityInElems + m_capacityInElems / 2;
		}
		if( nextCapacity < newCapacityInElems ) {
			capacityToActuallyUse = newCapacityInElems;
		} else {
			capacityToActuallyUse = nextCapacity;
		}
	} else {
		capacityToActuallyUse = newCapacityInElems;
	}

	void *newDataBytes = std::realloc( m_dataBytes, elemSize * capacityToActuallyUse );
	if( !newDataBytes ) [[unlikely]] {
		wsw::failWithBadAlloc();
	}

	m_capacityInElems = capacityToActuallyUse;
	m_dataBytes       = (uint8_t *)newDataBytes;
}

void BasePodVector::destroy() noexcept {
	std::free( m_dataBytes );
}

void BasePodVector::constructFromThat( const BasePodVector &that, size_t elemSize ) {
	if( that.m_sizeInElems ) [[likely]] {
		ensureCapacityInElems( that.m_capacityInElems, elemSize, Exact );
		std::memcpy( m_dataBytes, that.m_dataBytes, elemSize * that.m_sizeInElems );
		m_sizeInElems = that.m_sizeInElems;
	}
}

void BasePodVector::copyFromThat( const BasePodVector &that, size_t elemSize ) {
	if( this != &that ) [[likely]] {
		if( m_capacityInElems < that.m_capacityInElems ) {
			ensureCapacityInElems( that.m_capacityInElems, elemSize, Exact );
		}
		std::memcpy( m_dataBytes, that.m_dataBytes, elemSize * that.m_sizeInElems );
		m_sizeInElems = that.m_sizeInElems;
	}
}

void BasePodVector::reserveImpl( size_t minDesiredCapacity, size_t elemSize ) {
	if( m_capacityInElems < minDesiredCapacity ) {
		ensureCapacityInElems( minDesiredCapacity, elemSize, Exact );
		assert( m_capacityInElems >= minDesiredCapacity );
	}
}

void BasePodVector::expandImpl( size_t position, size_t elemSize, size_t numElems ) {
	assert( position <= m_sizeInElems );
	if( numElems > 0 ) [[likely]] {
		const size_t requiredCapacityInElems = numElems + m_sizeInElems;
		if( m_capacityInElems < requiredCapacityInElems ) {
			ensureCapacityInElems( requiredCapacityInElems, elemSize, Optimal );
		}
		if( position != m_sizeInElems ) {
			const size_t tailLengthInElems = m_sizeInElems - position;
			const void *from = m_dataBytes + elemSize * position;
			void *const to   = m_dataBytes + elemSize * ( position + numElems );
			std::memmove( to, from, elemSize * tailLengthInElems );
		}
	}
}

void BasePodVector::eraseImpl( size_t position, size_t elemSize, size_t numElems ) {
	assert( position + numElems <= m_sizeInElems );
	if( numElems ) [[likely]] {
		if( position + numElems < m_sizeInElems ) {
			const size_t tailLengthInElems = m_sizeInElems - ( position + numElems );
			const void *from = m_dataBytes + elemSize * ( position + numElems );
			void *const to   = m_dataBytes + elemSize * position;
			std::memmove( to, from, elemSize * tailLengthInElems );
		}
		m_sizeInElems -= numElems;
	}
}

void BasePodVector::shrinkImpl( size_t elemSize ) {
	if( m_sizeInElems < m_capacityInElems ) [[likely]] {
		if( m_sizeInElems ) [[likely]] {
			void *newDataBytes = realloc( m_dataBytes, m_sizeInElems * elemSize );
			// TODO: What to do, just keep it as-is?
			if( !newDataBytes ) [[unlikely]] {
				wsw::failWithBadAlloc();
			}
			m_dataBytes       = (uint8_t *)newDataBytes;
			m_capacityInElems = m_sizeInElems;
		} else {
			// TODO: Should we do that?
			std::free( m_dataBytes );
			m_dataBytes       = nullptr;
			m_sizeInElems     = 0;
			m_capacityInElems = 0;
		}
	}
}

}

void memset16( void *__restrict dest, uint16_t value, size_t count ) {
	assert( !( (uintptr_t)dest % 2 ) );
	size_t i = 0;
	do {
		( (uint16_t *)dest )[i] = value;
	} while( ++i < count );
}

void memset32( void *__restrict dest, uint32_t value, size_t count ) {
	assert( !( (uintptr_t)dest % 4 ) );
	size_t i = 0;
	do {
		( (uint32_t *)dest )[i] = value;
	} while( ++i < count );
}

void memset64( void *__restrict dest, uint64_t value, size_t count ) {
	assert( !( (uintptr_t)dest % 8 ) );
	size_t i = 0;
	do {
		( (uint64_t *)dest )[i] = value;
	} while( ++i < count );
}