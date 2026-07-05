#ifndef EventFilter_Phase2TrackerRawToDigi_DTCTrailer_H
#define EventFilter_Phase2TrackerRawToDigi_DTCTrailer_H

#include <cstdint>
#include <array>
#include <cstdio>

class DTCTrailer {
public:

    DTCTrailer() : words_{{0, 0, 0, 0}} {}
    explicit DTCTrailer(const std::array<uint32_t, 4>& words) : words_(words) {}
    DTCTrailer(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) 
        : words_{{w0, w1, w2, w3}} {}

    /**
     * @brief Print the DTC trailer in hexadecimal format
     */
    void print() const {
        printf("0x%08X %08X %08X %08X\n", 
               (unsigned int)words_[0],
               (unsigned int)words_[1],
               (unsigned int)words_[2],
               (unsigned int)words_[3]);
    }

private:
    std::array<uint32_t, 4> words_;  // 4 x 32-bit = 128-bit trailer
};

#endif