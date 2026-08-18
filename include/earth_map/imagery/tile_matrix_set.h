/**
 * @file tile_matrix_set.h
 * @brief Canonical imagery tile identities and page-table addressing.
 *
 * Imagery is projected data draped over the WGS84 globe.  This file is the
 * contract between future geographic-patch selection, streaming, residency,
 * GPU page-table updates, and shader sampling.  It intentionally contains no
 * normalized globe-space coordinates and no OpenGL types.
 */

#pragma once

#include <earth_map/geodesy/wgs84_ellipsoid.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace earth_map::imagery {

/** Increment when a future incompatible virtual-address layout is introduced. */
inline constexpr std::uint32_t kVirtualImageryAddressContractVersion = 1;

/** Projection used by the imagery source tile matrix. */
enum class ImageryProjection : std::uint8_t {
    WebMercator,
};

/** Row direction in a source tile matrix. */
enum class TileRowOrder : std::uint8_t {
    NorthToSouth,  ///< XYZ / slippy-map convention: row zero is the north edge.
    SouthToNorth,  ///< TMS convention: row zero is the south edge.
};

/** Source-matrix-local integer address. */
struct ImageTileAddress final {
    std::uint32_t level = 0;
    std::uint32_t column = 0;
    std::uint32_t row = 0;

    [[nodiscard]] std::optional<ImageTileAddress> Parent() const noexcept;

    constexpr bool operator==(const ImageTileAddress& other) const noexcept = default;
};

/**
 * Globally unambiguous imagery identity.
 *
 * The source and matrix-set identifiers are mandatory.  `level/column/row`
 * alone are not cache or residency identities because different providers can
 * serve different content for the same XYZ address.
 */
struct ImageTileKey final {
    std::string imagery_source_id;
    std::string matrix_set_id;
    ImageTileAddress address;

    [[nodiscard]] bool IsValid() const noexcept;

    constexpr bool operator==(const ImageTileKey& other) const noexcept = default;
};

struct ImageTileKeyHash final {
    [[nodiscard]] std::size_t operator()(const ImageTileKey& key) const noexcept;
};

/**
 * Declares the integer layout and geographic domain of one imagery source.
 *
 * Web Mercator is only an imagery matrix projection; WGS84 ECEF/geodetic
 * remains the physical globe model.
 */
class TileMatrixSet final {
public:
    static constexpr double kWebMercatorMaxLatitudeRadians = 1.4844222297453324;
    static constexpr std::uint32_t kMaximumSupportedLevel = 30;

    std::string id;
    ImageryProjection projection = ImageryProjection::WebMercator;
    TileRowOrder row_order = TileRowOrder::NorthToSouth;
    std::uint32_t tile_size_pixels = 256;
    std::uint32_t minimum_level = 0;
    std::uint32_t maximum_level = 22;
    bool wraps_horizontally = true;
    double minimum_latitude_radians = -kWebMercatorMaxLatitudeRadians;
    double maximum_latitude_radians = kWebMercatorMaxLatitudeRadians;

    [[nodiscard]] static TileMatrixSet WebMercatorXYZ(
        std::string id = "WebMercatorQuad");

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsLevelSupported(std::uint32_t level) const noexcept;
    [[nodiscard]] std::uint64_t MatrixDimension(std::uint32_t level) const noexcept;

    /**
     * Canonicalizes a source address.  Horizontal wrapping is applied only
     * when this matrix set declares it; rows outside the matrix are rejected.
     */
    [[nodiscard]] std::optional<ImageTileAddress> NormalizeAddress(
        std::uint32_t level,
        std::int64_t column,
        std::int64_t row) const noexcept;

    /** Converts WGS84 radians to a source-matrix-local integer tile address. */
    [[nodiscard]] std::optional<ImageTileAddress> GeodeticToTile(
        const geodesy::GeodeticPosition& geodetic,
        std::uint32_t level) const noexcept;
};

/** Integer texel relative to one page-table window. */
struct PageTableTexel final {
    std::int32_t x = 0;
    std::int32_t y = 0;

    constexpr bool operator==(const PageTableTexel& other) const noexcept = default;
};

/**
 * Immutable description of one page-table generation.
 *
 * The render snapshot owns this value.  A shader may sample a page table only
 * using an address resolved against the same source, matrix set, level, and
 * generation.  The window origin is integer tile space, never a float world
 * coordinate.
 */
struct PageTableWindow final {
    std::uint32_t contract_version = kVirtualImageryAddressContractVersion;
    std::uint64_t generation = 0;
    std::string imagery_source_id;
    std::string matrix_set_id;
    std::uint32_t level = 0;
    std::int64_t origin_column = 0;
    std::int64_t origin_row = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool Matches(const ImageTileKey& key) const noexcept;
    [[nodiscard]] std::optional<PageTableTexel> TryGetTexel(
        const ImageTileKey& key) const noexcept;
};

}  // namespace earth_map::imagery
