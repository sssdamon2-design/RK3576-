#pragma once

#include <cstdint>
#include <vector>

struct ImageFrame
{
    std::vector<uint8_t> data; //创建一个专门存 uint8_t 的数组，但是大小可以动态变化，而且内存会自动管理

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pixel_format = 0;

    uint64_t timestamp_ns = 0;
    uint64_t frame_id = 0;
};