#include <cstddef>
#include "shared_types.h"
static_assert(sizeof(FrameData) == 96);
static_assert(alignof(FrameData) == 16);
static_assert(offsetof(FrameData, cameraPos)    == 64);
static_assert(offsetof(FrameData, viewportSize) == 80);
static_assert(offsetof(FrameData, time)         == 88);