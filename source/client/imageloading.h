#ifndef WSW_4873d100_2c89_42c8_967f_f8815788b518_H
#define WSW_4873d100_2c89_42c8_967f_f8815788b518_H

#include <cstdlib>
#include <cstdint>
#include <optional>
#include <utility>

struct ImageOptions;

namespace wsw {

[[nodiscard]]
auto rasterizeSvg( const void *rawSvgData, size_t rawSvgDataSize, void *dest, size_t destCapacity, const ImageOptions &options )
	-> std::optional<std::pair<unsigned, unsigned>>;

// TODO: Supply dest buffers instead of returning newly allocated data
[[nodiscard]]
auto decodeImageData( const void *rawImageData, size_t rawImageDataSize, unsigned *width, unsigned *height, unsigned *samples,
					  std::optional<unsigned> requestedSamples = std::nullopt ) -> uint8_t *;

// TODO: Supply dest buffers instead of returning newly allocated data
[[nodiscard]]
auto scaleImageData( const void *srcImageData, unsigned srcWidth, unsigned srcHeight,
					 unsigned desiredWidth, unsigned desiredHeight, int samples ) -> uint8_t *;

}

#endif