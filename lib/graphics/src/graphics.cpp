//
// Created by Charlito33 on 23/12/2023.
// Copyright (c) 2023 Charlito33. All rights reserved.
//

#include "graphics.hpp"

#include "standby.hpp"

#include <Surface.hpp>
#include <iostream>
#include <libsystem.hpp>
#include <threads.hpp>

#ifdef ESP_PLATFORM

#include <FT6236G.h>
#include <Wire.h>
FT6236G ct;

#endif

namespace
{
    bool running;

    std::shared_ptr<LGFX> lcd;

    graphics::EScreenOrientation screenOrientation;
    std::shared_ptr<graphics::Surface> landscapeBuffer;

    int16_t touchX = 0, touchY = 0;

    bool liveTouchState = false; // is screen touched (brute and fast)
    int16_t newTouchX = 0, newTouchY = 0;

    bool isTouchRead = false;

    uint16_t brightness = 0xFF / 3;
} // namespace

void graphics::touchIsRead()
{
    isTouchRead = true;
}

uint16_t graphics::getBrightness()
{
    return brightness;
}

void graphics::setBrightness(uint16_t value, const bool temp)
{
    static bool running = false;
    if (running)
        return;
    running = true;

    if (!temp)
        brightness = value;

#ifdef ESP_PLATFORM
    static uint16_t oldValue = 0;

    if (oldValue == value)
    {
        running = false;
        return;
    }

    libsystem::log("Brightness: " + std::to_string(value));

    while (value < oldValue)
    {
        oldValue--;
        lcd->setBrightness(oldValue);
        delay(1);
    }

    while (value > oldValue)
    {
        oldValue++;
        lcd->setBrightness(oldValue);
        delay(1);
    }

#else

    // Simulate a switched off display
    if (value == 0)
        lcd->fillScreen(0x0000);

#endif

    running = false;
}

graphics::GraphicsInitCode graphics::init()
{
#ifdef ESP_PLATFORM

    running = true; // It doesn't feel right to set this here...

    lcd = std::make_shared<LGFX>();

#else

    lcd = std::make_shared<LGFX>(getScreenWidth(), getScreenHeight());

    // We need to create a "landscape buffer" used as a screen.
    // Because LovyanGFX as a weird color glitch when using "setRotation()".
    // But, by using a temporary buffer, the glitch doesn't appear.
    landscapeBuffer = std::make_shared<Surface>(getScreenHeight(), getScreenWidth());

#endif

    lcd->init();
    lcd->setColorDepth(16);
    lcd->setTextColor(TFT_WHITE);
    lcd->fillScreen(TFT_BLACK);

    // Show init text
    const std::string& initText = "Paxo";
    lcd->setFont(&DejaVu40);
    lcd->setTextColor(packRGB565(58, 186, 153));
    lcd->setCursor(
        static_cast<int32_t>(
            0.5 * static_cast<double>(getScreenWidth() - lcd->textWidth(initText.c_str()))
        ),
        static_cast<int32_t>(0.5 * static_cast<double>(getScreenHeight() - lcd->fontHeight()))
    );
    lcd->printf("%s", initText.c_str());

#ifdef ESP_PLATFORM

    // Init touchscreen

    ct.init(21, 22, false, 400000);

    // Check if only 1 I2C (touchscreen) device is found
    // 0 => No device are connected
    // 2+ => Faulty touchscreen
    const uint8_t i2cDevicesCount = hardware::scanI2C(21, 22, false);

    /*
    if (i2cDevicesCount == 0) {
        return ERROR_NO_TOUCHSCREEN;
    }

    if (i2cDevicesCount >= 2) {
        return ERROR_FAULTY_TOUCHSCREEN;
    }
    */

#endif

    return SUCCESS;
}

uint16_t graphics::getScreenWidth()
{
    switch (screenOrientation)
    {
    case graphics::PORTRAIT:
        return 320;
    case graphics::LANDSCAPE:
        return 480;
    }

    return -1;
}

uint16_t graphics::getScreenHeight()
{
    switch (screenOrientation)
    {
    case graphics::PORTRAIT:
        return 480;
    case graphics::LANDSCAPE:
        return 320;
    }

    return -1;
}

bool graphics::isRunning()
{
    return running;
}

#ifndef ESP_PLATFORM

struct SDLUpdateData
{
    void (*appMain)();
};

static int SDLUpdate(void* data)
{
    const auto* updateData = static_cast<SDLUpdateData*>(data);

    updateData->appMain();

    return 0;
}

void graphics::SDLInit(void (*appMain)())
{
    lgfx::Panel_sdl::setup();
    // lgfx::Panel_sdl::loop(); // Ensure to create the window before creating a new thread

    SDLUpdateData updateData{appMain};

    running = true;

    // Multithreading can be an issue, be careful
    SDL_Thread* thread = SDL_CreateThread(SDLUpdate, "graphics_update", &updateData);
    if (thread == nullptr)
    {
        printf("Unable to create thread : %s\n", SDL_GetError());
        exit(1);
    }

    while (lgfx::Panel_sdl::loop() == 0)
    {
    };

    running = false;

    // SDL_WaitThread(thread, nullptr);

    lgfx::Panel_sdl::close();
}

#endif

