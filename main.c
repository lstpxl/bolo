#include "raylib.h"

int main() {
    const int screenWidth = 640;
    const int screenHeight = 480;
    const float moveSpeed = 220.0f;
    const float axisDeadzone = 0.20f;

    InitWindow(screenWidth, screenHeight, "RG353V Controls Test");
    SetTargetFPS(60);
    Vector2 cursor = { screenWidth/2.0f, screenHeight/2.0f };

    while (!WindowShouldClose()) {
        /* Handheld-friendly quit: START + SELECT */
        int shouldQuit = IsKeyPressed(KEY_ESCAPE) ||
            (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT) &&
             IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT));
        if (shouldQuit) {
            break;
        }

        float inputX = 0.0f;
        float inputY = 0.0f;

#if defined(__APPLE__)
        /* macOS: keyboard arrows only */
        if (IsKeyDown(KEY_LEFT)) inputX -= 1.0f;
        if (IsKeyDown(KEY_RIGHT)) inputX += 1.0f;
        if (IsKeyDown(KEY_UP)) inputY -= 1.0f;
        if (IsKeyDown(KEY_DOWN)) inputY += 1.0f;
#else
        /* Handheld: joystick + d-pad only */
        if (IsGamepadAvailable(0)) {
            float axisX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            float axisY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);

            if (axisX > axisDeadzone || axisX < -axisDeadzone) inputX += axisX;
            if (axisY > axisDeadzone || axisY < -axisDeadzone) inputY += axisY;

            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) inputX -= 1.0f;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) inputX += 1.0f;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) inputY -= 1.0f;
            if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) inputY += 1.0f;
        }
#endif

        if (inputX > 1.0f) inputX = 1.0f;
        if (inputX < -1.0f) inputX = -1.0f;
        if (inputY > 1.0f) inputY = 1.0f;
        if (inputY < -1.0f) inputY = -1.0f;

        cursor.x += inputX * moveSpeed * GetFrameTime();
        cursor.y += inputY * moveSpeed * GetFrameTime();

        if (cursor.x < 10.0f) cursor.x = 10.0f;
        if (cursor.x > screenWidth - 10.0f) cursor.x = screenWidth - 10.0f;
        if (cursor.y < 10.0f) cursor.y = 10.0f;
        if (cursor.y > screenHeight - 10.0f) cursor.y = screenHeight - 10.0f;

        BeginDrawing();
        ClearBackground(BLACK);
        DrawCircleV(cursor, 10.0f, GREEN);

#if defined(__APPLE__)
        DrawText("macOS test: move with Arrow keys", 120, 20, 20, RAYWHITE);
        DrawText("Exit: ESC", 260, 45, 18, GRAY);
#else
        DrawText("RG353V test: move with D-pad + Left Stick", 55, 20, 20, RAYWHITE);
        DrawText("Exit: START + SELECT", 190, 45, 18, GRAY);
        DrawText(TextFormat("Left stick: %.2f, %.2f",
                            GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X),
                            GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y)),
                 180, 70, 18, GRAY);
#endif
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
