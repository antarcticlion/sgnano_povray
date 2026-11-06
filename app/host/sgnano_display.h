// ホスト用の POV-Ray 表示。描画ブロックを sgnano_app へ渡す。
#ifndef SGNANO_DISPLAY_H
#define SGNANO_DISPLAY_H

#include "disp_text.h"

namespace pov_frontend
{
class SgnanoDisplay : public UnixTextDisplay
{
public:
    SgnanoDisplay(unsigned int w, unsigned int h, GammaCurvePtr gamma, vfeSession *session, bool visible)
        : UnixTextDisplay(w, h, gamma, session, visible) {}

    void Initialise() override;
    void Close() override;
    void DrawPixel(unsigned int x, unsigned int y, const RGBA8 &colour) override;
    void DrawPixelBlock(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8 *colour) override;
    void DrawFilledRectangle(unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2, const RGBA8 &colour) override;
    void UpdateScreen(bool Force = false) override;
    bool HandleEvents() override { return false; }

private:
    bool ready = false;
};
}

#endif
