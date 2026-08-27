#ifndef EventFilter_Phase2TrackerRawToDigi_TrackerBlock_H
#define EventFilter_Phase2TrackerRawToDigi_TrackerBlock_H

#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include <vector>
#include <iostream>
#include <bitset>

using namespace Phase2DAQFormatSpecification;

// Base class to be then specialised for tracker header and trailer
class TrackerBlock {
public:
  explicit TrackerBlock(size_t nLines) : values_(nLines, 0) {}
  virtual ~TrackerBlock() = default;

  void setValue(std::vector<uint32_t>& newValues) {
    values_ = newValues;
    is2S_ = ((values_[0] >> (N_BITS_PER_WORD - MODULE_TYPE_BITS)) & ((1u << MODULE_TYPE_BITS) - 1)) == MODULE_TYPE_2S;
    setSpecificValue();
  }

  bool is2S() const { return is2S_; }

  void printValues() const {
    for (size_t i = 0; i < values_.size(); ++i)
      printValue(i);
  }

  void printValue(size_t i) const {
    std::cout << blockName() << "[" << i << "]: " << values_[i] << "   " << std::bitset<32>(values_[i]) << std::endl;
  }

protected:
  std::vector<uint32_t> values_;
  bool is2S_{false};

  virtual std::string blockName() const { return "TrackerBlock"; }
  virtual void setSpecificValue() {} // overridden by sub-classes

};


class TrackerHeader : public TrackerBlock {
public:
  TrackerHeader() : TrackerBlock(HEADER_N_LINES) {}

  uint8_t hasExtendedData() const { return hasED_; }

protected:
  std::string blockName() const override { return "TrackerHeader"; }

  void setSpecificValue() override {
    hasED_ = (values_[0] >> (HEADER_COREID_BITS + HEADER_RESERVED_BITS)) & ((1u << HEADER_ED_BITS) - 1);
  }

private: 
  uint8_t hasED_{0}; // indicates if extended data are present

};



class TrackerTrailer : public TrackerBlock {
public:
  TrackerTrailer() : TrackerBlock(TRAILER_N_LINES) {}

  uint8_t afterExtendedData() const { return endED_; }

protected:
  std::string blockName() const override { return "TrackerTrailer"; }

  void setSpecificValue() override {
    endED_ = (values_[0]) & ((1u << TRAILER_ENDED_BITS) - 1);
  }

private:
  // indicates if trailer is at end of a normal or extended data
  uint8_t endED_{0};  
  
};

#endif