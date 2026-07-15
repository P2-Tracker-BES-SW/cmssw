// EDProducer unpacking FEDRawData to create an output collection of Phase2TrackerCluster1D
// for the Phase2 Outer Tracker

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/CommonTopologies/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"
#include <unordered_map>

#include "EventFilter/Phase2TrackerRawToDigi/interface/TrackerHeader.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/ChannelsOffset.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2TrackerSpecifications;
using namespace Phase2DAQFormatSpecification;

// namespace to be added

class RawToClusterProducer : public edm::stream::EDProducer<> {
public:
  explicit RawToClusterProducer(const edm::ParameterSet&);
  ~RawToClusterProducer() override;
  void beginRun(const edm::Run&, const edm::EventSetup&) override;

  uint32_t readLine(const unsigned char* dataPtr, int lineIdx);
  uint32_t get32bWordAtByte(const unsigned char*& data, size_t initByte, size_t startByte, bool debug /* = false */) ;

  void readPayload(std::vector<uint32_t>& clusterWords,
                   std::vector<uint32_t>& lines,
                   int numClusters,
                   int& nAvailableBits,
                   int& iLine,
                   int& bitsToRead,
                   int& nFullClusters,
                   int clusterBits,
                   int clusterWordMask,
                   bool isPixelCluster,
                   int nFullClustersStrips = 0);

  int createMask(int nBits);
  std::pair<Phase2TrackerCluster1D, bool> unpack2S(uint32_t, unsigned int);
  Phase2TrackerCluster1D unpackStripOnPS(uint32_t, unsigned int);
  Phase2TrackerCluster1D unpackPixelOnPS(uint32_t, unsigned int);
  
  void dumpRawFile(const unsigned char*, size_t, bool);

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  const edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> trackerGeometryToken_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> trackerTopologyToken_;
  edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> OutputClusterCollectionToken_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;
  const TrackerGeometry* trackerGeometry_ = nullptr;
  const TrackerTopology* trackerTopology_ = nullptr;
  std::map<int, std::pair<int, int>> stackMap_;
};

RawToClusterProducer::RawToClusterProducer(const edm::ParameterSet& iConfig)
    : fedRawDataToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
      cablingMapToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      trackerGeometryToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord, edm::Transition::BeginRun>()),
      trackerTopologyToken_(esConsumes<TrackerTopology, TrackerTopologyRcd, edm::Transition::BeginRun>()) {
  produces<Phase2TrackerCluster1DCollectionNew>();
}

RawToClusterProducer::~RawToClusterProducer() {}

void RawToClusterProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
  // get cabling from event setup
  cablingMap_ = &iSetup.getData(cablingMapToken_);

  // from the OLD CODE
  // FIXME: build map of stacks to compensate for missing trackertopology methods
  trackerGeometry_ = &iSetup.getData(trackerGeometryToken_);
  trackerTopology_ = &iSetup.getData(trackerTopologyToken_);

  for (auto iu = trackerGeometry_->detUnits().begin(); iu != trackerGeometry_->detUnits().end(); ++iu) {
    unsigned int detId_raw = (*iu)->geographicalId().rawId();
    DetId detId = DetId(detId_raw);
    if (detId.det() == DetId::Detector::Tracker) {
      // build map of upper and lower for each module
      if (trackerTopology_->isLower(detId) != 0) {
        stackMap_[trackerTopology_->stack(detId)].first = detId;
      }
      if (trackerTopology_->isUpper(detId) != 0) {
        stackMap_[trackerTopology_->stack(detId)].second = detId;
      }
    }
  }  // end loop on detunits
}

void RawToClusterProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  auto outputClusterCollection = std::make_unique<Phase2TrackerCluster1DCollectionNew>();

  edm::Handle<FEDRawDataCollection> fedRawDataCollection;
  iEvent.getByToken(fedRawDataToken_, fedRawDataCollection);

  TrackerHeader theHeader;
  ChannelsOffset theOffsets;

  if (!fedRawDataCollection.isValid()) {
    edm::LogError("RawtoClusterProducer") << "ERROR: No FEDRawDataCollection found!";
    return;
  }

  // Read one entire DTC (#dtcID)
  for (int dtcID = MIN_DTC_ID; dtcID < MAX_DTC_ID + 1; dtcID++) {
    // read the 4 slinks
    for (unsigned int iSlink = 0; iSlink < SLINKS_PER_DTC; iSlink++) {
      // as defined in the DAQProducer code
      unsigned totID = iSlink + SLINKS_PER_DTC * (dtcID - 1) + CMSSW_TRACKER_ID;
      const FEDRawData& fedData = fedRawDataCollection->FEDData(totID);
      if (fedData.size() > 0 ) {

        const unsigned char* dataPtr = fedData.data();
        
//         dumpRawFile(dataPtr, fedData.size(), false);
//         dumpRawFile(dataPtr, fedData.size(), true);

        // double check if we see the BOE magic word in the S-link header
        bool isBOE = (static_cast<uint8_t>(dataPtr[SLINK_HEADER_BYTES-1]) == SLINK_BOE) ? true : false;
        if (!isBOE)
            edm::LogWarning("RawToClusterProducer") 
                << "WARNING: Couldn't find BOE in the S-Link Header " ;

        // read the header
        std::vector<uint32_t> headerWords;
        /// skip the first 128bits, corresponding to the S-link header
        for (size_t i = SLINK_HEADER_BYTES; i < HEADER_N_LINES * N_BYTES_PER_WORD + SLINK_HEADER_BYTES;
             i += N_BYTES_PER_WORD)  // Read 4 bytes (32 bits) at a time
        {
          // Extract 4 bytes (32 bits) and pack them into a uint32_t word
          headerWords.push_back(get32bWordAtByte(dataPtr, i, SLINK_HEADER_BYTES, false));
          
        }
        theHeader.setValue(headerWords);

        // read the offsets: each 32 bit word contains two offset words of 16 bit each
        std::vector<uint64_t> offsetWords; 
        
        size_t nOffsetsLines = OFFSET_BITS * CICs_PER_SLINK / N_BITS_PER_WORD ; 
        size_t initByte = SLINK_HEADER_BYTES + HEADER_N_LINES * N_BYTES_PER_WORD ; 
        size_t endByte =
            (nOffsetsLines - 1) * N_BYTES_PER_WORD + initByte;  // -1 because we only need the starting i of the line

        for (size_t i = initByte; i <= endByte; i += N_BYTES_PER_WORD)  { // tmp Read 8 bytes (64 bits) at a time
	  uint32_t word32b = get32bWordAtByte(dataPtr, i, initByte, false);
	  uint16_t low  = static_cast<uint16_t>(word32b & ((uint32_t{1} << OFFSET_BITS) - 1)); 
	  uint16_t high = static_cast<uint16_t>((word32b >> OFFSET_BITS) & ((uint32_t{1} << OFFSET_BITS) - 1));
          offsetWords.push_back(high);
          offsetWords.push_back(low);
        }  
        theOffsets.setValue(offsetWords);
//         theOffsets.printValues();
        int initial_offset = initByte + (nOffsetsLines + 2 ) * N_BYTES_PER_WORD; // + 2 = N_RESERVED_LINES;
        
        // now read the payload (channel header + clusters)
        // all channel headers should be there, even if 0 clusters are found
        // the loop is not on the actual channel number, as in the ClusterToRaw conversion each channel is split by CIC0_CIC1
        // NOTE: we need to save into the Phase2TrackerCluster1D collection two "channels" at the time
        // in order to get all the clusters from the same lpGBT and fill them once at the end
        std::vector<Phase2TrackerCluster1D> thisChannel1DSeedClusters, thisChannel1DCorrClusters;
        for (unsigned int iChannel = 0; iChannel < CICs_PER_SLINK; iChannel++) {
          // clear the collection if iChannel is even
          if (iChannel % 2 == 0) {
            thisChannel1DSeedClusters.clear();
            thisChannel1DCorrClusters.clear();
          }

          // retrieve the module type:
          // first we need to construct the DTCElinkId object ## dtc_id, gbtlink_id, elink_id
          // to get the gbt_id we should reverse what is done in the packer function,
          // where clusters from channel X are split into 2*i and 2*i+1 based on being from CIC0 or CIC1
          
          unsigned int gbt_id = iSlink * MODULES_PER_SLINK_CRACK + std::div(iChannel, 2).quot \
                                + 2*MODULES_PER_SLINK_CRACK; // temp for 6th may files
          // if tray 1, gbt_id should be 60   
          // if tray 2, gbt_id should be 48   
          // if tray 3, gbt_id should be 36   
          // if tray 4, gbt_id should be 24   
//           DTCELinkId thisDTCElinkId(dtcID, gbt_id, 0);
          DTCELinkId thisDTCElinkId(1, gbt_id, 0);

          int thisDetId = -1;
          bool is2SModule = false;
          // then pass it to the map to get the detid
          if (cablingMap_->knowsDTCELinkId(thisDTCElinkId)) {
            auto possibleDetIds = cablingMap_->dtcELinkIdToDetId(thisDTCElinkId);  // this returns a pair, detid will be an uint32_t (not a DetId)
            thisDetId = possibleDetIds->second;
            LogTrace("RawToClusterProducer") << "slink: " << iSlink << "\tiDTC: " << unsigned(dtcID) << "\tiGBT: " << unsigned(gbt_id)
                                             << "\tielink: " << unsigned(0) << "\t -> detId:" << thisDetId << std::endl;
            is2SModule =
                trackerGeometry_->getDetectorType(stackMap_[thisDetId].first) == TrackerGeometry::ModuleType::Ph2SS;
          } else {
            LogTrace("RawToClusterProducer") << "slink: " << iSlink << "\tiDTC: " << unsigned(dtcID)
                                              << "\tiGBT: " << unsigned(gbt_id) << " -> not connected? ";
            continue;
          }

          // retrieve the channel offset
          int channelOffset = theOffsets.getOffsetForChannel(iChannel);

          size_t idx = initial_offset + channelOffset * N_BYTES_PER_WORD; // (N_BYTES_PER_WORD=4)
          uint32_t headerWord = get32bWordAtByte(dataPtr, idx, initial_offset, false);

          // unsigned long eventID = (headerWord >> (N_BITS_PER_WORD - L1ID_BITS)) & L1ID_MAX_VALUE; // 9-bit field
          int channelErrors = (headerWord >> (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS)) & CIC_ERROR_MASK; // 9-bit field
          unsigned int numPixelClusters =
              (headerWord >> (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS - N_STRIP_CLUSTER_BITS)) &
              N_CLUSTER_MASK;                                           // 7-bit field
          unsigned int numStripClusters = (headerWord)&N_CLUSTER_MASK;  // 7-bit field

          LogTrace("RawToClusterProducer") << "CHANNEL " << iChannel << " HEADER " <<std::bitset<32>(headerWord) 
                                           << " (" << channelErrors << " channelErrors, "
                                           << numPixelClusters << " pixel clusters, "
                                           << numStripClusters << " strip clusters)\n";

          if (channelErrors > 0) {
            LogTrace("RawToClusterProducer") 
                << "WARNING: Channel " << iChannel << " has errors " ;
            // TBD: maybe improve handling here?
            continue;
          } else if (is2SModule && numPixelClusters > 0) {
            edm::LogError("RawToClusterProducer") << "ERROR: Header for channel " << iChannel << " expects non-zero ("
                                                  << numPixelClusters << ") pixel clusters on a 2S module\n" <<
                                                  std::bitset<32>(headerWord);
          }                                         

          // define the number of lines of the payload
          unsigned int nLines =
              (numStripClusters + numPixelClusters > 0)
                  ? int((numStripClusters * SS_CLUSTER_BITS + numPixelClusters * PX_CLUSTER_BITS) / N_BITS_PER_WORD) + 1
                  : 0;

          if (numStripClusters + numPixelClusters > 0) {
            LogTrace("RawToClusterProducer")
                << "\tchannel " << iChannel << "\theader: " << std::bitset<N_BITS_PER_WORD>(headerWord)
                << "\tn strip clusters = " << numStripClusters << "\tn pixel clusters = " << numPixelClusters
                << " (n lines = " << nLines << ")" << std::endl;
          }
          
          // first retrieve all lines filled with clusters
          std::vector<uint32_t> lines;
          for (unsigned int iline = 0; iline < nLines; iline++) {
            // header is at channelOffset, so payload starts at channelOffset + 1
            int cluster_payload_idx = channelOffset + 1 + iline;
            size_t bytePos = initial_offset + cluster_payload_idx * N_BYTES_PER_WORD;
            uint32_t word = get32bWordAtByte(dataPtr, bytePos, initial_offset, false);
            lines.push_back(word);
          }
          
          if (lines.size() != nLines) {
            edm::LogError("RawtoClusterProducer")
                << "ERROR: Numbers of stored lines does not match with size of lines to be read!";
            return;
          }
          // first retrieve the cluster words
          // this was uint16, check if can be changed back
          std::vector<uint32_t> stripClustersWords;
          stripClustersWords.resize(numStripClusters);

          std::vector<uint32_t> pixelClustersWords;
          pixelClustersWords.resize(numPixelClusters);

          // create groups of 14 (17) bits for 2S (PS) clusters, joining consecutive lines if needed
          int nAvailableBits = N_BITS_PER_WORD;
          int iLine = 0;
          int bitsToRead = 0;
          int nFullClustersStrip = 0;
          int nFullClustersPix = 0;

          readPayload(stripClustersWords,
                      lines,
                      numStripClusters,
                      nAvailableBits,
                      iLine,
                      bitsToRead,
                      nFullClustersStrip,
                      SS_CLUSTER_BITS,
                      SS_CLUSTER_WORD_MASK,
                      false);
          readPayload(pixelClustersWords,
                      lines,
                      numPixelClusters,
                      nAvailableBits,
                      iLine,
                      bitsToRead,
                      nFullClustersPix,
                      PX_CLUSTER_BITS,
                      PX_CLUSTER_WORD_MASK,
                      true,
                      nFullClustersStrip);

          // unpack the cluster words and create Phase2TrackerCluster1D objects
          int count_clusters = 0;
          if (is2SModule) {
            // create the Phase2TrackerCluster1D objects for 2S modules
            for (auto icluster : stripClustersWords) {
              std::pair<Phase2TrackerCluster1D, bool> thisCluster = unpack2S(icluster, iChannel);
              if (thisCluster.second)
                thisChannel1DSeedClusters.push_back(thisCluster.first);
              else
                thisChannel1DCorrClusters.push_back(thisCluster.first);
              count_clusters++;
            }  // end loop on cluster words
          } else {
            // create the Phase2TrackerCluster1D objects for PS modules
            // first loop on strip clusters
            for (auto icluster : stripClustersWords) {
              Phase2TrackerCluster1D thisCluster = unpackStripOnPS(icluster, iChannel);
              // for PS, strip is always correlated sensor
              thisChannel1DCorrClusters.push_back(thisCluster);
              count_clusters++;
            }
            // then loop on pixel clusters
            for (auto icluster : pixelClustersWords) {
              Phase2TrackerCluster1D thisCluster = unpackPixelOnPS(icluster, iChannel);
              // for PS, pixel is always seed sensor
              thisChannel1DSeedClusters.push_back(thisCluster);
              count_clusters++;
            }
          }

          // use FastFiller to fill the output DetSetVector output collection
          // fill every time that 2 channels are read
          if (iChannel % 2 != 1)
            continue;

          // Store clusters of this channel
          std::vector<Phase2TrackerCluster1D>::iterator it;
          {
            // inner detid is defined as module detid + 1. First int in the pair from the map
            edmNew::DetSetVector<Phase2TrackerCluster1D>::FastFiller spcs(*outputClusterCollection,
                                                                          stackMap_[thisDetId].first);
            for (it = thisChannel1DSeedClusters.begin(); it != thisChannel1DSeedClusters.end(); it++) {
              spcs.push_back(*it);
            }
          }
          {
            // outer detid is defined as inner detid + 1 or module detid + 2. Second int in the pair from the map
            edmNew::DetSetVector<Phase2TrackerCluster1D>::FastFiller spcc(*outputClusterCollection,
                                                                          stackMap_[thisDetId].second);
            for (it = thisChannel1DCorrClusters.begin(); it != thisChannel1DCorrClusters.end(); it++) {
              spcc.push_back(*it);
            }
          }

        }  // end loop on channels for this dtc
      }  // end fed data size > 0
    }  // end loop on 4 slink of this dtc
  }  // end loop on dtcs
  LogTrace("RawToClusterProducer") << "output cluster collection contains clusters from "
                                   << outputClusterCollection->size() << " channels";
  iEvent.put(std::move(outputClusterCollection));
}

