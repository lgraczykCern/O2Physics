// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file candidateCreatorSigmac0plusplus.cxx
/// \brief Σc0,++ → Λc+(→pK-π+) π-,+ candidate builder
/// \note Λc± candidates selected from the HFLcCandidateSelector.cxx
///
/// \author Mattia Faggin <mfaggin@cern.ch>, University and INFN PADOVA

#include "CCDB/BasicCCDBManager.h"             // for dca recalculation
#include "DataFormatsParameters/GRPMagField.h" // for dca recalculation
#include "DataFormatsParameters/GRPObject.h"   // for dca recalculation
#include "DetectorsBase/GeometryManager.h"     // for dca recalculation
#include "DetectorsBase/Propagator.h"          // for dca recalculation
#include "DetectorsVertexing/PVertexer.h"      // for dca recalculation
#include "Framework/AnalysisTask.h"
#include "Framework/runDataProcessing.h"

#include "Common/Core/TrackSelection.h"
#include "Common/Core/trackUtilities.h"
#include "Common/DataModel/CollisionAssociationTables.h"

#include "PWGHF/DataModel/CandidateReconstructionTables.h"
#include "PWGHF/DataModel/CandidateSelectionTables.h"
#include "PWGHF/Utils/utilsBfieldCCDB.h" // for dca recalculation

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::aod::hf_cand_3prong;

struct HfCandidateCreatorSigmac0plusplus {

  /// Table with Σc0,++ info
  Produces<aod::HfCandScBase> rowScCandidates;

  /// Selection of candidates Λc+
  Configurable<int> selectionFlagLc{"selectionFlagLc", 1, "Selection Flag for Lc"};
  Configurable<double> yCandLcMax{"yCandLcMax", -1., "max. candLc. Lc rapidity"};
  Configurable<double> mPKPiCandLcMax{"mPKPiCandLcMax", 0.03, "max. spread (abs. value) between PDG(Lc) and Minv(pKpi)"};
  Configurable<double> mPiKPCandLcMax{"mPiKPCandLcMax", 0.03, "max. spread (abs. value) between PDG(Lc) and Minv(piKp)"};

  /// Selections on candidate soft π-,+
  Configurable<float> softPiEtaMax{"softPiEtaMax", 0.9f, "Soft pion max value for pseudorapidity (abs vale)"};
  Configurable<int> softPiItsHitMap{"softPiItsHitMap", 127, "Soft pion ITS hitmap"};
  Configurable<int> softPiItsHitsMin{"softPiItsHitsMin", 1, "Minimum number of ITS layers crossed by the soft pion among those in \"softPiItsHitMap\""};
  Configurable<float> softPiDcaXYMax{"softPiDcaXYMax", 0.065, "Soft pion max dcaXY (cm)"};
  Configurable<float> softPiDcaZMax{"softPiDcaZMax", 0.065, "Soft pion max dcaZ (cm)"};

  // CCDB
  Configurable<bool> isRun2Ccdb{"isRun2Ccdb", false, "enable Run 2 or Run 3 GRP objects for magnetic field"};
  Configurable<std::string> ccdbUrl{"ccdbUrl", "http://alice-ccdb.cern.ch", "url of the ccdb repository"};
  Configurable<std::string> ccdbPathLut{"ccdbPathLut", "GLO/Param/MatLUT", "Path for LUT parametrization"};
  Configurable<std::string> ccdbPathGrp{"ccdbPathGrp", "GLO/GRP/GRP", "Path of the grp file (Run 2)"};
  Configurable<std::string> ccdbPathGrpMag{"ccdbPathGrpMag", "GLO/Config/GRPMagField", "CCDB path of the GRPMagField object (Run 3)"};

  HistogramRegistry histos;

  using TracksSigmac = soa::Join<aod::FullTracks, aod::TracksDCA>;
  using CandidatesLc = soa::Filtered<soa::Join<aod::HfCand3Prong, aod::HfSelLc>>;

  /// Filter the candidate Λc+ used for the Σc0,++ creation
  Filter filterSelectCandidateLc = (aod::hf_sel_candidate_lc::isSelLcToPKPi >= selectionFlagLc || aod::hf_sel_candidate_lc::isSelLcToPiKP >= selectionFlagLc);

  /// Prepare the slicing of candidate Λc+ and pions to be used for the Σc0,++ creation
  Preslice<aod::TrackAssoc> trackIndicesPerCollision = aod::track_association::collisionId;
  // Preslice<CandidatesLc> hf3ProngPerCollision = aod::track_association::collisionId;
  Preslice<CandidatesLc> hf3ProngPerCollision = aod::hf_cand::collisionId;