// You should only use this function with a "Canvas" (Surface that is the size of the screen)
void graphics::showSurface(const Surface* surface, int x, int y)
{
    lgfx::LGFX_Sprite sprite = surface->m_sprite; // we are friends !

#ifdef ESP_PLATFORM
    sprite.pushSprite(lcd.get(), x, y);
    // lcd.get()->waitDMA();
    // while(lcd.get()->dmaBusy());
    // lcd.get()->initDMA();
    // lcd.get()->setWindow(x, y, sprite.width(), sprite.height());
    // lcd.get()->pushImageDMA(x, y, sprite.width(), sprite.height(), sprite.getBuffer(),
    // lgfx::color_depth_t::rgb565_2Byte, sprite.getPalette());
    // lcd.get()->waitDMA();

#else
    if (screenOrientation == LANDSCAPE)
    {
        landscapeBuffer->pushSurface(
            const_cast<Surface*>(surface),
            static_cast<int16_t>(x),
            static_cast<int16_t>(y)
        );
        landscapeBuffer->m_sprite.pushSprite(lcd.get(), 0, 0);
    }
    else
    {
        sprite.pushSprite(lcd.get(), x, y);
    }
#endif
}

void graphics::setWindow(
    const uint16_t x, const uint16_t y, const uint16_t width, const uint16_t height
)
{
    lcd->setWindow(x, y, x + width, y + height);
}

void graphics::setWindow()
{
    lcd->setWindow(0, 0, getScreenWidth(), getScreenHeight());
}

void graphics::flip()
{
    // lcd->display();
}

void graphics::getTouchPos(int16_t* x, int16_t* y)
{
    *x = touchX;
    *y = touchY;
}

#ifdef ESP_PLATFORM
int getTouch(uint16_t* pPoints)
{
    TOUCHINFO ti;
    if (ct.getSamples(&ti) != FT_SUCCESS)
        return 0; // something went wrong
    if (pPoints)
    {
        // swap X/Y since the display is used 90 degrees rotated
        pPoints[0] = ti.x[0];
        pPoints[1] = ti.y[0];
        pPoints[2] = ti.x[1];
        pPoints[3] = ti.y[1];
    }
    return ti.count;
}
#endif

void graphics::touchUpdate()
{
    if (StandbyMode::state() == true)
        return;

    int16_t currentLiveTouchX = 0, currentLiveTouchY = 0;

    // 1. Read raw touch data into currentLiveTouchX, currentLiveTouchY
#ifdef ESP_PLATFORM
    uint16_t points[4];
    int i = getTouch(points);
    if (i == 1)
    {
        if (screenOrientation == PORTRAIT)
        {
            currentLiveTouchX = (points[0] - 16) * 320 / 303;
            currentLiveTouchY = (points[1] - 23) * 480 / 442;
        }
        else
        {
            currentLiveTouchX = (points[1] - 23) * 480 / 442;
            currentLiveTouchY = 320 - (points[0] - 16) * 320 / 303 - 10;
        }
    }
    else
    {
        currentLiveTouchX = -1;
        currentLiveTouchY = -1;
    }
#else // Simulator path
    bool rawTouchActive = lcd->getTouch(&currentLiveTouchX, &currentLiveTouchY);
    if (!rawTouchActive)
    {
        currentLiveTouchX = -1;
        currentLiveTouchY = -1;
    }
#endif

    // 2. Validate and normalize touch data (e.g., boundary checks, map to -1 if invalid)
    if (currentLiveTouchX <= 0 || currentLiveTouchY <= 0 ||
        currentLiveTouchX > graphics::getScreenWidth() ||
        currentLiveTouchY > graphics::getScreenHeight())
    {
        currentLiveTouchX = -1;
        currentLiveTouchY = -1;
    }

    // 3. Trigger standby if the live state differs from the last application-acknowledged state.
    //    `touchX` and `touchY` (at this point) hold the values the application last processed.
    if (currentLiveTouchX != touchX || currentLiveTouchY != touchY)
    {
        if (!StandbyMode::state()) // only trigger if not already in standby
            StandbyMode::trigger();
    }
    // else
    // {
    //     // Debugging: Touch state for standby trigger is unchanged
    //     std::cout << "Touch state for standby unchanged: live(" << currentLiveTouchX << "," <<
    //     currentLiveTouchY
    //               << ") vs app_ack(" << touchX << "," << touchY << ")" << std::endl;
    // }

    // 4. Update the intermediate buffer (newTouchX, newTouchY) with the current live state.
    //    This ensures newTouchX/Y always reflect the latest hardware reading, including -1 for no
    //    touch.
    newTouchX = currentLiveTouchX;
    newTouchY = currentLiveTouchY;

    // 5. If the application has processed the previous `touchX`/`touchY` values (isTouchRead ==
    // true):
    //    Update `touchX`/`touchY` to the new state from the buffer.
    if (isTouchRead)
    {
        touchX = newTouchX; // Update application-visible coordinates
        touchY = newTouchY;

        // Reset newTouchX/Y as per original logic after consumption.
        // With step 4, newTouchX/Y will be correctly repopulated in the next call.
        newTouchX = -1;
        newTouchY = -1;
        isTouchRead = false;
    }
}

bool graphics::isTouched()
{
    return touchX != -1 && touchY != -1;
}

graphics::EScreenOrientation graphics::getScreenOrientation()
{
    return screenOrientation;
}

void graphics::setScreenOrientation(const EScreenOrientation screenOrientation)
{
    // Update the screen orientation (and the screen size)
    // Maybe use another name for the parameter ?
    // Or store it in another place ?
    ::screenOrientation = screenOrientation;

    switch (screenOrientation)
    {
    case PORTRAIT:
        lcd->setRotation(0);
        break;
    case LANDSCAPE:
        lcd->setRotation(1);
        break;
    }
}

LGFX* graphics::getLCD()
{
    return lcd.get();
}

#ifdef ESP_PLATFORM

FT6236G* graphics::getTouchController()
{
    return &ct;
}

#endif
