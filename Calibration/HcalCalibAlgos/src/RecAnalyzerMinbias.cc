// system include files
#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

// user include files
#include "CondFormats/DataRecord/interface/HcalRespCorrsRcd.h"
#include "CondFormats/HcalObjects/interface/HcalRespCorrs.h"
#include "FWCore/Common/interface/TriggerNames.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Common/interface/TriggerResults.h"
#include "DataFormats/HcalDetId/interface/HcalDetId.h"
#include "DataFormats/HcalDetId/interface/HcalSubdetector.h"
#include "DataFormats/HcalRecHit/interface/HcalRecHitCollections.h"
#include "DataFormats/L1GlobalTrigger/interface/L1GlobalTriggerObjectMap.h"
#include "DataFormats/L1GlobalTrigger/interface/L1GlobalTriggerObjectMapRecord.h"
#include "Geometry/CaloTopology/interface/HcalTopology.h"

#include "TH1D.h"
#include "TFile.h"
#include "TTree.h"

// class declaration
class RecAnalyzerMinbias : public edm::EDAnalyzer {

public:
  explicit RecAnalyzerMinbias(const edm::ParameterSet&);
  ~RecAnalyzerMinbias();

  virtual void analyze(const edm::Event&, const edm::EventSetup&) override;
  virtual void beginJob();
  virtual void beginRun(edm::Run const& iRun, edm::EventSetup const& iSetup);
  virtual void endJob();
    
private:
  void analyzeHcal(const HBHERecHitCollection&, const HFRecHitCollection&, int, bool);
    
  // ----------member data ---------------------------
  std::string                fOutputFileName_;
  bool                       theRecalib_, ignoreL1_, runNZS_, fillHist_, init_;
  double                     eLowHB_, eHighHB_, eLowHE_, eHighHE_;
  double                     eLowHF_, eHighHF_;
  std::map<DetId,double>     corrFactor_;
  std::vector<unsigned int>  hcalID_;
  TFile                     *hOutputFile_;
  TTree                     *myTree_;
  std::vector<TH1D*>         histo_;
  std::map<HcalDetId,TH1D*>  histHB_, histHE_, histHF_;
  std::vector<int>           trigbit_;
  double                     rnnum_;
  struct myInfo{
    double theMB0, theMB1, theMB2, theMB3, theMB4, runcheck;
    myInfo() {
      theMB0 = theMB1 = theMB2 = theMB3 = theMB4 = runcheck = 0;
    }
  };
  // Root tree members
  double                     rnnumber;
  int                        mysubd, depth, iphi, ieta, cells, trigbit;
  float                      mom0_MB, mom1_MB, mom2_MB, mom3_MB, mom4_MB;
  std::map<std::pair<int,HcalDetId>,myInfo>        myMap_;
  edm::EDGetTokenT<HBHERecHitCollection>           tok_hbherecoMB_;
  edm::EDGetTokenT<HFRecHitCollection>             tok_hfrecoMB_;
  edm::EDGetTokenT<L1GlobalTriggerObjectMapRecord> tok_hltL1GtMap_;
};