  /// Cut selection object for soft π-,+
  TrackSelection softPiCuts;

  // Needed for dcaXY, dcaZ recalculation of soft pions reassigned to a new collision
  Service<o2::ccdb::BasicCCDBManager> ccdb;
  o2::base::MatLayerCylSet* lut;
  o2::base::Propagator::MatCorrType noMatCorr = o2::base::Propagator::MatCorrType::USEMatCorrNONE;
  int runNumber;

  /// @brief init function, to define the soft pion selections and histograms
  /// @param
  void init(InitContext&)
  {
    auto h = histos.add<TH1>("hCounter", "", kTH1D, {{6, 0.5, 6.5}});
    h->GetXaxis()->SetBinLabel(1, "collisions");
    h->GetXaxis()->SetBinLabel(2, "Lc (before cuts)");
    h->GetXaxis()->SetBinLabel(3, "Lc (after cuts)");
    h->GetXaxis()->SetBinLabel(4, "Soft #pi (before cuts)");
    h->GetXaxis()->SetBinLabel(5, "Soft #pi (after cuts)");
    h->GetXaxis()->SetBinLabel(6, "#Sigma_{c}");

    ////////////////////////////////////////
    /// set the selections for soft pion ///
    ////////////////////////////////////////
    // softPiCuts.SetPtRange(0.001, 1000.); // pt
    softPiCuts.SetEtaRange(-softPiEtaMax, softPiEtaMax); // eta
    // softPiCuts.SetMaxDcaXY(softPiDcaXYMax);              // dcaXY
    // softPiCuts.SetMaxDcaZ(softPiDcaZMax);                // dcaZ
    //  ITS hitmap
    std::set<uint8_t> setSoftPiItsHitMap; // = {};
    for (int idItsLayer = 0; idItsLayer < 7; idItsLayer++) {
      if (TESTBIT(softPiItsHitMap, idItsLayer)) {
        setSoftPiItsHitMap.insert(static_cast<uint8_t>(idItsLayer));
      }
    }
    LOG(info) << "### ITS hitmap for soft pion";
    LOG(info) << "    >>> setSoftPiItsHitMap.size(): " << setSoftPiItsHitMap.size();
    LOG(info) << "    >>> Custom ITS hitmap dfchecked: ";
    for (std::set<uint8_t>::iterator it = setSoftPiItsHitMap.begin(); it != setSoftPiItsHitMap.end(); it++) {
      LOG(info) << "        Layer " << static_cast<int>(*it) << " ";
    }
    LOG(info) << "############";
    softPiCuts.SetRequireITSRefit();
    softPiCuts.SetRequireHitsInITSLayers(softPiItsHitsMin, setSoftPiItsHitMap);

    /// CCDB for dcaXY, dcaZ recalculation of soft pions reassigned to another collision
    ccdb->setURL(ccdbUrl);
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();
    lut = o2::base::MatLayerCylSet::rectifyPtrFromFile(ccdb->get<o2::base::MatLayerCylSet>(ccdbPathLut));
    runNumber = 0;
  }