uint32_t RawToClusterProducer::readLine(const unsigned char* dataPtr, int lineIdx) {
  uint32_t line = (static_cast<uint32_t>(dataPtr[lineIdx]) << 24) |
                  (static_cast<uint32_t>(dataPtr[lineIdx + 1]) << 16) |
                  (static_cast<uint32_t>(dataPtr[lineIdx + 2]) << 8) | 
                  (static_cast<uint32_t>(dataPtr[lineIdx + 3]));
  return line;
}


uint32_t RawToClusterProducer::get32bWordAtByte(const unsigned char*& data,
                                                size_t bytePos,
                                                size_t startByte,
                                                bool debug /* = false */)
{
    // word index relative to the beginning of the offset area
    size_t wordIndex = (bytePos - startByte) / N_BYTES_PER_WORD;

    // reverse inside groups of 4 words
    size_t group = wordIndex / 4;
    size_t offset = wordIndex % 4;
    size_t reversedWordIndex = group * 4 + (3 - offset);

    // back to absolute byte position
    size_t byteOffset = startByte + reversedWordIndex * N_BYTES_PER_WORD;

    uint32_t word = (static_cast<uint32_t>(data[byteOffset + 3]) << 24) |
                    (static_cast<uint32_t>(data[byteOffset + 2]) << 16) |
                    (static_cast<uint32_t>(data[byteOffset + 1]) << 8) |
                    (static_cast<uint32_t>(data[byteOffset]));

    if (debug) {
        std::cout << "wordIndex= " << wordIndex << std::endl;
        std::cout << "byteOffset= " << byteOffset << std::endl;
        std::cout << "word= " << std::bitset<32>(word) << std::endl;
//         printf("wordIndex=%zu byteOffset=%zu word=%08X\n", wordIndex,byteOffset, word);
    }
    return word;
}


