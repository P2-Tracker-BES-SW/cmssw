#include <vector>
#include <bitset>
#include <DataFormats/FEDRawData/interface/FEDRawData.h>
#include <DataFormats/FEDRawData/interface/FEDRawDataCollection.h>
#include <DataFormats/FEDRawData/interface/FEDHeader.h>
#include <DataFormats/FEDRawData/interface/FEDTrailer.h>
#include <DataFormats/FEDRawData/src/fed_header.h>
#include <DataFormats/FEDRawData/src/fed_trailer.h>
#include <DataFormats/Common/interface/Wrapper.h>
#include <DataFormats/Common/interface/RefProd.h>

// Adding for alpaka 
#include "DataFormats/FEDRawData/interface/StripPixelCollection.h"
#include "DataFormats/FEDRawData/interface/StripPixelHostCollection.h"
#include "DataFormats/FEDRawData/interface/StripPixelSoA.h"
