#include "infinite/SurfaceShading.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace infinite;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static Mesh seam(float angle) {
    Mesh m;
    m.vertices = {
        {0, 0, 0, 1}, {1, 0, 0, 2}, {0, 1, 0, 3}, {0, 0, 0, 4}, {0, -std::cos(angle), std::sin(angle), 5},
        {1, 0, 0, 6}};
    m.indices = {0, 1, 2, 3, 4, 5};
    return m;
}
int main() {
    try {
        for (int i = 0; i < 20000; i++) {
            const auto direction =
                glm::normalize(glm::vec3(std::sin(i * .73), std::cos(i * .37), std::sin(i * .19)));
            const auto decoded = unpackNormal(packNormal(direction));
            check(glm::length(glm::cross(glm::dvec3(direction), glm::dvec3(decoded))) < 1e-4,
                  "Packed normal lost angular precision");
            check(std::abs(glm::length(decoded) - 1) < 1e-6, "Decoded normal is not unit length");
        }
        auto mesh = seam(.4f);
        const auto before = mesh.vertices;
        const auto indices = mesh.indices;
        auto normals = surfaceNormals(mesh);
        check(normals.size() == mesh.vertices.size(), "Shading replaced mesh topology");
        check(normals[0].x == normals[3].x && normals[0].y == normals[3].y,
              "Duplicate iso-surface positions have a lighting seam");
        check(std::memcmp(before.data(), mesh.vertices.data(), before.size() * sizeof(Vertex)) == 0 &&
                  indices == mesh.indices,
              "Shading changed authoritative positions/colours/indices");
        const auto reference = unpackNormal(normals[0]);
        for (float scale : {1e-12f, 1.f, 1e12f}) {
            auto copy = mesh;
            for (auto& vertex : copy.vertices) {
                vertex.x *= scale;
                vertex.y *= scale;
                vertex.z *= scale;
            }
            const auto rescaled = surfaceNormals(copy);
            check(glm::dot(reference, unpackNormal(rescaled[0])) > .999999f,
                  "Shading depends on universe magnification units");
            copy.compact();
            const auto compact = surfaceNormals(copy);
            check(std::memcmp(rescaled.data(), compact.data(), compact.size() * sizeof(ShadingNormal)) == 0,
                  "16/32-bit index formats change lighting");
        }
        const auto sharp = surfaceNormals(seam(1.5707963f));
        check(glm::dot(unpackNormal(sharp[0]), unpackNormal(sharp[3])) < .001f,
              "Sharp geological crease was rounded away");
        std::atomic<bool> cancel{true};
        check(surfaceNormals(mesh, &cancel).empty(), "Cancelled shading task produced work");
        mesh.vertices.assign(3, Vertex{0, 0, 0, 0});
        mesh.indices = {0, 1, 2};
        for (auto packed : surfaceNormals(mesh))
            check(std::isfinite(glm::length(unpackNormal(packed))), "Degenerate triangle produced NaN");
        check(atomPresentationLevel(100, true) == 2 && atomPresentationLevel(100, false) == 3 &&
                  atomPresentationLevel(12, false) == 1,
              "Atom screen-space detail changed Original mode");
        for (bool homogeneous : {false, true})
            for (bool bottom : {false, true})
                for (int i = 0; i < 40; i++) {
                    const auto anchor = glm::normalize(glm::dvec3(std::sin(i + .3), .7, std::cos(i))) * 6e6;
                    const auto light = glm::normalize(glm::dvec3(std::cos(i), .5, std::sin(i + .2)));
                    const auto eye = anchor + glm::dvec3(160, 220, 310);
                    const auto a =
                        sunShadowFrame(anchor, light, {1, 0, 0, 0}, eye, 1, 2048, homogeneous, bottom);
                    const auto p = anchor + glm::dvec3(34, 23, -17);
                    const auto expected = a.textureFromWorld * glm::dvec4(p - eye, 1);
                    check(std::abs((a.textureFromWorld * glm::dvec4(a.anchorPlanet - eye, 1)).x - .5) < 1e-9,
                          "Sun shadow light frame lost its centre");
                    const auto rotation = glm::angleAxis(.73, glm::normalize(glm::dvec3(.3, .4, .8)));
                    for (double units : {1e-9, .01, 1., 1e3, 1e9}) {
                        const auto rebasedEye = eye + glm::dvec3(71, -43, 15);
                        const auto b = sunShadowFrame(anchor, light, rotation, rebasedEye, units, 2048,
                                                      homogeneous, bottom);
                        const auto actual =
                            b.textureFromWorld * glm::dvec4(rotation * (p - rebasedEye) / units, 1);
                        check(glm::length(actual - expected) < 1e-9 &&
                                  glm::length(a.anchorPlanet - b.anchorPlanet) < 1e-9,
                              "Camera rebase or magnification moved the sun shadow field");
                    }
                }
        std::cout << "Surface shading: packing, seam welding, crease preservation, scale invariance, "
                     "immutable topology, cancellation and sun shadow frame invariance passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
