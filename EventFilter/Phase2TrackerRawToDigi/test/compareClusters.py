## Script to read the clusterTree ntuples from various files, and compare the distributions of cluster variables for common OT modules.
## Can be run by comparing clusters from specific DTCs, detIDs, or on the full cluster collection (in this case it can be very slow).

import argparse
import ROOT
import os, pdb
from datetime import datetime
from array import array
import numpy as np

parser = argparse.ArgumentParser(description="Reads root file and compares for specific detIds`")
parser.add_argument('inputfile', type=str, nargs='+', help='input file names')
parser.add_argument("-d", "--detid", type=int, nargs='+', dest = "detId", default = [-1])
parser.add_argument("--dtcid", type=int, dest = "dtcID", default = 999)
## -99 -> do not check detIds
args = parser.parse_args()

ROOT.gStyle.SetOptStat(0)
ROOT.gROOT.SetBatch(True)
ROOT.TH1.SetDefaultSumw2()
ROOT.gROOT.ProcessLine( "gErrorIgnoreLevel = kInfo;" )

# create output directory for plots
output_folder = os.path.join('validation_plots', datetime.now().strftime('%Y-%m-%d_%H-%M-%S')+ '_dtc' + str(args.dtcID))
os.makedirs(output_folder, exist_ok=True)
        

class PlotContainer:
    def __init__(self, var, sel, xtitle, norm, pltopt, nbins=0, xmin=0, xmax=0, mybins=0, label=None, logx=False, logy=False):
        self.var = var
        self.sel = sel
        self.xtitle = xtitle
        self.norm = norm
        self.nbins = nbins
        self.xmin = xmin
        self.xmax = xmax
        self.pltopt = pltopt
        self.logy = logy
        self.logx = logx
        self.mybins = mybins or []
        self.label = label or var

def setHistoProperties(h, color, xtitle, ytitle, marker_style, marker_size, alpha=0.8, line_width = 1):
    h.SetLineColor(color)
    h.SetLineWidth(line_width)
    h.SetFillColorAlpha(color, alpha)
    h.SetMarkerStyle(marker_style)
    h.SetMarkerSize(marker_size)
    h.SetMarkerColor(color)
    h.GetXaxis().SetTitle(xtitle)
    h.GetYaxis().SetTitle(ytitle)
    h.GetXaxis().SetLabelColor(ROOT.kWhite)
    h.GetXaxis().SetLabelSize(0.)
    h.SetNdivisions(0)

def set_baseline_histo(rp_th1, xtitle):
    rp_th1 .SetTitle("");
    rp_th1 .GetXaxis().SetTitle(xtitle);
    rp_th1 .GetYaxis().SetTitle('redigi/original');
    
    rp_th1 .GetXaxis().SetTitleSize(0.12);
    rp_th1 .GetYaxis().SetTitleSize(0.12);
    rp_th1 .GetXaxis().SetTitleOffset(0.8);
    rp_th1 .GetYaxis().SetTitleOffset(0.4);
    rp_th1 .GetYaxis().SetNdivisions(4+800)
    rp_th1 .GetXaxis().SetTickLength(0.05)
    rp_th1 .GetXaxis().SetLabelSize( 0.1)
    rp_th1 .GetYaxis().SetLabelSize( 0.1)

def getTreeFromFile(filename, treename):
    file = ROOT.TFile.Open(filename)
    if not file or file.IsZombie():
        print(f"Error: File '{filename}' not found or is corrupted.")
        exit(1)
    tree = file.Get(treename)
    if not tree:
        print(f"Error: Tree '{treename}' not found in file '{filename}'.")
        exit(1)
    return tree, file

def create_histogram(name, nbins, xmin, xmax, mybins=None):
    if mybins:
        return ROOT.TH1F(name, '', len(mybins) - 1, array('d', mybins))
    return ROOT.TH1F(name, '', nbins, xmin, xmax)


## parse input files and get ttrees
input_files = args.inputfile
trees, files = [], []
for filename in input_files:
    tree, file = getTreeFromFile(filename, 'Phase2TrackerDumpClusters/ClusterTree')
    trees.append(tree)
    files.append(file)

t0, t1 = trees[0], trees[1]

detId = args.detId
dtcID = args.dtcID
common_detIds_string = ''
common_detIds = []
## find the detid in common between the two ttrees, to be used if not specified
if detId[0] != -99 or dtcID != 999:
    detId_t0 = set(entry.detId for entry in t0 if dtcID == 999 or entry.dtcID == dtcID)
    detId_t1 = set(entry.detId for entry in t1 if dtcID == 999 or entry.dtcID == dtcID)
    common_detIds = detId_t0.intersection(detId_t1)
    if common_detIds:
        common_detIds_string = '&& (' + " || ".join([f"detId=={d}" for d in common_detIds]) + ')'
        print('common_detIds_string:', common_detIds_string)
    else:
        print('no common detIds found!')


