#include "platform/PlayerFigure.h"

#include <cmath>

namespace {
void DrawTriangleDoubleSided(Vector2 a, Vector2 b, Vector2 c, Color color) {
    DrawTriangle(a, b, c, color);
    DrawTriangle(a, c, b, color);
}

Vector2 TransformPoint(
    Vector2 center,
    float scale,
    float cosTheta,
    float sinTheta,
    float x,
    float y) {
    const float localX = (x - 50.0F) * scale;
    const float localY = (y - 50.0F) * scale;
    return Vector2{
        center.x + (localX * cosTheta - localY * sinTheta),
        center.y + (localX * sinTheta + localY * cosTheta),
    };
}

void DrawQuad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color color) {
    DrawTriangleDoubleSided(a, b, c, color);
    DrawTriangleDoubleSided(a, c, d, color);
}
}  // namespace

void DrawPlayerFigure(Vector2 center, float sizePixels, float headingRadians, Color color) {
    const float scale = sizePixels / 100.0F;
    const float cosTheta = std::cos(headingRadians);
    const float sinTheta = std::sin(headingRadians);

    const Vector2 leftTrackA = TransformPoint(center, scale, cosTheta, sinTheta, 10.0F, 35.0F);
    const Vector2 leftTrackB = TransformPoint(center, scale, cosTheta, sinTheta, 30.0F, 35.0F);
    const Vector2 leftTrackC = TransformPoint(center, scale, cosTheta, sinTheta, 30.0F, 90.0F);
    const Vector2 leftTrackD = TransformPoint(center, scale, cosTheta, sinTheta, 10.0F, 85.0F);
    DrawQuad(leftTrackA, leftTrackB, leftTrackC, leftTrackD, color);

    const Vector2 rightTrackA = TransformPoint(center, scale, cosTheta, sinTheta, 90.0F, 35.0F);
    const Vector2 rightTrackB = TransformPoint(center, scale, cosTheta, sinTheta, 70.0F, 35.0F);
    const Vector2 rightTrackC = TransformPoint(center, scale, cosTheta, sinTheta, 70.0F, 90.0F);
    const Vector2 rightTrackD = TransformPoint(center, scale, cosTheta, sinTheta, 90.0F, 85.0F);
    DrawQuad(rightTrackA, rightTrackB, rightTrackC, rightTrackD, color);

    const Vector2 bodyA = TransformPoint(center, scale, cosTheta, sinTheta, 30.0F, 40.0F);
    const Vector2 bodyB = TransformPoint(center, scale, cosTheta, sinTheta, 50.0F, 20.0F);
    const Vector2 bodyC = TransformPoint(center, scale, cosTheta, sinTheta, 70.0F, 40.0F);
    const Vector2 bodyD = TransformPoint(center, scale, cosTheta, sinTheta, 60.0F, 85.0F);
    const Vector2 bodyE = TransformPoint(center, scale, cosTheta, sinTheta, 40.0F, 85.0F);
    DrawTriangleDoubleSided(bodyA, bodyB, bodyC, color);
    DrawTriangleDoubleSided(bodyA, bodyC, bodyD, color);
    DrawTriangleDoubleSided(bodyA, bodyD, bodyE, color);

    const Vector2 barrelA = TransformPoint(center, scale, cosTheta, sinTheta, 42.0F, 0.0F);
    const Vector2 barrelB = TransformPoint(center, scale, cosTheta, sinTheta, 58.0F, 0.0F);
    const Vector2 barrelC = TransformPoint(center, scale, cosTheta, sinTheta, 58.0F, 30.0F);
    const Vector2 barrelD = TransformPoint(center, scale, cosTheta, sinTheta, 42.0F, 30.0F);
    DrawQuad(barrelA, barrelB, barrelC, barrelD, color);

    const Vector2 turretA = TransformPoint(center, scale, cosTheta, sinTheta, 38.0F, 25.0F);
    const Vector2 turretB = TransformPoint(center, scale, cosTheta, sinTheta, 62.0F, 25.0F);
    const Vector2 turretC = TransformPoint(center, scale, cosTheta, sinTheta, 62.0F, 35.0F);
    const Vector2 turretD = TransformPoint(center, scale, cosTheta, sinTheta, 38.0F, 35.0F);
    DrawQuad(turretA, turretB, turretC, turretD, color);
}
