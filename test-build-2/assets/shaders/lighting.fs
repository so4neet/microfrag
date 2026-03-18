#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;      // Vertex color — carries baked lighting when a .lbake is loaded

#define MAX_LIGHTS 8
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec4 lightColors[MAX_LIGHTS];
uniform int  lightCount;
uniform vec4 ambientColor;
uniform sampler2D texture0;

out vec4 finalColor;

void main() {
    vec4 texel  = texture(texture0, fragTexCoord);
    vec4 tinted = texel * fragColor;

    vec3 lightEffect = ambientColor.rgb;
    for (int i = 0; i < lightCount; i++) {
        vec3  lightDir = normalize(lightPositions[i] - fragPosition);
        float NdotL    = max(dot(fragNormal, lightDir), 0.0);
        lightEffect   += lightColors[i].rgb * NdotL;
    }
    lightEffect = clamp(lightEffect, 0.0, 1.0);

    finalColor = vec4(lightEffect, 1.0) * tinted;
}
