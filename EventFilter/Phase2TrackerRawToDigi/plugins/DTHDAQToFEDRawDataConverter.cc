// A utility to read and parse a .raw orbit aggregasion file from the DTH, convert each event fragment to a FEDRawData entry
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
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <sstream>
#include <algorithm>

// Include the constants for bit field widths, markers, and size in BYTES:
#include "EventFilter/Phase2TrackerRawToDigi/interface/DTHOrbitFieldSizes.h"

// helper for endianness, the LXPLUS architecture is little-endian, so is the raw data from the DTH
uint64_t readLittleEndian(const char* data, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value |= (static_cast<uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i));
    }
    return value;
}

/**
 * A small struct to hold the data for ONE 'fragment' (i.e., an event).
 * We will produce exactly one CMSSW event per fragment.
 * 
 */
struct FragmentData {
    // Orbit-level info (for logging/diagnostics)
    unsigned int orbitIdx    = 0;  
    uint32_t runNumber       = 0;  
    uint32_t orbitNumber     = 0;
    uint32_t sourceId        = 0;
    uint16_t fragFlags       = 0;  
    uint32_t fragSize        = 0;  
    uint64_t eventId         = 0;  
    uint16_t crc             = 0;  

    // The actual binary payload for this fragment
    std::vector<char> payloadBytes; 
};

class DTHDAQToFEDRawDataConverter : public edm::one::EDProducer<> {
public:
    explicit DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config);
    ~DTHDAQToFEDRawDataConverter() override = default;

    // Called once at job start; we parse the entire file here
    void beginJob() override;

    // Called once per CMSSW event; we publish the "next" fragment each time
    void produce(edm::Event& event, const edm::EventSetup&) override;

private:
    // Configuration
    std::string  inputFile_;
   

    // We store all parsed fragments from the raw file:
    std::vector<FragmentData> allFragments_;

    // Index for the next fragment to publish as a CMSSW event
    size_t currentFragmentIndex_ = 0;

    // Helper: read the entire file into memory
    std::vector<char> readRawFile(const std::string& inputFile);

    // Helper: parse the entire buffer into orbits/fragments (once per job)
    void parseAllOrbitsAndFragments(const std::vector<char>& buffer);

    // Optional debug print
    void printHex(const std::vector<char>& buffer, size_t maxLength);
};

// Constructor: read parameters, declare our output
DTHDAQToFEDRawDataConverter::DTHDAQToFEDRawDataConverter(const edm::ParameterSet& config)
    : inputFile_(config.getParameter<std::string>("inputFile"))
{
    // We will produce a FEDRawDataCollection each time produce(...) is called.
    // Since this is an EDProducer and the cfg file uses "EmptySource" to read from .raw file, 
    // produce() is called as many times as "maxEvent" is set to
    produces<FEDRawDataCollection>();
}

// 1) Read and parse the file in beginJob() rather than in produce()
void DTHDAQToFEDRawDataConverter::beginJob() {
    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "====> beginJob(): Reading and parsing the entire raw file once.";

    // Read the raw file into a memory buffer
    std::vector<char> buffer = readRawFile(inputFile_);

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Raw data read: " << buffer.size() << " bytes from: " << inputFile_;

    // Debug: Print first 64 bytes
    printHex(buffer, 64);

    // Parse all orbits/fragments and store in allFragments_
    parseAllOrbitsAndFragments(buffer);

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "====> Completed parsing. Total fragments found: " 
        << allFragments_.size();
}

// 2) For each CMSSW event, we take the next fragment from allFragments_
//    and produce a FEDRawDataCollection.
void DTHDAQToFEDRawDataConverter::produce(edm::Event& event, const edm::EventSetup&) {
    // Check if we have any fragments left
    if (currentFragmentIndex_ >= allFragments_.size()) {
        // We are out of fragments to produce. 
        edm::LogWarning("DTHDAQToFEDRawDataConverter")
            << "[DTHDAQToFEDRawDataConverter] No more fragments left to produce. "
            << "Already produced " << currentFragmentIndex_ << " events.";
        return;    
    }

    // Get the next fragment data
    const FragmentData& frag = allFragments_.at(currentFragmentIndex_);

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Producing event for Fragment #" << currentFragmentIndex_
        << " (Orbit " << frag.orbitIdx 
        << ", orbitNumber=" << frag.orbitNumber
        << ", eventId=" << frag.eventId << ") "
        << "with payload size=" << frag.payloadBytes.size() << " bytes.";

    // Create a FEDRawDataCollection
    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

    // Put the fragment payload into the sourceId slot (used as fedId_)
    FEDRawData& fedData = fedRawDataCollection->FEDData(frag.sourceId);
    fedData.resize(frag.payloadBytes.size());
    std::copy(frag.payloadBytes.begin(), frag.payloadBytes.end(), fedData.data());

    // Put the collection into the event
    event.put(std::move(fedRawDataCollection));

    // Move on to the next fragment
    currentFragmentIndex_++;
}

