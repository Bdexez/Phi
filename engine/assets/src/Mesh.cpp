#include <phi/assets/Mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace phi::assets {

void computeTangents(MeshData& mesh) {
    std::vector<Vec3> alongU(mesh.vertices.size(), Vec3(0.0f));
    std::vector<Vec3> alongV(mesh.vertices.size(), Vec3(0.0f));
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const u32 corners[3] = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        const Vertex& a = mesh.vertices[corners[0]];
        const Vertex& b = mesh.vertices[corners[1]];
        const Vertex& c = mesh.vertices[corners[2]];
        const Vec3 edge1 = b.position - a.position;
        const Vec3 edge2 = c.position - a.position;
        const Vec2 delta1 = b.uv - a.uv;
        const Vec2 delta2 = c.uv - a.uv;
        const f32 determinant = delta1.x * delta2.y - delta2.x * delta1.y;
        if (std::abs(determinant) < 1e-12f) {
            continue;  // no UV mapping on this triangle
        }
        // Weighted by the triangle's UV area, so larger faces count more.
        const Vec3 u = (edge1 * delta2.y - edge2 * delta1.y) * (1.0f / determinant);
        const Vec3 v = (edge2 * delta1.x - edge1 * delta2.x) * (1.0f / determinant);
        for (const u32 corner : corners) {
            alongU[corner] += u;
            alongV[corner] += v;
        }
    }

    for (usize i = 0; i < mesh.vertices.size(); ++i) {
        Vertex& vertex = mesh.vertices[i];
        const Vec3 normal = vertex.normal;
        // Gram-Schmidt: keep the part of U's direction along the surface.
        Vec3 tangent = alongU[i] - normal * glm::dot(normal, alongU[i]);
        if (glm::dot(tangent, tangent) < 1e-12f) {
            const Vec3 axis = std::abs(normal.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
            tangent = glm::cross(axis, normal);
        }
        tangent = glm::normalize(tangent);
        // Texture "up" is decreasing V.
        const f32 handedness = glm::dot(glm::cross(normal, tangent), -alongV[i]) < 0.0f ? -1.0f : 1.0f;
        vertex.tangent = Vec4(tangent, handedness);
    }
}

MeshData makeCube(f32 size) {
    struct Face {
        Vec3 normal;
        Vec3 right;  // +U direction on the face
        Vec3 up;     // +V direction on the face; right x up == normal
    };
    static const std::array<Face, 6> faces = {{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};

    const f32 half = size * 0.5f;
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& face : faces) {
        const u32 base = static_cast<u32>(mesh.vertices.size());
        const Vec3 center = face.normal * half;
        const Vec3 right = face.right * half;
        const Vec3 up = face.up * half;
        // Counter-clockwise when seen from outside (right x up = normal).
        mesh.vertices.push_back({center - right - up, face.normal, {0.0f, 1.0f}});
        mesh.vertices.push_back({center + right - up, face.normal, {1.0f, 1.0f}});
        mesh.vertices.push_back({center + right + up, face.normal, {1.0f, 0.0f}});
        mesh.vertices.push_back({center - right + up, face.normal, {0.0f, 0.0f}});
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    computeTangents(mesh);
    return mesh;
}

MeshData makePlane(f32 size, f32 uvScale) {
    const f32 half = size * 0.5f;
    const Vec3 normal(0.0f, 1.0f, 0.0f);
    MeshData mesh;
    mesh.vertices = {
        {{-half, 0.0f, half}, normal, {0.0f, uvScale}},
        {{half, 0.0f, half}, normal, {uvScale, uvScale}},
        {{half, 0.0f, -half}, normal, {uvScale, 0.0f}},
        {{-half, 0.0f, -half}, normal, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    computeTangents(mesh);
    return mesh;
}

MeshData makeSphere(f32 radius, u32 segments, u32 rings) {
    segments = std::max(segments, 3u);
    rings = std::max(rings, 2u);
    const f32 pi = std::numbers::pi_v<f32>;

    MeshData mesh;
    mesh.vertices.reserve(static_cast<usize>(rings + 1) * (segments + 1));
    mesh.indices.reserve(static_cast<usize>(rings) * segments * 6);

    // Rings go from the north pole (theta = 0) to the south pole.
    for (u32 ring = 0; ring <= rings; ++ring) {
        const f32 v = static_cast<f32>(ring) / static_cast<f32>(rings);
        const f32 theta = v * pi;
        for (u32 segment = 0; segment <= segments; ++segment) {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
            const f32 phi = u * 2.0f * pi;
            const Vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta),
                              std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({normal * radius, normal, {u, v}});
        }
    }

    const u32 stride = segments + 1;
    for (u32 ring = 0; ring < rings; ++ring) {
        for (u32 segment = 0; segment < segments; ++segment) {
            const u32 a = ring * stride + segment;  // upper ring
            const u32 b = a + stride;               // lower ring
            // Skip the degenerate triangles at the poles.
            if (ring != 0) {
                mesh.indices.insert(mesh.indices.end(), {a, a + 1, b});
            }
            if (ring != rings - 1) {
                mesh.indices.insert(mesh.indices.end(), {a + 1, b + 1, b});
            }
        }
    }
    computeTangents(mesh);
    return mesh;
}

}  // namespace phi::assets
