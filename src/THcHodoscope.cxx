///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// THcHodoscope                                                              //
//                                                                           //
// Class for a generic hodoscope consisting of multiple                      //
// planes with multiple paddles with phototubes on both ends.                //
// This differs from Hall A scintillator class in that it is the whole       //
// hodoscope array, not just one plane.                                      //
//                                                                           //
// Date July 8 2014:                                                         //
// Zafr Ahmed                                                                //
// Beta and chis square are calculated for each of the hodoscope track.      //
// Two new variables are added. fBeta and fBetaChisq                         //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "THcSignalHit.h"
#include "THcHodoHit.h"
#include "THcShower.h"
#include "THcCherenkov.h"
#include "THcHallCSpectrometer.h"

#include "THcHitList.h"
#include "THcRawShowerHit.h"
#include "TClass.h"
#include "math.h"
#include "THaSubDetector.h"

#include "THcHodoscope.h"
#include "THaEvData.h"
#include "THaDetMap.h"
#include "THcDetectorMap.h"
#include "THaGlobals.h"
#include "THaCutList.h"
#include "THcGlobals.h"
#include "THcParmList.h"
#include "VarDef.h"
#include "VarType.h"
#include "THaTrack.h"
#include "TClonesArray.h"
#include "TMath.h"

#include "THaTrackProj.h"
#include <vector>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>

using namespace std;
using std::vector;

//_____________________________________________________________________________
THcHodoscope::THcHodoscope( const char* name, const char* description,
				  THaApparatus* apparatus ) :
  THaNonTrackingDetector(name,description,apparatus)
{
  // Constructor

  //fTrackProj = new TClonesArray( "THaTrackProj", 5 );
  // Construct the planes
  fNPlanes = 0;			// No planes until we make them
  fStartTime=-1e5;
  fGoodStartTime=kFALSE;
}

//_____________________________________________________________________________
THcHodoscope::THcHodoscope( ) :
  THaNonTrackingDetector()
{
  // Constructor
}

//_____________________________________________________________________________
void THcHodoscope::Setup(const char* name, const char* description)
{

  //  static const char* const here = "Setup()";
  //  static const char* const message = 
  //    "Must construct %s detector with valid name! Object construction failed.";

  cout << "In THcHodoscope::Setup()" << endl;
  // Base class constructor failed?
  if( IsZombie()) return;

  fDebug   = 1;  // Keep this at one while we're working on the code    

  char prefix[2];

  prefix[0]=tolower(GetApparatus()->GetName()[0]);
  prefix[1]='\0';

  string planenamelist;
  DBRequest listextra[]={
    {"hodo_num_planes", &fNPlanes, kInt},
    {"hodo_plane_names",&planenamelist, kString},
    {0}
  };
  //fNPlanes = 4; 		// Default if not defined
  gHcParms->LoadParmValues((DBRequest*)&listextra,prefix);
  
  cout << "Plane Name List : " << planenamelist << endl;

  vector<string> plane_names = vsplit(planenamelist);
  // Plane names  
  if(plane_names.size() != (UInt_t) fNPlanes) {
    cout << "ERROR: Number of planes " << fNPlanes << " doesn't agree with number of plane names " << plane_names.size() << endl;
    // Should quit.  Is there an official way to quit?
  }
  fPlaneNames = new char* [fNPlanes];
  for(Int_t i=0;i<fNPlanes;i++) {
    fPlaneNames[i] = new char[plane_names[i].length()+1];
    strcpy(fPlaneNames[i], plane_names[i].c_str());
  }

  // Probably shouldn't assume that description is defined
  char* desc = new char[strlen(description)+100];
  fPlanes = new THcScintillatorPlane* [fNPlanes];
  for(Int_t i=0;i < fNPlanes;i++) {
    strcpy(desc, description);
    strcat(desc, " Plane ");
    strcat(desc, fPlaneNames[i]);
    fPlanes[i] = new THcScintillatorPlane(fPlaneNames[i], desc, i+1, this); // Number planes starting from zero!!
    cout << "Created Scintillator Plane " << fPlaneNames[i] << ", " << desc << endl;
  }

  // --------------- To get energy from THcShower ----------------------
  const char* shower_detector_name = "cal";  
  //  THaApparatus* app;
  THcHallCSpectrometer *app = dynamic_cast<THcHallCSpectrometer*>(GetApparatus());
  THaDetector* det = app->GetDetector( shower_detector_name );

  if( dynamic_cast<THcShower*>(det) ) {
    fShower = dynamic_cast<THcShower*>(det);
  }
  else if( !dynamic_cast<THcShower*>(det) ) {
    cout << "Warining: calorimeter analysis module " 
	 << shower_detector_name << " not loaded for spectrometer "
	 << prefix << endl;
    
    fShower = NULL;
  }
  
  // --------------- To get energy from THcShower ----------------------

  // --------------- To get NPEs from THcCherenkov -------------------
  const char* chern_detector_name = "cher";
  THaDetector* detc = app->GetDetector( chern_detector_name );
  
  if( dynamic_cast<THcCherenkov*>(detc) ) {
    fChern = dynamic_cast<THcCherenkov*>(detc);  
  }
  else if( !dynamic_cast<THcCherenkov*>(detc) ) {
    cout << "Warining: Cherenkov detector analysis module " 
	 << chern_detector_name << " not loaded for spectrometer "
	 << prefix << endl;
    
    fChern = NULL;
  }
  
  // --------------- To get NPEs from THcCherenkov -------------------

  fScinShould = 0;
  fScinDid = 0;
  gHcParms->Define(Form("%shodo_did",prefix),"Total hodo tracks",fScinDid);
  gHcParms->Define(Form("%shodo_should",prefix),"Total hodo triggers",fScinShould);

  // Save the nominal particle mass
  fPartMass = app->GetParticleMass();
  fBetaNominal = app->GetBetaAtPcentral();

  delete [] desc;
}