  /// @brief process function for Σc0,++ → Λc+(→pK-π+) π- candidate reconstruction considering also reassigned tracks for soft pions
  /// @param collision is a o2::aod::Collision
  /// @param tracks are the tracks (with dcaXY, dcaZ information) in the collision → soft-pion candidate tracks
  /// @param candidates are 3-prong candidates satisfying the analysis selections for Λc+ → pK-π+ (and charge conj.)
  void process(aod::Collisions const& collisions,
               aod::TrackAssoc const& trackIndices,
               TracksSigmac const& tracks,
               CandidatesLc const& candidates,
               aod::BCsWithTimestamps const& bcWithTimeStamps)
  {

    for (const auto& collision : collisions) {
      histos.fill(HIST("hCounter"), 1);
      auto thisCollId = collision.globalIndex();

      /// loop over Λc+ → pK-π+ (and charge conj.) candidates
      auto candidatesThisColl = candidates.sliceBy(hf3ProngPerCollision, thisCollId);
      for (const auto& candLc : candidatesThisColl) {
        histos.fill(HIST("hCounter"), 2);

        /// keep only the candidates flagged as possible Λc+ (and charge conj.) decaying into a charged pion, kaon and proton
        /// if not selected, skip it and go to the next one
        if (!(candLc.hfflag() & 1 << DecayType::LcToPKPi)) {
          continue;
        }
        /// keep only the candidates Λc+ (and charge conj.) within the desired rapidity
        /// if not selected, skip it and go to the next one
        if (yCandLcMax >= 0. && std::abs(yLc(candLc)) > yCandLcMax) {
          continue;
        }

        /// selection on the Λc+ inv. mass window we want to consider for Σc0,++ candidate creation
        auto statusSpreadMinvPKPiFromPDG = 0;
        auto statusSpreadMinvPiKPFromPDG = 0;
        if (candLc.isSelLcToPKPi() >= 1 && std::abs(invMassLcToPKPi(candLc) - RecoDecay::getMassPDG(pdg::Code::kLambdaCPlus)) <= mPKPiCandLcMax) {
          statusSpreadMinvPKPiFromPDG = 1;
        }
        if (candLc.isSelLcToPiKP() >= 1 && std::abs(invMassLcToPiKP(candLc) - RecoDecay::getMassPDG(pdg::Code::kLambdaCPlus)) <= mPiKPCandLcMax) {
          statusSpreadMinvPiKPFromPDG = 1;
        }
        if (statusSpreadMinvPKPiFromPDG == 0 && statusSpreadMinvPiKPFromPDG == 0) {
          /// none of the two possibilities are satisfied, therefore this candidate Lc can be skipped
          continue;
        }
        histos.fill(HIST("hCounter"), 3);

        /// loop over tracks
        auto trackIdsThisCollision = trackIndices.sliceBy(trackIndicesPerCollision, thisCollId);
        for (const auto& trackId : trackIdsThisCollision) {

          auto trackSoftPi = trackId.track_as<TracksSigmac>();
          // auto trackSoftPi = tracks.rawIteratorAt(trackId.trackId());
          histos.fill(HIST("hCounter"), 4);

          /////////////////////////////////////////////////////////////////////////////////
          ///                       Σc0,++ candidate creation                           ///
          ///                                                                           ///
          /// For each candidate Λc, let's loop over all the candidate soft-pion tracks ///
          /////////////////////////////////////////////////////////////////////////////////

          /// keep only soft-pion candidate tracks
          /// if not selected, skip it and go to the next one
          if (!softPiCuts.IsSelected(trackSoftPi)) {
            continue;
          }
          /// dcaXY, dcaZ selections
          /// To be done separately from the others, because for reassigned tracks the dca must be recalculated
          /// TODO: to be properly adapted in case of PV refit usage
          if (trackSoftPi.collisionId() == thisCollId) {
            /// this is a track originally assigned to the current collision
            /// therefore, the dcaXY, dcaZ are those already calculated in the track-propagation workflow
            if (std::abs(trackSoftPi.dcaXY()) > softPiDcaXYMax || std::abs(trackSoftPi.dcaZ()) > softPiDcaZMax) {
              continue;
            }
          } else {
            /// this is a reassigned track
            /// therefore we need to calculate the dcaXY, dcaZ with respect to this new primary vertex
            auto bc = collision.bc_as<o2::aod::BCsWithTimestamps>();
            initCCDB(bc, runNumber, ccdb, isRun2Ccdb ? ccdbPathGrp : ccdbPathGrpMag, lut, isRun2Ccdb);
            auto trackParSoftPi = getTrackPar(trackSoftPi);
            o2::gpu::gpustd::array<float, 2> dcaInfo{-999., -999.};
            o2::base::Propagator::Instance()->propagateToDCABxByBz({collision.posX(), collision.posY(), collision.posZ()}, trackParSoftPi, 2.f, noMatCorr, &dcaInfo);
            if (std::abs(dcaInfo[0]) > softPiDcaXYMax || std::abs(dcaInfo[1]) > softPiDcaZMax) {
              continue;
            }
          }

          /// Exclude the current candidate soft pion if it corresponds already to a candidate Lc prong
          int indexProng0 = candLc.prong0_as<aod::Tracks>().globalIndex();
          int indexProng1 = candLc.prong1_as<aod::Tracks>().globalIndex();
          int indexProng2 = candLc.prong2_as<aod::Tracks>().globalIndex();
          int indexSoftPi = trackSoftPi.globalIndex();
          if (indexSoftPi == indexProng0 || indexSoftPi == indexProng1 || indexSoftPi == indexProng2) {
            continue;
          }
          histos.fill(HIST("hCounter"), 5);

          /// determine the Σc candidate charge
          int chargeLc = candLc.prong0_as<TracksSigmac>().sign() + candLc.prong1_as<TracksSigmac>().sign() + candLc.prong2_as<TracksSigmac>().sign();
          int chargeSoftPi = trackSoftPi.sign();
          int8_t chargeSigmac = chargeLc + chargeSoftPi;
          if (std::abs(chargeSigmac) != 0 && std::abs(chargeSigmac) != 2) {
            /// this shall never happen
            LOG(fatal) << ">>> Sc candidate with charge +1 built, not possible! Charge Lc: " << chargeLc << ", charge soft pion: " << chargeSoftPi;
            continue;
          }
          histos.fill(HIST("hCounter"), 6);

          /// fill the Σc0,++ candidate table
          rowScCandidates(/* general columns */
                          candLc.collisionId(),
                          /* 2-prong specific columns */
                          candLc.px(), candLc.py(), candLc.pz(),
                          trackSoftPi.px(), trackSoftPi.py(), trackSoftPi.pz(),
                          candLc.globalIndex(), trackSoftPi.globalIndex(),
                          candLc.hfflag(),
                          /* Σc0,++ specific columns */
                          chargeSigmac,
                          statusSpreadMinvPKPiFromPDG, statusSpreadMinvPiKPFromPDG);
        } /// end loop over tracks
      }   /// end loop over candidates

    } /// end loop over collisions

  } /// end processWithAmbTracks
};

