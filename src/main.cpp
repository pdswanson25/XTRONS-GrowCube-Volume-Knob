#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <CST816S.h>

#include "USB.h"
#include "USBHIDConsumerControl.h"

// =====================================================
// PIN DEFINITIONS
// =====================================================

// Rotary encoder
constexpr int ENCODER_A = 45;
constexpr int ENCODER_B = 42;
constexpr int ENCODER_BUTTON = 41;

// Display
constexpr int DISPLAY_POWER = 1;

constexpr int TFT_SCLK = 10;
constexpr int TFT_MOSI = 11;
constexpr int TFT_CS   = 9;
constexpr int TFT_DC   = 3;
constexpr int TFT_RST  = 14;
constexpr int TFT_BL   = 46;

// Touch controller
constexpr int TOUCH_SDA = 6;
constexpr int TOUCH_SCL = 7;
constexpr int TOUCH_RST = 13;
constexpr int TOUCH_INT = 5;

// =====================================================
// COLORS
// =====================================================

constexpr uint16_t COLOR_BG        = RGB565(18, 52, 86);
constexpr uint16_t COLOR_TEXT      = RGB565(255, 255, 255);
constexpr uint16_t COLOR_HIGHLIGHT = RGB565(70, 130, 190);

// =====================================================
// TIMING
// =====================================================

constexpr unsigned long TOUCH_DEBOUNCE_MS = 250;
constexpr unsigned long TOUCH_LOCKOUT_AFTER_KNOB_MS = 400;
constexpr unsigned long TOUCH_FLASH_MS = 120;

// =====================================================
// USB HID
// =====================================================

USBHIDConsumerControl ConsumerControl;

// =====================================================
// DISPLAY
// =====================================================

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCLK,
    TFT_MOSI,
    GFX_NOT_DEFINED
);

Arduino_GFX *gfx = new Arduino_GC9A01(
    bus,
    TFT_RST,
    0,
    true
);

// =====================================================
// TOUCH
// =====================================================

CST816S touch(
    TOUCH_SDA,
    TOUCH_SCL,
    TOUCH_RST,
    TOUCH_INT
);

// =====================================================
// STATE
// =====================================================

int lastA;
bool lastButton = HIGH;

unsigned long lastTouchTime = 0;
unsigned long ignoreTouchUntil = 0;

// =====================================================
// USB MEDIA HELPER
// =====================================================

void sendMediaCommand(uint16_t command)
{
    ConsumerControl.press(command);
    delay(20);
    ConsumerControl.release();
}

// =====================================================
// BUTTON DRAWING
// =====================================================

void drawPrevButton(uint16_t bgColor)
{
    gfx->fillRect(0, 70, 80, 120, bgColor);

    gfx->setTextColor(COLOR_TEXT);

    gfx->setTextSize(4);
    gfx->setCursor(28, 100);
    gfx->print("<");

    gfx->setTextSize(1);
    gfx->setCursor(22, 155);
    gfx->print("PREV");
}

void drawPlayButton(uint16_t bgColor)
{
    gfx->fillRect(80, 70, 80, 120, bgColor);

    gfx->setTextColor(COLOR_TEXT);

    gfx->setTextSize(3);
    gfx->setCursor(92, 103);
    gfx->print(">||");

    gfx->setTextSize(1);
    gfx->setCursor(100, 155);
    gfx->print("PLAY");
}

void drawNextButton(uint16_t bgColor)
{
    gfx->fillRect(160, 70, 80, 120, bgColor);

    gfx->setTextColor(COLOR_TEXT);

    gfx->setTextSize(4);
    gfx->setCursor(190, 100);
    gfx->print(">");

    gfx->setTextSize(1);
    gfx->setCursor(188, 155);
    gfx->print("NEXT");
}

// =====================================================
// DRAW MEDIA SCREEN
// =====================================================

