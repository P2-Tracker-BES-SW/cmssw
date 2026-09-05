#ifndef EventFilter_Phase2TrackerRawToDigi_TrackerHeader_H
#define EventFilter_Phase2TrackerRawToDigi_TrackerHeader_H

#include <cstdint>
#include <array>
#include <cstdio>

class TrackerHeader {
public:

    TrackerHeader() : words_{{0, 0, 0, 0}} {}
    explicit TrackerHeader(const std::array<uint32_t, 4>& words) : words_(words) {}
    TrackerHeader(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) 
        : words_{{w0, w1, w2, w3}} {}
    
    // Getters for word0 fields
    uint32_t getBoardType() const { return (words_[0] >> 24) & 0xFF; }      // bits 31-24 (8 bits)
    uint32_t getVersionMajor() const { return (words_[0] >> 21) & 0x7; }    // bits 23-21 (3 bits)
    uint32_t getVersionMinor() const { return (words_[0] >> 16) & 0x1F; }   // bits 20-16 (5 bits)
    uint32_t getMode() const { return (words_[0] >> 13) & 0x7; }            // bits 15-13 (3 bits)
    uint32_t getED() const { return (words_[0] >> 12) & 0x1; }              // bit 12 (1 bit)
    uint32_t getBoardID() const { return (words_[0] >> 4) & 0xFF; }         // bits 11-4 (8 bits)
    uint32_t getDAQpathCoreID() const { return words_[0] & 0xF; }           // bits 3-0 (4 bits)
    
    // Print helper
    void print() const {
        printf("0x%08X %08X %08X %08X\n", 
               (unsigned int)words_[0], 
               (unsigned int)words_[1], 
               (unsigned int)words_[2], 
               (unsigned int)words_[3]);
    }
    
    void printFields() const {
        printf("Board Type: %02X, Ver Major: %03d, Ver Minor: %04d, Mode: %03d, ED: %01d, Board ID: %02X, DAQpath Core ID: %01X\n",
               getBoardType(), getVersionMajor(), getVersionMinor(), getMode(), 
               getED(), getBoardID(), getDAQpathCoreID());
    }
    
private:
    std::array<uint32_t, 4> words_;  // 4 x 32-bit = 128-bit header
};

#endif