#include <earth_map/placemarks/placemark_icon_registry.h>

#include <stb_image.h>
#include <spdlog/spdlog.h>

#include <shared_mutex>
#include <utility>

namespace earth_map::placemarks {
namespace {

class PlacemarkIconRegistryImpl final : public PlacemarkIconRegistry {
public:
    PlacemarkIconRegistryImpl() : icons_(std::make_shared<const IconAtlasMap>()) {}

    bool Register(const std::string& icon_key, std::uint32_t width, std::uint32_t height,
                 std::vector<std::uint8_t> rgba_pixels) override {
        if (icon_key.empty() || width != kIconCellSize || height != kIconCellSize ||
            rgba_pixels.size() != static_cast<std::size_t>(width) * height * 4U) {
            spdlog::error(
                "Rejected placemark icon '{}': must be {}x{} RGBA8 ({} bytes), got {}x{} "
                "({} bytes)",
                icon_key, kIconCellSize, kIconCellSize,
                static_cast<std::size_t>(kIconCellSize) * kIconCellSize * 4U, width, height,
                rgba_pixels.size());
            return false;
        }

        std::unique_lock lock(mutex_);
        const auto existing = icons_->find(icon_key);
        std::uint32_t slot = 0;
        if (existing != icons_->end()) {
            slot = existing->second.placement.atlas_slot;
        } else if (icons_->size() >= kMaxIconCount) {
            spdlog::error("Rejected placemark icon '{}': registry is at capacity ({})",
                          icon_key, kMaxIconCount);
            return false;
        } else {
            slot = static_cast<std::uint32_t>(icons_->size());
        }

        auto next = std::make_shared<IconAtlasMap>(*icons_);
        (*next)[icon_key] = RegisteredIcon{
            icon_key,
            IconAtlasEntry{slot, width, height},
            std::make_shared<const std::vector<std::uint8_t>>(std::move(rgba_pixels)),
        };
        icons_ = std::move(next);
        ++revision_;
        return true;
    }

    bool RegisterFromFile(const std::string& icon_key, const std::string& file_path) override {
        int width = 0;
        int height = 0;
        int source_channels = 0;
        constexpr int kDesiredChannels = 4;
        unsigned char* decoded =
            stbi_load(file_path.c_str(), &width, &height, &source_channels, kDesiredChannels);
        if (!decoded) {
            const char* error = stbi_failure_reason();
            spdlog::error("Failed to decode placemark icon file '{}': {}", file_path,
                          error ? error : "unknown error");
            return false;
        }

        std::vector<std::uint8_t> pixels(
            decoded, decoded + static_cast<std::size_t>(width) * height * kDesiredChannels);
        stbi_image_free(decoded);

        return Register(icon_key, static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height), std::move(pixels));
    }

    [[nodiscard]] PlacemarkIconSnapshot Snapshot() const override {
        std::shared_lock lock(mutex_);
        return MakeSnapshot(revision_, icons_);
    }

private:
    mutable std::shared_mutex mutex_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<const IconAtlasMap> icons_;
};

}  // namespace

std::shared_ptr<PlacemarkIconRegistry> PlacemarkIconRegistry::Create() {
    return std::make_shared<PlacemarkIconRegistryImpl>();
}

}  // namespace earth_map::placemarks
