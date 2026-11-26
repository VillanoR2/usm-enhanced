#pragma once
// Procedural stand-ins for the two PS2 mask textures (64x64 + 6 mips, BGRA),
// used when mask-0dc218aa.bgra / mask-6064aa59.bgra were not extracted from
// the user's ISO. Original work: a few shape parameters (hole, ring, arcs)
// chosen to reproduce the effect, not a copy of the PS2 pixels. As on PS2,
// the 32x32 level is drawn on its own and the smaller ones are averages.
#include <cmath>
#include <cstdint>

namespace mask_generated {

inline float smooth(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0 > 1e-3f ? edge1 - edge0 : 1e-3f);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}

inline float wrap(float angle) {
    constexpr float pi = 3.14159265f;
    while (angle > pi) angle -= 2 * pi;
    while (angle < -pi) angle += 2 * pi;
    return angle;
}

// First mask: rounded hole (level `inner`), bright arcs above and at the sides.
struct Hole { float base, cx, cy, hx, hy, power, soft, amp, rcx, rcy, radius, width, spread, inner; };
inline constexpr Hole first64 = {99.027f, 32.185f, 35.562f, 21.714f, 27.305f, 4.701f, 0.231f, 76.259f,
                                 32.053f, 32.021f, 27.454f, 2.954f, 0.444f, 0.f};
inline constexpr Hole first32 = {101.61f, 32.253f, 35.186f, 23.488f, 28.555f, 8.f, 0.338f, 72.825f,
                                 32.048f, 32.054f, 27.35f, 2.863f, 0.503f, 52.043f};

inline float alpha(const Hole& p, float x, float y) {
    const float d = std::pow(std::pow(std::fabs((x - p.cx) / p.hx), p.power) +
                             std::pow(std::fabs((y - p.cy) / p.hy), p.power), 1.f / p.power);
    const float hole = 1.f - smooth(1.f - p.soft, 1.f, d);
    const float r = std::hypot(x - p.rcx, y - p.rcy);
    const float angle = std::atan2(y - p.rcy, x - p.rcx);
    const float ring = std::exp(-std::pow((r - p.radius) / p.width, 2.f));
    auto lobe = [&](float centre) { return std::exp(-std::pow(wrap(angle - centre) / p.spread, 2.f)); };
    const float arcs = lobe(-1.5707963f) + lobe(3.14159265f) + lobe(0.f);
    return p.inner * hole + (p.base + p.amp * ring * arcs) * (1.f - hole);
}

// Second mask: soft ring over a uniform base, brighter above and at the sides.
struct Ring { float cx, cy, rx, ry, power, d0, d1, d2, d3, base, peak, outer, a1, b1, a2, a4; };
inline constexpr Ring second64 = {32.086f, 33.957f, 53.501f, 59.254f, 2.08f, 0.298f, 0.738f, 0.408f, 0.662f,
                                  93.171f, 230.173f, -0.472f, -0.011f, -0.489f, 0.408f, 0.466f};
inline constexpr Ring second32 = {32.113f, 34.108f, 260.776f, 286.933f, 2.306f, 0.072f, 0.25f, 0.065f, 0.132f,
                                  125.799f, 1691.398f, -31.829f, -0.033f, -0.728f, 0.55f, 0.69f};

inline float alpha(const Ring& p, float x, float y) {
    const float u = (x - p.cx) / p.rx, v = (y - p.cy) / p.ry;
    const float d = std::pow(std::pow(std::fabs(u), p.power) + std::pow(std::fabs(v), p.power), 1.f / p.power);
    const float angle = std::atan2(v, u);
    float shape = 1.f + p.a1 * std::cos(angle) + p.b1 * std::sin(angle) + p.a2 * std::cos(2 * angle) +
                  p.a4 * std::cos(4 * angle);
    shape = shape < 0.f ? 0.f : (shape > 3.f ? 3.f : shape);
    const float fall = smooth(p.d2, p.d3, d);
    return p.base + p.peak * shape * smooth(p.d0, p.d1, d) * (1.f - fall) + p.outer * fall;
}

inline uint32_t texel(bool second, float a) {
    a = a < 0.f ? 0.f : (a > 255.f ? 255.f : a);
    float r = 0, g = 0, b = 0;
    if (second && a > 94.f) { r = 0.0288f * (a - 94.f); g = 0.0221f * (a - 94.f); b = 0.0155f * (a - 94.f); }
    auto byte = [](float v) { return static_cast<uint32_t>(v + 0.5f); };
    return (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

// Fills one level; coordinates are in 64-texel units.
inline void draw(bool second, bool half, uint32_t* out, unsigned size) {
    const float scale = 64.f / size;
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; ++x) {
            const float px = (x + 0.5f) * scale, py = (y + 0.5f) * scale;
            const float a = second ? alpha(half ? second32 : second64, px, py)
                                   : alpha(half ? first32 : first64, px, py);
            out[y * size + x] = texel(second, a);
        }
}

// 5461 texels: 64x64, 32x32, then box-filtered mips down to 1x1.
inline void generate(bool second, uint32_t* texels) {
    draw(second, false, texels, 64);
    draw(second, true, texels + 64 * 64, 32);
    unsigned source = 64 * 64, offset = 64 * 64 + 32 * 32;
    for (unsigned size = 16; size >= 1; size >>= 1) {
        const unsigned parent = size * 2;
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                uint32_t sum[4] = {};
                for (unsigned dy = 0; dy < 2; ++dy)
                    for (unsigned dx = 0; dx < 2; ++dx) {
                        const uint32_t p = texels[source + (y * 2 + dy) * parent + x * 2 + dx];
                        for (unsigned c = 0; c < 4; ++c) sum[c] += (p >> (8 * c)) & 255;
                    }
                uint32_t out = 0;
                for (unsigned c = 0; c < 4; ++c) out |= ((sum[c] + 2) / 4) << (8 * c);
                // First mask: on PS2 the 16x16 level is lighter than an average
                // (hole fades out at distance); lift its alpha toward 118.
                if (!second && size == 16) {
                    const float a = static_cast<float>(out >> 24) * 0.525f + 118.f * 0.475f;
                    out = (out & 0x00ffffffu) | (static_cast<uint32_t>(a + 0.5f) << 24);
                }
                texels[offset + y * size + x] = out;
            }
        source = offset;
        offset += size * size;
        if (size == 1) break;
    }
}

}  // namespace mask_generated
