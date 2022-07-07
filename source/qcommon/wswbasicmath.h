#ifndef WSW_519ad756_e10f_4b29_9c04_01f9a63308ba_H
#define WSW_519ad756_e10f_4b29_9c04_01f9a63308ba_H

#include "wswbasicarch.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace wsw {

template <typename T>
[[nodiscard]]
constexpr wsw_forceinline auto min( const T &a, const T &b ) -> T {
	return a < b ? a : b;
}

template <typename T>
[[nodiscard]]
constexpr wsw_forceinline auto max( const T &a, const T &b ) -> T {
	return a < b ? b : a;
}

template <typename T>
[[nodiscard]]
constexpr wsw_forceinline auto clamp( const T &v, const T &lo, const T &hi ) -> T {
	return min( max( v, lo ), hi );
}

template <typename T>
[[nodiscard]]
constexpr wsw_forceinline auto square( const T &v ) -> T {
	return v * v;
}

template <typename T>
[[nodiscard]]
constexpr wsw_forceinline auto cube( const T &v ) -> T {
	return v * v * v;
}

}

#endif
