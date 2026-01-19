#version 330 core

// Globe fragment shader with lighting and atmospheric effects
in vec3 vFragPos;                 // Fragment position in world space
in vec3 vNormal;                  // Normal in view space
in vec2 vTexCoord;                 // Texture coordinates
in vec3 vLightDirection;            // Direction to light
in float vDistance;                 // Distance from camera

// Uniforms
uniform vec3 uLightColor;               // Light color/intensity
uniform vec3 uAmbientColor;            // Ambient light color
uniform float uAmbientStrength;         // Ambient light strength
uniform vec3 uObjectColor;             // Base object color
uniform float uSpecularStrength;         // Specular reflection strength
uniform float uShininess;               // Material shininess
uniform vec3 uCameraPosition;           // Camera position for fog
uniform float uTime;                    // Animation time
uniform sampler2D uTexture;            // Texture sampler

// Output
out vec4 FragColor;

void main() {
    // Ambient lighting
    vec3 ambient = uAmbientColor * uAmbientStrength;
    
    // Diffuse lighting
    float diff = max(dot(vNormal, vLightDirection), 0.0);
    vec3 diffuse = diff * uLightColor * uObjectColor;
    
    // Specular lighting
    vec3 reflectDir = reflect(-vLightDirection, vNormal);
    float spec = pow(max(dot(reflectDir, normalize(-vec3(0.0, 0.0, 1.0))), 0.0), uShininess);
    vec3 specular = uSpecularStrength * spec * uLightColor;
    
    // Combine lighting components
    vec3 lighting = ambient + diffuse + specular;
    
    // Base color from texture (simplified - no texture for basic globe)
    vec4 textureColor = vec4(uObjectColor, 1.0);
    
    // Apply lighting to texture color
    vec3 finalColor = textureColor.rgb * lighting;
    
    // Simple atmospheric fog based on distance
    float fogStart = 10000.0;    // Start fog at 10km
    float fogEnd = 50000.0;      // Full fog at 50km
    float fogFactor = clamp((vDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    
    // Apply fog
    finalColor = mix(finalColor, uAmbientColor, fogFactor);
    
    // Output final color
    FragColor = vec4(finalColor, textureColor.a);
}