#include "modelpreview.h"

#include <QHash>
#include <QtEndian>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int Supersample = 2;
constexpr int MaxCubes = 200000;
constexpr int MaxTriangles = 300000;
constexpr int MaterialCount = 69;
constexpr int Fallback = 24;
constexpr float Ambient = 0.45f;
constexpr float Occlusion = 0.5f;
constexpr float FieldOfView = 35.0f;
constexpr float Turn = 225.0f;
constexpr float Margin = 0.08f;

constexpr std::array<uchar, 8> Identity = {20, 120, 124, 24, 4, 104, 100, 0};

constexpr std::array<QRgb, MaterialCount> Colors = {
    0xb43429, 0x85241d, 0x5a0f08, 0xd9a76e, 0xa389af, 0x4194e3, 0x295696, 0x002c64,
    0xa04e06, 0x735e89, 0x6d8315, 0x4d5a08, 0x283800, 0x632a05, 0x42335a, 0xda7b09,
    0xbf5d00, 0x9e3d00, 0xfece65, 0xb69042, 0xa8a7a5, 0x838283, 0x575657, 0x212121,
    0x746342, 0x7cc5ee, 0x321f18, 0x5e1f1a, 0x54b413, 0x828285, 0x784426, 0x6f5128,
    0x495458, 0x717170, 0x877948, 0x315449, 0x6d5519, 0x611717, 0xa37914, 0x6c6c6c,
    0x4b4642, 0x534d49, 0xe79898, 0x2b3853, 0xff34a1, 0x490a09, 0x25341d, 0x123851,
    0x5d5454, 0x7ba76a, 0x747374, 0x43741c, 0x840425, 0x301b00, 0x924102, 0xcde7ed,
    0x505817, 0xaf8b40, 0x7f7d7f, 0xaedcf3, 0x535151, 0x27749e, 0x3f5067, 0x7b797a,
    0x9746f7, 0x796f6f, 0x7c6f82, 0x774c4a, 0x706e6d,
};

float emission(int material)
{
    switch (material) {
    case 26: return 0.75f;
    case 28: return 0.7f;
    case 45:
    case 46: return 1.0f;
    case 47: return 0.5f;
    case 55: return 0.9f;
    case 64: return 0.4f;
    default: return 0.0f;
    }
}

struct Vec {
    float x = 0;
    float y = 0;
    float z = 0;
};

Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Vec normalized(Vec v)
{
    const float length = std::sqrt(dot(v, v));
    return length > 0 ? v * (1 / length) : v;
}


Vec yaw(Vec v, float degrees)
{
    const float c = std::cos(qDegreesToRadians(degrees)), s = std::sin(qDegreesToRadians(degrees));
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

Vec pitch(Vec v, float degrees)
{
    const float c = std::cos(qDegreesToRadians(degrees)), s = std::sin(qDegreesToRadians(degrees));
    return {v.x, c * v.y - s * v.z, s * v.y + c * v.z};
}

struct Face {
    std::array<int, 4> corners;
    std::array<int, 3> normal;
    int own;
    int opposite;
};

constexpr std::array<Face, 6> Faces = {{
    {{0, 1, 2, 3}, {0, 1, 0}, 1, 2},
    {{4, 5, 6, 7}, {0, -1, 0}, 2, 1},
    {{7, 6, 1, 0}, {0, 0, -1}, 4, 8},
    {{5, 4, 3, 2}, {0, 0, 1}, 8, 4},
    {{4, 7, 0, 3}, {-1, 0, 0}, 16, 32},
    {{6, 5, 2, 1}, {1, 0, 0}, 32, 16},
}};

struct Side {
    int flag;
    std::array<int, 4> corners;
};

constexpr std::array<Side, 6> Sides = {{
    {1, {0, 1, 2, 3}},
    {2, {4, 5, 6, 7}},
    {8, {2, 3, 4, 5}},
    {4, {0, 1, 6, 7}},
    {16, {0, 3, 4, 7}},
    {32, {1, 2, 5, 6}},
}};

struct Cube {
    int x = 0;
    int y = 0;
    int z = 0;
    std::array<uchar, 8> corners{};
    std::array<uchar, 6> materials{};
    int sides = 0;
};

using Cubes = QHash<quint64, Cube>;

struct Triangle {
    std::array<Vec, 3> points;
    std::array<float, 3> light;
    Vec normal;
    int material;
};

struct Projected {
    float x;
    float y;
    float w;
};

quint64 key(int x, int y, int z)
{
    return quint64(quint16(x)) << 32 | quint64(quint16(y)) << 16 | quint16(z);
}

Vec corner(uchar value)
{
    return {-0.5f + value / 25 * 0.25f, -0.5f + value / 5 % 5 * 0.25f, -0.5f + value % 5 * 0.25f};
}

int unindented(const std::array<uchar, 8> &corners)
{
    std::array<bool, 8> moved{};
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        moved[i] = corners[i] != Identity[i];
        if (moved[i]) ++count;
    }
    if (count == 0) return 63;
    if (count > 4) return 0;
    int sides = 0;
    for (const Side &side : Sides)
        if (std::none_of(side.corners.begin(), side.corners.end(), [&](int i) { return moved[i]; }))
            sides |= side.flag;
    return sides;
}

