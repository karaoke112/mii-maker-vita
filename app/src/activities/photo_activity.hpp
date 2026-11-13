#pragma once

#include <borealis.hpp>
#include <mii/mii.h>

#include <memory>

class MiiPreview;

// Photo Studio: a Mii in a pose, with an expression, turned with the right
// stick or a finger, on a background; Take Photo saves the picture
// (960 x 544, the Vita's screen) as a PNG in ux0:picture/MIIMAKER/, where
// the Photos app finds it. The pose and expression start as the Mii screen
// had them, and go back there.
class PhotoActivity : public brls::Activity
{
  public:
    PhotoActivity(const mii_charinfo_t& mii, bool favorite, int* expression, int* pose);

    brls::View* createContentView() override;

  private:
    void takePhoto();

    mii_charinfo_t mii;
    bool favorite;
    int* expression;
    int* pose;
    float yaw = 0.0f, pitch = 0.0f;
    bool facingFront = false;
    int background = 0;
    bool closeUp = false;
    // shared with the thread drawing a photo (which may end after this screen)
    std::shared_ptr<bool> busy = std::make_shared<bool>(false);
    MiiPreview* preview = nullptr;
    brls::Box* stageBox = nullptr;
    static constexpr float kStageW = 600.0f, kStageH = 600.0f * 544.0f / 960.0f;
};
