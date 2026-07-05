// Empty analyzer skeleton for inspecting FEDRawDataCollection
// By [Your Name], July 2026

#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"

#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/DTCHeader.h"

#include <iostream>

class FormatInspector : public edm::one::EDAnalyzer<> {
public:
  FormatInspector(const edm::ParameterSet& pset);
  ~FormatInspector() override = default;
  void analyze(const edm::Event&, const edm::EventSetup&) override;

private:
  edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
  uint32_t get32bWordAtLine(const unsigned char*& data, size_t index, bool debug);
  void dumpPacket(const FEDRawData&);
  size_t getNumberOf32bWords(const unsigned char*& data, size_t dataSize);
  DTCHeader getDTCHeader(const unsigned char*& data);
  
};

FormatInspector::FormatInspector(const edm::ParameterSet& pset)
    : fedRawDataToken_(consumes<FEDRawDataCollection>(
          pset.getParameter<edm::InputTag>("fedRawDataCollectionTag"))) {std::cout << "Hi" << std::endl;}

void FormatInspector::analyze(const edm::Event& event, const edm::EventSetup& es) {

    edm::Handle<FEDRawDataCollection> fedRawDataCollection;
    event.getByToken(fedRawDataToken_, fedRawDataCollection);

    if (!fedRawDataCollection.isValid()) {
        return;
    }

    for (int fedId = 0; fedId < 216; ++fedId) {
        const FEDRawData& fedData = fedRawDataCollection->FEDData(fedId);
        
        if (fedData.size() > 0) {

            const unsigned char* data = fedData.data();
            size_t dataSize = fedData.size();

            edm::LogInfo("FormatInspector") 
                << "FED ID: " << fedId 
                << " Size: " << dataSize << " bytes";

            edm::LogInfo("FormatInspector")
                << "CMSSW Unpacker Version: v" << Phase2DAQFormatSpecification::VERSION_MAJOR << "." << Phase2DAQFormatSpecification::VERSION_MINOR << ".0";

            DTCHeader ExtractedDTCHeader = getDTCHeader(data);
            ExtractedDTCHeader.printFields();

            if (Phase2DAQFormatSpecification::VERSION_MAJOR == ExtractedDTCHeader.getVersionMajor() && Phase2DAQFormatSpecification::VERSION_MINOR == ExtractedDTCHeader.getVersionMinor()) {
                edm::LogInfo("FormatInspector") << "Found Perfect Match. CMSSW Can Decode the Binary.";
            }

        }

    }

    return;
}

void FormatInspector::dumpPacket(const FEDRawData& fedData) {
    const unsigned char* data = fedData.data();
    size_t dataSize = fedData.size();
    for (size_t l16byteslineID = 0; l16byteslineID < (dataSize + 15) / 16; l16byteslineID++) {
        for (size_t byte_within_line = 0; byte_within_line < 16; byte_within_line++) {
            size_t index = l16byteslineID * 16 + byte_within_line;
            if (index >= dataSize) break;  // Stop if we've printed all bytes
            printf("%02X ", (unsigned int)data[index]);
        }
        printf("\n");
    }
}

uint32_t FormatInspector::get32bWordAtLine(const unsigned char*& data, size_t LineID, bool debug = false) {
    int group = LineID / 4;
    int offset = LineID % 4;
    int reversedOffset = 3 - offset;
    size_t reversedWordIndex = group * 4 + reversedOffset;
    
    size_t byteOffset = reversedWordIndex * 4;
    uint32_t word = (static_cast<uint32_t>(data[byteOffset + 3]) << 24) |
                    (static_cast<uint32_t>(data[byteOffset + 2]) << 16) |
                    (static_cast<uint32_t>(data[byteOffset + 1]) << 8)  |
                    (static_cast<uint32_t>(data[byteOffset + 0]));
    if (debug) {
        printf("%08X \n", (unsigned int)word);
    }            
    return word;
}

size_t FormatInspector::getNumberOf32bWords(const unsigned char*& data, size_t dataSize) {
    return dataSize / 4;  // Each 32-bit word is 4 bytes
}

DTCHeader FormatInspector::getDTCHeader(const unsigned char*& data) {
    std::array<uint32_t, 4> words;
    for (int i = 0; i < Phase2DAQFormatSpecification::DTC_HEADER_SIZE; ++i) {
        words[i] = get32bWordAtLine(data, Phase2DAQFormatSpecification::DTC_HEADER_OFFSET + i);
    }
    DTCHeader captureHeader(words);
    return captureHeader;
}

DEFINE_FWK_MODULE(FormatInspector);