// constructors and destructor
RecAnalyzerMinbias::RecAnalyzerMinbias(const edm::ParameterSet& iConfig) :
  init_(false) {

  // get name of output file with histogramms
  runNZS_               = iConfig.getParameter<bool>("RunNZS");
  eLowHB_               = iConfig.getParameter<double>("ELowHB");
  eHighHB_              = iConfig.getParameter<double>("EHighHB");
  eLowHE_               = iConfig.getParameter<double>("ELowHE");
  eHighHE_              = iConfig.getParameter<double>("EHighHE");
  eLowHF_               = iConfig.getParameter<double>("ELowHF");
  eHighHF_              = iConfig.getParameter<double>("EHighHF");
  fOutputFileName_      = iConfig.getUntrackedParameter<std::string>("HistOutFile");
  trigbit_              = iConfig.getUntrackedParameter<std::vector<int>>("TriggerBits");
  ignoreL1_             = iConfig.getUntrackedParameter<bool>("IgnoreL1",false);
  std::string      cfile= iConfig.getUntrackedParameter<std::string>("CorrFile");
  fillHist_             = iConfig.getUntrackedParameter<bool>("FillHisto",false);
  std::vector<int> ieta = iConfig.getUntrackedParameter<std::vector<int>>("HcalIeta");
  std::vector<int> iphi = iConfig.getUntrackedParameter<std::vector<int>>("HcalIphi");
  std::vector<int> depth= iConfig.getUntrackedParameter<std::vector<int>>("HcalDepth");

  // get token names of modules, producing object collections
  tok_hbherecoMB_   = consumes<HBHERecHitCollection>(iConfig.getParameter<edm::InputTag>("hbheInputMB"));
  tok_hfrecoMB_     = consumes<HFRecHitCollection>(iConfig.getParameter<edm::InputTag>("hfInputMB"));
  tok_hltL1GtMap_   = consumes<L1GlobalTriggerObjectMapRecord>(edm::InputTag("hltL1GtObjectMap"));

  // Read correction factors
  std::ifstream infile(cfile);
  if (!infile.is_open()) {
    theRecalib_ = false;
    edm::LogInfo("AnalyzerMB") << "Cannot open '" << cfile 
			       << "' for the correction file";
  } else {
    unsigned int ndets(0), nrec(0);
    while(1) {
      unsigned int id;
      double       cfac;
      infile >> id >> cfac;
      if (!infile.good()) break;
      HcalDetId detId(id);
      nrec++;
      std::map<DetId,double>::iterator itr = corrFactor_.find(detId);
      if (itr == corrFactor_.end()) {
	corrFactor_[detId] = cfac;
	ndets++;
      }
    }
    infile.close();
    edm::LogInfo("AnalyzerMB") << "Reads " << nrec << " correction factors for "
			       << ndets << " detIds";
    theRecalib_ = (ndets>0);
  }

  edm::LogInfo("AnalyzerMB") << " Flags (ReCalib): " << theRecalib_
			     << " (IgnoreL1): " << ignoreL1_ << " (NZS) " 
			     << runNZS_ << " and with " << ieta.size() 
			     << " detId for full histogram";
  edm::LogInfo("AnalyzerMB") << "Thresholds for HB " << eLowHB_ << ":" 
			     << eHighHB_ << "  for HE " << eLowHE_ << ":" 
			     << eHighHE_ << "  for HF " << eLowHF_ << ":" 
			     << eHighHF_;
  for (unsigned int k=0; k<ieta.size(); ++k) {
    HcalSubdetector subd = ((std::abs(ieta[k]) > 29) ? HcalForward : 
			    (std::abs(ieta[k]) > 16) ? HcalEndcap :
			    ((std::abs(ieta[k]) == 16) && (depth[k] == 3)) ? HcalEndcap :
			    (depth[k] == 4) ? HcalOuter : HcalBarrel);
    unsigned int id = (HcalDetId(subd,ieta[k],iphi[k],depth[k])).rawId();
    hcalID_.push_back(id);
    edm::LogInfo("AnalyzerMB") << "DetId[" << k << "] " << HcalDetId(id);
  }
  edm::LogInfo("AnalyzerMB") << "Select on " << trigbit_.size() 
			     << " L1 Trigger selection";
  for (unsigned int k=0; k<trigbit_.size(); ++k)
    edm::LogInfo("AnalyzerMB") << "Bit[" << k << "] " << trigbit_[k];
}
  
RecAnalyzerMinbias::~RecAnalyzerMinbias() {}
  