//_____________________________________________________________________________
THaAnalysisObject::EStatus THcHodoscope::Init( const TDatime& date )
{
  cout << "In THcHodoscope::Init()" << endl;
  Setup(GetName(), GetTitle());

  // Should probably put this in ReadDatabase as we will know the
  // maximum number of hits after setting up the detector map
  // But it needs to happen before the sub detectors are initialized
  // so that they can get the pointer to the hitlist.


  InitHitList(fDetMap, "THcRawHodoHit", 100);

  EStatus status;
  // This triggers call of ReadDatabase and DefineVariables
  if( (status = THaNonTrackingDetector::Init( date )) )
    return fStatus=status;

  for(Int_t ip=0;ip<fNPlanes;ip++) {
    if((status = fPlanes[ip]->Init( date ))) {
      return fStatus=status;
    }
  }

  // Replace with what we need for Hall C
  //  const DataDest tmp[NDEST] = {
  //    { &fRTNhit, &fRANhit, fRT, fRT_c, fRA, fRA_p, fRA_c, fROff, fRPed, fRGain },
  //    { &fLTNhit, &fLANhit, fLT, fLT_c, fLA, fLA_p, fLA_c, fLOff, fLPed, fLGain }
  //  };
  //  memcpy( fDataDest, tmp, NDEST*sizeof(DataDest) );

  char EngineDID[]="xSCIN";
  EngineDID[0] = toupper(GetApparatus()->GetName()[0]);
  if( gHcDetectorMap->FillMap(fDetMap, EngineDID) < 0 ) {
    static const char* const here = "Init()";
    Error( Here(here), "Error filling detectormap for %s.", 
	     EngineDID);
      return kInitError;
  }

  fNScinHits     = new Int_t [fNPlanes];
  fGoodPlaneTime = new Bool_t [fNPlanes];
  fNPlaneTime    = new Int_t [fNPlanes];
  fSumPlaneTime  = new Double_t [fNPlanes];

  //  Double_t  fHitCnt4 = 0., fHitCnt3 = 0.;
  
  // Int_t m = 0;
  // fScinHit = new Double_t*[fNPlanes];         
  // for ( m = 0; m < fNPlanes; m++ ){
  //   fScinHit[m] = new Double_t[fNPaddle[0]];
  // }
  

  return fStatus = kOK;
}
//_____________________________________________________________________________
Int_t THcHodoscope::ReadDatabase( const TDatime& date )
{

  // Read this detector's parameters from the database file 'fi'.
  // This function is called by THaDetectorBase::Init() once at the
  // beginning of the analysis.
  // 'date' contains the date/time of the run being analyzed.

  //  static const char* const here = "ReadDatabase()";
  char prefix[2];
  char parname[100];

  // Read data from database 
  // Pull values from the THcParmList instead of reading a database
  // file like Hall A does.

  // Will need to determine which spectrometer in order to construct
  // the parameter names (e.g. hscin_1x_nr vs. sscin_1x_nr)

  prefix[0]=tolower(GetApparatus()->GetName()[0]);
  //
  prefix[1]='\0';
  strcpy(parname,prefix);
  strcat(parname,"scin_");
  //  Int_t plen=strlen(parname);
  cout << " readdatabse hodo fnplanes = " << fNPlanes << endl;

  fBetaP = 0.;
  fBetaNoTrk = 0.;
  fBetaNoTrkChiSq = 0.;
  
  fNPaddle = new UInt_t [fNPlanes];
  fFPTime = new Double_t [fNPlanes];
  fPlaneCenter = new Double_t[fNPlanes];
  fPlaneSpacing = new Double_t[fNPlanes];

  prefix[0]=tolower(GetApparatus()->GetName()[0]);
  //
  prefix[1]='\0';

  for(Int_t i=0;i<fNPlanes;i++) {
    
    DBRequest list[]={
      {Form("scin_%s_nr",fPlaneNames[i]), &fNPaddle[i], kInt},
      {0}
    };
    gHcParms->LoadParmValues((DBRequest*)&list,prefix);
  }

  // GN added
  // reading variables from *hodo.param
  fMaxScinPerPlane=fNPaddle[0];
  for (Int_t i=1;i<fNPlanes;i++) {
    fMaxScinPerPlane=(fMaxScinPerPlane > fNPaddle[i])? fMaxScinPerPlane : fNPaddle[i];
  }
// need this for "padded arrays" i.e. 4x16 lists of parameters (GN)
  fMaxHodoScin=fMaxScinPerPlane*fNPlanes; 
  if (fDebug>=1)  cout <<"fMaxScinPerPlane = "<<fMaxScinPerPlane<<" fMaxHodoScin = "<<fMaxHodoScin<<endl;
  
  fHodoVelLight=new Double_t [fMaxHodoScin];
  fHodoPosSigma=new Double_t [fMaxHodoScin];
  fHodoNegSigma=new Double_t [fMaxHodoScin];
  fHodoPosMinPh=new Double_t [fMaxHodoScin];
  fHodoNegMinPh=new Double_t [fMaxHodoScin];
  fHodoPosPhcCoeff=new Double_t [fMaxHodoScin];
  fHodoNegPhcCoeff=new Double_t [fMaxHodoScin];
  fHodoPosTimeOffset=new Double_t [fMaxHodoScin];
  fHodoNegTimeOffset=new Double_t [fMaxHodoScin];
  fHodoPosPedLimit=new Int_t [fMaxHodoScin];
  fHodoNegPedLimit=new Int_t [fMaxHodoScin];
  fHodoPosInvAdcOffset=new Double_t [fMaxHodoScin];
  fHodoNegInvAdcOffset=new Double_t [fMaxHodoScin];
  fHodoPosInvAdcLinear=new Double_t [fMaxHodoScin];
  fHodoNegInvAdcLinear=new Double_t [fMaxHodoScin];
  fHodoPosInvAdcAdc=new Double_t [fMaxHodoScin];
  fHodoNegInvAdcAdc=new Double_t [fMaxHodoScin];
  
  fNHodoscopes = 2;
  fxLoScin = new Int_t [fNHodoscopes]; 
  fxHiScin = new Int_t [fNHodoscopes]; 
  fyLoScin = new Int_t [fNHodoscopes]; 
  fyHiScin = new Int_t [fNHodoscopes]; 
  fHodoSlop = new Double_t [fNPlanes];

  DBRequest list[]={
    {"start_time_center",                &fStartTimeCenter,                      kDouble},
    {"start_time_slop",                  &fStartTimeSlop,                        kDouble},
    {"scin_tdc_to_time",                 &fScinTdcToTime,                        kDouble},
    {"scin_tdc_min",                     &fScinTdcMin,                           kDouble},
    {"scin_tdc_max",                     &fScinTdcMax,                           kDouble},
    {"tof_tolerance",                    &fTofTolerance,          kDouble,         0,  1},
    {"pathlength_central",               &fPathLengthCentral,                    kDouble},
    {"hodo_vel_light",                   &fHodoVelLight[0],       kDouble,  fMaxHodoScin},
    {"hodo_pos_sigma",                   &fHodoPosSigma[0],       kDouble,  fMaxHodoScin},
    {"hodo_neg_sigma",                   &fHodoNegSigma[0],       kDouble,  fMaxHodoScin},
    {"hodo_pos_minph",                   &fHodoPosMinPh[0],       kDouble,  fMaxHodoScin},
    {"hodo_neg_minph",                   &fHodoNegMinPh[0],       kDouble,  fMaxHodoScin},
    {"hodo_pos_phc_coeff",               &fHodoPosPhcCoeff[0],    kDouble,  fMaxHodoScin},
    {"hodo_neg_phc_coeff",               &fHodoNegPhcCoeff[0],    kDouble,  fMaxHodoScin},
    {"hodo_pos_time_offset",             &fHodoPosTimeOffset[0],  kDouble,  fMaxHodoScin},
    {"hodo_neg_time_offset",             &fHodoNegTimeOffset[0],  kDouble,  fMaxHodoScin},
    {"hodo_pos_ped_limit",               &fHodoPosPedLimit[0],    kInt,     fMaxHodoScin},
    {"hodo_neg_ped_limit",               &fHodoNegPedLimit[0],    kInt,     fMaxHodoScin},
    {"tofusinginvadc",                   &fTofUsingInvAdc,        kInt,            0,  1},       
    {"xloscin",                          &fxLoScin[0],            kInt,     (UInt_t) fNHodoscopes},
    {"xhiscin",                          &fxHiScin[0],            kInt,     (UInt_t) fNHodoscopes},
    {"yloscin",                          &fyLoScin[0],            kInt,     (UInt_t) fNHodoscopes},
    {"yhiscin",                          &fyHiScin[0],            kInt,     (UInt_t) fNHodoscopes},
    {"track_eff_test_num_scin_planes",   &fTrackEffTestNScinPlanes,                 kInt},
    {"cer_npe",                          &fNCerNPE,               kDouble,         0,  1},
    {"normalized_energy_tot",            &fNormETot,              kDouble,         0,  1},
    {"hodo_slop",                        fHodoSlop,               kDouble,  fNPlanes},
    {"debugprintscinraw",                &fdebugprintscinraw,               kInt,  0,1},
    {0}
  };
  fdebugprintscinraw=0;
  fTofUsingInvAdc = 0;		// Default if not defined
  fTofTolerance = 3.0;		// Default if not defined
  fNCerNPE = 2.0;
  fNormETot = 0.7;

  gHcParms->LoadParmValues((DBRequest*)&list,prefix);

  cout << " x1 lo = " << fxLoScin[0] 
       << " x2 lo = " << fxLoScin[1] 
       << " x1 hi = " << fxHiScin[0] 
       << " x2 hi = " << fxHiScin[1] 
       << endl;

  cout << " y1 lo = " << fyLoScin[0] 
       << " y2 lo = " << fyLoScin[1] 
       << " y1 hi = " << fyHiScin[0] 
       << " y2 hi = " << fyHiScin[1] 
       << endl;

  cout << "Hdososcope planes hits for trigger = " << fTrackEffTestNScinPlanes 
       << " normalized energy min = " << fNormETot
       << " number of photo electrons = " << fNCerNPE
       << endl;

  if (fTofUsingInvAdc) {
    DBRequest list2[]={
      {"hodo_pos_invadc_offset",&fHodoPosInvAdcOffset[0],kDouble,fMaxHodoScin},
      {"hodo_neg_invadc_offset",&fHodoNegInvAdcOffset[0],kDouble,fMaxHodoScin},
      {"hodo_pos_invadc_linear",&fHodoPosInvAdcLinear[0],kDouble,fMaxHodoScin},
      {"hodo_neg_invadc_linear",&fHodoNegInvAdcLinear[0],kDouble,fMaxHodoScin},
      {"hodo_pos_invadc_adc",&fHodoPosInvAdcAdc[0],kDouble,fMaxHodoScin},
      {"hodo_neg_invadc_adc",&fHodoNegInvAdcAdc[0],kDouble,fMaxHodoScin},
      {0}
    };
    gHcParms->LoadParmValues((DBRequest*)&list2,prefix);
  };
  if (fDebug >=1) {
    cout <<"******* Testing Hodoscope Parameter Reading ***\n";
    cout<<"StarTimeCenter = "<<fStartTimeCenter<<endl;
    cout<<"StartTimeSlop = "<<fStartTimeSlop<<endl;
    cout <<"ScintTdcToTime = "<<fScinTdcToTime<<endl;
    cout <<"TdcMin = "<<fScinTdcMin<<" TdcMax = "<<fScinTdcMax<<endl;
    cout <<"TofTolerance = "<<fTofTolerance<<endl;
    cout <<"*** VelLight ***\n";
    for (Int_t i1=0;i1<fNPlanes;i1++) {
      cout<<"Plane "<<i1<<endl;
      for (UInt_t i2=0;i2<fMaxScinPerPlane;i2++) {
	cout<<fHodoVelLight[GetScinIndex(i1,i2)]<<" ";
      }
      cout <<endl;
    }
    cout <<endl<<endl;
    // check fHodoPosPhcCoeff
    /*
    cout <<"fHodoPosPhcCoeff = ";
    for (int i1=0;i1<fMaxHodoScin;i1++) {
      cout<<this->GetHodoPosPhcCoeff(i1)<<" ";
    }
    cout<<endl;
    */
  }
  //
  if ((fTofTolerance > 0.5) && (fTofTolerance < 10000.)) {
    cout << "USING "<<fTofTolerance<<" NSEC WINDOW FOR FP NO_TRACK CALCULATIONS.\n";
  }
  else {
    fTofTolerance= 3.0;
    cout << "*** USING DEFAULT 3 NSEC WINDOW FOR FP NO_TRACK CALCULATIONS!! ***\n";
  }
  fIsInit = true;
  return kOK;
}

