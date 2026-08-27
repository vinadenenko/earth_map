/** @file geographic_quadtree.cpp */

#include <earth_map/renderer/geographic_quadtree.h>

#include <earth_map/constants.h>

#include <cmath>
#include <limits>

namespace earth_map::renderer {
namespace {

constexpr std::uint32_t kMaximumPatchGridSubdivisions = 256;

[[nodiscard]] bool Intersects(const GeographicPatchBounds& first,
                              const GeographicPatchBounds& second) noexcept {
    return first.west_longitude_radians < second.east_longitude_radians &&
           first.east_longitude_radians > second.west_longitude_radians &&
           first.south_latitude_radians < second.north_latitude_radians &&
           first.north_latitude_radians > second.south_latitude_radians;
}

[[nodiscard]] bool IntersectsAny(
    const GeographicPatchBounds& patch_bounds,
    const std::vector<GeographicPatchBounds>& visible_regions) noexcept {
    for (const GeographicPatchBounds& region : visible_regions) {
        if (Intersects(patch_bounds, region)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] double InverseWebMercatorLatitude(double north_to_south) noexcept {
    return std::atan(std::sinh(
        constants::math::PI * (1.0 - 2.0 * north_to_south)));
}

[[nodiscard]] std::optional<std::uint64_t> MatrixDimension(
    const imagery::TileMatrixSet& matrix_set,
    const imagery::ImageTileAddress& address) noexcept {
    if (!matrix_set.IsLevelSupported(address.level)) {
        return std::nullopt;
    }

    const std::uint64_t dimension = matrix_set.MatrixDimension(address.level);
    if (dimension == 0 || address.column >= dimension || address.row >= dimension) {
        return std::nullopt;
    }
    return dimension;
}

}  // namespace

bool GeographicPatchBounds::IsValid() const noexcept {
    return std::isfinite(west_longitude_radians) &&
           std::isfinite(south_latitude_radians) &&
           std::isfinite(east_longitude_radians) &&
           std::isfinite(north_latitude_radians) &&
           west_longitude_radians < east_longitude_radians &&
           south_latitude_radians < north_latitude_radians;
}

std::optional<GeographicQuadtreePatch> MakeGeographicQuadtreePatch(
    const imagery::TileMatrixSet& matrix_set,
    const imagery::ImageTileKey& imagery_key) noexcept {
    if (!imagery_key.IsValid() || imagery_key.matrix_set_id != matrix_set.id ||
        matrix_set.projection != imagery::ImageryProjection::WebMercator) {
        return std::nullopt;
    }

    const auto dimension = MatrixDimension(matrix_set, imagery_key.address);
    if (!dimension.has_value()) {
        return std::nullopt;
    }

    const double tile_count = static_cast<double>(*dimension);
    const double west =
        (static_cast<double>(imagery_key.address.column) / tile_count) *
            (2.0 * constants::math::PI) -
        constants::math::PI;
    const double east =
        (static_cast<double>(imagery_key.address.column + 1U) / tile_count) *
            (2.0 * constants::math::PI) -
        constants::math::PI;

    const std::uint64_t north_to_south_row =
        matrix_set.row_order == imagery::TileRowOrder::NorthToSouth
            ? imagery_key.address.row
            : *dimension - 1U - imagery_key.address.row;
    const double north_to_south_top =
        static_cast<double>(north_to_south_row) / tile_count;
    const double north_to_south_bottom =
        static_cast<double>(north_to_south_row + 1U) / tile_count;

    GeographicPatchBounds bounds{
        west,
        InverseWebMercatorLatitude(north_to_south_bottom),
        east,
        InverseWebMercatorLatitude(north_to_south_top),
    };
    if (!bounds.IsValid()) {
        return std::nullopt;
    }

    return GeographicQuadtreePatch{imagery_key, bounds, matrix_set.row_order};
}

std::optional<geodesy::GeodeticPosition> PatchLocalToGeodetic(
    const GeographicQuadtreePatch& patch,
    const glm::dvec2& local_uv,
    double ellipsoid_height_meters) noexcept {
    if (!patch.imagery_key.IsValid() || !patch.bounds.IsValid() ||
        !std::isfinite(local_uv.x) || !std::isfinite(local_uv.y) ||
        !std::isfinite(ellipsoid_height_meters) || local_uv.x < 0.0 ||
        local_uv.x > 1.0 || local_uv.y < 0.0 || local_uv.y > 1.0) {
        return std::nullopt;
    }

    const std::uint32_t level = patch.imagery_key.address.level;
    if (level > imagery::TileMatrixSet::kMaximumSupportedLevel) {
        return std::nullopt;
    }

    const double tile_count = static_cast<double>(std::uint64_t{1} << level);
    if (patch.imagery_key.address.column >= tile_count ||
        patch.imagery_key.address.row >= tile_count) {
        return std::nullopt;
    }

    const std::uint64_t north_to_south_row =
        patch.row_order == imagery::TileRowOrder::NorthToSouth
            ? patch.imagery_key.address.row
            : static_cast<std::uint64_t>(tile_count) - 1U -
                  patch.imagery_key.address.row;
    const double north_to_south =
        (static_cast<double>(north_to_south_row) + local_uv.y) / tile_count;
    const double longitude =
        ((static_cast<double>(patch.imagery_key.address.column) + local_uv.x) /
             tile_count) *
            (2.0 * constants::math::PI) -
        constants::math::PI;

    geodesy::GeodeticPosition geodetic{
        InverseWebMercatorLatitude(north_to_south),
        longitude,
        ellipsoid_height_meters,
    };
    return geodetic.IsValid() ? std::optional<geodesy::GeodeticPosition>(geodetic)
                              : std::nullopt;
}

std::optional<geodesy::EcefPosition> PatchLocalToEcef(
    const GeographicQuadtreePatch& patch,
    const glm::dvec2& local_uv,
    double ellipsoid_height_meters) noexcept {
    const auto geodetic = PatchLocalToGeodetic(
        patch, local_uv, ellipsoid_height_meters);
    if (!geodetic.has_value()) {
        return std::nullopt;
    }
    return geodesy::Wgs84Ellipsoid::ToEcef(*geodetic);
}

std::optional<ResolvedImageryPatch> ResolveResidentImageryPatch(
    const GeographicQuadtreePatch& patch,
    std::uint32_t max_ancestor_levels,
    const std::function<std::optional<std::uint16_t>(
        const imagery::ImageTileKey&)>& resident_layer) {
    if (!patch.imagery_key.IsValid() || !patch.bounds.IsValid() || !resident_layer) {
        return std::nullopt;
    }

    imagery::ImageTileKey candidate = patch.imagery_key;
    for (std::uint32_t ancestor_levels = 0;; ++ancestor_levels) {
        if (const auto layer = resident_layer(candidate); layer.has_value()) {
            const std::uint64_t child_dimension = std::uint64_t{1} << ancestor_levels;
            const std::uint32_t ancestor_column = candidate.address.column;
            const std::uint32_t ancestor_row = candidate.address.row;
            const std::uint32_t child_column =
                patch.imagery_key.address.column -
                static_cast<std::uint32_t>(ancestor_column * child_dimension);
            const std::uint32_t child_row =
                patch.imagery_key.address.row -
                static_cast<std::uint32_t>(ancestor_row * child_dimension);
            const double inverse_dimension = 1.0 / static_cast<double>(child_dimension);

            return ResolvedImageryPatch{
                patch,
                candidate,
                *layer,
                ancestor_levels,
                {inverse_dimension, inverse_dimension},
                {static_cast<double>(child_column) * inverse_dimension,
                 static_cast<double>(child_row) * inverse_dimension},
            };
        }

        if (ancestor_levels == max_ancestor_levels || candidate.address.level == 0) {
            break;
        }

        const auto parent_address = candidate.address.Parent();
        if (!parent_address.has_value()) {
            break;
        }
        candidate.address = *parent_address;
    }

    return std::nullopt;
}

std::optional<GeographicPatchGrid> MakeGeographicPatchGrid(
    std::uint32_t subdivisions) noexcept {
    if (subdivisions == 0 || subdivisions > kMaximumPatchGridSubdivisions) {
        return std::nullopt;
    }

    const std::uint64_t vertex_side = static_cast<std::uint64_t>(subdivisions) + 1U;
    const std::uint64_t vertex_count = vertex_side * vertex_side;
    const std::uint64_t index_count =
        static_cast<std::uint64_t>(subdivisions) * subdivisions * 6U;
    if (vertex_count > std::numeric_limits<std::size_t>::max() ||
        index_count > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }

    GeographicPatchGrid grid;
    grid.subdivisions = subdivisions;
    grid.local_coordinates.reserve(static_cast<std::size_t>(vertex_count));
    grid.indices.reserve(static_cast<std::size_t>(index_count));

    const float inverse_subdivisions = 1.0F / static_cast<float>(subdivisions);
    for (std::uint32_t row = 0; row <= subdivisions; ++row) {
        for (std::uint32_t column = 0; column <= subdivisions; ++column) {
            grid.local_coordinates.emplace_back(
                static_cast<float>(column) * inverse_subdivisions,
                static_cast<float>(row) * inverse_subdivisions);
        }
    }

    const std::uint32_t stride = subdivisions + 1U;
    for (std::uint32_t row = 0; row < subdivisions; ++row) {
        for (std::uint32_t column = 0; column < subdivisions; ++column) {
            const std::uint32_t top_left = row * stride + column;
            const std::uint32_t top_right = top_left + 1U;
            const std::uint32_t bottom_left = top_left + stride;
            const std::uint32_t bottom_right = bottom_left + 1U;

            grid.indices.insert(grid.indices.end(), {
                top_left, bottom_left, top_right,
                top_right, bottom_left, bottom_right,
            });
        }
    }

    return grid;
}

std::vector<imagery::ImageTileKey> SelectVisibleGeographicQuadtreeLeaves(
    const imagery::TileMatrixSet& matrix_set,
    std::string imagery_source_id,
    const GeographicQuadtreeSelectionConfig& config) {
    if (!matrix_set.IsValid() || imagery_source_id.empty() ||
        !matrix_set.IsLevelSupported(config.target_level) ||
        config.maximum_leaf_count == 0 || config.visible_regions.empty()) {
        return {};
    }
    for (const GeographicPatchBounds& region : config.visible_regions) {
        if (!region.IsValid()) {
            return {};
        }
    }

    const std::uint64_t root_dimension =
        matrix_set.MatrixDimension(matrix_set.minimum_level);
    const std::uint64_t maximum_leaf_count =
        static_cast<std::uint64_t>(config.maximum_leaf_count);
    if (root_dimension == 0 || root_dimension > maximum_leaf_count / root_dimension) {
        // A source whose first available level alone cannot fit in the budget
        // has no valid coarser source leaf. Do not invent one; the caller can
        // retain its documented coarse-globe fallback instead.
        return {};
    }

    std::vector<imagery::ImageTileKey> frontier;
    frontier.reserve(static_cast<std::size_t>(root_dimension * root_dimension));
    for (std::uint32_t row = 0; row < root_dimension; ++row) {
        for (std::uint32_t column = 0; column < root_dimension; ++column) {
            imagery::ImageTileKey key{
                imagery_source_id,
                matrix_set.id,
                {matrix_set.minimum_level, column, row},
            };
            const auto patch = MakeGeographicQuadtreePatch(matrix_set, key);
            if (patch.has_value() &&
                IntersectsAny(patch->bounds, config.visible_regions)) {
                frontier.push_back(std::move(key));
            }
        }
    }

    while (!frontier.empty() &&
           frontier.front().address.level < config.target_level) {
        std::vector<imagery::ImageTileKey> children;
        children.reserve(frontier.size() * 4U);

        for (const imagery::ImageTileKey& parent : frontier) {
            const std::uint32_t child_level = parent.address.level + 1U;
            const std::uint32_t first_column = parent.address.column * 2U;
            const std::uint32_t first_row = parent.address.row * 2U;
            for (std::uint32_t row_offset = 0; row_offset < 2U; ++row_offset) {
                for (std::uint32_t column_offset = 0; column_offset < 2U;
                     ++column_offset) {
                    imagery::ImageTileKey child{
                        imagery_source_id,
                        matrix_set.id,
                        {child_level,
                         first_column + column_offset,
                         first_row + row_offset},
                    };
                    const auto patch = MakeGeographicQuadtreePatch(matrix_set, child);
                    if (patch.has_value() &&
                        IntersectsAny(patch->bounds, config.visible_regions)) {
                        children.push_back(std::move(child));
                    }
                }
            }
        }

        if (children.empty() || children.size() > config.maximum_leaf_count) {
            break;
        }
        frontier = std::move(children);
    }

    return frontier;
}

}  // namespace earth_map::renderer
