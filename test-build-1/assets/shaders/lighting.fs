#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;
in vec4 fragColor;      // Vertex color — carries baked lighting when a .lbake is loaded

#define MAX_LIGHTS 8
uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec4 lightColors[MAX_LIGHTS];
uniform int lightCount;
uniform vec4 ambientColor;
uniform sampler2D texture0;

// Fog Uniforms
uniform vec3 viewPos;
uniform float fogStart;
uniform float fogEnd;
uniform vec4 fogColor;

out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);

    // Multiply texture by vertex color first.
    // When baked: fragColor holds the precomputed lighting, ambientColor is white,
    //             lightCount is 0 — so this alone produces the correct result.
    // When unbaked: fragColor is white (raylib default), so this is a no-op and
    //               the dynamic light loop below does all the work as before.
    vec4 tinted = texel * fragColor;

    vec3 lightEffect = ambientColor.rgb;
    for (int i = 0; i < lightCount; i++) {
        vec3 lightDir = normalize(lightPositions[i] - fragPosition);
        float NdotL = max(dot(fragNormal, lightDir), 0.0);
        lightEffect += (lightColors[i].rgb * NdotL);
    }

    lightEffect = min(lightEffect, vec3(1.5));
    lightEffect *= 0.4;

    vec4 sceneColor = vec4(lightEffect, 1.0) * tinted;

    // Fog
    float dist = length(viewPos - fragPosition);
    float fogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = mix(fogColor, sceneColor, fogFactor);
}