std::pair<Phase2TrackerCluster1D, bool> RawToClusterProducer::unpack2S(uint32_t clusterWord, unsigned int iChannel) {
  uint32_t chipID = (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;  // 3 bits
  uint32_t sclusterAddress = (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_ONLY_BITS_2S)) &
                             SCLUSTER_ADDRESS_MASK;  // why not uint16?
  bool isSeedSensor =
      (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_2S)) & IS_SEED_SENSOR_MASK;  // 8 bits
  uint32_t width = clusterWord & WIDTH_MAX_VALUE;                                                          // 3 bits
  // cluster width is truncated during packing (3 bits)
  // since width = 0 is unphysical, we can at least recover cluster with width == 8
  // by assuming that clusters packed with width == 0 had in reality width = 8
  // this is a tmp fix, we should maybe think about how to properly do this.
  // also, for original widths > 8: again, due to truncation, they get an incorrect width of
  // cluster.getWidth() & WIDTH_MAX_VALUE. should be probably fixed in the packer
  // (e.g. if width > 8, pack with width = 0)
//   if (width == 0)
//     width = 8;
  LogTrace("RawToClusterProducer") << "\t[unpacking] chipID : " << (chipID) << "\t " << std::bitset<3>(chipID);
  LogTrace("RawToClusterProducer") << "\t[unpacking] address : " << (sclusterAddress) << "\t "
                                   << std::bitset<8>(sclusterAddress);
  LogTrace("RawToClusterProducer") << "\t[unpacking] width : " << (width) << "\t " << std::bitset<3>(width);
  LogTrace("RawToClusterProducer") << "";

  unsigned int x = STRIPS_PER_CBC * chipID + sclusterAddress;
  unsigned int y = iChannel % 2 == 0 ? 0 : 1;

  Phase2TrackerCluster1D thisCluster = Phase2TrackerCluster1D(x, y, width);
  return std::make_pair(thisCluster, isSeedSensor);
}

