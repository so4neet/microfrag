#version 330

// Attribute locations Raylib expects
layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec2 vertexTexCoord;
layout (location = 2) in vec3 vertexNormal;
layout (location = 3) in vec4 vertexColor;

// Output variables for the fragment shader
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;
out vec4 fragColor;

// Raylib built-in uniforms
uniform mat4 mvp;
uniform mat4 matModel;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