//_____________________________________________________________________________
Int_t THcHodoscope::DefineVariables( EMode mode )
{
  // Initialize global variables and lookup table for decoder
  cout << "THcHodoscope::DefineVariables called " << GetName() << endl;
  if( mode == kDefine && fIsSetup ) return kOK;
  fIsSetup = ( mode == kDefine );

  // Register variables in global list

  RVarDef vars[] = {
    // Move these into THcHallCSpectrometer using track fTracks
    {"betap",             "betaP",                "fBetaP"},
    {"betanotrack",       "Beta from scintillator hits",                "fBetaNoTrk"},
    {"betachisqnotrack",  "Chi square of beta from scintillator hits",  "fBetaNoTrkChiSq"},
    {"fpHitsTime",        "Time at focal plane from all hits",            "fFPTime"},
    {"starttime",         "Hodoscope Start Time",                         "fStartTime"},
    {"goodstarttime",     "Hodoscope Good Start Time",                    "fGoodStartTime"},
    {"goodscinhit",       "Hit in fid area",                              "fGoodScinHits"},
    //    {"goodscinhitx",    "Hit in fid x range",                     "fGoodScinHitsX"},
    {"scinshould",        "Total scin Hits in fid area",                  "fScinShould"},
    {"scindid",           "Total scin Hits in fid area with a track",     "fScinDid"},
    { 0 }
  };
  return DefineVarsFromList( vars, mode );
  //  return kOK;
}

//_____________________________________________________________________________
THcHodoscope::~THcHodoscope()
{
  // Destructor. Remove variables from global list.

  delete [] fFPTime;
  delete [] fPlaneCenter;
  delete [] fPlaneSpacing;

  if( fIsSetup )
    RemoveVariables();
  if( fIsInit )
    DeleteArrays();
  if (fTrackProj) {
    fTrackProj->Clear();
    delete fTrackProj; fTrackProj = 0;
  }
}

//_____________________________________________________________________________
void THcHodoscope::DeleteArrays()
{
  // Delete member arrays. Used by destructor.
  // Int_t k;  
  // for( k = 0; k < fNPlanes; k++){
  //   delete [] fScinHit[k];
  // }
  // delete [] fScinHit;
  
  delete [] fxLoScin;             fxLoScin = NULL;
  delete [] fxHiScin;             fxHiScin = NULL;
  delete [] fHodoSlop;            fHodoSlop = NULL;

  delete [] fNPaddle;             fNPaddle = NULL;
  delete [] fHodoVelLight;        fHodoVelLight = NULL;
  delete [] fHodoPosSigma;        fHodoPosSigma = NULL;
  delete [] fHodoNegSigma;        fHodoNegSigma = NULL;
  delete [] fHodoPosMinPh;        fHodoPosMinPh = NULL;
  delete [] fHodoNegMinPh;        fHodoNegMinPh = NULL;
  delete [] fHodoPosPhcCoeff;     fHodoPosPhcCoeff = NULL;
  delete [] fHodoNegPhcCoeff;     fHodoNegPhcCoeff = NULL;
  delete [] fHodoPosTimeOffset;   fHodoPosTimeOffset = NULL;
  delete [] fHodoNegTimeOffset;   fHodoNegTimeOffset = NULL;
  delete [] fHodoPosPedLimit;     fHodoPosPedLimit = NULL;
  delete [] fHodoNegPedLimit;     fHodoNegPedLimit = NULL;
  delete [] fHodoPosInvAdcOffset; fHodoPosInvAdcOffset = NULL;
  delete [] fHodoNegInvAdcOffset; fHodoNegInvAdcOffset = NULL;
  delete [] fHodoPosInvAdcLinear; fHodoPosInvAdcLinear = NULL;
  delete [] fHodoNegInvAdcLinear; fHodoNegInvAdcLinear = NULL;
  delete [] fHodoPosInvAdcAdc;    fHodoPosInvAdcAdc = NULL;
  delete [] fGoodPlaneTime;       fGoodPlaneTime = NULL;
  delete [] fNPlaneTime;          fNPlaneTime = NULL;
  delete [] fSumPlaneTime;        fSumPlaneTime = NULL;
  delete [] fNScinHits;           fNScinHits = NULL;

}

//_____________________________________________________________________________
inline 
void THcHodoscope::ClearEvent()
{

  fBetaP = 0.;
  fBetaNoTrk = 0.0;
  fBetaNoTrkChiSq = 0.0;

  for(Int_t ip=0;ip<fNPlanes;ip++) {
    fPlanes[ip]->Clear();
    fFPTime[ip]=0.;
    fPlaneCenter[ip]=0.;
    fPlaneSpacing[ip]=0.;
  }
  fdEdX.clear();
  fScinHitPaddle.clear();
  fNScinHit.clear();
  fNClust.clear();
  fThreeScin.clear();
  fGoodScinHitsX.clear();
}

//_____________________________________________________________________________
Int_t THcHodoscope::Decode( const THaEvData& evdata )
{
  ClearEvent();
  // Get the Hall C style hitlist (fRawHitList) for this event
  Int_t nhits = DecodeToHitList(evdata);
  //
  // GN: print event number so we can cross-check with engine
  // if (evdata.GetEvNum()>1000) 
  //   cout <<"\nhcana_event " << evdata.GetEvNum()<<endl;
  
  fCheckEvent = evdata.GetEvNum();
  fEventType =  evdata.GetEvType();

  if(gHaCuts->Result("Pedestal_event")) {
    Int_t nexthit = 0;
    for(Int_t ip=0;ip<fNPlanes;ip++) {
            
      nexthit = fPlanes[ip]->AccumulatePedestals(fRawHitList, nexthit);
    }
    fAnalyzePedestals = 1;	// Analyze pedestals first normal events
    return(0);
  }
  if(fAnalyzePedestals) {
    for(Int_t ip=0;ip<fNPlanes;ip++) {
      
      fPlanes[ip]->CalculatePedestals();
    }
    fAnalyzePedestals = 0;	// Don't analyze pedestals next event
  }

  // Let each plane get its hits
  Int_t nexthit = 0;

  fNfptimes=0;
  for(Int_t ip=0;ip<fNPlanes;ip++) {

    fPlaneCenter[ip] = fPlanes[ip]->GetPosCenter(0) + fPlanes[ip]->GetPosOffset();
    fPlaneSpacing[ip] = fPlanes[ip]->GetSpacing();
    
    //    nexthit = fPlanes[ip]->ProcessHits(fRawHitList, nexthit);
    // GN: select only events that have reasonable TDC values to start with
    // as per the Engine h_strip_scin.f
    nexthit = fPlanes[ip]->ProcessHits(fRawHitList,nexthit);
  }

  EstimateFocalPlaneTime();

  if (fdebugprintscinraw == 1) {
  for(UInt_t ihit = 0; ihit < fNRawHits ; ihit++) {
    THcRawHodoHit* hit = (THcRawHodoHit *) fRawHitList->At(ihit);
    cout << ihit << " : " << hit->fPlane << ":" << hit->fCounter << " : "
	 << hit->fADC_pos << " " << hit->fADC_neg << " "  <<  hit->fTDC_pos
	 << " " <<  hit->fTDC_neg << endl;
  }
  cout << endl;
  }
  ///  fStartTime = 500;		// Drift Chamber will need this

  return nhits;
}