qsizetype cubesStart(const QByteArray &file)
{
    return file.startsWith("OKM1") ? 8 : 0;
}

bool parse(const QByteArray &file, Cubes &cubes)
{
    const auto *data = reinterpret_cast<const uchar *>(file.constData());
    const qsizetype size = file.size();
    qsizetype pos = cubesStart(file);
    if (pos + 4 > size) return false;
    const qint32 count = qFromBigEndian<qint32>(data + pos);
    pos += 4;

    for (qint32 i = 0; i < count; ++i) {
        if (pos + 7 > size) return false;
        Cube cube;
        cube.x = qFromBigEndian<qint16>(data + pos);
        cube.y = qFromBigEndian<qint16>(data + pos + 2);
        cube.z = qFromBigEndian<qint16>(data + pos + 4);
        const uchar flags = data[pos + 6];
        pos += 7;
        const qsizetype corners = flags & 1 ? 0 : 8;
        const qsizetype materials = flags & 2 ? 1 : 6;
        if (pos + corners + materials > size) return false;

        if (corners) std::copy_n(data + pos, 8, cube.corners.begin());
        else cube.corners = Identity;
        pos += corners;
        if (materials == 1) cube.materials.fill(data[pos]);
        else std::copy_n(data + pos, 6, cube.materials.begin());
        pos += materials;
        cube.sides = unindented(cube.corners);

        const int run = std::max(flags >> 2, 1);
        for (int step = 0; step < run; ++step) {
            Cube placed = cube;
            placed.x += step;
            cubes.insert(key(placed.x, placed.y, placed.z), placed);
        }
        if (cubes.size() > MaxCubes) return false;
    }
    return !cubes.isEmpty();
}

float occlusion(const Cubes &cubes, const Cube &cube, const Face &face, Vec canonical)
{
    const std::array<int, 3> base = {cube.x + face.normal[0], cube.y + face.normal[1], cube.z + face.normal[2]};
    const std::array<float, 3> toward = {canonical.x, canonical.y, canonical.z};
    std::array<std::array<int, 3>, 2> steps{};
    for (int axis = 0, found = 0; axis < 3; ++axis)
        if (face.normal[axis] == 0) steps[found++][axis] = toward[axis] > 0 ? 1 : -1;

    int free = 0;
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) {
            const int x = base[0] + a * steps[0][0] + b * steps[1][0];
            const int y = base[1] + a * steps[0][1] + b * steps[1][1];
            const int z = base[2] + a * steps[0][2] + b * steps[1][2];
            if (!cubes.contains(key(x, y, z))) ++free;
        }
    return free * 0.25f;
}

bool triangulate(const Cubes &cubes, std::vector<Triangle> &triangles)
{
    for (const Cube &cube : cubes) {
        for (int index = 0; index < 6; ++index) {
            const Face &face = Faces[index];
            const auto neighbor = cubes.constFind(key(cube.x + face.normal[0], cube.y + face.normal[1], cube.z + face.normal[2]));
            if (neighbor != cubes.cend() && cube.sides & face.own && neighbor->sides & face.opposite) continue;

            std::array<Vec, 4> points;
            std::array<float, 4> light;
            const Vec position{float(cube.x), float(cube.y), float(cube.z)};
            for (int k = 0; k < 4; ++k) {
                const int at = face.corners[k];
                points[k] = yaw(position + corner(cube.corners[at]), Turn);
                light[k] = occlusion(cubes, cube, face, corner(Identity[at]));
            }
            const Vec normal = yaw({float(face.normal[0]), float(face.normal[1]), float(face.normal[2])}, Turn);
            const int material = cube.materials[index] < MaterialCount ? cube.materials[index] : Fallback;
            for (const auto &[a, b, c] : {std::array{0, 3, 2}, std::array{2, 1, 0}})
                triangles.push_back({{points[a], points[b], points[c]}, {light[a], light[b], light[c]}, normal, material});
        }
        if (triangles.size() > size_t(MaxTriangles)) return false;
    }
    return !triangles.empty();
}

}

