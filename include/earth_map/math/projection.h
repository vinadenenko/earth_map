#pragma once

/**
 * @file projection.h
 * @brief Map projection system and utilities
 *
 * Defines projection interfaces and implementations for various
 * coordinate systems including Web Mercator and Equirectangular.
 */

#include "../coordinates/coordinate_spaces.h"
#include "bounding_box.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace earth_map {

// Import coordinate types from coordinates namespace
using coordinates::Geographic;
using coordinates::Projected;
using coordinates::ProjectedBounds;

/**
 * @brief Projection types supported by the system
 */
enum class ProjectionType {
    WEB_MERCATOR = 3857,   ///< Web Mercator (EPSG:3857)
    WGS84 = 4326,         ///< WGS84 Geographic (EPSG:4326)
    EQUIRECTANGULAR = 4087 ///< Equirectangular (EPSG:4087)
};

// Note: ProjectedCoordinates removed - use coordinates::Projected instead

/**
 * @brief Base interface for map projections
 */
class Projection {
public:
    virtual ~Projection() = default;
    
    /**
     * @brief Get the projection type
     * 
     * @return ProjectionType Projection type identifier
     */
    virtual ProjectionType GetType() const = 0;
    
    /**
     * @brief Get the projection name
     * 
     * @return std::string Human-readable projection name
     */
    virtual std::string GetName() const = 0;
    
    /**
     * @brief Get the EPSG code
     * 
     * @return int EPSG identifier
     */
    virtual int GetEPSGCode() const = 0;
    
    /**
     * @brief Project geographic coordinates to projected coordinates
     *
     * @param geo Geographic coordinates
     * @return Projected Projected coordinates
     */
    virtual Projected Project(const Geographic& geo) const = 0;

    /**
     * @brief Unproject projected coordinates to geographic coordinates
     *
     * @param proj Projected coordinates
     * @return Geographic Geographic coordinates
     */
    virtual Geographic Unproject(const Projected& proj) const = 0;

    /**
     * @brief Check if geographic coordinates are within projection bounds
     *
     * @param geo Geographic coordinates
     * @return true if within valid projection range, false otherwise
     */
    virtual bool IsValidLocation(const Geographic& geo) const = 0;

    /**
     * @brief Get the projected bounds of the entire Earth
     *
     * @return ProjectedBounds Projected bounds
     */
    virtual ProjectedBounds GetProjectedBounds() const = 0;

    /**
     * @brief Calculate the approximate scale at a given location
     *
     * @param geo Geographic coordinates
     * @return double Meters per projected unit
     */
    virtual double GetScale(const Geographic& geo) const = 0;
};

/**
 * @brief Web Mercator projection (EPSG:3857)
 */
class WebMercatorProjection : public Projection {
public:
    /**
     * @brief Web Mercator projection parameters
     */
    static constexpr double WEB_MERCATOR_HALF_WORLD = 20037508.342789244;
    static constexpr double WEB_MERCATOR_ORIGIN_SHIFT = 2.0 * 20037508.342789244;
    static constexpr double MAX_LATITUDE = 85.05112877980660;  ///< Valid latitude limit
    
    ProjectionType GetType() const override { return ProjectionType::WEB_MERCATOR; }
    std::string GetName() const override { return "Web Mercator"; }
    int GetEPSGCode() const override { return 3857; }
    
    Projected Project(const Geographic& geo) const override;
    Geographic Unproject(const Projected& proj) const override;
    bool IsValidLocation(const Geographic& geo) const override;
    ProjectedBounds GetProjectedBounds() const override;
    double GetScale(const Geographic& geo) const override;

    /**
     * @brief Convert geographic coordinates to normalized Web Mercator coordinates [0, 1]
     *
     * @param geo Geographic coordinates
     * @return glm::dvec2 Normalized coordinates
     */
    glm::dvec2 ToNormalized(const Geographic& geo) const;