//_____________________________________________________________________________
void THcHodoscope::EstimateFocalPlaneTime( void )
{

  Int_t timehist[200];

  for (Int_t i=0;i<200;i++) {
    timehist[i] = 0;
  }
  Int_t ihit=0;
  for(Int_t ip=0;ip<fNPlanes;ip++) {
    Int_t nphits=fPlanes[ip]->GetNScinHits();
    TClonesArray* hodoHits = fPlanes[ip]->GetHits();
    for(Int_t i=0;i<nphits;i++) {
      Double_t postime=((THcHodoHit*) hodoHits->At(i))->GetPosTOFCorrectedTime();
      Double_t negtime=((THcHodoHit*) hodoHits->At(i))->GetNegTOFCorrectedTime();
	  
      for (Int_t k=0;k<200;k++) {
	Double_t tmin=0.5*(k+1);
	if ((postime> tmin) && (postime < tmin+fTofTolerance)) {
	  timehist[k]++;
	}
	if ((negtime> tmin) && (negtime < tmin+fTofTolerance)) {
	  timehist[k]++;
	}
      }
      ihit++;
    }
  }

  // Find the bin with most hits
  ihit=0;
  Int_t binmax=0;
  Int_t maxhit=0;
  for(Int_t i=0;i<200;i++) {
    if(timehist[i]>maxhit) {
      binmax = i+1;
      maxhit = timehist[i];
    }
  }

  ihit = 0;
  Double_t fpTimeSum = 0.0;
  Int_t jhit = 0;
  fNfptimes=0;
    
  fNoTrkPlaneInfo.clear();
  fNoTrkHitInfo.clear();
  for(Int_t ip=0;ip<fNPlanes;ip++) {
    fNoTrkPlaneInfo.push_back(NoTrkPlaneInfo());
    fNoTrkPlaneInfo[ip].goodplanetime = kFALSE;
    Int_t nphits=fPlanes[ip]->GetNScinHits();
    TClonesArray* hodoHits = fPlanes[ip]->GetHits();
    for(Int_t i=0;i<nphits;i++) {
      fNoTrkHitInfo.push_back(NoTrkHitInfo());
      fNoTrkHitInfo[jhit].goodtwotimes = kFALSE;
      fNoTrkHitInfo[jhit].goodscintime = kFALSE;
      Double_t tmin = 0.5*binmax;
      Double_t postime=((THcHodoHit*) hodoHits->At(i))->GetPosTOFCorrectedTime();
      Double_t negtime=((THcHodoHit*) hodoHits->At(i))->GetNegTOFCorrectedTime();
      if ((postime>tmin) && (postime<tmin+fTofTolerance) &&
	  (negtime>tmin) && (negtime<tmin+fTofTolerance)) {
	fNoTrkHitInfo[jhit].goodtwotimes = kTRUE;
	fNoTrkHitInfo[jhit].goodscintime = kTRUE;
	// Both tubes fired
	Int_t index=((THcHodoHit*)hodoHits->At(i))->GetPaddleNumber()-1;
	Double_t fptime = ((THcHodoHit*)hodoHits->At(i))->GetScinCorrectedTime() 
	  - (fPlanes[ip]->GetZpos()+(index%2)*fPlanes[ip]->GetDzpos())
	  / (29.979 * fBetaNominal);
	if(TMath::Abs(fptime-fStartTimeCenter)<=fStartTimeSlop) {
	  // Should also fill the all FP times histogram
	  fpTimeSum += fptime;
	  fNfptimes++;
	  fNoTrkPlaneInfo[ip].goodplanetime = kTRUE;
	}
      }
      jhit++;
    }
    ihit++;
  }

  if(fNfptimes>0) {
    fStartTime = fpTimeSum/fNfptimes;
    fGoodStartTime=kTRUE;
  } else {
    fStartTime = fStartTimeCenter;
    fGoodStartTime=kFALSE;
  }


  if ( ( fNoTrkPlaneInfo[0].goodplanetime || fNoTrkPlaneInfo[1].goodplanetime ) &&
       ( fNoTrkPlaneInfo[2].goodplanetime || fNoTrkPlaneInfo[3].goodplanetime ) ){

    Double_t sumW = 0.;
    Double_t sumT = 0.;
    Double_t sumZ = 0.;
    Double_t sumZZ = 0.;
    Double_t sumTZ = 0.;    
    Int_t ihhit = 0;  

    for(Int_t ip=0;ip<fNPlanes;ip++) {
      Int_t nphits=fPlanes[ip]->GetNScinHits();
      TClonesArray* hodoHits = fPlanes[ip]->GetHits();

      for(Int_t i=0;i<nphits;i++) {	
	Int_t index=((THcHodoHit*)hodoHits->At(i))->GetPaddleNumber()-1;
	    
	if ( fNoTrkHitInfo[ihhit].goodscintime ) {
	  
	  Double_t sigma = 0.5 * ( TMath::Sqrt( TMath::Power( fHodoPosSigma[GetScinIndex(ip,index)],2) + 
						TMath::Power( fHodoNegSigma[GetScinIndex(ip,index)],2) ) );
	  Double_t scinWeight = 1 / TMath::Power(sigma,2);
	  Double_t zPosition = fPlanes[ip]->GetZpos() + (index%2)*fPlanes[ip]->GetDzpos();
	  
	  //	  cout << "hit = " << ihhit + 1 << "   zpos = " << zPosition << "   sigma = " << sigma << endl;

	  sumW  += scinWeight;
	  sumT  += scinWeight * ((THcHodoHit*)hodoHits->At(i))->GetScinCorrectedTime();
	  sumZ  += scinWeight * zPosition;
	  sumZZ += scinWeight * ( zPosition * zPosition );
	  sumTZ += scinWeight * zPosition * ((THcHodoHit*)hodoHits->At(i))->GetScinCorrectedTime();
	  
	} // condition of good scin time
	ihhit ++;
      } // loop over hits of plane
    } // loop over planes

    Double_t tmp = sumW * sumZZ - sumZ * sumZ ;
    Double_t t0 = ( sumT * sumZZ - sumZ * sumTZ ) / tmp ;
    Double_t tmpDenom = sumW * sumTZ - sumZ * sumT;
    
    if ( TMath::Abs( tmpDenom ) > ( 1 / 10000000000.0 ) ) {
      
      fBetaNoTrk = tmp / tmpDenom;
      fBetaNoTrkChiSq = 0.;	  
      ihhit = 0;
      
      for (Int_t ip = 0; ip < fNPlanes; ip++ ){                           // Loop over planes
	Int_t nphits=fPlanes[ip]->GetNScinHits();
	TClonesArray* hodoHits = fPlanes[ip]->GetHits();

	for(Int_t i=0;i<nphits;i++) {
	  Int_t index=((THcHodoHit*)hodoHits->At(i))->GetPaddleNumber()-1;
	  
	  if ( fNoTrkHitInfo[ihhit].goodscintime ) {
	    
	    Double_t zPosition = fPlanes[ip]->GetZpos() + (index%2)*fPlanes[ip]->GetDzpos();
	    Double_t timeDif = ( ((THcHodoHit*)hodoHits->At(i))->GetScinCorrectedTime() - t0 );		
	    Double_t sigma = 0.5 * ( TMath::Sqrt( TMath::Power( fHodoPosSigma[GetScinIndex(ip,index)],2) + 
						  TMath::Power( fHodoNegSigma[GetScinIndex(ip,index)],2) ) );
	    fBetaNoTrkChiSq += ( ( zPosition / fBetaNoTrk - timeDif ) *  
				 ( zPosition / fBetaNoTrk - timeDif ) ) / ( sigma * sigma );
	    
	    
	  } // condition for good scin time
	  ihhit++;
	} // loop over hits of a plane
      } // loop over planes
	  
      Double_t pathNorm = 1.0;
      
      fBetaNoTrk = fBetaNoTrk * pathNorm;
      fBetaNoTrk = fBetaNoTrk / 29.979;    // velocity / c	  
      
    }  // condition for fTmpDenom	
    else {
      fBetaNoTrk = 0.;
      fBetaNoTrkChiSq = -2.;
    } // else condition for fTmpDenom
  }  

}
//_____________________________________________________________________________
Int_t THcHodoscope::ApplyCorrections( void )
{
  return(0);
}
//_____________________________________________________________________________
Double_t THcHodoscope::TimeWalkCorrection(const Int_t& paddle,
					     const ESide side)
{
  return(0.0);
}

//_____________________________________________________________________________
Int_t THcHodoscope::CoarseProcess( TClonesArray&  tracks  )
{

  ApplyCorrections();
 
  return 0;
}

