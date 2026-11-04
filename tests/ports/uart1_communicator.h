#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
// Captures TX only. No ESP-IDF implementation is bundled.
class Uart1Communicator {
public:
    inline static std::vector<uint8_t> sent;
    inline static bool short_write=false;
    static Uart1Communicator& GetInstance(){static Uart1Communicator p;return p;}
    int Uart1Send(const uint8_t *p,size_t n){sent.assign(p,p+n);return static_cast<int>(n)-(short_write?1:0);}
};
