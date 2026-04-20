#version 450

// Fullscreen triangle — no vertex buffer needed.
// Invoke with vkCmdDraw(cmd, 3, 1, 0, 0).

layout(location = 0) out vec2 fragUV;

void main() {
    // Generate fullscreen triangle from vertex index:
    //   v0 = (-1, -1), uv = (0, 0)
    //   v1 = ( 3, -1), uv = (2, 0)
    //   v2 = (-1,  3), uv = (0, 2)
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