// Helper to read the entire file into a std::vector<char>
std::vector<char> DTHDAQToFEDRawDataConverter::readRawFile(const std::string& inputFile) {
    std::ifstream rawFile(inputFile, std::ios::binary | std::ios::ate);
    if (!rawFile.is_open()) {
        throw cms::Exception("FileOpenError") 
            << "Could not open input file: " << inputFile;
    }

    std::streamsize fileSize = rawFile.tellg();
    rawFile.seekg(0, std::ios::beg);

    std::vector<char> buffer(fileSize);
    if (!rawFile.read(buffer.data(), fileSize)) {
        throw cms::Exception("FileReadError") 
            << "Could not read input file: " << inputFile;
    }

    rawFile.close();
    return buffer;
}

// A debug function to print up to maxLength bytes in hex
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

/**
 * 3) Parse the entire buffer, find all orbits, then for each orbit, find all fragments,
 *    and store them in allFragments_ so that each fragment can become a CMSSW event.
 */
void DTHDAQToFEDRawDataConverter::parseAllOrbitsAndFragments(const std::vector<char>& buffer) {
    size_t startIdx = 0;
    unsigned int orbitIdx = 0;  // count the orbits read

    // Loop until we run out of data in the buffer
    while (startIdx < buffer.size()) {
        // (1) Check if there is enough data for an orbit header
        if (buffer.size() - startIdx < orbitHeaderSize) {
            edm::LogWarning("DTHDAQToFEDRawDataConverter")
                << "Not enough data for an Orbit Header at orbitIdx=" << (orbitIdx + 1)
                << ". Ending orbit parsing.";
            break;
        }

        // (2) Read orbit header markers ('H','O') and verify them
        uint8_t markerH = static_cast<uint8_t>(buffer[startIdx]);
        uint8_t markerO = static_cast<uint8_t>(buffer[startIdx + 1]);
        if (markerH != orbitHeaderMarkerH || markerO != orbitHeaderMarkerO) {
            edm::LogError("DTHDAQToFEDRawDataConverter")
                << "Invalid Orbit Header marker at orbitIdx=" << (orbitIdx + 1)
                << " (startIdx=" << startIdx << ").";
            break;  // or consider skipping this orbit if appropriate
        }
        startIdx += 2;  // consume the marker bytes

        // (3) Parse orbit header fields
        uint16_t version = static_cast<uint16_t>(readLittleEndian(&buffer[startIdx], orbitVersionSize));
        startIdx += orbitVersionSize;
        uint32_t sourceId = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], sourceIdSize));
        startIdx += sourceIdSize;
        uint32_t runNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], runNumberSize));
        startIdx += runNumberSize;
        uint32_t orbitNumber = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], orbitNumberSize));
        startIdx += orbitNumberSize;
        uint32_t eventCountReserved = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], eventCountResSize));
        uint16_t eventCount = eventCountReserved & 0xFFF;  // assume a 12-bit event count
        startIdx += eventCountResSize;
        uint32_t packetWordCount = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], packetWordCountSize));
        startIdx += packetWordCountSize;
        uint32_t flags = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], flagsSize));
        startIdx += flagsSize;
        uint32_t checksum = static_cast<uint32_t>(readLittleEndian(&buffer[startIdx], checksumSize));
        startIdx += checksumSize;

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

        // (4) Determine orbit data size (in bytes)
        // (Assuming packetWordCount is in 16-byte words and subtracting the orbit header size)
        size_t orbitDataSizeBytes = static_cast<size_t>(packetWordCount) * fragmentPayloadWordSize - orbitHeaderSize;
        size_t orbitDataStart = startIdx;
        size_t orbitDataEnd = orbitDataStart + orbitDataSizeBytes;
        if (orbitDataEnd > buffer.size()) {
            edm::LogError("DTHDAQToFEDRawDataConverter")
                << "Orbit " << (orbitIdx + 1) << " claims " << orbitDataSizeBytes
                << " bytes, but only " << (buffer.size() - startIdx) << " bytes remain.";
            break;
        }
        startIdx += orbitDataSizeBytes;  // advance pointer to end of orbit

        // (5) Parse fragments within this orbit (in reverse order)
        std::vector<FragmentData> orbitFragments;
        orbitFragments.reserve(eventCount);
        size_t currentPos = orbitDataEnd;
        edm::LogInfo("DTHDAQToFEDRawDataConverter")
            << "Parsing " << eventCount << " fragments in reverse for orbit " << (orbitIdx + 1);
        for (unsigned int fragIdx = 0; fragIdx < eventCount; ++fragIdx) {
            // (a) Check if there is enough data for a fragment trailer
            if (currentPos < fragmentTrailerSize) {
                edm::LogError("DTHDAQToFEDRawDataConverter")
                    << "Not enough data for Fragment Trailer in orbit " << (orbitIdx + 1)
                    << ", fragment #" << (fragIdx + 1);
                break;
            }
            size_t trailerPos = currentPos - fragmentTrailerSize;

            // (b) Verify trailer markers
            uint8_t markerT_trailer = static_cast<uint8_t>(buffer[trailerPos]);
            uint8_t markerF_trailer = static_cast<uint8_t>(buffer[trailerPos + 1]);
            if (markerT_trailer != fragmentTrailerMarkerT || markerF_trailer != fragmentTrailerMarkerF) {
                edm::LogError("DTHDAQToFEDRawDataConverter")
                    << "Invalid Fragment Trailer marker in orbit " << (orbitIdx + 1)
                    << ", fragment #" << (fragIdx + 1);
                break;
            }

            // (c) Parse trailer fields
            uint16_t fragFlags = static_cast<uint16_t>(readLittleEndian(&buffer[trailerPos + fragFlagSize], fragFlagSize));
            uint32_t fragSize  = static_cast<uint32_t>(readLittleEndian(&buffer[trailerPos + fragSizeSize], fragSizeSize));
            uint64_t eventId   = readLittleEndian(&buffer[trailerPos + 8], 6) & 0xFFFFFFFFFFFULL; //The event ID is a 44-bit (5.5 bytes) field, eventID+res+CRC = 8 bytes
            //                                                                                    0xFFFFFFFFFFFULL is a mask to keep the  first 44 bits (5.5 bytes) out of the 48 bits (6 bytes), 
            //                                                                                    the other 4 bits (0.5 bytes)are reserved and this code does not extract them
            uint16_t crc       = static_cast<uint16_t>(readLittleEndian(&buffer[trailerPos + 14], 2)); // the buffer index did not update in the last statment, this line updates it: 8+6 = 14 bytes
            //                                                                                    CRC checksum is 16-bit (2-byte) field. 16 +44 + 4 (reserved) = 64 bits (8 bytes)

            // (d) Calculate payload size in bytes
            size_t payloadSizeBytes = fragSize * fragmentPayloadWordSize;
            if (trailerPos < payloadSizeBytes) {
                edm::LogError("DTHDAQToFEDRawDataConverter")
                    << "Not enough space for payload in orbit " << (orbitIdx + 1)
                    << ", fragment #" << (fragIdx + 1)
                    << ". trailerPos=" << trailerPos
                    << " < payloadSizeBytes=" << payloadSizeBytes;
                break;
            }
            size_t payloadStart = trailerPos - payloadSizeBytes;

            // (e) Create and fill a FragmentData struct
            FragmentData fdata;
            fdata.orbitIdx    = orbitIdx + 1;
            fdata.runNumber   = runNumber;
            fdata.orbitNumber = orbitNumber;
            fdata.sourceId    = sourceId;
            fdata.fragFlags   = fragFlags;
            fdata.fragSize    = fragSize;
            fdata.eventId     = eventId;
            fdata.crc         = crc;
            fdata.payloadBytes.resize(payloadSizeBytes);
            std::copy(buffer.begin() + payloadStart,
                      buffer.begin() + payloadStart + payloadSizeBytes,
                      fdata.payloadBytes.begin());

            orbitFragments.push_back(std::move(fdata));

            // Update currentPos to the beginning of this fragment (payload + trailer)
            currentPos = payloadStart;
        }

        // The fragments were read in reverse order; reverse them to restore natural order
        std::reverse(orbitFragments.begin(), orbitFragments.end());
        // Add all the fragments from this orbit to the global list
        allFragments_.insert(allFragments_.end(), orbitFragments.begin(), orbitFragments.end());

        ++orbitIdx;
    }

    edm::LogInfo("DTHDAQToFEDRawDataConverter")
        << "Completed parsing: total orbits read = " << orbitIdx
        << "; total fragments stored = " << allFragments_.size();
}


DEFINE_FWK_MODULE(DTHDAQToFEDRawDataConverter);
