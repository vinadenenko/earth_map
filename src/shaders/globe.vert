#version 330 core

// Globe vertex shader with basic lighting and texturing
layout (location = 0) in vec3 aPosition;     // Vertex position in world space
layout (location = 1) in vec3 aNormal;       // Surface normal
layout (location = 2) in vec2 aTexCoord;     // Texture coordinates

// Uniforms
uniform mat4 uModel;                    // Model matrix (world transform)
uniform mat4 uView;                     // View matrix (camera transform)
uniform mat4 uProjection;               // Projection matrix (clip space)
uniform vec3 uLightPosition;             // Light position in world space
uniform vec3 uLightColor;               // Light color/intensity
uniform vec3 uViewPosition;             // Camera position in world space
uniform float uTime;                    // Animation time

// Outputs to fragment shader
out vec3 vFragPos;                   // Fragment position in world space
out vec3 vNormal;                    // Normal in view space
out vec2 vTexCoord;                  // Texture coordinates
out vec3 vLightDirection;            // Direction to light
out float vDistance;                  // Distance from camera

void main() {
    // Calculate world position
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    
    // Calculate view space position
    vec4 viewPos = uView * worldPos;
    
    // Calculate clip space position
    gl_Position = uProjection * viewPos;
    
    // Pass through texture coordinates
    vTexCoord = aTexCoord;
    
    // Calculate normal in view space
    mat3 normalMatrix = mat3(uView);
    vNormal = normalize(normalMatrix * aNormal);
    
    // Calculate fragment position in world space
    vFragPos = worldPos.xyz;
    
    // Calculate light direction in view space
    vec3 viewLightPos = vec3(uView * vec4(uLightPosition, 1.0));
    vLightDirection = normalize(viewLightPos - viewPos.xyz);
    
    // Calculate distance from camera
    vDistance = length(viewPos.xyz);
    
    // Simple animation for testing (subtle rotation)
    vec3 animatedPosition = aPosition;
    animatedPosition.y += sin(uTime * 2.0) * 0.01;  // Small oscillation
    worldPos = uModel * vec4(animatedPosition, 1.0);
}