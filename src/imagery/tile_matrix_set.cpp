#include <earth_map/imagery/tile_matrix_set.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <utility>

namespace earth_map::imagery {
namespace {

[[nodiscard]] std::size_t HashCombine(std::size_t seed, std::size_t value) noexcept {
    return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] std::int64_t PositiveModulo(
    std::int64_t value,
    std::int64_t divisor) noexcept {
    const std::int64_t remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

}  // namespace

std::optional<ImageTileAddress> ImageTileAddress::Parent() const noexcept {
    if (level == 0) {
        return std::nullopt;
    }

    return ImageTileAddress{level - 1, column >> 1U, row >> 1U};
}

bool ImageTileKey::IsValid() const noexcept {
    return !imagery_source_id.empty() && !matrix_set_id.empty();
}

std::size_t ImageTileKeyHash::operator()(const ImageTileKey& key) const noexcept {
    std::size_t seed = std::hash<std::string>{}(key.imagery_source_id);
    seed = HashCombine(seed, std::hash<std::string>{}(key.matrix_set_id));
    seed = HashCombine(seed, std::hash<std::uint32_t>{}(key.address.level));
    seed = HashCombine(seed, std::hash<std::uint32_t>{}(key.address.column));
    return HashCombine(seed, std::hash<std::uint32_t>{}(key.address.row));
}

TileMatrixSet TileMatrixSet::WebMercatorXYZ(std::string matrix_set_id) {
    TileMatrixSet matrix_set;
    matrix_set.id = std::move(matrix_set_id);
    return matrix_set;
}

bool TileMatrixSet::IsValid() const noexcept {
    return !id.empty() && tile_size_pixels > 0 && minimum_level <= maximum_level &&
           maximum_level <= kMaximumSupportedLevel &&
           std::isfinite(minimum_latitude_radians) &&
           std::isfinite(maximum_latitude_radians) &&
           minimum_latitude_radians < maximum_latitude_radians &&
           minimum_latitude_radians >= -std::numbers::pi_v<double> * 0.5 &&
           maximum_latitude_radians <= std::numbers::pi_v<double> * 0.5;
}

bool TileMatrixSet::IsLevelSupported(std::uint32_t level) const noexcept {
    return IsValid() && level >= minimum_level && level <= maximum_level;
}

std::uint64_t TileMatrixSet::MatrixDimension(std::uint32_t level) const noexcept {
    if (!IsLevelSupported(level)) {
        return 0;
    }

    return std::uint64_t{1} << level;
}

std::optional<ImageTileAddress> TileMatrixSet::NormalizeAddress(
    std::uint32_t level,
    std::int64_t column,
    std::int64_t row) const noexcept {
    const std::uint64_t dimension = MatrixDimension(level);
    if (dimension == 0 || dimension > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    const std::int64_t signed_dimension = static_cast<std::int64_t>(dimension);
    if (wraps_horizontally) {
        column = PositiveModulo(column, signed_dimension);
    } else if (column < 0 || column >= signed_dimension) {
        return std::nullopt;
    }

    if (row < 0 || row >= signed_dimension) {
        return std::nullopt;
    }

    return ImageTileAddress{
        level,
        static_cast<std::uint32_t>(column),
        static_cast<std::uint32_t>(row),
    };
}

std::optional<ImageTileAddress> TileMatrixSet::GeodeticToTile(
    const geodesy::GeodeticPosition& geodetic,
    std::uint32_t level) const noexcept {
    if (!geodetic.IsValid() || !IsLevelSupported(level) ||
        geodetic.latitude_radians < minimum_latitude_radians ||
        geodetic.latitude_radians > maximum_latitude_radians) {
        return std::nullopt;
    }

    // Only Web Mercator is declared in this first contract version.  Keep the
    // projection branch explicit so a future matrix type cannot silently use
    // Web Mercator math.
    if (projection != ImageryProjection::WebMercator) {
        return std::nullopt;
    }

    const double dimension = static_cast<double>(MatrixDimension(level));
    const double normalized_x =
        (geodetic.longitude_radians + std::numbers::pi_v<double>) /
        (2.0 * std::numbers::pi_v<double>);
    const double north_to_south_y = 0.5 -
        std::asinh(std::tan(geodetic.latitude_radians)) /
            (2.0 * std::numbers::pi_v<double>);
    const double source_y = row_order == TileRowOrder::NorthToSouth
        ? north_to_south_y
        : 1.0 - north_to_south_y;

    const std::int64_t column = static_cast<std::int64_t>(std::floor(normalized_x * dimension));
    const double clamped_source_y = std::clamp(source_y, 0.0, std::nextafter(1.0, 0.0));
    const std::int64_t row = static_cast<std::int64_t>(std::floor(clamped_source_y * dimension));
    return NormalizeAddress(level, column, row);
}

bool PageTableWindow::IsValid() const noexcept {
    return contract_version == kVirtualImageryAddressContractVersion && generation > 0 &&
           !imagery_source_id.empty() && !matrix_set_id.empty() && width > 0 && height > 0;
}

bool PageTableWindow::Matches(const ImageTileKey& key) const noexcept {
    return IsValid() && key.IsValid() && key.imagery_source_id == imagery_source_id &&
           key.matrix_set_id == matrix_set_id && key.address.level == level;
}

std::optional<PageTableTexel> PageTableWindow::TryGetTexel(
    const ImageTileKey& key) const noexcept {
    if (!Matches(key)) {
        return std::nullopt;
    }

    const std::int64_t local_x = static_cast<std::int64_t>(key.address.column) - origin_column;
    const std::int64_t local_y = static_cast<std::int64_t>(key.address.row) - origin_row;
    if (local_x < 0 || local_y < 0 ||
        local_x >= static_cast<std::int64_t>(width) ||
        local_y >= static_cast<std::int64_t>(height)) {
        return std::nullopt;
    }

    return PageTableTexel{
        static_cast<std::int32_t>(local_x),
        static_cast<std::int32_t>(local_y),
    };
}

}  // namespace earth_map::imagery
