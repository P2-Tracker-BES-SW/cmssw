// compareClusterRow.C
// ROOT macro to compare clusterRow from two different ROOT files
// Usage: root -l 'compareClusterRow.C("file1.root", "file2.root")'

#include <TFile.h>
#include <TTree.h>
#include <TH1.h>
#include <TH2.h>
#include <TCanvas.h>
#include <TLegend.h>
#include <iostream>
#include <vector>

void TrackerPackerUnpackerPerformance(const char* file1, const char* file2) {
    // Open both files
    TFile* f1 = TFile::Open(file1);
    TFile* f2 = TFile::Open(file2);
    
    if (!f1 || f1->IsZombie()) {
        std::cerr << "Error: Cannot open file " << file1 << std::endl;
        return;
    }
    if (!f2 || f2->IsZombie()) {
        std::cerr << "Error: Cannot open file " << file2 << std::endl;
        return;
    }
    
    // Get the trees
    TTree* tree1 = (TTree*)f1->Get("ClusterAnalyzer/ClusterTree");
    TTree* tree2 = (TTree*)f2->Get("ClusterAnalyzer/ClusterTree");
    
    if (!tree1 || !tree2) {
        std::cerr << "Error: ClusterTree not found in one of the files" << std::endl;
        return;
    }
    
    // Get the number of entries
    Long64_t nEntries1 = tree1->GetEntries();
    Long64_t nEntries2 = tree2->GetEntries();
    
    std::cout << "File 1: " << file1 << " has " << nEntries1 << " events" << std::endl;
    std::cout << "File 2: " << file2 << " has " << nEntries2 << " events" << std::endl;
    
    // Create histograms for clusterRow
    TH1D* h1 = new TH1D("h1", "clusterRow from File 1", 100, 0, 1000);
    TH1D* h2 = new TH1D("h2", "clusterRow from File 2", 100, 0, 1000);
    
    // Fill histograms from the trees
    tree1->Draw("clusterRow>>h1");
    tree2->Draw("clusterRow>>h2");
    
    // Normalize histograms
    h1->Scale(1.0 / h1->Integral());
    h2->Scale(1.0 / h2->Integral());
    
    // Create a canvas
    TCanvas* c1 = new TCanvas("c1", "", 800, 600);
    
    // Set histogram styles with transparency
    h1->SetLineColor(kBlue);
    h1->SetLineWidth(2);
    h1->SetFillColor(kBlue);
    h1->SetFillStyle(3001);  // Solid fill with transparency
    h1->SetFillColorAlpha(kBlue, 0.3);  // 30% opacity
    
    h2->SetLineColor(kRed);
    h2->SetLineWidth(2);
    h2->SetFillColor(kRed);
    h2->SetFillStyle(3000);
    h2->SetFillColorAlpha(kRed, 0.3);  // 30% opacity
    
    // Draw histograms
    h1->Draw("hist");
    h2->Draw("histsame");  // Draw filled histogram on same canvas
    
    // Add title and labels
    h1->SetTitle("");
    h1->GetXaxis()->SetTitle("clusterRow");
    h1->GetYaxis()->SetTitle("Fraction of Entries");
    
    // Add legend at bottom left
    TLegend* leg = new TLegend(0.12, 0.12, 0.35, 0.28);  // x1, y1, x2, y2 in NDC coordinates
    leg->AddEntry(h1, file1, "f");
    leg->AddEntry(h2, file2, "f");
    leg->SetBorderSize(1);      // Add a border
    leg->SetFillColor(0);        // White background (0 = white)
    leg->Draw();
    
    // Print some statistics
    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "File 1 - Mean: " << h1->GetMean() << ", StdDev: " << h1->GetRMS() << std::endl;
    std::cout << "File 2 - Mean: " << h2->GetMean() << ", StdDev: " << h2->GetRMS() << std::endl;
    
    // Calculate and print chi2 for comparison
    double chi2 = 0;
    int ndf = 0;
    for (int i = 1; i <= h1->GetNbinsX(); ++i) {
        double val1 = h1->GetBinContent(i);
        double val2 = h2->GetBinContent(i);
        double err1 = h1->GetBinError(i);
        double err2 = h2->GetBinError(i);
        
        if (val1 > 0 || val2 > 0) {
            if (err1 > 0 && err2 > 0) {
                double diff = val1 - val2;
                double err = sqrt(err1*err1 + err2*err2);
                chi2 += (diff * diff) / (err * err);
                ndf++;
            }
        }
    }
    if (ndf > 0) {
        std::cout << "Chi2/ndf: " << chi2 << "/" << ndf << " = " << chi2/ndf << std::endl;
    }
    
    // Save the canvas
    c1->Print("clusterRow_comparison.pdf");
    
    // Clean up
    delete leg;
    
    std::cout << "\nPlots saved to clusterRow_comparison.pdf and clusterRow_comparison.png" << std::endl;
}