void RecAnalyzerMinbias::beginJob() {

  std::string hc[5] = {"Empty", "HB", "HE", "HO", "HF"};
  char        name[700], title[700];
  for (unsigned int i=0; i<hcalID_.size(); i++) {
    HcalDetId id = HcalDetId(hcalID_[i]);
    int subdet   = id.subdetId();
    sprintf (name, "%s%d_%d_%d", hc[subdet].c_str(), id.ieta(), id.iphi(), id.depth());
    sprintf (title, "Energy Distribution for %s ieta %d iphi %d depth %d", hc[subdet].c_str(), id.ieta(), id.iphi(), id.depth());
    double xmin = (subdet == 4) ? -10 : -1;
    double xmax = (subdet == 4) ? 90 : 9;
    TH1D*  hh   = new TH1D(name, title, 50, xmin, xmax);
    histo_.push_back(hh);
  };

  hOutputFile_  = new TFile(fOutputFileName_.c_str(), "RECREATE");
  myTree_       = new TTree("RecJet","RecJet Tree");
  myTree_->Branch("cells",    &cells,    "cells/I");
  myTree_->Branch("mysubd",   &mysubd,   "mysubd/I");
  myTree_->Branch("depth",    &depth,    "depth/I");
  myTree_->Branch("ieta",     &ieta,     "ieta/I");
  myTree_->Branch("iphi",     &iphi,     "iphi/I");
  myTree_->Branch("mom0_MB",  &mom0_MB,  "mom0_MB/F");
  myTree_->Branch("mom1_MB",  &mom1_MB,  "mom1_MB/F");
  myTree_->Branch("mom2_MB",  &mom2_MB,  "mom2_MB/F");
  myTree_->Branch("mom3_MB",  &mom2_MB,  "mom3_MB/F");
  myTree_->Branch("mom4_MB",  &mom4_MB,  "mom4_MB/F");
  myTree_->Branch("trigbit",  &trigbit,  "trigbit/I");
  myTree_->Branch("rnnumber", &rnnumber, "rnnumber/D");

  myMap_.clear();
}

  
void RecAnalyzerMinbias::beginRun(edm::Run const&, edm::EventSetup const& iS) {
 
  if (!init_) {
    init_ = true;
    if (fillHist_) {
      edm::ESHandle<HcalTopology> htopo;
      iS.get<IdealGeometryRecord>().get(htopo);
      if (htopo.isValid()) {
	const HcalTopology* hcaltopology = htopo.product();

	char  name[700], title[700];
	// For HB
	int maxDepthHB = hcaltopology->maxDepthHB();
	int nbinHB     = int(200*eHighHB_);
	for (int eta = -50; eta < 50; eta++) {
	  for (int phi = 0; phi < 100; phi++) {
	    for (int depth = 1; depth < maxDepthHB; depth++) {
	      HcalDetId cell (HcalBarrel, eta, phi, depth);
	      if (hcaltopology->valid(cell)) {
		sprintf (name, "HBeta%dphi%ddep%d", eta, phi, depth);
		sprintf (title,"Energy (HB #eta %d #phi %d depth %d)", eta, phi, depth);
		TH1D* h = new TH1D(name, title, nbinHB, 0, 2*eHighHB_);
		histHB_[cell] = h;
	      }
	    }
	  }
	}
	// For HE
	int maxDepthHE = hcaltopology->maxDepthHB();
	int nbinHE     = int(200*eHighHE_);
	for (int eta = -50; eta < 50; eta++) {
	  for (int phi = 0; phi < 100; phi++) {
	    for (int depth = 1; depth < maxDepthHE; depth++) {
	      HcalDetId cell (HcalEndcap, eta, phi, depth);
	      if (hcaltopology->valid(cell)) {
		sprintf (name, "HEeta%dphi%ddep%d", eta, phi, depth);
		sprintf (title,"Energy (HE #eta %d #phi %d depth %d)", eta, phi, depth);
		TH1D* h = new TH1D(name, title, nbinHE, 0, 2*eHighHE_);
		histHE_[cell] = h;
	      }
	    }
	  }
	}
	// For HF
	int maxDepthHF = 4;
	int nbinHF     = int(200*eHighHF_);
	for (int eta = -50; eta < 50; eta++) {
	  for (int phi = 0; phi < 100; phi++) {
	    for (int depth = 1; depth < maxDepthHF; depth++) {
	      HcalDetId cell (HcalForward, eta, phi, depth);
	      if (hcaltopology->valid(cell)) {
		sprintf (name, "HFeta%dphi%ddep%d", eta, phi, depth);
		sprintf (title,"Energy (HF #eta %d #phi %d depth %d)", eta, phi, depth);
		TH1D* h = new TH1D(name, title, nbinHF, 0, 2*eHighHF_);
		histHF_[cell] = h;
	      }
	    }
	  }
	}
      }
    }
  }
}

//  EndJob
//
void RecAnalyzerMinbias::endJob() {

  cells = 0;
  for (std::map<std::pair<int,HcalDetId>,myInfo>::const_iterator itr=myMap_.begin(); itr != myMap_.end(); ++itr) {
    edm::LogInfo("AnalyzerMB") << "Fired trigger bit number "<<itr->first.first;
    myInfo info = itr->second;
    if (info.theMB0 > 0) { 
      mom0_MB  = info.theMB0;
      mom1_MB  = info.theMB1;
      mom2_MB  = info.theMB2;
      mom3_MB  = info.theMB3;
      mom4_MB  = info.theMB4;
      rnnumber = info.runcheck;
      trigbit  = itr->first.first;
      mysubd   = itr->first.second.subdet();
      depth    = itr->first.second.depth();
      iphi     = itr->first.second.iphi();
      ieta     = itr->first.second.ieta();
      edm::LogInfo("AnalyzerMB") << " Result=  " << trigbit << " " << mysubd 
				 << " " << ieta << " " << iphi << " mom0  " 
				 << mom0_MB << " mom1 " << mom1_MB << " mom2 " 
				 << mom2_MB << " mom3 " << mom3_MB << " mom4 " 
				 << mom4_MB;
      myTree_->Fill();
      cells++;
    }
  }
  edm::LogInfo("AnalyzerMB") << "cells" << " " << cells;
  
  hOutputFile_->Write();   
  hOutputFile_->cd();
  for (unsigned int i=0; i<histo_.size(); i++) histo_[i]->Write();
  for (std::map<HcalDetId,TH1D*>::iterator itr=histHB_.begin();
       itr != histHB_.end(); ++itr) itr->second->Write();
  for (std::map<HcalDetId,TH1D*>::iterator itr=histHE_.begin();
       itr != histHE_.end(); ++itr) itr->second->Write();
  for (std::map<HcalDetId,TH1D*>::iterator itr=histHF_.begin();
       itr != histHF_.end(); ++itr) itr->second->Write();
  myTree_->Write();
  hOutputFile_->Close() ;
}

