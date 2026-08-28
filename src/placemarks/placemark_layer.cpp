#include <earth_map/placemarks/placemark_layer.h>

#include <mutex>
#include <shared_mutex>
#include <utility>

namespace earth_map::placemarks {
namespace {

const FeatureMetadata& Metadata(const PlacemarkFeature& feature) noexcept {
    return std::visit([](const auto& value) -> const FeatureMetadata& {
        return value.metadata;
    }, feature);
}

bool IsValidPosition(const geodesy::GeodeticPosition& position) noexcept {
    return position.IsValid();
}

class PlacemarkLayerImpl final : public PlacemarkLayer {
public:
    PlacemarkLayerImpl()
        : features_(std::make_shared<const PlacemarkFeatureMap>()) {}

    PlacemarkApplyResult Apply(const PlacemarkChangeSet& changes) override {
        PlacemarkApplyResult result;
        for (const PlacemarkFeature& feature : changes.upserts) {
            if (!IsValid(feature)) {
                result.rejected_upsert_ids.push_back(GetPlacemarkId(feature));
            }
        }

        std::unique_lock lock(mutex_);
        if (!result.rejected_upsert_ids.empty()) {
            result.revision = revision_;
            return result;
        }

        auto next = std::make_shared<PlacemarkFeatureMap>(*features_);
        for (PlacemarkId id : changes.erase_ids) {
            next->erase(id);
        }
        for (const PlacemarkFeature& feature : changes.upserts) {
            next->insert_or_assign(GetPlacemarkId(feature), feature);
        }

        if (!changes.erase_ids.empty() || !changes.upserts.empty()) {
            features_ = std::move(next);
            ++revision_;
            result.applied = true;
        }
        result.revision = revision_;
        return result;
    }

    [[nodiscard]] PlacemarkSnapshot Snapshot() const override {
        std::shared_lock lock(mutex_);
        return MakeSnapshot(revision_, features_);
    }

private:
    mutable std::shared_mutex mutex_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<const PlacemarkFeatureMap> features_;
};

}  // namespace

PlacemarkId GetPlacemarkId(const PlacemarkFeature& feature) noexcept {
    return Metadata(feature).id;
}

bool IsValid(const PlacemarkFeature& feature) noexcept {
    return std::visit([](const auto& value) {
        if (value.metadata.id == 0) {
            return false;
        }

        using Feature = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Feature, PointPlacemark>) {
            return IsValidPosition(value.position);
        } else if constexpr (std::is_same_v<Feature, LineStringPlacemark>) {
            return value.positions.size() >= 2 &&
                std::all_of(value.positions.begin(), value.positions.end(), IsValidPosition);
        } else if constexpr (std::is_same_v<Feature, PolygonPlacemark>) {
            if (value.outer_boundary.size() < 3 ||
                !std::all_of(value.outer_boundary.begin(), value.outer_boundary.end(), IsValidPosition)) {
                return false;
            }
            for (const auto& hole : value.holes) {
                if (hole.size() < 3 ||
                    !std::all_of(hole.begin(), hole.end(), IsValidPosition)) {
                    return false;
                }
            }
            return true;
        } else {
            return !value.type_key.empty() && IsValidPosition(value.anchor);
        }
    }, feature);
}

std::shared_ptr<PlacemarkLayer> PlacemarkLayer::Create() {
    return std::make_shared<PlacemarkLayerImpl>();
}

}  // namespace earth_map::placemarks