namespace ModelPreview {

int cubeCount(const QByteArray &file)
{
    const qsizetype start = cubesStart(file);
    if (file.size() < start + 4) return 0;
    return std::max(qFromBigEndian<qint32>(file.constData() + start), 0);
}

bool hasScale(const QByteArray &file)
{
    return cubesStart(file) != 0 && file.size() >= 8;
}

float scale(const QByteArray &file)
{
    if (!hasScale(file)) return 1;
    const float value = qFromBigEndian<float>(file.constData() + 4);
    return std::isfinite(value) && value > 0 ? value : 1;
}

QImage render(const QByteArray &file, const QSize &size)
{
    Cubes cubes;
    std::vector<Triangle> triangles;
    if (size.isEmpty() || !parse(file, cubes) || !triangulate(cubes, triangles)) return {};

    constexpr float Infinity = std::numeric_limits<float>::infinity();
    Vec low{Infinity, Infinity, Infinity}, high{-Infinity, -Infinity, -Infinity};
    for (const Triangle &triangle : triangles)
        for (const Vec &point : triangle.points) {
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        }
    const Vec center = (low + high) * 0.5f;
    const float extent = std::max({high.x - low.x, high.y - low.y, high.z - low.z});
    const float fit = extent > 0 ? 2 / extent : 1;

    const Vec eye{2.4f, 2.6f, -3.8f};
    const Vec forward = normalized(eye * -1);
    const Vec right = normalized(cross({0, 1, 0}, forward));
    const Vec up = cross(forward, right);
    const float focal = 1 / std::tan(qDegreesToRadians(FieldOfView / 2));

    std::vector<std::array<Projected, 3>> projected(triangles.size());
    float minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (size_t i = 0; i < triangles.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const Vec view = (triangles[i].points[k] - center) * fit - eye;
            const float depth = dot(view, forward);
            const float x = focal * dot(view, right) / depth;
            const float y = focal * dot(view, up) / depth;
            projected[i][k] = {x, y, 1 / depth};
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }

    const int width = size.width() * Supersample;
    const int height = size.height() * Supersample;
    const float span = std::max(maxX - minX, maxY - minY);
    const float scale = std::min(width, height) * (1 - 2 * Margin) / (span > 0 ? span : 1);
    const float offsetX = width / 2.0f - (minX + maxX) / 2 * scale;
    const float offsetY = height / 2.0f + (minY + maxY) / 2 * scale;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    std::vector<float> depths(size_t(width) * height, 0.0f);
    const Vec light = normalized(yaw(pitch({0, 0, 1}, 50), -30) * -1);
    const Vec tint{1.0f, 0.988f, 0.753f};

    for (size_t i = 0; i < triangles.size(); ++i) {
        const Triangle &triangle = triangles[i];
        Vec normal = cross(triangle.points[1] - triangle.points[0], triangle.points[2] - triangle.points[0]);
        if (dot(normal, normal) < 1e-12f) continue;
        normal = normalized(normal);
        if (dot(normal, triangle.normal) < 0) normal = normal * -1;
        const Vec centroid = ((triangle.points[0] + triangle.points[1] + triangle.points[2]) * (1.0f / 3) - center) * fit;
        if (dot(normal, eye - centroid) <= 0) continue;

        const float diffuse = std::max(dot(normal, light), 0.0f);
        const float glow = emission(triangle.material);
        const QRgb color = Colors[triangle.material];
        const std::array<float, 3> albedo = {float(qRed(color)), float(qGreen(color)), float(qBlue(color))};
        const std::array<float, 3> shade = {Ambient + diffuse * tint.x, Ambient + diffuse * tint.y, Ambient + diffuse * tint.z};

        const auto &[a, b, c] = projected[i];
        const float ax = offsetX + a.x * scale, ay = offsetY - a.y * scale;
        const float bx = offsetX + b.x * scale, by = offsetY - b.y * scale;
        const float cx = offsetX + c.x * scale, cy = offsetY - c.y * scale;
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::abs(area) < 1e-6f) continue;

        const int fromX = std::max(0, int(std::floor(std::min({ax, bx, cx}))));
        const int toX = std::min(width - 1, int(std::ceil(std::max({ax, bx, cx}))));
        const int fromY = std::max(0, int(std::floor(std::min({ay, by, cy}))));
        const int toY = std::min(height - 1, int(std::ceil(std::max({ay, by, cy}))));
        for (int y = fromY; y <= toY; ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            const float py = y + 0.5f;
            for (int x = fromX; x <= toX; ++x) {
                const float px = x + 0.5f;
                const float wa = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
                const float wb = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
                const float wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                const float w = wa * a.w + wb * b.w + wc * c.w;
                float &stored = depths[size_t(y) * width + x];
                if (w <= stored) continue;
                stored = w;
                const float lit = Occlusion + (1 - Occlusion) * (wa * triangle.light[0] + wb * triangle.light[1] + wc * triangle.light[2]);
                std::array<int, 3> rgb{};
                for (int channel = 0; channel < 3; ++channel)
                    rgb[channel] = std::min(255, int(albedo[channel] * (shade[channel] * lit + glow)));
                line[x] = qRgb(rgb[0], rgb[1], rgb[2]);
            }
        }
    }
    return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}
