/**
 * @file placemark_icon_registry.h
 * @brief Public, renderer-independent registry mapping an icon key to pixels.
 *
 * `IconStyle::icon_key` (placemark_layer.h) is resolved through this
 * registry. It is deliberately GL-free: it stores decoded RGBA8 pixels only,
 * so it is fully unit-testable without a graphics context. A private
 * renderer consumes an immutable `PlacemarkIconSnapshot` to build its own GPU
 * atlas texture.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace earth_map::placemarks {

/** One registered icon's atlas placement and source dimensions. */
struct IconAtlasEntry final {
    std::uint32_t atlas_slot = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/** One registered icon, including its decoded pixels (RGBA8, row-major). */
struct RegisteredIcon final {
    std::string icon_key;
    IconAtlasEntry placement;
    std::shared_ptr<const std::vector<std::uint8_t>> rgba_pixels;
};

using IconAtlasMap = std::unordered_map<std::string, RegisteredIcon>;

/**
 * Immutable icon-set view with lifetime independent of the owning registry.
 *
 * A private renderer compares `Revision()` against the atlas texture it last
 * built to decide whether a rebuild is needed.
 */
class PlacemarkIconSnapshot final {
public:
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    [[nodiscard]] const IconAtlasMap& Icons() const noexcept { return *icons_; }

    [[nodiscard]] std::optional<IconAtlasEntry> Find(const std::string& icon_key) const {
        const auto it = icons_->find(icon_key);
        return it != icons_->end() ? std::optional<IconAtlasEntry>(it->second.placement)
                                    : std::nullopt;
    }

private:
    friend class PlacemarkIconRegistry;
    PlacemarkIconSnapshot(std::uint64_t revision,
                          std::shared_ptr<const IconAtlasMap> icons) noexcept
        : revision_(revision), icons_(std::move(icons)) {}

    std::uint64_t revision_ = 0;
    std::shared_ptr<const IconAtlasMap> icons_;
};

/**
 * Public icon-registration API.
 *
 * Register and Snapshot are safe to call concurrently. Atlas layout here is
 * deliberately a fixed-size grid rather than general bin-packing: every icon
 * must be exactly `kIconCellSize` x `kIconCellSize` pixels, and the registry
 * holds at most `kMaxIconCount` distinct keys. That is enough for a small,
 * curated icon set; a real bin-packer is not needed for that and would be
 * unused complexity.
 */
class PlacemarkIconRegistry {
public:
    static constexpr std::uint32_t kIconCellSize = 64;
    static constexpr std::uint32_t kMaxIconCount = 64;

    static std::shared_ptr<PlacemarkIconRegistry> Create();
    virtual ~PlacemarkIconRegistry() = default;

    /**
     * Registers (or replaces) the icon at `icon_key` with the given RGBA8
     * pixels (row-major, `width * height * 4` bytes).
     *
     * @return false if `icon_key` is empty, dimensions are not exactly
     *   `kIconCellSize` x `kIconCellSize`, the pixel buffer size does not
     *   match, or the registry is at `kMaxIconCount` and `icon_key` is new.
     */
    virtual bool Register(const std::string& icon_key, std::uint32_t width,
                          std::uint32_t height,
                          std::vector<std::uint8_t> rgba_pixels) = 0;

    /**
     * Convenience overload: decodes `file_path` (PNG/JPG/etc via stb_image)
     * and registers it. The decoded image must still satisfy the same
     * `kIconCellSize` x `kIconCellSize` constraint as Register().
     */
    virtual bool RegisterFromFile(const std::string& icon_key,
                                  const std::string& file_path) = 0;

    [[nodiscard]] virtual PlacemarkIconSnapshot Snapshot() const = 0;

protected:
    [[nodiscard]] static PlacemarkIconSnapshot MakeSnapshot(
        std::uint64_t revision, std::shared_ptr<const IconAtlasMap> icons) noexcept {
        return {revision, std::move(icons)};
    }
};

}  // namespace earth_map::placemarks