/// Extends the base table with expression columns.

struct HfCandidateSigmac0plusplusMc {

  Spawns<aod::HfCandScExt> candidatesSigmac;
  Produces<aod::HfCandScMcRec> rowMCMatchScRec;
  Produces<aod::HfCandScMcGen> rowMCMatchScGen;

  using LambdacMc = soa::Join<aod::HfCand3Prong, aod::HfSelLc, aod::HfCand3ProngMcRec>;
  // using LambdacMcGen = soa::Join<aod::McParticles, aod::HfCand3ProngMcGen>;
  using TracksMC = soa::Join<aod::Tracks, aod::McTrackLabels>;

  /// @brief init function
  void init(InitContext const&) {}

  /// @brief dummy process function, to be run on data
  /// @param
  void process(aod::Tracks const&) {}

  /// @brief process function for MC matching of Σc0,++ → Λc+(→pK-π+) π- reconstructed candidates and counting of generated ones
  /// @param candidatesSigmac reconstructed Σc0,++ candidates
  /// @param particlesMc table of generated particles
  void processMc(aod::McParticles const& particlesMc,
                 TracksMC const& tracks,
                 LambdacMc const& /*, const LambdacMcGen&*/)
  {

    // Match reconstructed candidates.
    candidatesSigmac->bindExternalIndices(&tracks);

    int indexRec = -1;
    int8_t sign = 0;
    int8_t flag = 0;
    int8_t origin = 0;
    int8_t chargeSigmac = 10;
    // std::vector<int> arrDaughIndex; /// index of daughters of MC particle

    /// Match reconstructed Σc0,++ candidates
    for (const auto& candSigmac : *candidatesSigmac) {
      indexRec = -1;
      sign = 0;
      flag = 0;
      origin = 0;
      // arrDaughIndex.clear();

      /// skip immediately the candidate Σc0,++ w/o a Λc+ matched to MC
      auto candLc = candSigmac.prongLc_as<LambdacMc>();
      if (!(std::abs(candLc.flagMcMatchRec()) == 1 << DecayType::LcToPKPi)) { /// (*)
        rowMCMatchScRec(flag, origin);
        continue;
      }

      /// matching to MC
      auto arrayDaughters = std::array{candLc.prong0_as<TracksMC>(),
                                       candLc.prong1_as<TracksMC>(),
                                       candLc.prong2_as<TracksMC>(),
                                       candSigmac.prong1_as<TracksMC>()};
      chargeSigmac = candSigmac.charge();
      if (chargeSigmac == 0) {
        /// candidate Σc0
        /// 3 levels:
        ///   1. Σc0 → Λc+ π-,+
        ///   2. Λc+ → pK-π+ direct (i) or Λc+ → resonant channel Λc± → p± K*, Λc± → Δ(1232)±± K∓ or Λc± → Λ(1520) π±  (ii)
        ///   3. in case of (ii): resonant channel to pK-π+
        indexRec = RecoDecay::getMatchedMCRec(particlesMc, arrayDaughters, pdg::Code::kSigmaC0, std::array{+kProton, -kKPlus, +kPiPlus, -kPiPlus}, true, &sign, 3);
        if (indexRec > -1) { /// due to (*) no need to check anything for LambdaC
          flag = sign * (1 << aod::hf_cand_sigmac::DecayType::Sc0ToPKPiPi);
        }
      } else if (std::abs(chargeSigmac) == 2) {
        /// candidate Σc++
        /// 3 levels:
        ///   1. Σc0 → Λc+ π-,+
        ///   2. Λc+ → pK-π+ direct (i) or Λc+ → resonant channel Λc± → p± K*, Λc± → Δ(1232)±± K∓ or Λc± → Λ(1520) π±  (ii)
        ///   3. in case of (ii): resonant channel to pK-π+
        indexRec = RecoDecay::getMatchedMCRec(particlesMc, arrayDaughters, pdg::Code::kSigmaCPlusPlus, std::array{+kProton, -kKPlus, +kPiPlus, +kPiPlus}, true, &sign, 3);
        if (indexRec > -1) { /// due to (*) no need to check anything for LambdaC
          flag = sign * (1 << aod::hf_cand_sigmac::DecayType::ScplusplusToPKPiPi);
        }
      }

      /// check the origin (prompt vs. non-prompt)
      if (flag != 0) {
        auto particle = particlesMc.rawIteratorAt(indexRec);
        origin = RecoDecay::getCharmHadronOrigin(particlesMc, particle);
      }

      /// fill the table with results of reconstruction level MC matching
      rowMCMatchScRec(flag, origin);
    } /// end loop over reconstructed Σc0,++ candidates

    /// Match generated Σc0,++ candidates
    for (const auto& particle : particlesMc) {
      flag = 0;
      origin = 0;

      /// 3 levels:
      ///   1. Σc0 → Λc+ π-,+
      ///   2. Λc+ → pK-π+ direct (i) or Λc+ → resonant channel Λc± → p± K*, Λc± → Δ(1232)±± K∓ or Λc± → Λ(1520) π±  (ii)
      ///   3. in case of (ii): resonant channel to pK-π+
      /// → here we check level 1. first, and then levels 2. and 3. are inherited by the Λc+ → pK-π+ MC matching in candidateCreator3Prong.cxx
      if (RecoDecay::isMatchedMCGen(particlesMc, particle, pdg::Code::kSigmaC0, std::array{static_cast<int>(pdg::Code::kLambdaCPlus), static_cast<int>(kPiMinus)}, true, &sign, 1)) {
        // generated Σc0
        // for (const auto& daughter : particle.daughters_as<LambdacMcGen>()) {
        for (const auto& daughter : particle.daughters_as<aod::McParticles>()) {
          // look for Λc+ daughter decaying in pK-π+
          if (std::abs(daughter.pdgCode()) != pdg::Code::kLambdaCPlus)
            continue;
          // if (std::abs(daughter.flagMcMatchGen()) == (1 << DecayType::LcToPKPi)) {
          if (RecoDecay::isMatchedMCGen(particlesMc, particle, pdg::Code::kLambdaCPlus, std::array{+kProton, -kKPlus, +kPiPlus}, true, &sign, 2)) {
            /// Λc+ daughter decaying in pK-π+ found!
            flag = sign * (1 << aod::hf_cand_sigmac::DecayType::Sc0ToPKPiPi);
            break;
          }
        }
      } else if (RecoDecay::isMatchedMCGen(particlesMc, particle, pdg::Code::kSigmaCPlusPlus, std::array{static_cast<int>(pdg::Code::kLambdaCPlus), static_cast<int>(kPiPlus)}, true, &sign, 1)) {
        // generated Σc++
        // for (const auto& daughter : particle.daughters_as<LambdacMcGen>()) {
        for (const auto& daughter : particle.daughters_as<aod::McParticles>()) {
          // look for Λc+ daughter decaying in pK-π+
          if (std::abs(daughter.pdgCode()) != pdg::Code::kLambdaCPlus)
            continue;
          // if (std::abs(daughter.flagMcMatchGen()) == (1 << DecayType::LcToPKPi)) {
          if (RecoDecay::isMatchedMCGen(particlesMc, particle, pdg::Code::kLambdaCPlus, std::array{+kProton, -kKPlus, +kPiPlus}, true, &sign, 2)) {
            /// Λc+ daughter decaying in pK-π+ found!
            flag = sign * (1 << aod::hf_cand_sigmac::DecayType::ScplusplusToPKPiPi);
            break;
          }
        }
      }

      /// check the origin (prompt vs. non-prompt)
      if (flag != 0) {
        auto particle = particlesMc.rawIteratorAt(indexRec);
        origin = RecoDecay::getCharmHadronOrigin(particlesMc, particle);
      }

      /// fill the table with results of generation level MC matching
      rowMCMatchScGen(flag, origin);

    } /// end loop over particlesMc
  }   /// end processMc
  PROCESS_SWITCH(HfCandidateSigmac0plusplusMc, processMc, "Process MC", false);
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  WorkflowSpec workflow{
    adaptAnalysisTask<HfCandidateCreatorSigmac0plusplus>(cfgc),
    adaptAnalysisTask<HfCandidateSigmac0plusplusMc>(cfgc)};
  return workflow;
}