//_____________________________________________________________________________
Int_t THcHodoscope::FineProcess( TClonesArray& tracks )
{

  Int_t ntracks = tracks.GetLast()+1; // Number of reconstructed tracks
  Int_t timehist[200];
  // -------------------------------------------------

  fGoodScinHits = 0;
  fScinShould = 0; fScinDid = 0;

  if (tracks.GetLast()+1 > 0 ) {

    // **MAIN LOOP: Loop over all tracks and get corrected time, tof, beta...
    Double_t* nPmtHit = new Double_t [ntracks];
    Double_t* timeAtFP = new Double_t [ntracks];
    for ( Int_t itrack = 0; itrack < ntracks; itrack++ ) { // Line 133
      nPmtHit[itrack]=0;
      timeAtFP[itrack]=0;

      THaTrack* theTrack = dynamic_cast<THaTrack*>( tracks.At(itrack) );
      if (!theTrack) return -1;
      
      for (Int_t ip = 0; ip < fNPlanes; ip++ ){ 
	fGoodPlaneTime[ip] = kFALSE; 
	fNScinHits[ip] = 0;
	fNPlaneTime[ip] = 0;
	fSumPlaneTime[ip] = 0.;
      }
      std::vector<Double_t> dedx_temp;
      fdEdX.push_back(dedx_temp); // Create array of dedx per hit
      
      Int_t nFPTime = 0;
      Double_t betaChiSq = -3;
      Double_t beta = 0;
      //      timeAtFP[itrack] = 0.;
      Double_t sumFPTime = 0.; // Line 138
      fNScinHit.push_back(0);
      Double_t p = theTrack->GetP(); // Line 142 
      fBetaP = p/( TMath::Sqrt( p * p + fPartMass * fPartMass) );
      
      //! Calculate all corrected hit times and histogram
      //! This uses a copy of code below. Results are save in time_pos,neg
      //! including the z-pos. correction assuming nominal value of betap
      //! Code is currently hard-wired to look for a peak in the
      //! range of 0 to 100 nsec, with a group of times that all
      //! agree withing a time_tolerance of time_tolerance nsec. The normal
      //! peak position appears to be around 35 nsec.
      //! NOTE: if want to find farticles with beta different than
      //! reference particle, need to make sure this is big enough
      //! to accomodate difference in TOF for other particles
      //! Default value in case user hasnt definedd something reasonable
      // Line 162 to 171 is already done above in ReadDatabase
      
      for (Int_t j=0; j<200; j++) { timehist[j]=0; } // Line 176
      
      // Loop over scintillator planes.
      // In ENGINE, its loop over good scintillator hits.
      
      fTOFCalc.clear();
      Int_t ihhit = 0;		// Hit # overall
      for(Int_t ip = 0; ip < fNPlanes; ip++ ) {
	
	fNScinHits[ip] = fPlanes[ip]->GetNScinHits();
	TClonesArray* hodoHits = fPlanes[ip]->GetHits();

	// first loop over hits with in a single plane
	fTOFPInfo.clear();
	for (Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++ ){
	  // iphit is hit # within a plane
	  	  
	  fTOFPInfo.push_back(TOFPInfo());
	  // Can remove these as we will initialize in the constructor
	  fTOFPInfo[iphit].time_pos = -99.0;
	  fTOFPInfo[iphit].time_neg = -99.0;
	  fTOFPInfo[iphit].keep_pos = kFALSE;
	  fTOFPInfo[iphit].keep_neg = kFALSE;
	  fTOFPInfo[iphit].scin_pos_time = 0.0;
	  fTOFPInfo[iphit].scin_neg_time = 0.0;
	  
	  Int_t paddle = ((THcHodoHit*)hodoHits->At(iphit))->GetPaddleNumber()-1;
	  
	  Double_t xHitCoord = theTrack->GetX() + theTrack->GetTheta() *
	    ( fPlanes[ip]->GetZpos() +
	      ( paddle % 2 ) * fPlanes[ip]->GetDzpos() ); // Line 183
	  
	  Double_t yHitCoord = theTrack->GetY() + theTrack->GetPhi() *
	    ( fPlanes[ip]->GetZpos() +
	      ( paddle % 2 ) * fPlanes[ip]->GetDzpos() ); // Line 184
	  	  
	  Double_t scinTrnsCoord, scinLongCoord;
	  if ( ( ip == 0 ) || ( ip == 2 ) ){ // !x plane. Line 185
	    scinTrnsCoord = xHitCoord;
	    scinLongCoord = yHitCoord;
	  }
	  
	  else if ( ( ip == 1 ) || ( ip == 3 ) ){ // !y plane. Line 188
	    scinTrnsCoord = yHitCoord;
	    scinLongCoord = xHitCoord;
	  }
	  else { return -1; } // Line 195
	  
	  Double_t scinCenter = fPlanes[ip]->GetPosCenter(paddle) + fPlanes[ip]->GetPosOffset();

	  // Index to access the 2d arrays of paddle/scintillator properties
	  Int_t fPIndex = fNPlanes * paddle + ip;
	  

	  if ( TMath::Abs( scinCenter - scinTrnsCoord ) <
	       ( fPlanes[ip]->GetSize() * 0.5 + fPlanes[ip]->GetHodoSlop() ) ){ // Line 293
	    
	    Double_t pathp = fPlanes[ip]->GetPosLeft() - scinLongCoord;
	    Double_t timep = ((THcHodoHit*)hodoHits->At(iphit))->GetPosCorrectedTime();
	    timep = timep - ( pathp / fHodoVelLight[fPIndex] ) - ( fPlanes[ip]->GetZpos() +  
								( paddle % 2 ) * fPlanes[ip]->GetDzpos() ) / ( 29.979 * fBetaP ) *
	      TMath::Sqrt( 1. + theTrack->GetTheta() * theTrack->GetTheta() +
			   theTrack->GetPhi() * theTrack->GetPhi() );
	    fTOFPInfo[iphit].time_pos = timep;
	      
	    for ( Int_t k = 0; k < 200; k++ ){ // Line 211
	      Double_t tmin = 0.5 * ( k + 1 ) ;
	      if ( ( timep > tmin ) && ( timep < ( tmin + fTofTolerance ) ) )
		timehist[k] ++;
	    }
	    
	    Double_t pathn =  scinLongCoord - fPlanes[ip]->GetPosRight();
	    Double_t timen = ((THcHodoHit*)hodoHits->At(iphit))->GetNegCorrectedTime();
	    timen = timen - ( pathn / fHodoVelLight[fPIndex] ) - ( fPlanes[ip]->GetZpos() +
								( paddle % 2 ) * fPlanes[ip]->GetDzpos() ) / ( 29.979 * fBetaP ) *
	      TMath::Sqrt( 1. + theTrack->GetTheta() * theTrack->GetTheta() +
			   theTrack->GetPhi() * theTrack->GetPhi() );
	    fTOFPInfo[iphit].time_neg = timen;
	      
	    for ( Int_t k = 0; k < 200; k++ ){ // Line 230
	      Double_t tmin = 0.5 * ( k + 1 );
	      if ( ( timen > tmin ) && ( timen < ( tmin + fTofTolerance ) ) )
		timehist[k] ++;
	    }
	  } // condition for cenetr on a paddle
	} // First loop over hits in a plane <---------
	
	//-----------------------------------------------------------------------------------------------
	//------------- First large loop over scintillator hits in a plane ends here --------------------
	//-----------------------------------------------------------------------------------------------
	
	Int_t jmax = 0; // Line 240
	Int_t maxhit = 0;
	
	for ( Int_t k = 0; k < 200; k++ ){
	  if ( timehist[k] > maxhit ){
	    jmax = k+1;
	    maxhit = timehist[k];
	  }
	}
	

	Double_t tmin = 0.5 * jmax;
	for(Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++) { // Loop over sinc. hits. in plane
	  if ( ( fTOFPInfo[iphit].time_pos > tmin ) && ( fTOFPInfo[iphit].time_pos < ( tmin + fTofTolerance ) ) ) {
	    fTOFPInfo[iphit].keep_pos=kTRUE;
	  }	
	  if ( ( fTOFPInfo[iphit].time_neg > tmin ) && ( fTOFPInfo[iphit].time_neg < ( tmin + fTofTolerance ) ) ){
	    fTOFPInfo[iphit].keep_neg=kTRUE;
	  }	
	}
	
	//---------------------------------------------------------------------------------------------	
	// ---------------------- Scond loop over scint. hits in a plane ------------------------------
	//---------------------------------------------------------------------------------------------

	for (Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++ ){
	  
	  fTOFCalc.push_back(TOFCalc());
	  // Do we set back to false for each track, or just once per event?
	  fTOFCalc[ihhit].good_scin_time = kFALSE;
	  // These need a track index too to calculate efficiencies
	  fTOFCalc[ihhit].good_tdc_pos = kFALSE;
	  fTOFCalc[ihhit].good_tdc_neg = kFALSE;
	  fTOFCalc[ihhit].pindex = ip;

	  //	  ihhit ++;
	  //	  fRawIndex ++;   // Is fRawIndex ever different from ihhit
	  Int_t rawindex = ihhit;

	  Int_t paddle = ((THcHodoHit*)hodoHits->At(iphit))->GetPaddleNumber()-1;
	  fTOFCalc[ihhit].hit_paddle = paddle;
	  fTOFCalc[rawindex].good_raw_pad = paddle;
	  
	  Double_t xHitCoord = theTrack->GetX() + theTrack->GetTheta() *
	    ( fPlanes[ip]->GetZpos() + ( paddle % 2 ) * fPlanes[ip]->GetDzpos() ); // Line 277
	  Double_t yHitCoord = theTrack->GetY() + theTrack->GetPhi() *
	    ( fPlanes[ip]->GetZpos() + ( paddle % 2 ) * fPlanes[ip]->GetDzpos() ); // Line 278
	  
	  Double_t scinTrnsCoord, scinLongCoord;
	  if ( ( ip == 0 ) || ( ip == 2 ) ){ // !x plane. Line 278
	    scinTrnsCoord = xHitCoord;
	    scinLongCoord = yHitCoord;
	  }
	  else if ( ( ip == 1 ) || ( ip == 3 ) ){ // !y plane. Line 281
	    scinTrnsCoord = yHitCoord;
	    scinLongCoord = xHitCoord;
	  }
	  else { return -1; } // Line 288
	  
	  Double_t scinCenter = fPlanes[ip]->GetPosCenter(paddle) + fPlanes[ip]->GetPosOffset();
	  Int_t fPIndex = fNPlanes * paddle + ip;
	  
	  // ** Check if scin is on track
	  if ( TMath::Abs( scinCenter - scinTrnsCoord ) >
	       ( fPlanes[ip]->GetSize() * 0.5 + fPlanes[ip]->GetHodoSlop() ) ){ // Line 293
	  }
	  else{	    
	    if ( fTOFPInfo[iphit].keep_pos ) { // 301
	      
	      // ** Calculate time for each tube with a good tdc. 'pos' side first.
	      fTOFCalc[ihhit].good_tdc_pos = kTRUE;
	      Double_t path = fPlanes[ip]->GetPosLeft() - scinLongCoord;
	      
	      // * Convert TDC value to time, do pulse height correction, correction for
	      // * propogation of light thru scintillator, and offset.	      
	      Double_t time = ((THcHodoHit*)hodoHits->At(iphit))->GetPosCorrectedTime();
	      time = time - ( path / fHodoVelLight[fPIndex] );
	      fTOFPInfo[iphit].scin_pos_time = time;
	      
	    } // check for good pos TDC condition
	    
	    if ( fTOFPInfo[iphit].keep_neg ) { //
	      
	      // ** Calculate time for each tube with a good tdc. 'pos' side first.
	      fTOFCalc[ihhit].good_tdc_neg = kTRUE;
	      //	      fNtof ++;
	      Double_t path = scinLongCoord - fPlanes[ip]->GetPosRight();
	      
	      // * Convert TDC value to time, do pulse height correction, correction for
	      // * propogation of light thru scintillator, and offset.
	      Double_t time = ((THcHodoHit*)hodoHits->At(iphit))->GetNegCorrectedTime();
	      time = time - ( path / fHodoVelLight[fPIndex] );
	      fTOFPInfo[iphit].scin_neg_time = time;
      
	    } // check for good neg TDC condition
	    
	    // ** Calculate ave time for scin and error.
	    if ( fTOFCalc[ihhit].good_tdc_pos ){
	      if ( fTOFCalc[ihhit].good_tdc_neg ){	
		fTOFCalc[ihhit].scin_time  = ( fTOFPInfo[iphit].scin_pos_time + 
					       fTOFPInfo[iphit].scin_neg_time ) / 2.;
		fTOFCalc[ihhit].scin_sigma = TMath::Sqrt( fHodoPosSigma[fPIndex] * fHodoPosSigma[fPIndex] + 
							  fHodoNegSigma[fPIndex] * fHodoNegSigma[fPIndex] )/2.;
		fTOFCalc[ihhit].good_scin_time = kTRUE;
	      }
	      else{
		fTOFCalc[ihhit].scin_time = fTOFPInfo[iphit].scin_pos_time;
		fTOFCalc[ihhit].scin_sigma = fHodoPosSigma[fPIndex];
		fTOFCalc[ihhit].good_scin_time = kTRUE;
	      }
	    }
	    else {
	      if ( fTOFCalc[ihhit].good_tdc_neg ){
		fTOFCalc[ihhit].scin_time = fTOFPInfo[iphit].scin_neg_time;
		fTOFCalc[ihhit].scin_sigma = fHodoNegSigma[fPIndex];
		fTOFCalc[ihhit].good_scin_time = kTRUE;
	      }
	    } // In h_tof.f this includes the following if condition for time at focal plane
	    // // because it is written in FORTRAN code

	    // c     Get time at focal plane
	    if ( fTOFCalc[ihhit].good_scin_time ){
	      
	      // scin_time_fp doesn't need to be an array
	      Double_t scin_time_fp = fTOFCalc[ihhit].scin_time -
	       	( fPlanes[ip]->GetZpos() + ( paddle % 2 ) * fPlanes[ip]->GetDzpos() ) /
	       	( 29.979 * fBetaP ) *
	       	TMath::Sqrt( 1. + theTrack->GetTheta() * theTrack->GetTheta() +
	       		     theTrack->GetPhi() * theTrack->GetPhi() );

	      sumFPTime = sumFPTime + scin_time_fp;
	      nFPTime ++;

	      fSumPlaneTime[ip] = fSumPlaneTime[ip] + scin_time_fp;
	      fNPlaneTime[ip] ++;
	      fNScinHit[itrack] ++;
	      
	      if ( ( fTOFCalc[ihhit].good_tdc_pos ) && ( fTOFCalc[ihhit].good_tdc_neg ) ){
	      	nPmtHit[itrack] = nPmtHit[itrack] + 2;
	      }
	      else {
	      	nPmtHit[itrack] = nPmtHit[itrack] + 1;
	      }

	      fdEdX[itrack].push_back(0.0);
	      
	      // --------------------------------------------------------------------------------------------
	      if ( fTOFCalc[ihhit].good_tdc_pos ){
		if ( fTOFCalc[ihhit].good_tdc_neg ){
		  fdEdX[itrack][fNScinHit[itrack]-1]=
		    TMath::Sqrt( TMath::Max( 0., ((THcHodoHit*)hodoHits->At(iphit))->GetPosADC() *
                                                 ((THcHodoHit*)hodoHits->At(iphit))->GetNegADC() ) );
		}
		else{
		  fdEdX[itrack][fNScinHit[itrack]-1]=
		    TMath::Max( 0., ((THcHodoHit*)hodoHits->At(iphit))->GetPosADC() );
	       	}
	      }
	      else{
		if ( fTOFCalc[ihhit].good_tdc_neg ){
		  fdEdX[itrack][fNScinHit[itrack]-1]=
		    TMath::Max( 0., ((THcHodoHit*)hodoHits->At(iphit))->GetNegADC() );
		}
		else{
		  fdEdX[itrack][fNScinHit[itrack]-1]=0.0;
		}
	      }
	      // --------------------------------------------------------------------------------------------


	    } // time at focal plane condition
	  } // on track else condition
	  
	  // ** See if there are any good time measurements in the plane.
	  if ( fTOFCalc[ihhit].good_scin_time ){
	    fGoodPlaneTime[ip] = kTRUE;
	    fTOFCalc[ihhit].dedx = fdEdX[itrack][fNScinHit[itrack]-1];
	  } else {
	    fTOFCalc[ihhit].dedx = 0.0;
	  }

	  // Can this be done after looping over hits and planes?
	  if ( fGoodPlaneTime[2] )	theTrack->SetGoodPlane3( 1 );
	  if ( !fGoodPlaneTime[2] )	theTrack->SetGoodPlane3( 0 );
	  if ( fGoodPlaneTime[3] )	theTrack->SetGoodPlane4( 1 );
	  if ( !fGoodPlaneTime[3] )	theTrack->SetGoodPlane4( 0 );

	  ihhit ++;

	} // Second loop over hits of a scintillator plane ends here
      } // Loop over scintillator planes ends here

      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------
      //------------------------------------------------------------------------------

      // * * Fit beta if there are enough time measurements (one upper, one lower)
      // From h_tof_fit
      if ( ( ( fGoodPlaneTime[0] ) || ( fGoodPlaneTime[1] ) ) && 
	   ( ( fGoodPlaneTime[2] ) || ( fGoodPlaneTime[3] ) ) ){	
	
	Double_t sumW = 0.;
	Double_t sumT = 0.;
	Double_t sumZ = 0.;
	Double_t sumZZ = 0.;
	Double_t sumTZ = 0.;

	ihhit = 0;  
	for (Int_t ip = 0; ip < fNPlanes; ip++ ){
	  
	  if (!fPlanes[ip])
	    return -1;
	  
	  fNScinHits[ip] = fPlanes[ip]->GetNScinHits();	  
	  for (Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++ ){
	    
	    if ( fTOFCalc[ihhit].good_scin_time ) {
	      
	      Double_t scinWeight = 1 / ( fTOFCalc[ihhit].scin_sigma * fTOFCalc[ihhit].scin_sigma );
	      Double_t zPosition = ( fPlanes[ip]->GetZpos() + ( fTOFCalc[ihhit].hit_paddle % 2 ) * 
			     fPlanes[ip]->GetDzpos() );
	      
	      sumW  += scinWeight;
	      sumT  += scinWeight * fTOFCalc[ihhit].scin_time;
	      sumZ  += scinWeight * zPosition;
	      sumZZ += scinWeight * ( zPosition * zPosition );
	      sumTZ += scinWeight * zPosition * fTOFCalc[ihhit].scin_time;
	      	      
	    } // condition of good scin time
	    ihhit ++;
	  } // loop over hits of plane
	} // loop over planes
	
	Double_t tmp = sumW * sumZZ - sumZ * sumZ ;
	Double_t t0 = ( sumT * sumZZ - sumZ * sumTZ ) / tmp ;
	Double_t tmpDenom = sumW * sumTZ - sumZ * sumT;
	
	if ( TMath::Abs( tmpDenom ) > ( 1 / 10000000000.0 ) ) {
	  
	  beta = tmp / tmpDenom;
	  betaChiSq = 0.;	  
	  ihhit = 0;

	  for (Int_t ip = 0; ip < fNPlanes; ip++ ){                           // Loop over planes
	    if (!fPlanes[ip])
	      return -1;
	    
	    fNScinHits[ip] = fPlanes[ip]->GetNScinHits();	  
	    for (Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++ ){                    // Loop over hits of a plane
	      
	      if ( fTOFCalc[ihhit].good_scin_time ){
		
		Double_t zPosition = ( fPlanes[ip]->GetZpos() + ( fTOFCalc[ihhit].hit_paddle % 2 ) * 
			       fPlanes[ip]->GetDzpos() );
		Double_t timeDif = ( fTOFCalc[ihhit].scin_time - t0 );		
		betaChiSq += ( ( zPosition / beta - timeDif ) * 
				( zPosition / beta - timeDif ) )  / 
		              ( fTOFCalc[ihhit].scin_sigma * fTOFCalc[ihhit].scin_sigma );
		
	      } // condition for good scin time
	      ihhit++;
	    } // loop over hits of a plane
	  } // loop over planes
	  
	  Double_t pathNorm = TMath::Sqrt( 1. + theTrack->GetTheta() * theTrack->GetTheta() + 
				       theTrack->GetPhi()   * theTrack->GetPhi() );

	  beta = beta / pathNorm;
	  beta = beta / 29.979;    // velocity / c	  
	  
	}  // condition for fTmpDenom	
	else {
	  beta = 0.;
	  betaChiSq = -2.;
	} // else condition for fTmpDenom
      }
      else {
	beta = 0.;
	betaChiSq = -1;
      }
      
      if ( nFPTime != 0 ){
      	timeAtFP[itrack] = ( sumFPTime / nFPTime ); 
      }
      //
      // ---------------------------------------------------------------------------
            
      Double_t FPTimeSum=0.0;
      Int_t nFPTimeSum=0;
      for (Int_t ip = 0; ip < fNPlanes; ip++ ){
	if ( fNPlaneTime[ip] != 0 ){
	  fFPTime[ip] = ( fSumPlaneTime[ip] / fNPlaneTime[ip] );
	  FPTimeSum += fSumPlaneTime[ip];
	  nFPTimeSum += fNPlaneTime[ip];
	}
	else{
	  fFPTime[ip] = 1000. * ( ip + 1 );
	}
      }
      Double_t fptime = FPTimeSum/nFPTimeSum;
      
      Double_t dedx=0.0;
      for(UInt_t ih=0;ih<fTOFCalc.size();ih++) {
	if(fTOFCalc[ih].good_scin_time) {
	  dedx = fTOFCalc[ih].dedx;
	  break;
	}
      }
      theTrack->SetDedx(dedx);
      theTrack->SetFPTime(fptime);
      theTrack->SetBeta(beta);
      theTrack->SetBetaChi2( betaChiSq );
      theTrack->SetNPMT(nPmtHit[itrack]);
      theTrack->SetFPTime( timeAtFP[itrack]);

      
    } // Main loop over tracks ends here.

  } // If condition for at least one track

  //-----------------------------------------------------------------------
  //
  //   Trnslation of h_track_tests.f file for tracking efficiency
  //
  //-----------------------------------------------------------------------

  //************************now look at some hodoscope tests
  //  *second, we move the scintillators.  here we use scintillator cuts to see
  //  *if a track should have been found.

  for(Int_t ip = 0; ip < fNPlanes; ip++ ) {

    std::vector<Double_t> scin_temp;
    fScinHitPaddle.push_back(scin_temp); // Create array of hits per plane

    for (UInt_t ipaddle = 0; ipaddle < fNPaddle[0]; ipaddle++ ){
	  fScinHitPaddle[ip].push_back(0.0);
	  fScinHitPaddle[ip][ipaddle] = 0.0;	  
    }
  }

  for(Int_t ip = 0; ip < fNPlanes; ip++ ) {
    if (!fPlanes[ip])
      return -1;

    TClonesArray* hodoHits = fPlanes[ip]->GetHits();
    //    TClonesArray* scinPosTDC = fPlanes[ip]->GetPosTDC();
    //    TClonesArray* scinNegTDC = fPlanes[ip]->GetNegTDC();

    fNScinHits[ip] = fPlanes[ip]->GetNScinHits();
    for (Int_t iphit = 0; iphit < fNScinHits[ip]; iphit++ ){
      Int_t paddle = ((THcHodoHit*)hodoHits->At(iphit))->GetPaddleNumber()-1;
      
      fScinHitPaddle[ip][paddle] = 1;
    }
  }

  //  *next, look for clusters of hits in a scin plane.  a cluster is a group of
  //  *adjacent scintillator hits separated by a non-firing scintillator.
  //  *Wwe count the number of three adjacent scintillators too.  (A signle track
  //  *shouldn't fire three adjacent scintillators.

  for(Int_t ip = 0; ip < fNPlanes; ip++ ) {
    // Planes ip = 0 = 1X
    // Planes ip = 2 = 2X
    if (!fPlanes[ip]) return -1;
    fNClust.push_back(0);
    fThreeScin.push_back(0);
  }

  // *look for clusters in x planes... (16 scins)  !this assume both x planes have same
  // *number of scintillators.
  Int_t icount;
  for (Int_t ip = 0; ip < 3; ip +=2 ) {
    icount = 0;
    if ( fScinHitPaddle[ip][0] == 1 )
      icount ++;

    for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[0] - 1; ipaddle++ ){
      // !look for number of clusters of 1 or more hits

      if ( ( fScinHitPaddle[ip][ipaddle] == 0 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 1] == 1 ) )
	icount ++;
      
    } // Loop over  paddles

    fNClust[ip] = icount;
    icount = 0;

    for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[0] - 2; ipaddle++ ){
      // !look for three or more adjacent hits

      if ( ( fScinHitPaddle[ip][ipaddle] == 1 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 1] == 1 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 2] == 1 ) )
	icount ++;
    } // Second loop over paddles

    if ( icount > 0 )
      fThreeScin[ip] = 1;

  } // Loop over X plane

  // *look for clusters in y planes... (10 scins)  !this assume both y planes have same  
  // *number of scintillators.

  for (Int_t ip = 1; ip < 4; ip +=2 ) {
    // Planes ip = 1 = 1Y
    // Planes ip = 3 = 2Y
    if (!fPlanes[ip]) return -1;

    icount = 0;
    if ( fScinHitPaddle[ip][0] == 1 )
      icount ++;

    for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[1] - 1; ipaddle++ ){
      //  !look for number of clusters of 1 or more hits

      if ( ( fScinHitPaddle[ip][ipaddle] == 0 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 1] == 1 ) )
	icount ++;

    } // Loop over Y paddles

    fNClust[ip] = icount;
    icount = 0;

    for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[1] - 2; ipaddle++ ){
      // !look for three or more adjacent hits

      if ( ( fScinHitPaddle[ip][ipaddle] == 1 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 1] == 1 ) &&
	   ( fScinHitPaddle[ip][ipaddle + 2] == 1 ) )
	icount ++;

    } // Second loop over Y paddles

    if ( icount > 0 )
      fThreeScin[ip] = 1;

  }// Loop over Y planes

  // *now put some "tracking" like cuts on the hslopes, based only on scins...
  // *by "slope" here, I mean the difference in the position of scin hits in two
  // *like-planes.  For example, a track that those great straight through will 
  // *have a slope of zero.  If it moves one scin over from s1x to s2x it has an
  // *x-slope of 1...  I pick the minimum slope if there are multiple scin hits.

  Double_t bestXpScin = 100.0;
  Double_t bestYpScin = 100.0;

  for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[0]; ipaddle++ ){
    for (Int_t ipaddle2 = 0; ipaddle2 < (Int_t) fNPaddle[0]; ipaddle2++ ){

      if ( ( fScinHitPaddle[0][ipaddle] == 1 ) &&
	   ( fScinHitPaddle[2][ipaddle2] == 1 ) ){

	Double_t slope = TMath::Abs(ipaddle - ipaddle2);

	if ( slope < bestXpScin ) {
	  bestXpScin = slope;

	}
      }
    }  // Second loop over X paddles
  } // First loop over X paddles


  for (Int_t ipaddle = 0; ipaddle < (Int_t) fNPaddle[1]; ipaddle++ ){
    for (Int_t ipaddle2 = 0; ipaddle2 < (Int_t) fNPaddle[1]; ipaddle2++ ){

      if ( ( fScinHitPaddle[1][ipaddle] == 1 ) &&
	   ( fScinHitPaddle[3][ipaddle2] == 1 ) ){

	Double_t slope = TMath::Abs(ipaddle - ipaddle2);

	if ( slope < bestYpScin ) {
	  bestYpScin = slope;	
	}
      }
    }  // Second loop over Y paddles
  } // First loop over Y paddles

  // *next we mask out the edge scintillators, and look at triggers that happened
  // *at the center of the acceptance.  To change which scins are in the mask
  // *change the values of h*loscin and h*hiscin in htracking.param

  //      fGoodScinHits = 0;
  for (Int_t ifidx = fxLoScin[0]; ifidx < (Int_t) fxHiScin[0]; ifidx ++ ){
    fGoodScinHitsX.push_back(0);
  }

  // *first x plane.  first see if there are hits inside the scin region
  for (Int_t ifidx = fxLoScin[0]-1; ifidx < fxHiScin[0]; ifidx ++ ){
    if ( fScinHitPaddle[0][ifidx] == 1 ){
      fHitSweet1X = 1;
      fSweet1XScin = ifidx + 1;
    }
  }

  // *  next make sure nothing fired outside the good region
  for (Int_t ifidx = 0; ifidx < fxLoScin[0]-1; ifidx ++ ){
    if ( fScinHitPaddle[0][ifidx] == 1 ){ fHitSweet1X = -1; }
  }
  for (Int_t ifidx = fxHiScin[0]; ifidx < (Int_t) fNPaddle[0]; ifidx ++ ){
    if ( fScinHitPaddle[0][ifidx] == 1 ){ fHitSweet1X = -1; }
  }

  // *second x plane.  first see if there are hits inside the scin region
  for (Int_t ifidx = fxLoScin[1]-1; ifidx < fxHiScin[1]; ifidx ++ ){
    if ( fScinHitPaddle[2][ifidx] == 1 ){
      fHitSweet2X = 1;
      fSweet2XScin = ifidx + 1;
    }
  }
  // *  next make sure nothing fired outside the good region
  for (Int_t ifidx = 0; ifidx < fxLoScin[1]-1; ifidx ++ ){
    if ( fScinHitPaddle[2][ifidx] == 1 ){ fHitSweet2X = -1; }
  }
  for (Int_t ifidx = fxHiScin[1]; ifidx < (Int_t) fNPaddle[2]; ifidx ++ ){
    if ( fScinHitPaddle[2][ifidx] == 1 ){ fHitSweet2X = -1; }
  }

  // *first y plane.  first see if there are hits inside the scin region
  for (Int_t ifidx = fyLoScin[0]-1; ifidx < fyHiScin[0]; ifidx ++ ){
    if ( fScinHitPaddle[1][ifidx] == 1 ){
      fHitSweet1Y = 1;
      fSweet1YScin = ifidx + 1;
    }
  }
  // *  next make sure nothing fired outside the good region
  for (Int_t ifidx = 0; ifidx < fyLoScin[0]-1; ifidx ++ ){
    if ( fScinHitPaddle[1][ifidx] == 1 ){ fHitSweet1Y = -1; }
  }
  for (Int_t ifidx = fyHiScin[0]; ifidx < (Int_t) fNPaddle[1]; ifidx ++ ){
    if ( fScinHitPaddle[1][ifidx] == 1 ){ fHitSweet1Y = -1; }
  }

  // *second y plane.  first see if there are hits inside the scin region
  for (Int_t ifidx = fyLoScin[1]-1; ifidx < fyHiScin[1]; ifidx ++ ){
    if ( fScinHitPaddle[3][ifidx] == 1 ){
      fHitSweet2Y = 1;
      fSweet2YScin = ifidx + 1;
    }
  }

  // *  next make sure nothing fired outside the good region
  for (Int_t ifidx = 0; ifidx < fyLoScin[1]-1; ifidx ++ ){
    if ( fScinHitPaddle[3][ifidx] == 1 ){ fHitSweet2Y = -1; }
  }
  for (Int_t ifidx = fyHiScin[1]; ifidx < (Int_t) fNPaddle[3]; ifidx ++ ){
    if ( fScinHitPaddle[3][ifidx] == 1 ){ fHitSweet2Y = -1; }
  }

  fTestSum = fHitSweet1X + fHitSweet2X + fHitSweet1Y + fHitSweet2Y;

  // * now define a 3/4 or 4/4 trigger of only good scintillators the value
  // * is specified in htracking.param...
  if ( fTestSum > fTrackEffTestNScinPlanes ){
    fGoodScinHits = 1;
    for (Int_t ifidx = fxLoScin[0]; ifidx < fxHiScin[0]; ifidx ++ ){
      if ( fSweet1XScin == ifidx )
	fGoodScinHitsX[ifidx] = 1;
    }
  }

  // * require front/back hodoscopes be close to each other
  if ( ( fGoodScinHits == 1 ) && ( fTrackEffTestNScinPlanes == 4 ) ){
    if ( TMath::Abs( fSweet1XScin - fSweet2XScin ) > 3 )
      fGoodScinHits = 0;
    if ( TMath::Abs( fSweet1YScin - fSweet2YScin ) > 2 )
      fGoodScinHits = 0;
  }


  if ( !fChern || !fShower ) { 
    return 0;    
  }

  
  if ( ( fGoodScinHits == 1 ) && ( fShower->GetNormETot() > fNormETot ) &&
       ( fChern->GetCerNPE() > fNCerNPE ) )
    fScinShould = 1;
  
  if ( ( fGoodScinHits == 1 ) && ( fShower->GetNormETot() > fNormETot ) &&
       ( fChern->GetCerNPE() > fNCerNPE ) && ( tracks.GetLast() + 1 > 0 ) ) {
      fScinDid = 1;
  }
  
  return 0;
  
}
//_____________________________________________________________________________
Int_t THcHodoscope::GetScinIndex( Int_t nPlane, Int_t nPaddle ) {
  // GN: Return the index of a scintillator given the plane # and the paddle #
  // This assumes that both planes and 
  // paddles start counting from 0!
  // Result also counts from 0.
  return fNPlanes*nPaddle+nPlane;
}
//_____________________________________________________________________________
Int_t THcHodoscope::GetScinIndex( Int_t nSide, Int_t nPlane, Int_t nPaddle ) {
  return nSide*fMaxHodoScin+fNPlanes*nPaddle+nPlane-1;
}
//_____________________________________________________________________________
Double_t THcHodoscope::GetPathLengthCentral() {
  return fPathLengthCentral;
}
ClassImp(THcHodoscope)
////////////////////////////////////////////////////////////////////////////////