## prepare the selection string if specific detIds are selected
if detId[0] != -99 and detId[0] != -1 and dtcID == 999:
    selectDetId = '&& (' + " || ".join([f"detId=={d}" for d in detId]) + ')'
elif detId[0] == -1 and dtcID == 999:
    selectDetId = common_detIds_string
elif dtcID != 999:
    selectDetId = f'&& (dtcID=={dtcID})'
else:
    selectDetId = ''
    
full_selection = 'clusterSize <=8 ' + selectDetId
# full_selection = 'clusterSize <=8 ' 
# full_selection = '' + selectDetId


## define the plots to be made
plots = [
    PlotContainer(var = 'clusterR'      , sel = full_selection,  xtitle = 'cluster radius (cm)', norm = False, nbins = 100  , xmin =  0   , xmax =  130  , pltopt = 'HIST'),
    PlotContainer(var = 'clusterZ'      , sel = full_selection,  xtitle = 'cluster z (cm)'     , norm = False, nbins = 100  , xmin = -300 , xmax =  300  , pltopt = 'HIST'),
    PlotContainer(var = 'clusterCenter' , sel = full_selection,  xtitle = 'cluster center'     , norm = False, nbins = 510  , xmin =  0   , xmax =  1020 , pltopt = 'HIST'),
    PlotContainer(var = 'clusterSize'   , sel = full_selection,  xtitle = 'cluster size'       , norm = False, nbins = 12   , xmin =  0   , xmax =  12   , pltopt = 'HIST'),
    PlotContainer(var = 'clusterGlobalX', sel = full_selection,  xtitle = 'cluster global X'   , norm = False, nbins = 120  , xmin = -120 , xmax =  120  , pltopt = 'HIST'),
    PlotContainer(var = 'clusterCol'    , sel = full_selection,  xtitle = 'cluster column'     , norm = False, nbins = 32   , xmin =  0   , xmax =  32   , pltopt = 'HIST'),
    ## example of non uniform binning
    ### PlotContainer(var = 'mu1Pt'                 , sel = selData, mcsel = selMC, xtitle = 'p_{T}(#mu_{1}) [GeV]'         , norm = True, nbins = 160 , xmin = pt_mu1_bins[0], xmax =  pt_mu1_bins[-1] , mybins = pt_mu1_bins, pltopt = 'HIST', label = 'leadingMuPt' ),
]
colorlist = [ROOT.kPink+8, ROOT.kTeal+4]

## keep track of modules with non-matching results
list_bad_detids = []

def makePlots(plot, selLabel = '', saveAnyway = True):
    h_redigi = create_histogram('hdata', plot.nbins, plot.xmin, plot.xmax, plot.mybins)
    h_origin = create_histogram('hmc', plot.nbins, plot.xmin, plot.xmax, plot.mybins)

    ytitle = 'clusters'
    setHistoProperties(h_redigi, colorlist[0], plot.xtitle, ytitle, 8, 0.6, 0. , 2)
    setHistoProperties(h_origin, colorlist[1], plot.xtitle, ytitle, 1, 0.6, 0.6 )
    
    t0.Draw('%s >> %s'%(plot.var, h_origin.GetName()), '%s'%(plot.sel), plot.pltopt)
    t1.Draw('%s >> %s'%(plot.var, h_redigi.GetName()), '%s'%(plot.sel), plot.pltopt)

    max_y = max(h_redigi.GetMaximum(), h_origin.GetMaximum()) or 1
    p_th1 = ROOT.TH1F("p_th1","",plot.nbins, plot.xmin, plot.xmax)
    p_th1.GetXaxis().SetLabelSize( 0.0)
    p_th1.GetYaxis().SetTitle(ytitle)
    p_th1.GetYaxis().SetTitleSize(0.045)
    p_th1.SetMaximum(1.25*max_y)
    
    c1 = ROOT.TCanvas('c%s'%plot.var, 'c%s'%plot.var, 700, 700)
    upperPad  = ROOT.TPad('upperPad' , '' , 0., 0.25 , 1.,  1.    )  
    lowerPad  = ROOT.TPad('lowerPad' , '' , 0., 0.01 , 1.,  0.248 )  
    upperPad.Draw()
    lowerPad .Draw()
    upperPad.SetBottomMargin(0.012)
    lowerPad.SetTopMargin(0)
    lowerPad.SetBottomMargin(0.2)
   
    upperPad.cd()
    p_th1.Draw()
    h_origin.Draw('same hist')
    h_redigi.Draw('same hist')

    ## legend
    legend = ROOT.TLegend(0.55, 0.73, 0.88, 0.88)
    legend.AddEntry(h_redigi, 're-digi', 'f')
    legend.AddEntry(h_origin, 'original clusters', 'f')
    legend.SetBorderSize(0)
    legend.SetTextSize(0.036)
    legend.Draw()

    ## more info
    sample_txt = ROOT.TLatex()
    sample_txt.SetTextFont(42)
    sample_txt.SetTextSize(0.042)
    sample_txt.DrawLatexNDC(.63, .91, 'TTBar + 200PU, D98')

