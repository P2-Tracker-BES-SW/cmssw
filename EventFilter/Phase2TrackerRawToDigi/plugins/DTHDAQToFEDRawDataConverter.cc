// A utility to read and parse a .raw orbit aggregation file from the DTH,
// and convert all event fragments belonging to the same event ID into
// one FEDRawDataCollection per CMSSW event.

#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/Run.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include <fstream>
#include <vector>
#include <map>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <sstream>
#include <algorithm>

#include "EventFilter/Phase2TrackerRawToDigi/interface/DTHOrbitFieldSizes.h"

uint64_t readLittleEndian(const char* data, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
    }
    return value;
}

// Represents a single fragment (part of a full event).
struct FragmentData {
    unsigned int orbitIdx = 0;
    uint32_t runNumber = 0;
    uint32_t orbitNumber = 0;
    uint32_t sourceId = 0;
    uint16_t fragFlags = 0;
    uint32_t fragSize = 0;
    uint64_t eventId = 0;
    uint16_t crc = 0;
    std::vector<char> payloadBytes;
};

class DTHDAQToFEDRawDataConverter : public edm::one::EDProducer<> {
public:
    explicit DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config);
    ~DTHDAQToFEDRawDataConverter() override = default;

    void beginJob() override;
    void produce(edm::Event& event, const edm::EventSetup&) override;

private:
    std::string inputFile_;

    // Store all fragments grouped by eventId
    std::map<uint64_t, std::vector<FragmentData>> eventIdToFragments_;
    std::map<uint64_t, std::vector<FragmentData>>::iterator currentEventIt_;

    std::vector<char> readRawFile(const std::string& inputFile);
    void parseAllOrbitsAndFragments(const std::vector<char>& buffer);
    void printHex(const std::vector<char>& buffer, size_t maxLength);
};

DTHDAQToFEDRawDataConverter::DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config)
    : inputFile_(config.getParameter<std::string>("inputFile"))
{
    produces<FEDRawDataCollection>();
}

void DTHDAQToFEDRawDataConverter::beginJob() {
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Reading raw file: " << inputFile_;
    std::vector<char> buffer = readRawFile(inputFile_);
    parseAllOrbitsAndFragments(buffer);
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << "Total unique eventIds found: " << eventIdToFragments_.size();
    currentEventIt_ = eventIdToFragments_.begin();
}

void DTHDAQToFEDRawDataConverter::produce(edm::Event& event, const edm::EventSetup&) {
    if (currentEventIt_ == eventIdToFragments_.end()) {
        edm::LogWarning("DTHDAQToFEDRawDataConverter") << "No more event groups to produce.";
        return;
    }

    uint64_t eventId = currentEventIt_->first;
    const auto& fragments = currentEventIt_->second;

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Producing CMSSW event for eventId=" << eventId
        << " with " << fragments.size() << " fragments.";

    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

    for (const auto& frag : fragments) {
        FEDRawData& fedData = fedRawDataCollection->FEDData(frag.sourceId);
        fedData.resize(frag.payloadBytes.size());
        std::copy(frag.payloadBytes.begin(), frag.payloadBytes.end(), fedData.data());
    }

    event.put(std::move(fedRawDataCollection));
    ++currentEventIt_;
}

std::vector<char> DTHDAQToFEDRawDataConverter::readRawFile(const std::string& inputFile) {
    std::ifstream rawFile(inputFile, std::ios::binary | std::ios::ate);
    if (!rawFile.is_open()) {
        throw cms::Exception("FileOpenError") << "Could not open input file: " << inputFile;
    }

    std::streamsize fileSize = rawFile.tellg();
    rawFile.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    if (!rawFile.read(buffer.data(), fileSize)) {
        throw cms::Exception("FileReadError") << "Could not read input file: " << inputFile;
    }

    rawFile.close();
    return buffer;
}

void DTHDAQToFEDRawDataConverter::printHex(const std::vector<char>& buffer, size_t maxLength) {
    std::ostringstream hexOutput;
    hexOutput << "Raw bitstream (up to " << maxLength << " bytes): ";
    size_t length = std::min(buffer.size(), maxLength);
    for (size_t i = 0; i < length; ++i) {
        hexOutput << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(static_cast<unsigned char>(buffer[i])) << " ";
    }
    edm::LogInfo("DTHDAQToFEDRawDataConverter") << hexOutput.str();
}

