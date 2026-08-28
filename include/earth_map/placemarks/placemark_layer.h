/**
 * @file placemark_layer.h
 * @brief Public, renderer-independent placemark feature and layer API.
 *
 * This header is deliberately free of OpenGL, tiles, and renderer-private
 * types. Applications own their geographic feature data through this API;
 * renderer implementations consume immutable snapshots privately.
 */

#pragma once

#include <earth_map/geodesy/wgs84_ellipsoid.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace earth_map::placemarks {

using PlacemarkId = std::uint64_t;
using UserProperties = std::map<std::string, std::string>;

/** How a feature's supplied height relates to terrain and the WGS84 ellipsoid. */
enum class AltitudeMode : std::uint8_t {
    /// Use the supplied WGS84 ellipsoid height directly.
    Absolute,
    /// Ignore the supplied height and place the feature on terrain.
    ClampToTerrain,
    /// Add the supplied height to resolved terrain height.
    RelativeToTerrain,
};

struct FeatureMetadata final {
    PlacemarkId id = 0;
    bool visible = true;
    /// Stable application-controlled ordering for clustering and decluttering.
    std::int32_t display_priority = 0;
    /// Application-owned serializable data. Renderer code never interprets it.
    UserProperties user_properties;
};

struct IconStyle final {
    /// Application icon key, resolved by a private icon registry/atlas.
    std::string icon_key;
    float scale = 1.0f;
    float rotation_degrees = 0.0f;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct LabelStyle final {
    bool enabled = false;
    float scale = 1.0f;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct LineStyle final {
    float width_pixels = 1.0f;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct FillStyle final {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct PointPlacemark final {
    FeatureMetadata metadata;
    geodesy::GeodeticPosition position;
    AltitudeMode altitude_mode = AltitudeMode::Absolute;
    IconStyle icon;
    std::string label;
    LabelStyle label_style;
};

struct LineStringPlacemark final {
    FeatureMetadata metadata;
    std::vector<geodesy::GeodeticPosition> positions;
    AltitudeMode altitude_mode = AltitudeMode::Absolute;
    LineStyle style;
};

struct PolygonPlacemark final {
    FeatureMetadata metadata;
    /// Closed implicitly by the renderer; callers provide each vertex once.
    std::vector<geodesy::GeodeticPosition> outer_boundary;
    std::vector<std::vector<geodesy::GeodeticPosition>> holes;
    AltitudeMode altitude_mode = AltitudeMode::Absolute;
    FillStyle fill_style;
    LineStyle outline_style;
};

/**
 * Application-defined feature payload reserved for future custom renderers.
 *
 * The core keeps the feature spatially addressable by `anchor`. A later
 * private renderer/plugin registry can interpret `type_key` and
 * `user_properties` without changing the public layer API.
 */
struct CustomPlacemark final {
    FeatureMetadata metadata;
    geodesy::GeodeticPosition anchor;
    AltitudeMode altitude_mode = AltitudeMode::Absolute;
    std::string type_key;
};

using PlacemarkFeature = std::variant<
    PointPlacemark,
    LineStringPlacemark,
    PolygonPlacemark,
    CustomPlacemark>;

[[nodiscard]] PlacemarkId GetPlacemarkId(const PlacemarkFeature& feature) noexcept;
[[nodiscard]] bool IsValid(const PlacemarkFeature& feature) noexcept;

/** Atomic change set: erases are applied before upserts, so an upsert wins. */
struct PlacemarkChangeSet final {
    std::vector<PlacemarkId> erase_ids;
    std::vector<PlacemarkFeature> upserts;
};

struct PlacemarkApplyResult final {
    bool applied = false;
    std::uint64_t revision = 0;
    std::vector<PlacemarkId> rejected_upsert_ids;
};

using PlacemarkFeatureMap = std::unordered_map<PlacemarkId, PlacemarkFeature>;

class PlacemarkLayer;

/** Immutable feature view with lifetime independent of the owning layer. */
class PlacemarkSnapshot final {
public:
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    [[nodiscard]] bool Empty() const noexcept { return features_->empty(); }
    [[nodiscard]] const PlacemarkFeatureMap& Features() const noexcept { return *features_; }

private:
    friend class PlacemarkLayer;
    PlacemarkSnapshot(std::uint64_t revision,
                      std::shared_ptr<const PlacemarkFeatureMap> features) noexcept
        : revision_(revision), features_(std::move(features)) {}

    std::uint64_t revision_ = 0;
    std::shared_ptr<const PlacemarkFeatureMap> features_;
};

/**
 * Public feature-layer ownership API.
 *
 * Apply and Snapshot are safe to call concurrently. This abstraction owns
 * data only: spatial indexing, terrain anchoring, GPU batches, label layout,
 * and picking remain private implementation concerns.
 */
class PlacemarkLayer {
public:
    static std::shared_ptr<PlacemarkLayer> Create();
    virtual ~PlacemarkLayer() = default;

    virtual PlacemarkApplyResult Apply(const PlacemarkChangeSet& changes) = 0;
    [[nodiscard]] virtual PlacemarkSnapshot Snapshot() const = 0;

protected:
    [[nodiscard]] static PlacemarkSnapshot MakeSnapshot(
        std::uint64_t revision,
        std::shared_ptr<const PlacemarkFeatureMap> features) noexcept {
        return {revision, std::move(features)};
    }
};

}  // namespace earth_map::placemarks
