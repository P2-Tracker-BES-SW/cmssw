// ================================ The alpaka device-side implementation (kernels) of the unpacking algorithm. ================================

// alpaka-related imports
#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/traits.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
// Andrea
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/chooseDevice.h"

#include "DataFormats/Phase2TrackerCluster/interface/ClusterPropDeviceCollection.h"  // uses the SoA layout
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2TrackerSpecifications.h"
#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2TrackerRawToDigi/plugins/alpaka/RawToClusterAlgo.h"  // for N_CORE_IDS (CRACK tables)

using namespace Phase2TrackerSpecifications;
using namespace Phase2DAQFormatSpecification;
using namespace ALPAKA_ACCELERATOR_NAMESPACE;

//#define Debug_GPU

// ------------------------------- constants ---------------------------------
// Upper bounds for per channel parsing
static constexpr int MaxOffsetWords = (OFFSET_BITS * CICs_PER_SLINK) / N_BITS_PER_WORD;
static constexpr int MaxStripClusters = N_CLUSTER_MASK + 1;
static constexpr int MaxPixelClusters = N_CLUSTER_MASK + 1;
static constexpr int MaxPayloadLines =
    ((MaxStripClusters * SS_CLUSTER_BITS + MaxPixelClusters * PX_CLUSTER_BITS) / N_BITS_PER_WORD) + 1;