    /**
     * @brief Convert normalized Web Mercator coordinates to geographic coordinates
     *
     * @param normalized Normalized coordinates [0, 1]
     * @return Geographic Geographic coordinates
     */
    Geographic FromNormalized(const glm::dvec2& normalized) const;
};

/**
 * @brief WGS84 Geographic projection (EPSG:4326) - identity projection
 */
class WGS84Projection : public Projection {
public:
    ProjectionType GetType() const override { return ProjectionType::WGS84; }
    std::string GetName() const override { return "WGS84 Geographic"; }
    int GetEPSGCode() const override { return 4326; }

    Projected Project(const Geographic& geo) const override;
    Geographic Unproject(const Projected& proj) const override;
    bool IsValidLocation(const Geographic& geo) const override;
    ProjectedBounds GetProjectedBounds() const override;
    double GetScale(const Geographic& geo) const override;
};

/**
 * @brief Equirectangular projection (EPSG:4087)
 */
class EquirectangularProjection : public Projection {
public:
    ProjectionType GetType() const override { return ProjectionType::EQUIRECTANGULAR; }
    std::string GetName() const override { return "Equirectangular"; }
    int GetEPSGCode() const override { return 4087; }

    Projected Project(const Geographic& geo) const override;
    Geographic Unproject(const Projected& proj) const override;
    bool IsValidLocation(const Geographic& geo) const override;
    ProjectedBounds GetProjectedBounds() const override;
    double GetScale(const Geographic& geo) const override;
};

/**
 * @brief Projection factory and registry
 */
class ProjectionRegistry {
public:
    /**
     * @brief Get a projection instance by type
     * 
     * @param type Projection type
     * @return std::shared_ptr<Projection> Projection instance
     */
    static std::shared_ptr<Projection> GetProjection(ProjectionType type);
    
    /**
     * @brief Get a projection instance by EPSG code
     * 
     * @param epsg_code EPSG code
     * @return std::shared_ptr<Projection> Projection instance
     */
    static std::shared_ptr<Projection> GetProjection(int epsg_code);
    
    /**
     * @brief Get a projection instance by name
     * 
     * @param name Projection name
     * @return std::shared_ptr<Projection> Projection instance
     */
    static std::shared_ptr<Projection> GetProjection(const std::string& name);
    
    /**
     * @brief Register a custom projection
     * 
     * @param projection Projection instance
     */
    static void RegisterProjection(std::shared_ptr<Projection> projection);
    
    /**
     * @brief Get list of available projection types
     * 
     * @return std::vector<ProjectionType> Available projections
     */
    static std::vector<ProjectionType> GetAvailableProjections();
    
private:
    static std::unordered_map<ProjectionType, std::shared_ptr<Projection>> projections_;
    static bool initialized_;
    
    static void Initialize();
};

/**
 * @brief Projection transformation utilities
 */
class ProjectionTransformer {
public:
    /**
     * @brief Transform coordinates between projections
     *
     * @param source_coords Source projected coordinates
     * @param source_proj Source projection
     * @param target_proj Target projection
     * @return Projected Transformed coordinates
     */
    static Projected Transform(const Projected& source_coords,
                              const Projection& source_proj,
                              const Projection& target_proj);

    /**
     * @brief Transform geographic coordinates directly between projections
     *
     * @param geo Geographic coordinates
     * @param source_proj Source projection
     * @param target_proj Target projection
     * @return Projected Transformed coordinates
     */
    static Projected TransformGeographic(const Geographic& geo,
                                        const Projection& source_proj,
                                        const Projection& target_proj);

    /**
     * @brief Create a transformation matrix between projections
     *
     * @param source_proj Source projection
     * @param target_proj Target projection
     * @param reference_geo Reference geographic location for scale calculation
     * @return glm::dmat3 2D transformation matrix
     */
    static glm::dmat3 CreateTransformationMatrix(const Projection& source_proj,
                                                const Projection& target_proj,
                                                const Geographic& reference_geo);
};

} // namespace earth_map