#pragma once

#include <borealis.hpp>

#include <algorithm>

namespace ui {

// How far a view is outside what its parents show (a scrolled list's
// frame...), in pixels: 0 when any of it is on screen. Borealis skips
// drawing only views that are not boxes; big grids of boxes (Mii cards,
// editor tiles) use this to skip theirs and to load only what is near.
inline float offscreenDistance(brls::View* v)
{
    const float top = v->getY(), left = v->getX();
    const float bottom = top + v->getHeight(), right = left + v->getWidth();
    float d = 0.0f;
    for (brls::Box* b = v->getParent(); b; b = b->getParent()) {
        float t, r, btm, l;
        b->getCullingBounds(&t, &r, &btm, &l);
        d = std::max({ d, t - bottom, top - btm, l - right, left - r });
    }
    return d;
}

} // namespace ui
