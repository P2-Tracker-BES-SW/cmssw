// Empty analyzer skeleton for inspecting FEDRawDataCollection
// By @amastron (A. Mastronikolis), July 2026

#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"

#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/DTCHeader.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/ChannelsMask.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/DTCTrailer.h"

#include <iostream>
#include <tuple>

class FormatInspector : public edm::one::EDAnalyzer<> {
public:
  FormatInspector(const edm::ParameterSet& pset);
  ~FormatInspector() override = default;
  void analyze(const edm::Event&, const edm::EventSetup&) override;

private:
  edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
  uint32_t get32bWordAtLine(const unsigned char*& data, size_t index, bool debug);
  void dumpPacket(const FEDRawData&);
  size_t getNumberOf32bWords(size_t dataSize);
  DTCHeader getDTCHeader(const unsigned char*& data);
  ChannelsMask getChannelMaskingProfile(const unsigned char*& data);
  std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> extractFields(uint32_t data);

  bool verbose_;
  
};

FormatInspector::FormatInspector(const edm::ParameterSet& pset)
    : fedRawDataToken_(consumes<FEDRawDataCollection>(
          pset.getParameter<edm::InputTag>("fedRawDataCollectionTag"))),
      verbose_(pset.getParameter<bool>("Debug"))  // default false if not specified
{}

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

            if (verbose_) {
                edm::LogInfo("FormatInspector") 
                    << "FED ID: " << fedId 
                    << " Size: " << dataSize << " bytes";
                edm::LogInfo("FormatInspector")
                    << "CMSSW Unpacker Version: v" << Phase2DAQFormatSpecification::VERSION_MAJOR << "." << Phase2DAQFormatSpecification::VERSION_MINOR << ".0";
            }

            if (verbose_) {
                dumpPacket(fedData);
            }

            if (verbose_) {
                for (size_t LineID = 0; LineID < getNumberOf32bWords(dataSize); LineID++) {
                    uint32_t word_at_line_id = get32bWordAtLine(data, LineID, false);
                    printf("Word @ Line ID %05lu: %08X \n", LineID, (unsigned int)word_at_line_id);
                }
            }

            DTCHeader ExtractedDTCHeader = getDTCHeader(data);
            if (verbose_) ExtractedDTCHeader.printFields();

            if (Phase2DAQFormatSpecification::VERSION_MAJOR == ExtractedDTCHeader.getVersionMajor() && 
                Phase2DAQFormatSpecification::VERSION_MINOR == ExtractedDTCHeader.getVersionMinor()) {
                if (verbose_) edm::LogInfo("FormatInspector") << "Found Perfect Match. CMSSW Can Decode the Binary.";
            }

            ChannelsMask ExtractedChannelsMask = getChannelMaskingProfile(data);
            if (verbose_) ExtractedChannelsMask.print();
            if (verbose_) ExtractedChannelsMask.printSummary();

            if (verbose_) {
                for (size_t i = 0; i < 35; i++) {
                    printf("Channel %02lu Masked? %s. Ignore Offset: %s\n", 
                    i,
                    ExtractedChannelsMask.isChannelMasked(i) ? "Yes" : "No ",
                    ExtractedChannelsMask.isChannelMasked(i) ? "Yes" : "No ");
                }
            }

            std::vector<uint16_t> offset_words; 
            offset_words.push_back(0); // channel #0 always @ 0.

            for (size_t i = 8; i < 26; i++) {
                uint32_t word32b = get32bWordAtLine(data, i, false);
                // offset low
                uint16_t low  = static_cast<uint16_t>(word32b & 0xFFFF);
                // offset high
                uint16_t high = static_cast<uint16_t>((word32b >> 16) & 0xFFFF);
                offset_words.push_back(high);
                offset_words.push_back(low);
            }

            if (verbose_) printf("Extracting CIC Headers from Non-Masked Channels\n");
            unsigned long channels_num = 0;
            double previous_end = -1;
            for (size_t i = 0; i < 35; i++) {
                if (!ExtractedChannelsMask.isChannelMasked(i)) {
                    uint32_t word = get32bWordAtLine(data, 28 + offset_words[i], false);
                    auto [L1ID, feError, pixelClusters, stripClusters] = extractFields(word);
                    const double start = 28 + offset_words[i];
                    const double end = start + std::ceil(stripClusters * 14.0 / 32.0);
                    if (previous_end != -1 && start != previous_end + 1) {
                        throw std::runtime_error("Non-contiguous intervals detected!");
                    }
                    previous_end = end;
                    channels_num++;
                }
            }
            if (verbose_) printf("Found %02lu Channels Active!\n", channels_num);
        }
    }

    return;
}

std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> FormatInspector::extractFields(uint32_t data) {
    uint32_t L1ID = (data >> 23) & 0x1FF;           // 9 bits (31-23)
    uint32_t feErrorBits = (data >> 14) & 0x1FF;    // 9 bits (22-14)
    uint32_t numPixelClusters = (data >> 7) & 0x7F; // 7 bits (13-07)
    uint32_t numStripClusters = data & 0x7F;        // 7 bits (06-00)
    
    return {L1ID, feErrorBits, numPixelClusters, numStripClusters};
}

/**
 * @brief Dumps the entire DAQ Packet, in hexdump -C view.
 * @return void
 */
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

/**
 * @brief Retrives a specific 32b word from the DAQ Payload.
 * @param data Pointer to the raw data buffer (passed by reference)
 * @param LineID Carefully defined in this google sheet
 * https://docs.google.com/spreadsheets/d/1RHZFqeHCoJhRaAfaKEO1Gx6U6c1Y3tRGhL_aSbZQROY/edit?gid=256168213#gid=256168213
 * @return 32bit word. MSB on the far left. LSB on the far right.
 */
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

/**
 * @brief Returns the number of 32b words in the DAQ Payload. This includes the SLink Header Words.
 * @param dataSize the size of the FEDRawData in bytes.
 * @note DAQ Packets are always multiples of 32bits. So the return below should be well defined for all cases.
 * @return Returns the number of 32b words in the DAQ Payload
 */
size_t FormatInspector::getNumberOf32bWords(size_t dataSize) {
    return dataSize / 4;  // Each 32-bit word is 4 bytes long
}

/**
 * @brief Returns DTC Header from the DAQ Packet.
 * @param data Pointer to the raw data buffer (passed by reference)
 * @return DTCHeader Class Object.
 */
DTCHeader FormatInspector::getDTCHeader(const unsigned char*& data) {
    std::array<uint32_t, 4> words;
    for (int i = 0; i < Phase2DAQFormatSpecification::DTC_HEADER_SIZE; ++i) {
        words[i] = get32bWordAtLine(data, Phase2DAQFormatSpecification::DTC_HEADER_OFFSET + i);
    }
    DTCHeader captureHeader(words);
    return captureHeader;
}

/**
 * @brief Returns the Channel Masking for this Run
 * @param data Pointer to the raw data buffer (passed by reference)
 * @return ChannelsMask Class Object.
 */
ChannelsMask FormatInspector::getChannelMaskingProfile(const unsigned char*& data) {
    std::array<uint32_t, 2> words;
    for (int i = 0; i < Phase2DAQFormatSpecification::DTC_CHANNEL_MASK_SIZE; ++i) {
        words[i] = get32bWordAtLine(data, Phase2DAQFormatSpecification::DTC_CHANNEL_MASK_OFFSET + i);
    }
    ChannelsMask captureMasking(words);
    return captureMasking;
}

DEFINE_FWK_MODULE(FormatInspector);