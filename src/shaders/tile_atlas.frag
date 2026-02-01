#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uTileTexture;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform float uTime;
uniform int uZoomLevel;

// Tile data from TileTextureCoordinator
#define MAX_TILES 256
uniform int uNumTiles;
uniform ivec3 uTileCoords[MAX_TILES];
uniform vec4 uTileUVs[MAX_TILES];

// Convert world position to geographic coordinates (lat/lon)
// Matches CoordinateMapper::CartesianToGeographic()
vec2 worldToGeo(vec3 pos) {
    vec3 normalized = normalize(pos);
    float lat = asin(normalized.y) * 180.0 / 3.14159265359;
    float lon = atan(normalized.x, normalized.z) * 180.0 / 3.14159265359;
    return vec2(lon, lat);
}

// Convert geographic to tile coordinates using Web Mercator
// Web Mercator is intentional: matches standard XYZ tile server layout (OSM, etc.)
ivec2 geoToTile(vec2 geo, int zoom) {
    const float PI = 3.14159265359;
    int n = 1 << zoom;

    // Longitude: simple linear mapping
    float norm_lon = (geo.x + 180.0) / 360.0;

    // Latitude: Web Mercator projection
    float lat_clamped = clamp(geo.y, -85.0511, 85.0511);
    float lat_rad = lat_clamped * PI / 180.0;
    float merc_y = log(tan(PI / 4.0 + lat_rad / 2.0));
    float norm_lat = (1.0 - merc_y / PI) / 2.0;

    int tile_x = int(floor(norm_lon * float(n)));
    int tile_y = int(floor(norm_lat * float(n)));

    // Clamp to valid range
    tile_x = clamp(tile_x, 0, n - 1);
    tile_y = clamp(tile_y, 0, n - 1);

    return ivec2(tile_x, tile_y);
}

// Calculate fractional position within tile
vec2 getTileFrac(vec2 geo, ivec2 tile, int zoom) {
    const float PI = 3.14159265359;
    int n = 1 << zoom;

    float norm_lon = (geo.x + 180.0) / 360.0;

    float lat_clamped = clamp(geo.y, -85.0511, 85.0511);
    float lat_rad = lat_clamped * PI / 180.0;
    float merc_y = log(tan(PI / 4.0 + lat_rad / 2.0));
    float norm_lat = (1.0 - merc_y / PI) / 2.0;

    float tile_x_f = norm_lon * float(n);
    float tile_y_f = norm_lat * float(n);

    float frac_x = fract(tile_x_f);
    float frac_y = fract(tile_y_f);

    return vec2(frac_x, frac_y);
}

// Find tile UV in atlas
vec4 findTileUV(vec2 geo, int zoom) {
    ivec2 tile = geoToTile(geo, zoom);
    vec2 tileFrac = getTileFrac(geo, tile, zoom);

    ivec3 tileCoord = ivec3(tile, zoom);
    for (int i = 0; i < uNumTiles && i < MAX_TILES; i++) {
        if (uTileCoords[i] == tileCoord) {
            vec4 uv = uTileUVs[i];
            vec2 atlasUV = mix(uv.xy, uv.zw, tileFrac);
            return vec4(atlasUV, 1.0, 1.0);
        }
    }
    return vec4(0.0, 0.0, 0.0, 0.0);
}

void main() {
    // Basic lighting
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * uLightColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    // Compute geographic coordinates from world position
    vec2 geo = worldToGeo(FragPos);

    // Look up tile using geographic coordinates
    int zoom = max(uZoomLevel, 0);
    vec4 uvResult = findTileUV(geo, zoom);

    vec3 result;
    if (uvResult.z > 0.5) {
        // Tile found - sample from atlas
        vec4 texColor = texture(uTileTexture, uvResult.xy);

        float texBrightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (texBrightness > 0.05) {
            // Valid texture data
            result = (ambient + diffuse) * texColor.rgb;
            FragColor = vec4(result, texColor.a);
        } else {
            // Tile loaded but empty - use neutral base color
            vec3 baseColor = vec3(0.85, 0.82, 0.75);
            result = (ambient + diffuse) * baseColor;
            FragColor = vec4(result, 1.0);
        }
    } else {
        // Tile not loaded yet - use neutral base color
        vec3 baseColor = vec3(0.85, 0.82, 0.75);
        result = (ambient + diffuse) * baseColor;
        FragColor = vec4(result, 1.0);
    }
}