//
// member functions
//
// ------------ method called to produce the data  ------------
  
void RecAnalyzerMinbias::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) {
    
  rnnum_ = (float)iEvent.run(); 
    
  edm::Handle<HBHERecHitCollection> hbheMB;
  iEvent.getByToken(tok_hbherecoMB_, hbheMB);
  if (!hbheMB.isValid()) {
    edm::LogWarning("AnalyzerMB") << "HcalCalibAlgos: Error! can't get hbhe product!";
    return ;
  }
  const HBHERecHitCollection HithbheMB = *(hbheMB.product());
  edm::LogInfo("AnalyzerMB") << "HBHE MB size of collection "<<HithbheMB.size();
  if (HithbheMB.size() < 5100 && runNZS_) {
    edm::LogWarning("AnalyzerMB") << "HBHE problem " << rnnum_ << " size "
				  << HithbheMB.size();
    return;
  }
    
  edm::Handle<HFRecHitCollection> hfMB;
  iEvent.getByToken(tok_hfrecoMB_, hfMB);
  if (!hfMB.isValid()) {
    edm::LogWarning("AnalyzerMB") << "HcalCalibAlgos: Error! can't get hbhe product!";
    return ;
  }
  const HFRecHitCollection HithfMB = *(hfMB.product());
  edm::LogInfo("AnalyzerMB") << "HF MB size of collection " << HithfMB.size();
  if (HithfMB.size() < 1700 && runNZS_) {
    edm::LogWarning("AnalyzerMB") << "HF problem " << rnnum_ << " size "
				  << HithfMB.size();
    return;
  }

  bool select(false);
  if (trigbit_.size() > 0) {
    edm::Handle<L1GlobalTriggerObjectMapRecord> gtObjectMapRecord;
    iEvent.getByToken(tok_hltL1GtMap_, gtObjectMapRecord);
    if (gtObjectMapRecord.isValid()) {
      const std::vector<L1GlobalTriggerObjectMap>& objMapVec = gtObjectMapRecord->gtObjectMap();
      for (std::vector<L1GlobalTriggerObjectMap>::const_iterator itMap = objMapVec.begin();
           itMap != objMapVec.end(); ++itMap) {
        bool resultGt = (*itMap).algoGtlResult();
        if (resultGt) {
          int algoBit = (*itMap).algoBitNumber();
          if (std::find(trigbit_.begin(),trigbit_.end(),algoBit) != 
	      trigbit_.end()) {
            select = true;
            break;
          }
        }
      }
    }
  }

  if (ignoreL1_ || ((trigbit_.size() > 0) && select)) {
    analyzeHcal(HithbheMB, HithfMB, 1, true);
  } else if ((!ignoreL1_) && (trigbit_.size() == 0)) {
    edm::Handle<L1GlobalTriggerObjectMapRecord> gtObjectMapRecord;
    iEvent.getByToken(tok_hltL1GtMap_, gtObjectMapRecord);
    if (gtObjectMapRecord.isValid()) {
      const std::vector<L1GlobalTriggerObjectMap>& objMapVec = gtObjectMapRecord->gtObjectMap();
      bool ok(false);
      for (std::vector<L1GlobalTriggerObjectMap>::const_iterator itMap = objMapVec.begin();
	   itMap != objMapVec.end(); ++itMap) {
	bool resultGt = (*itMap).algoGtlResult();
	if (resultGt) {
	  int algoBit = (*itMap).algoBitNumber();
	  analyzeHcal(HithbheMB, HithfMB, algoBit, (!ok));
	  ok          = true;
	} 
      }
      if (!ok) {
	edm::LogInfo("AnalyzerMB") << "No passed L1 Trigger found";
      }
    }
  }
}