#     evt_txt = ROOT.TLatex()
#     evt_txt.SetTextFont(42)
#     evt_txt.SetTextSize(0.042)
#     evt_txt.DrawLatexNDC(.13, .91, 'one evt, one DTC, 24 detIds')   
#     evt_txt.DrawLatexNDC(.13, .91, 'one evt, all DTCs')   

    ## ratio plot in the bottom pad
    lowerPad.cd()
    rp_th1 = ROOT.TH1F("rp_th1", "rp_th1", plot.nbins, plot.xmin, plot.xmax)
    set_baseline_histo(rp_th1, plot.xtitle)
    rp_th1.SetMaximum( 1.12)
    rp_th1.SetMinimum( 0.88 )

    hr_redigi = h_redigi.Clone("hr_redigi")
    hr_origin = h_origin.Clone("hr_origin")
    hr_redigi.Divide(hr_redigi, hr_origin, 1, 1, 'B')
    g_ratio = ROOT.TGraphAsymmErrors(hr_redigi)
    g_ratio.SetLineColor(colorlist[1])
    g_ratio.SetMarkerColor(colorlist[1])
    g_ratio.SetMarkerStyle(8)
    g_ratio.SetMarkerSize(.8)
    
    rp_th1. Draw("")
    line = ROOT.TLine(plot.xmin,1,plot.xmax,1)
    line.SetLineColor(ROOT.kGreen+4)
    line.Draw()
    g_ratio.Draw("same p")

    c1.Update()
    c1.Modified()

    yvals =  np.array(g_ratio.GetY())
    save = False
    if len(yvals[(yvals!=1) & (yvals !=0)]):
      save = True
      if 'Module' not in selLabel:
        list_bad_detids.append(int(0 if selLabel=='' else selLabel.replace('_', '')))

    ## only save plots for the full set of detids, and for those failing comparison
    if save or saveAnyway:
      c1.SaveAs(output_folder + '/%s%s.pdf' %(plot.label,selLabel))
#       if saveAnyway:
#         c1.SaveAs(output_folder + '/%s%s.png' %(plot.label,selLabel))


## make overall plots
for plot in plots:
    makePlots(plot, '', saveAnyway = False)

## make plots per cluster type
for isel in ['isPSModulePixel', 'isPSModuleStrip', 'is2SModule']:
    selectDetId = '({isel} == 1)'.format(isel = isel)
    full_selection = 'clusterSize <=8 && ' + selectDetId
    plots = [
        PlotContainer(var = 'clusterR'      , sel = full_selection,  xtitle = 'cluster radius (cm)', norm = False, nbins = 100  , xmin =  0   , xmax =  130  , pltopt = 'HIST'),
        PlotContainer(var = 'clusterZ'      , sel = full_selection,  xtitle = 'cluster z (cm)'     , norm = False, nbins = 100  , xmin = -300 , xmax =  300  , pltopt = 'HIST'),
        PlotContainer(var = 'clusterSize'   , sel = full_selection,  xtitle = 'cluster size'       , norm = False, nbins = 12   , xmin =  0   , xmax =  12   , pltopt = 'HIST'),
    ]
    for plot in plots:
        makePlots(plot, '_'+isel, saveAnyway = True)
  

if detId[0] != -99 or dtcID != 999:
  # make plots per detId
  for idetid in common_detIds:
      selectDetId = '(detId=={detId})'.format(detId = idetid)
      full_selection = 'clusterSize <=8 && ' + selectDetId
  
      plots = [
          PlotContainer(var = 'clusterR'      , sel = full_selection,  xtitle = 'cluster radius (cm)', norm = False, nbins = 100  , xmin =  0   , xmax =  130  , pltopt = 'HIST'),
          PlotContainer(var = 'clusterZ'      , sel = full_selection,  xtitle = 'cluster z (cm)'     , norm = False, nbins = 100  , xmin = -300 , xmax =  300  , pltopt = 'HIST'),
      ]
  
      for plot in plots:
          makePlots(plot, '_'+str(idetid), saveAnyway = False)

# ## print detId of modules which do not agree
print ('modules with differences', set(list_bad_detids))
print ('modules not present in original but not in re-digi:', detId_t0.difference(detId_t1))
print ('modules not present in re-digi but not in original:', detId_t1.difference(detId_t0))
