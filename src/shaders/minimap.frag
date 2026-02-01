#version 330 core
in vec2 TexCoord;

uniform sampler2D uTexture;
uniform vec4 uColor;
uniform bool uUseTexture;

out vec4 FragColor;

void main() {
    if (uUseTexture) {
        vec4 texColor = texture(uTexture, TexCoord);
        FragColor = vec4(texColor.rgb * uColor.rgb, 1.0);
    } else {
        FragColor = uColor;
    }
}