// Parse entire .raw file buffer into fragments grouped by eventId
void DTHDAQToFEDRawDataConverter::parseAllOrbitsAndFragments(const std::vector<char>& buffer) {
    size_t startIdx = 0;
    unsigned int orbitIdx = 0;

    while (startIdx < buffer.size()) {
        if (buffer.size() - startIdx < orbitHeaderSize) break;
        if (buffer[startIdx] != orbitHeaderMarkerH || buffer[startIdx + 1] != orbitHeaderMarkerO) break;
        startIdx += 2;

        uint16_t version = readLittleEndian(&buffer[startIdx], orbitVersionSize); startIdx += orbitVersionSize;
        uint32_t sourceId = readLittleEndian(&buffer[startIdx], sourceIdSize); startIdx += sourceIdSize;
        uint32_t runNumber = readLittleEndian(&buffer[startIdx], runNumberSize); startIdx += runNumberSize;
        uint32_t orbitNumber = readLittleEndian(&buffer[startIdx], orbitNumberSize); startIdx += orbitNumberSize;
        uint32_t eventCountReserved = readLittleEndian(&buffer[startIdx], eventCountResSize);
        uint16_t eventCount = eventCountReserved & 0xFFF; startIdx += eventCountResSize;
        uint32_t packetWordCount = readLittleEndian(&buffer[startIdx], packetWordCountSize); startIdx += packetWordCountSize;
        uint32_t flags = readLittleEndian(&buffer[startIdx], flagsSize); startIdx += flagsSize;
        uint32_t checksum = readLittleEndian(&buffer[startIdx], checksumSize); startIdx += checksumSize;


        edm::LogInfo("DTHDAQToFEDRawDataConverter")
            << "Orbit " << (orbitIdx + 1)
            << ": Version=" << version
            << ", SourceID=" << sourceId
            << ", RunNumber=" << runNumber
            << ", OrbitNumber=" << orbitNumber
            << ", EventCount=" << eventCount
            << ", PacketWordCount=" << packetWordCount
            << ", Flags=" << flags
            << ", Checksum=" << checksum;

        size_t orbitDataSizeBytes = packetWordCount * fragmentPayloadWordSize - orbitHeaderSize;
        size_t orbitDataEnd = startIdx + orbitDataSizeBytes;
        if (orbitDataEnd > buffer.size()) break;
        size_t currentPos = orbitDataEnd;
        startIdx += orbitDataSizeBytes;

        for (unsigned int fragIdx = 0; fragIdx < eventCount; ++fragIdx) {
            if (currentPos < fragmentTrailerSize) break;
            size_t trailerPos = currentPos - fragmentTrailerSize;

            if (buffer[trailerPos] != fragmentTrailerMarkerT || buffer[trailerPos + 1] != fragmentTrailerMarkerF) break;

            uint16_t fragFlags = readLittleEndian(&buffer[trailerPos + fragFlagSize], fragFlagSize);
            uint32_t fragSize = readLittleEndian(&buffer[trailerPos + fragSizeSize], fragSizeSize);
            uint64_t eventId = readLittleEndian(&buffer[trailerPos + trailerOffsetEventId], eventIdSize) & eventIdMask;
            uint16_t crc = readLittleEndian(&buffer[trailerPos + trailerOffsetCRC], crcSize);

            size_t payloadSizeBytes = fragSize * fragmentPayloadWordSize;
            if (trailerPos < payloadSizeBytes) break;
            size_t payloadStart = trailerPos - payloadSizeBytes;

            FragmentData frag;
            frag.orbitIdx = orbitIdx + 1;
            frag.runNumber = runNumber;
            frag.orbitNumber = orbitNumber;
            frag.sourceId = sourceId;
            frag.fragFlags = fragFlags;
            frag.fragSize = fragSize;
            frag.eventId = eventId;
            frag.crc = crc;
            frag.payloadBytes.assign(buffer.begin() + payloadStart, buffer.begin() + payloadStart + payloadSizeBytes);

            eventIdToFragments_[eventId].emplace_back(std::move(frag));
            currentPos = payloadStart;
        }

        ++orbitIdx;
    }
}

DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