void drawMediaScreen()
{
    gfx->fillScreen(COLOR_BG);

    gfx->setTextColor(COLOR_TEXT);

    gfx->setTextSize(2);
    gfx->setCursor(88, 35);
    gfx->print("MEDIA");

    drawPrevButton(COLOR_BG);
    drawPlayButton(COLOR_BG);
    drawNextButton(COLOR_BG);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    // -------------------------------------------------
    // Encoder
    // -------------------------------------------------

    pinMode(ENCODER_A, INPUT);
    pinMode(ENCODER_B, INPUT);
    pinMode(ENCODER_BUTTON, INPUT_PULLUP);

    lastA = digitalRead(ENCODER_A);

    // -------------------------------------------------
    // USB HID
    // -------------------------------------------------

    ConsumerControl.begin();
    USB.begin();

    // -------------------------------------------------
    // Power display + touch
    // -------------------------------------------------

    pinMode(DISPLAY_POWER, OUTPUT);
    digitalWrite(DISPLAY_POWER, HIGH);

    delay(100);

    // -------------------------------------------------
    // Backlight
    // -------------------------------------------------

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // -------------------------------------------------
    // Display
    // -------------------------------------------------

    gfx->begin();
    drawMediaScreen();

    // -------------------------------------------------
    // Touch
    // -------------------------------------------------

    touch.begin();

    delay(500);
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    // =================================================
    // ROTARY ENCODER
    //
    // One physical detent = one HID command
    // =================================================

    int currentA = digitalRead(ENCODER_A);

    if (currentA != lastA)
    {
        if (currentA == LOW)
        {
            int currentB = digitalRead(ENCODER_B);

            if (currentA != currentB)
            {
                // Clockwise = volume up
                sendMediaCommand(
                    CONSUMER_CONTROL_VOLUME_INCREMENT
                );
            }
            else
            {
                // Counter-clockwise = volume down
                sendMediaCommand(
                    CONSUMER_CONTROL_VOLUME_DECREMENT
                );
            }
        }

        lastA = currentA;
    }

    // =================================================
    // PHYSICAL KNOB PRESS
    //
    // Press = mute
    // Physical switch has priority over touchscreen
    // =================================================

    bool button = digitalRead(ENCODER_BUTTON);

    if (lastButton == HIGH && button == LOW)
    {
        ignoreTouchUntil =
            millis() + TOUCH_LOCKOUT_AFTER_KNOB_MS;

        sendMediaCommand(
            CONSUMER_CONTROL_MUTE
        );

        delay(150);
    }

    lastButton = button;

    // =================================================
    // TOUCH SCREEN
    // =================================================

    if (touch.available())
    {
        unsigned long now = millis();

        // Physical knob always wins
        if (button == LOW || now < ignoreTouchUntil)
        {
            delay(1);
            return;
        }

        if (now - lastTouchTime > TOUCH_DEBOUNCE_MS)
        {
            lastTouchTime = now;

            int x = touch.data.x;
            int y = touch.data.y;

            // Only respond within the media button area
            if (y > 60 && y < 190)
            {
                // -------------------------------------
                // PREVIOUS
                // -------------------------------------

                if (x < 80)
                {
                    drawPrevButton(COLOR_HIGHLIGHT);

                    sendMediaCommand(
                        CONSUMER_CONTROL_SCAN_PREVIOUS
                    );

                    delay(TOUCH_FLASH_MS);

                    drawPrevButton(COLOR_BG);
                }

                // -------------------------------------
                // PLAY / PAUSE
                // -------------------------------------

                else if (x < 160)
                {
                    drawPlayButton(COLOR_HIGHLIGHT);

                    sendMediaCommand(
                        CONSUMER_CONTROL_PLAY_PAUSE
                    );

                    delay(TOUCH_FLASH_MS);

                    drawPlayButton(COLOR_BG);
                }

                // -------------------------------------
                // NEXT
                // -------------------------------------

                else
                {
                    drawNextButton(COLOR_HIGHLIGHT);

                    sendMediaCommand(
                        CONSUMER_CONTROL_SCAN_NEXT
                    );

                    delay(TOUCH_FLASH_MS);

                    drawNextButton(COLOR_BG);
                }
            }
        }
    }

    delay(1);
}