Phase2TrackerCluster1D RawToClusterProducer::unpackStripOnPS(uint32_t clusterWord, unsigned int iChannel) {
  // FIXME: we don't need uint32 everywere
  uint32_t chipID = (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;  // 3 bits
  uint32_t sclusterAddress = (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS)) &
                             SCLUSTER_ADDRESS_PS_MAX_VALUE;  // 7 bits
  uint32_t width = (clusterWord >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS - WIDTH_BITS)) &
                   WIDTH_MAX_VALUE;               // 3 bits
  uint32_t mipBit = clusterWord & MIP_BITS_MASK;  // 1 bits
  // see warning above for how to treat the width
  if (width == 0)
    width = 8;
  LogTrace("RawToClusterProducer") << "\t[unpacking] chipID : " << (chipID) << "\t "
                                   << std::bitset<CHIP_ID_BITS>(chipID) << std::endl;
  LogTrace("RawToClusterProducer") << "\t[unpacking] address : " << (sclusterAddress) << "\t "
                                   << std::bitset<SCLUSTER_ADDRESS_BITS_PS>(sclusterAddress) << std::endl;
  LogTrace("RawToClusterProducer") << "\t[unpacking] width : " << (width) << "\t " << std::bitset<WIDTH_BITS>(width)
                                   << std::endl;
  LogTrace("RawToClusterProducer") << "\t[unpacking] mpBit : " << (mipBit) << "\t " << std::bitset<1>(mipBit)
                                   << std::endl;

  unsigned int x = STRIPS_PER_SSA * chipID + sclusterAddress;
  unsigned int y = iChannel % 2 == 0 ? 0 : 1;

  return Phase2TrackerCluster1D(x, y, width, mipBit);
}