// ------------------------------- helpers from CPU based code ---------------------------------

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  ALPAKA_FN_ACC inline int createMask(int nBits) { return (1 << nBits) - 1; }

  //readLine() replaced by get32bWordAtByte(), same as RawToClusterProducer::get32bWordAtByte():
  // the 4 32-bit words inside each 128-bit line are stored in reverse order and each word is little-endian
  ALPAKA_FN_ACC inline uint32_t get32bWordAtByte(const unsigned char* data, size_t bytePos, size_t startByte) {
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
                    (static_cast<uint32_t>(data[byteOffset + 1]) << 8) | (static_cast<uint32_t>(data[byteOffset]));
    return word;
  }

  ALPAKA_FN_ACC inline int getLineIndex(int channelIdx, unsigned int iline) {
    return channelIdx + N_BYTES_PER_WORD + iline * N_BYTES_PER_WORD;
  }

  ALPAKA_FN_ACC inline void readPayload(uint32_t* clusterWords,
                                        const uint32_t* lines,
                                        int numClusters,
                                        int& nAvailableBits,
                                        int& iLine,
                                        int& bitsToRead,
                                        int& nFullClusters,
                                        const int clusterBits,
                                        const int clusterWordMask,
                                        const bool isPixelCluster,
                                        int nFullClustersStrips = 0) {
    for (int icluster = 0; icluster < numClusters; ++icluster) {
      if (nAvailableBits >= clusterBits) {
        int shift = N_BITS_PER_WORD - bitsToRead - (nFullClusters + 1) * clusterBits;
        if (icluster == 0 && isPixelCluster)
          shift -= (nFullClustersStrips)*SS_CLUSTER_BITS;
        nFullClustersStrips = 0;
        clusterWords[icluster] = (lines[iLine] >> shift) & clusterWordMask;
        nAvailableBits -= clusterBits;
        nFullClusters++;
        if (nAvailableBits == 0) {
          ++iLine;
          nAvailableBits = N_BITS_PER_WORD;
          nFullClusters = 0;
          bitsToRead = 0;
        }
      } else {
        const int nMask = createMask(nAvailableBits);
        const uint16_t wordLeft = static_cast<uint16_t>(lines[iLine] & nMask);
        bitsToRead = clusterBits - nAvailableBits;
        const int nextMask = createMask(bitsToRead);
        const uint16_t wordRight =
            static_cast<uint16_t>((lines[iLine + 1] >> (N_BITS_PER_WORD - bitsToRead)) & nextMask);
        clusterWords[icluster] = (static_cast<uint32_t>(wordLeft) << bitsToRead) | wordRight;
        nAvailableBits = N_BITS_PER_WORD - bitsToRead;
        ++iLine;
        nFullClusters = 0;
      }
    }
  }

  // Upper bound on total number of clusters across all DTCs/Slinks/CICs.
  static constexpr size_t MaxTotalClusters =
      (N_CLUSTER_MASK + 1) * CICs_PER_SLINK * (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC;

  // Unpacking Kernel, decodes raw FED data into cluster Properties SoA
  struct Unpacker {
    template <typename RawBufView,
              typename SizeBufView,
              typename OffBufView,
              typename ModuleTypeView,
              typename InnerDetIdView,
              typename OuterDetIdView,
              typename OutView>
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  RawBufView in,
                                  SizeBufView sizes,
                                  OffBufView offsets,
                                  ModuleTypeView const& detIdxModuleTypeMap,
                                  InnerDetIdView const& innerDetIdForFlatIdx,
                                  OuterDetIdView const& outerDetIdForFlatIdx,
                                  const bool analyzeCRACK,  // analyzeCRACK parameter
                                  OutView out,
                                  uint32_t* globalCounter) const {
      // per thread scratch arrays in registers/local memory
      uint32_t offsetWords[MaxOffsetWords];
      uint32_t lines[MaxPayloadLines];
      uint32_t stripClusterWords[MaxStripClusters];
      uint32_t pixelClusterWords[MaxPixelClusters];
      // Total number of S-Links (FED fragments) to parse across all DTCs
      const uint32_t NSlinks = (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC;
      // loop over FEDs assigned to this thread (grid stride loop)
      for (uint32_t frdId : cms::alpakatools::uniform_elements(acc, NSlinks)) {
        if (sizes[frdId] == 0)
          continue;

        // The whole fragment is copied to the device, the tracker data start after the
        // S-Link header, as fedData.payload(slink_header_size, slink_trailer_size) in RawToClusterProducer
        const unsigned char* dataPtr = in + offsets[frdId] + sizeof(SLinkRocketHeader_v3);

#ifdef Debug_GPU
        // read header
        uint32_t headerWords[HEADER_N_LINES];
        for (size_t k = 0; k < HEADER_N_LINES; ++k) {
          const int byteIdx = static_cast<int>(k * N_BYTES_PER_WORD);
          headerWords[k] = get32bWordAtByte(dataPtr, byteIdx, 0);  
        }
        printf("headerWords[0] = %u\n", headerWords[0]);
#endif

        // DAQ path core ID (bits 3-0 of tracker header word 0, as
        // TrackerHeader::getDAQpathCoreID()), used only with CRACK to pick the CRACK mapping
        unsigned int coreID = 0;
        if (analyzeCRACK) {
          const size_t headerStartByte = DTC_HEADER_OFFSET * N_BYTES_PER_WORD;
          const uint32_t headerWord0 = get32bWordAtByte(dataPtr, headerStartByte, headerStartByte);
          coreID = headerWord0 & ((1u << C_NUM_BITS_CORE_ID) - 1);
        }

        // Channel mask (tracker words 22 and 23, bits 17-0 of each)
        const uint32_t mask0 = get32bWordAtByte(dataPtr, DTC_CHANNEL_MASK_OFFSET * N_BYTES_PER_WORD, 0) & 0x3FFFF;
        const uint32_t mask1 = get32bWordAtByte(dataPtr, (DTC_CHANNEL_MASK_OFFSET + 1) * N_BYTES_PER_WORD, 0) & 0x3FFFF;
        const uint64_t globalMask = (static_cast<uint64_t>(mask1) << 18) | mask0;

        // read offset table
        const size_t nOffsetsLines = MaxOffsetWords;
        const size_t initByte = HEADER_N_LINES * N_BYTES_PER_WORD;
        for (size_t k = 0; k < nOffsetsLines; ++k) {
          const int byteIdx = static_cast<int>(initByte + k * N_BYTES_PER_WORD);
          offsetWords[k] = get32bWordAtByte(dataPtr, byteIdx, initByte);  
        }

        // loop over channels
        for (unsigned int iChannel = 0; iChannel < CICs_PER_SLINK; ++iChannel) {
          // skip masked channels
          if ((globalMask >> iChannel) & 0x1ULL)
            continue;

          unsigned flatIdx = frdId * CICs_PER_SLINK + iChannel;
          // with CRACK the module of a channel is given by the CRACK mapping of
          // (DTC, core ID, channel / 2): the tables filled in beginRun are then indexed by (DTC, core ID, channel)
          // (frdId / SLINKS_PER_DTC = DTC index)
          if (analyzeCRACK)
            flatIdx = ((frdId / SLINKS_PER_DTC) * N_CORE_IDS + coreID) * CICs_PER_SLINK + iChannel;

          const int moduleType = detIdxModuleTypeMap[flatIdx];
          if (moduleType == 0)
            continue;  // skip unconnected

          const bool is2SModule = (moduleType == 1);

          // find payload start (after header + module table)
          // the channel area starts after the tracker header, the offsets and the
          // reserved lines (byte 96), as initial_offset in RawToClusterProducer
          const size_t offsetTableStart = initByte + (nOffsetsLines + RESERVED_N_LINES) * N_BYTES_PER_WORD;

          // offsets as in ChannelsOffset: channel 0 has offset 0, channel i > 0 uses the
          // 16-bit slot i - 1, where slot 2k is the high half and slot 2k + 1 the low half of offset word k
          uint16_t channelOffset16 = 0;
          if (iChannel > 0) {
            const unsigned int slot = iChannel - 1;
            const int wordIdx = static_cast<int>(slot / 2);
            channelOffset16 = (slot % 2 == 0) ? static_cast<uint16_t>(offsetWords[wordIdx] >> 16)  // even slot: high half
                                              : static_cast<uint16_t>(offsetWords[wordIdx] & 0xFFFFu);  // odd slot: low half
          }

          // compute byte index into FED payload for this channel
          const int idx = static_cast<int>(offsetTableStart + channelOffset16 * N_BYTES_PER_WORD);

          // read channel header
          const uint32_t chHeaderWord =
              get32bWordAtByte(dataPtr, idx, offsetTableStart);  

          // skip channels with CIC/FE errors
          const unsigned int channelErrors =
              (chHeaderWord >> (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS)) & CIC_ERROR_MASK;
          if (channelErrors > 0)
            continue;

          const unsigned int numStripClusters =
              (chHeaderWord >> (N_BITS_PER_WORD - L1ID_BITS - CIC_ERROR_BITS - N_STRIP_CLUSTER_BITS)) & N_CLUSTER_MASK;
          const unsigned int numPixelClusters = chHeaderWord & N_CLUSTER_MASK;

          // read payload lines
          unsigned int nLines = 0;
          if (numStripClusters + numPixelClusters > 0) {
            const unsigned int neededBits = numStripClusters * SS_CLUSTER_BITS + numPixelClusters * PX_CLUSTER_BITS;
            nLines = static_cast<unsigned int>(neededBits / N_BITS_PER_WORD) + 1;
          }
          if (nLines > MaxPayloadLines)
            nLines = MaxPayloadLines;

          for (unsigned int k = 0; k < nLines; ++k) {
            const int byteIdx = getLineIndex(idx, k);
            lines[k] = get32bWordAtByte(dataPtr, byteIdx, offsetTableStart); 
          }

          // unpack strip/pixel cluster words
          int nAvailableBits = N_BITS_PER_WORD;
          int iLine = 0;
          int bitsToRead = 0;
          int nFullClustersStrip = 0;
          int nFullClustersPix = 0;

          // clamp strip and pixel cluster counts to array capacity (avoid overflow)
          const unsigned int useStrip = (numStripClusters <= MaxStripClusters) ? numStripClusters : MaxStripClusters;
          const unsigned int usePixel = (numPixelClusters <= MaxPixelClusters) ? numPixelClusters : MaxPixelClusters;

          if (useStrip > 0) {
            readPayload(stripClusterWords,
                        lines,
                        static_cast<int>(useStrip),
                        nAvailableBits,
                        iLine,
                        bitsToRead,
                        nFullClustersStrip,
                        SS_CLUSTER_BITS,
                        SS_CLUSTER_WORD_MASK,
                        false);
          }
          if (!is2SModule && usePixel > 0) {
            readPayload(pixelClusterWords,
                        lines,
                        static_cast<int>(usePixel),
                        nAvailableBits,
                        iLine,
                        bitsToRead,
                        nFullClustersPix,
                        PX_CLUSTER_BITS,
                        PX_CLUSTER_WORD_MASK,
                        true,
                        nFullClustersStrip);
          }

          // reserve output slots
          const uint32_t writeCount = is2SModule ? useStrip : (useStrip + usePixel);
          if (writeCount == 0)
            continue;
          const uint32_t base = alpaka::atomicAdd(acc, globalCounter, writeCount);

          const uint32_t innerDet = innerDetIdForFlatIdx[flatIdx];
          const uint32_t outerDet = outerDetIdForFlatIdx[flatIdx];
          const uint8_t parity = static_cast<uint8_t>(iChannel & 0x1);

          // write strips (2S or PS outer)
          if (is2SModule) {
            for (unsigned int ic = 0; ic < useStrip; ++ic) {
              const uint32_t word = stripClusterWords[ic];
              const uint32_t chip = (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;
              const uint32_t addr =
                  (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_ONLY_BITS_2S)) & SCLUSTER_ADDRESS_MASK;
              const bool seed =
                  (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_2S)) & IS_SEED_SENSOR_MASK;
              uint32_t w = word & WIDTH_MAX_VALUE;
              //   if (w == 0)
              //     w = 8;

              const uint32_t outIdx = base + ic;
              if (outIdx >= MaxTotalClusters)
                continue;

              out[outIdx].detId() = seed ? innerDet : outerDet;
              out[outIdx].x() = static_cast<uint16_t>(STRIPS_PER_CBC * chip + addr);
              out[outIdx].y() = static_cast<uint16_t>(parity);
              out[outIdx].z() = 0u;
              out[outIdx].width() = static_cast<uint8_t>(w);
              out[outIdx].isSeed() = static_cast<uint8_t>(seed);
              out[outIdx].mip() = 0u;
              out[outIdx].moduleType() = 1u;
            }
          } else {
            // PS strips (outer)
            for (unsigned int ic = 0; ic < useStrip; ++ic) {
              const uint32_t word = stripClusterWords[ic];
              const uint32_t chip = (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;
              const uint32_t addr =
                  (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS)) & SCLUSTER_ADDRESS_PS_MAX_VALUE;
              uint32_t w =
                  (word >> (SS_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS - WIDTH_BITS)) & WIDTH_MAX_VALUE;
              const uint32_t mip = word & MIP_BITS_MASK;
              //   if (w == 0)
              //     w = 8;

              const uint32_t outIdx = base + ic;
              if (outIdx >= MaxTotalClusters)
                continue;

              out[outIdx].detId() = outerDet;
              out[outIdx].x() = static_cast<uint16_t>(STRIPS_PER_SSA * chip + addr);
              out[outIdx].y() = static_cast<uint16_t>(parity);
              out[outIdx].z() = 0u;
              out[outIdx].width() = static_cast<uint8_t>(w);
              out[outIdx].isSeed() = 0u;
              out[outIdx].mip() = static_cast<uint8_t>(mip);
              out[outIdx].moduleType() = 2u;
            }

            // PS pixels (inner)
            for (unsigned int ic = 0; ic < usePixel; ++ic) {
              const uint32_t word = pixelClusterWords[ic];
              const uint32_t chip = (word >> (PX_CLUSTER_BITS - CHIP_ID_BITS)) & CHIP_ID_MAX_VALUE;
              const uint32_t addr =
                  (word >> (PX_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS)) & SCLUSTER_ADDRESS_PS_MAX_VALUE;
              uint32_t w =
                  (word >> (PX_CLUSTER_BITS - CHIP_ID_BITS - SCLUSTER_ADDRESS_BITS_PS - WIDTH_BITS)) & WIDTH_MAX_VALUE;
              const uint32_t z = word & PS_Z_BITS_MASK;
              //   if (w == 0)
              //     w = 8;

              const uint32_t outIdx = base + useStrip + ic;
              if (outIdx >= MaxTotalClusters)
                continue;

              out[outIdx].detId() = innerDet;
              out[outIdx].x() = static_cast<uint16_t>(STRIPS_PER_SSA * chip + addr);
              out[outIdx].y() = static_cast<uint16_t>(parity == 0 ? z : (z + 16));
              out[outIdx].z() = static_cast<uint8_t>(z);
              out[outIdx].width() = static_cast<uint8_t>(w);
              out[outIdx].isSeed() = 1u;
              out[outIdx].mip() = 0u;
              out[outIdx].moduleType() = 2u;
            }
          }
        }
      }
    }
  };

  void launchUnpacker(Queue& queue,
                      cms::alpakatools::device_buffer<Device, unsigned char[]> const& rawdatabuff,
                      cms::alpakatools::device_buffer<Device, size_t[]> const& sizedatabuff,
                      cms::alpakatools::device_buffer<Device, size_t[]> const& offsetdatabuff,
                      cms::alpakatools::device_buffer<Device, int[]> const& detIdxModuleTypeDevice,
                      cms::alpakatools::device_buffer<Device, uint32_t[]> const& innerDetIdDevice,
                      cms::alpakatools::device_buffer<Device, uint32_t[]> const& outerDetIdDevice,
                      const bool analyzeCRACK,  
                      Phase2RawToCluster::ClusterPropDeviceCollection::View out,
                      uint32_t* globalCounter) {
    const uint32_t NSlinks = (MAX_DTC_ID - MIN_DTC_ID + 1) * SLINKS_PER_DTC;
    const uint32_t threadsPerBlock = 128;
    const uint32_t blocks = (NSlinks + threadsPerBlock - 1) / threadsPerBlock;

    auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(blocks, threadsPerBlock);

    alpaka::exec<Acc1D>(queue,
                        workDiv,
                        Unpacker{},
                        rawdatabuff.data(),
                        sizedatabuff.data(),
                        offsetdatabuff.data(),
                        detIdxModuleTypeDevice.data(),
                        innerDetIdDevice.data(),
                        outerDetIdDevice.data(),
                        analyzeCRACK,  
                        out,
                        globalCounter);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE
