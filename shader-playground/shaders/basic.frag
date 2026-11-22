#version 330 core
in vec3 vNormal;
out vec4 outColor;
uniform vec3 baseColor;
uniform vec3 lightDirection;
void main() {
    float diffuse = max(dot(normalize(vNormal), normalize(lightDirection)), 0.0);
    outColor = vec4(baseColor * (0.15 + 0.85 * diffuse), 1.0);
}