Phase2TrackerCluster1D RawToClusterProducer::unpackPixelOnPS(uint32_t clusterWord, unsigned int iChannel) {
  // FIXME: we don't need uint32 everywere
  uint32_t chipID = (clusterWord >> (PX_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;  // 3 bits
  uint32_t sclusterAddress = (clusterWord >> (PX_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS)) &
                             SCLUSTER_ADDRESS_PS_MAX_VALUE;  // why not uint16?
  uint32_t width = (clusterWord >> (PX_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS - WIDTH_BITS)) &
                   WIDTH_MAX_VALUE;  // 3 bits
  // see warning above for how to treat the width
  if (width == 0)
    width = 8;
  uint32_t z = clusterWord & PS_Z_BITS_MASK;  // 4 bits

  LogTrace("RawToClusterProducer") << "\t[unpacking] chipID : " << (chipID) << "\t "
                                   << std::bitset<CHIP_ID_BITS>(chipID);
  LogTrace("RawToClusterProducer") << "\t[unpacking] address : " << (sclusterAddress) << "\t "
                                   << std::bitset<SCLUSTER_ADDRESS_BITS_PS>(sclusterAddress);
  LogTrace("RawToClusterProducer") << "\t[unpacking] width : " << (width) << "\t " << std::bitset<WIDTH_BITS>(width);
  LogTrace("RawToClusterProducer") << "\t[unpacking] z : " << (z) << "\t " << std::bitset<4>(z);

  unsigned int x = STRIPS_PER_SSA * chipID + sclusterAddress;
  // NB: not really clear from the packer code. Got it from the old code.
  unsigned int y = iChannel % 2 == 0 ? z : (z + 16);
  // (chipId() >= MAX_CBC_PER_FE / 2) ? (rawY() + PS_COLS / 2) : rawY();

  return Phase2TrackerCluster1D(x, y, width);
}

// create groups of 14/17 bits, joining consecutive lines if needed
// each 2S cluster payload consists of 3bits for chipID, 8 bits for address, 3 bits for width = 14 bits
// each P on PS cluster payload consists of 3bits for chipID, 7 bits for address, 1 bit for mpBit, 3 bits for width = 17 bits
// each S on PS cluster payload consists of 3bits for chipID, 8 bits for address, 1 bit for z, 3 bits for width = 17 bits
void RawToClusterProducer::readPayload(std::vector<uint32_t>& clusterWords,
                                       std::vector<uint32_t>& lines,
                                       int numClusters,
                                       int& nAvailableBits,
                                       int& iLine,
                                       int& bitsToRead,
                                       int& nFullClusters,
                                       int clusterBits,
                                       int clusterWordMask,
                                       bool isPixelCluster,
                                       int nFullClustersStrips) {
  for (int icluster = 0; icluster < numClusters; icluster++) {
    if (nAvailableBits >= clusterBits) {
      // calculate the shift
      int shift = N_BITS_PER_WORD - bitsToRead - (nFullClusters + 1) * clusterBits;
      // take into account bits already used for the last strip cluster
      if (icluster == 0 && isPixelCluster)
        shift -= (nFullClustersStrips)*SS_CLUSTER_BITS;
      nFullClustersStrips = 0;  // reset

      // mask, and save cluster word
      clusterWords[icluster] = (lines[iLine] >> shift) & clusterWordMask;
      // update available bits and number of full clusters from this line
      nAvailableBits -= clusterBits;
      nFullClusters++;

      if (nAvailableBits == 0) {
        iLine++;
        nAvailableBits = N_BITS_PER_WORD;
        nFullClusters = 0;
        bitsToRead = 0;
      }
    } else {
      // get the remaining bits from the current line. first create the mask, then mask
      int nMask = createMask(nAvailableBits);
      uint16_t wordLeft = lines[iLine] & nMask;

      // create mask for next line
      bitsToRead = clusterBits - nAvailableBits;
      int nextMask = createMask(bitsToRead);
      // shift and mask
      uint16_t wordRight = (lines[iLine + 1] >> (N_BITS_PER_WORD - bitsToRead)) & nextMask;

      // compose the full cluster word
      clusterWords[icluster] = (wordLeft << bitsToRead) | wordRight;

      // re-set n available bits
      nAvailableBits = N_BITS_PER_WORD - bitsToRead;
      // advance by one line and re-init the number of complete clusters read from the current line
      iLine++;
      nFullClusters = 0;
    }
  }
}

void RawToClusterProducer::dumpRawFile(const unsigned char* dataPtr, size_t data_size, bool hexa) {


  if (hexa) {

    for (size_t i = 0; i < data_size; i += 2)
    {
        if (i % 16 == 0)
        {
            std::cout << std::hex
                      << std::setw(7)
                      << std::setfill('0')
                      << std::nouppercase
                      << i
                      << " ";
        }
    
        // Combine two bytes into a 16-bit value (original byte order: high byte first)
        uint16_t word = static_cast<uint16_t>(static_cast<uint8_t>(dataPtr[i])) << 8;
        if (i + 1 < data_size)
            word |= static_cast<uint8_t>(dataPtr[i + 1]);
    
        std::cout << std::hex
                  << std::setw(4)
                  << std::setfill('0')
                  << std::nouppercase
                  << word;
    
        if ((i + 2) % 16 == 0 || i + 2 >= data_size)
            std::cout << "\n";
        else
            std::cout <<  std::dec << " ";
    }
    std::cout << std::dec << std::endl;  
  } else {
    std::cout << "    " ;
    for (size_t i = 0; i < data_size; ++i)
    {
      std::bitset<8> bits(dataPtr[i]);
      std::cout << "    " << bits << " ";
      if ((i + 1) % 8 == 0)
        std::cout << "\n" << i+1 << "    " ;
    }
    std::cout << std::endl;
  }    
}


int RawToClusterProducer::createMask(int nBits) { return (1 << nBits) - 1; }

DEFINE_FWK_MODULE(RawToClusterProducer);