void RecAnalyzerMinbias::analyzeHcal(const HBHERecHitCollection & HithbheMB,
				     const HFRecHitCollection & HithfMB,
				     int algoBit, bool fill) {
  // Signal part for HB HE
  for (HBHERecHitCollection::const_iterator hbheItr=HithbheMB.begin(); 
       hbheItr!=HithbheMB.end(); hbheItr++) {
    // Recalibration of energy
    DetId mydetid = hbheItr->id().rawId();
    double icalconst(1.);	 
    if (theRecalib_) {
      std::map<DetId,double>::iterator itr = corrFactor_.find(mydetid);
      if (itr != corrFactor_.end()) icalconst = itr->second;
    }
    HBHERecHit aHit(hbheItr->id(),hbheItr->energy()*icalconst,hbheItr->time());
    double energyhit = aHit.energy();
    DetId id         = (*hbheItr).detid(); 
    HcalDetId hid    = HcalDetId(id);
    double eLow      = (hid.subdet() == HcalEndcap) ? eLowHE_  : eLowHB_;
    double eHigh     = (hid.subdet() == HcalEndcap) ? eHighHE_ : eHighHB_;
    if (fill) {
      for (unsigned int i = 0; i < hcalID_.size(); i++) {
	if (hcalID_[i] == id.rawId()) {
	  histo_[i]->Fill(energyhit);
	  break;
	}
      }
      std::map<HcalDetId,TH1D*>::iterator itr1 = histHB_.find(hid);
      if (itr1 != histHB_.end()) itr1->second->Fill(energyhit);
      std::map<HcalDetId,TH1D*>::iterator itr2 = histHE_.find(hid);
      if (itr2 != histHE_.end()) itr2->second->Fill(energyhit);
    }

    if (runNZS_ || (energyhit >= eLow && energyhit <= eHigh)) {
      std::map<std::pair<int,HcalDetId>,myInfo>::iterator itr1 = myMap_.find(std::pair<int,HcalDetId>(algoBit,hid));
      if (itr1 == myMap_.end()) {
	myInfo info;
	myMap_[std::pair<int,HcalDetId>(algoBit,hid)] = info;
	itr1 = myMap_.find(std::pair<int,HcalDetId>(algoBit,hid));
      } 
      itr1->second.theMB0++;
      itr1->second.theMB1 += energyhit;
      itr1->second.theMB2 += (energyhit*energyhit);
      itr1->second.theMB3 += (energyhit*energyhit*energyhit);
      itr1->second.theMB4 += (energyhit*energyhit*energyhit*energyhit);
      itr1->second.runcheck = rnnum_;
    }
  } // HBHE_MB
 
  // Signal part for HF
  for (HFRecHitCollection::const_iterator hfItr=HithfMB.begin(); 
       hfItr!=HithfMB.end(); hfItr++) {
    // Recalibration of energy
    DetId mydetid = hfItr->id().rawId();
    double icalconst(1.);	 
    if (theRecalib_) {
      std::map<DetId,double>::iterator itr = corrFactor_.find(mydetid);
      if (itr != corrFactor_.end()) icalconst = itr->second;
    }
    HFRecHit aHit(hfItr->id(),hfItr->energy()*icalconst,hfItr->time());
    
    double energyhit = aHit.energy();
    DetId id         = (*hfItr).detid(); 
    HcalDetId hid    = HcalDetId(id);
    if (fill) {
      for (unsigned int i = 0; i < hcalID_.size(); i++) {
	if (hcalID_[i] == id.rawId()) {
	  histo_[i]->Fill(energyhit);
	  break;
	}
      }
      std::map<HcalDetId,TH1D*>::iterator itr1 = histHF_.find(hid);
      if (itr1 != histHF_.end()) itr1->second->Fill(energyhit);
    }

    //
    // Remove PMT hits
    //	 
    if ((runNZS_ && fabs(energyhit) <= 40.) || 
	(energyhit >= eLowHF_ && energyhit <= eHighHF_)) {
      std::map<std::pair<int,HcalDetId>,myInfo>::iterator itr1 = myMap_.find(std::pair<int,HcalDetId>(algoBit,hid));
      if (itr1 == myMap_.end()) {
	myInfo info;
	myMap_[std::pair<int,HcalDetId>(algoBit,hid)] = info;
	itr1 = myMap_.find(std::pair<int,HcalDetId>(algoBit,hid));
      }
      itr1->second.theMB0++;
      itr1->second.theMB1 += energyhit;
      itr1->second.theMB2 += (energyhit*energyhit);
      itr1->second.theMB3 += (energyhit*energyhit*energyhit);
      itr1->second.theMB4 += (energyhit*energyhit*energyhit*energyhit);
      itr1->second.runcheck = rnnum_;
    }
  }
}

//define this as a plug-in                                                      
#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(RecAnalyzerMinbias);
