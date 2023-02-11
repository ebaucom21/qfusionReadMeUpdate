#include "wswsortbyfield.h"
#include "wswbasicarch.h"

#include <algorithm>

namespace wsw::_details {

// This template gets used to actually generate specialized implementations

template <typename Field, size_t Size, size_t Alignment, bool Ascending>
wsw_forceinline void sortStructsByFieldImpl( void *begin, void *end, uintptr_t fieldOffset ) {
	struct alignas( Alignment ) Struct { char _contents[Size]; };
	static_assert( alignof( Struct ) == Alignment && sizeof( Struct ) == Size );

	assert( ( (uintptr_t)begin % Alignment ) == 0 );
	assert( ( (uintptr_t)end % Alignment ) == 0 );

	std::sort( (Struct *)begin, (Struct *)end, [fieldOffset]( const Struct &lhs, const Struct &rhs ) -> bool {
		const auto rawLeftStructBytes  = (const uint8_t *)std::addressof( lhs );
		const auto rawRightStructBytes = (const uint8_t *)std::addressof( rhs );
		const auto leftField           = *( (Field *)( rawLeftStructBytes + fieldOffset ) );
		const auto rightField          = *( (Field *)( rawRightStructBytes + fieldOffset ) );
		if constexpr( Ascending ) {
			return leftField < rightField;
		} else {
			return rightField > leftField;
		}
	});
}

void sortStructsByFloatField( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<float, 8, 4, true>( begin, end, fieldOffset );
}

void sortStructsByInt32Field( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<int32_t, 8, 4, true>( begin, end, fieldOffset );
}

void sortStructsByUInt32Field( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<uint32_t, 8, 4, true>( begin, end, fieldOffset );
}

void sortStructsByFloatFieldDescending( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<float, 8, 4, false>( begin, end, fieldOffset );
}

void sortStructsByInt32FieldDescending( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<int32_t, 8, 4, false>( begin, end, fieldOffset );
}

void sortStructsByUInt32FieldDescending( void *begin, void *end, size_t size, size_t alignment, uintptr_t fieldOffset ) {
	assert( size == 8 && alignment == 4 );
	sortStructsByFieldImpl<uint32_t, 8, 4, false>( begin, end, fieldOffset );
}

}