////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of NCrystal (see https://mctools.github.io/ncrystal/)   //
//                                                                            //
//  Copyright 2015-2020 NCrystal developers                                   //
//                                                                            //
//  Licensed under the Apache License, Version 2.0 (the "License");           //
//  you may not use this file except in compliance with the License.          //
//  You may obtain a copy of the License at                                   //
//                                                                            //
//      http://www.apache.org/licenses/LICENSE-2.0                            //
//                                                                            //
//  Unless required by applicable law or agreed to in writing, software       //
//  distributed under the License is distributed on an "AS IS" BASIS,         //
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  //
//  See the License for the specific language governing permissions and       //
//  limitations under the License.                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "G4NCrystal/G4NCMatHelper.hh"
#include "G4NCrystal/G4NCManager.hh"
#include "NCrystal/NCInfo.hh"
#include "NCrystal/NCScatter.hh"
#include "NCrystal/NCVersion.hh"
#include "NCrystal/NCCompositionUtils.hh"
#include "NCrystal/NCFactory.hh"
#include "G4NistManager.hh"
#include "G4ios.hh"
#include <atomic>

namespace NC = NCrystal;
namespace NCCU = NCrystal::CompositionUtils;

namespace G4NCrystal {

  static std::atomic<bool> s_verbose( getenv("NCRYSTAL_DEBUG_G4MATERIALS")!=nullptr );
  void enableCreateMaterialVerbosity(bool flag)
  {
    s_verbose = flag;
  }

  //Function which lets NCrystal::CompositionUtils use Geant4's knowledge of
  //natural abundances:
  std::vector<std::pair<unsigned,double>> g4NaturalAbundanceProvider(unsigned zz)
  {
    std::vector<std::pair<unsigned,double>> result;
    if ( zz<1 || zz>150 )
      return result;
    G4int z = static_cast<G4int>(zz);
    auto mgr = G4NistManager::Instance();
    G4int A0 = mgr->GetNistFirstIsotopeN(z);
    G4int Alim = A0 + mgr->GetNumberOfNistIsotopes(z);
    if ( ! (A0<1000 && Alim<1000 && A0>= z && Alim >= A0) )
      NCRYSTAL_THROW2(CalcError,"G4NistManager provided unexpexted A values (A="<<A0
                      <<" Alim="<<Alim<<") for natural element with Z="<<z);
    result.reserve(Alim-A0);
    for (G4int a = A0; a < Alim; ++a) {
      double val = mgr->GetIsotopeAbundance(z,a);
      nc_assert( 0.0<=val && val <= 1.0 );
      if (val>0.0)
        result.emplace_back(a,val);
    }
    return result;
  }

  void ensureCacheClearFctRegistered();


  //Class which contains all factory code and caches for creating G4Material's
  //based on NCrystal cfg objects. It is instantiated only as a global
  //singleton, access to which is protected by a mutex. It does not cache
  //pointers to Geant4 objects directly, but rather keep their indices into
  //Geant4's global database. The advantage is that this will let us know if the
  //objects were deleted and have to be recreated.

  class G4ObjectProvider final : public NC::MoveOnly {
  public:

    //////////////////////////////////////////////////////////////////////
    // Basic isotopes (masses provided completely by Geant4):           //
    //////////////////////////////////////////////////////////////////////

    G4Isotope * getIsotope(std::pair<unsigned,unsigned> key) {
      //Key is (Z,A)
      auto it = m_g4isotopes.find(key);
      if ( it != m_g4isotopes.end() ) {
        auto isotope = G4Isotope::GetIsotopeTable()->at(it->second);
        if (isotope!=nullptr)
          return isotope;//was created and still alive
      }
      //Must create:
      auto name = NC::AtomData::elementZToName(key.first);
      std::stringstream isoname;
      isoname << NC::AtomData::elementZToName(key.first) << key.second;
      auto isotope = new G4Isotope(isoname.str(), key.first, key.second );
      m_g4isotopes[key] = isotope->GetIndex();
      return isotope;
    }

    //////////////////////////////////////////////////////////////////////
    // Elements (natural abundances directly from G4's nist manager):   //
    //////////////////////////////////////////////////////////////////////

    G4Element * getElement(NCCU::ElementBreakdownLW&& key) {
      auto it = m_g4elements.find(key);
      if ( it != m_g4elements.end() ) {
        G4Element * elem0 = G4Element::GetElementTable()->at(it->second);
        if (elem0!=nullptr)
          return elem0;//was created and still alive
      }
      //Must create:
      G4Element * elem;
      if ( key.isNaturalElement() ) {
        //Natural element:
        elem = G4NistManager::Instance()->FindOrBuildElement(key.Z(),true);
        if (!elem)
          NCRYSTAL_THROW2(BadInput,"G4NistManager could not provide natural element for Z="<<key.Z());
      } else {
        //Custom element:
        unsigned Z = key.Z();
        auto name = NC::AtomData::elementZToName(Z);
        unsigned nIsotopes = key.nIsotopes();
        nc_assert( nIsotopes > 0 );
        elem = new G4Element(name,name,nIsotopes);
        for (unsigned i = 0; i < nIsotopes; ++i) {
          elem->AddIsotope( this->getIsotope( std::make_pair(Z,key.A(i) ) ),
                            key.fraction(i) );
        }
      }
      m_g4elements[ std::move(key) ] = elem->GetIndex();
      return elem;
    }

    //////////////////////////////////////////////////////////////////////
    // Base materials, corresponding to a given composition (monoatomic //
    // natural elements directly from G4's nist manager):               //
    //////////////////////////////////////////////////////////////////////

    G4Material * getBaseMaterial( const NC::Info::Composition& cmp ) {
      auto key = NCCU::createLWBreakdown( cmp, g4NaturalAbundanceProvider );
      nc_assert(!key.empty());
      NCRYSTAL_DEBUGONLY(for (auto& e: key) { nc_assert_always(e.second.valid()); });
      auto it = m_g4basematerials.find(key);
      if ( it != m_g4basematerials.end() ) {
        G4Material* mat = G4Material::GetMaterialTable()->at(it->second);
        if (mat)
          return mat;//was created and still alive
      }
      //Must create:
      if (key.size()==1 && key.front().second.isNaturalElement()) {
        //Monoatomic with natural element:
        G4Material* mat = G4NistManager::Instance()->FindOrBuildSimpleMaterial(key.front().second.Z());
        if (!mat)
          NCRYSTAL_THROW2(BadInput,"G4NistManager could not provide simple material for Z="<<key.front().second.Z());
        m_g4basematerials[ std::move(key) ] = mat->GetIndex();
        return mat;
      } else {
        //Must create the material the hard way.

        //Put dummy parameters for density/temperature/etc. on base
        //material. Top-level materials will anyway override.

        //Make sure all base material names are unique by adding unique ID to
        //the name. Note that this means that G4 material names will depend on
        //the order in which materials are created, which is unfortunate but
        //better than the alternative of possibly getting warnings from Geant4
        //about duplicate material names.

        static std::atomic<uint64_t> s_global_uid_counter(1);
        uint64_t uidval = s_global_uid_counter++;
        std::stringstream ss;
        ss << "NCrystalBase[uid=" << uidval << "]::" << breakdownToStr(key,15);

        G4Material * mat = new G4Material( ss.str(),
                                           1.0*CLHEP::gram/CLHEP::cm3,
                                           key.size(),//number of elements
                                           kStateSolid,
                                           293.15 * CLHEP::kelvin,
                                           1.0 * CLHEP::atmosphere );
        //Add Elements! Here we use the mat->AddElement form which use *mass*
        //fractions. Thus we must first construct the elements, then use their
        //masses and their (number) fractions to calculate mass fractions:
        std::vector<std::pair<double,G4Element*> > elements;
        elements.reserve(key.size());
        double tot_mass(0.0);//NB: StableSum would be better (but is in private interface)
        for ( auto& frac_elembd : key ) {
          G4Element * elem = getElement(std::move(frac_elembd.second));
          nc_assert(elem);
          double mass_contrib = frac_elembd.first * elem->GetAtomicMassAmu();
          tot_mass += mass_contrib;
          elements.emplace_back(mass_contrib,elem);
        }
        for (auto& e : elements)
          mat->AddElement( e.second, e.first / tot_mass );
        m_g4basematerials[ std::move(key) ] = mat->GetIndex();
        return mat;
      }
    }

    ///////////////////////////////////////////////////////////
    // Final materials, corresponding to a given cfg string. //
    // Cached also based on unique id's of Info objects      //
    ///////////////////////////////////////////////////////////

    G4Material * getFinalMaterial( const NC::MatCfg& cfg ) {
      G4Material * mat = getFinalMaterialImpl(cfg);
      if (s_verbose) {
        auto mgr = ::G4NCrystal::Manager::getInstance();
        const NCrystal::Scatter* sc = mgr->getScatterProperty(mat);
        nc_assert_always(sc&&mat);
        G4cout<<"G4NCrystal: Created NCrystal-enabled G4Material (G4Material index: "<<mat->GetIndex()
              <<", NCrystal Scatter \""<<sc->getCalcName()<<"\" with unique id: "<<sc->getUniqueID().value<<")"<<G4endl;
        G4cout<<"G4NCrystal::The material: ---------------------------------------------------------------------"<<G4endl<<G4endl;
        G4cout<<" Material index in table: "<<mat->GetIndex()<<G4endl;
        G4cout<<*mat<<G4endl;
        G4cout<<"G4NCrystal::The base material: ----------------------------------------------------------------"<<G4endl<<G4endl;
        auto bm = mat->GetBaseMaterial();
        if (bm) {
          G4cout<<" Material index in table: "<<bm->GetIndex()<<G4endl;
          G4cout<<*bm<<G4endl;
        }
        G4cout<<"-----------------------------------------------------------------------------------------------"<<G4endl;
      }
      return mat;
    }

    G4Material * getFinalMaterialImpl( const NC::MatCfg& cfg ) {

      //Construct key. NB: Uses file name as specified in key, to avoid absolute
      //paths in material names. We include the info objects unique id in the
      //cache key, to safe-guard against problems where input data was changed
      //and required reload.
      NC::RCHolder<const NC::Info> info(NC::createInfo(cfg));
      nc_assert_always(info.obj());
      const std::string cfg_as_str = cfg.toStrCfg(true,0);
      auto cache_key = std::make_pair( info.obj()->getUniqueID().value, cfg_as_str );

      //check if we already have this material available:
      auto it = m_g4finalmaterials.find(cache_key);
      if ( it != m_g4finalmaterials.end() ) {
        G4Material * mat = G4Material::GetMaterialTable()->at(it->second);
        if (mat)
          return mat;//was created and still alive
      }
      //Must create:

      ensureCacheClearFctRegistered();//good place to put this

      //Check that input has density and composition (for temperature we simply
      //wall back to room temperature below):
      if (!info.obj()->hasDensity())
        NCRYSTAL_THROW(MissingInfo,"Selected crystal info source lacks info about material density.");
      if (!info.obj()->hasComposition())
        NCRYSTAL_THROW(MissingInfo,"Selected crystal info source lacks info about material composition.");

      //Make sure that we are at all able to initialise an NCrystal scatter object
      //for the given configuration:
      NC::RCHolder<const NC::Scatter> scatter(NC::createScatter(cfg));

      //Base G4 material for the given chemical composition:
      G4Material * matBase = getBaseMaterial( info.obj()->getComposition() );

      //Create derived material with specific density, temperature and NCrystal scatter physics:

      //NB: Default temperature is same as NC::MatCfg's default (293.15) rather
      //than G4's STP (273.15). It will anyway always be overridden in the derived
      //material, but we do it like this to avoid two different temperatures even
      //when the user didn't specify anything.

      double temp = info.obj()->hasTemperature()
        ? info.obj()->getTemperature()*CLHEP::kelvin
        : 293.15*CLHEP::kelvin;

      G4String matnameprefix("NCrystal::");
      G4Material * mat = new G4Material( matnameprefix+cache_key.second,
                                         cfg.get_packfact()*info.obj()->getDensity()*(CLHEP::gram/CLHEP::cm3),
                                         matBase, kStateSolid, temp, 1.0 * CLHEP::atmosphere );

      G4NCrystal::Manager::getInstance()->addScatterProperty(mat,scatter.obj());

      //Add to cache and return:
      m_g4finalmaterials[cache_key] = mat->GetIndex();
      return mat;
    }

  private:
    //Maps of G4 objects (actually to their indices into G4's global tables -
    //that way we can detect if deleted and recreate):
    typedef std::size_t G4Index;
    typedef std::pair<unsigned,unsigned> IsotopeZA;
    std::map<IsotopeZA,G4Index> m_g4isotopes;
    std::map<NCCU::ElementBreakdownLW,G4Index> m_g4elements;
    std::map<NCCU::LWBreakdown,G4Index> m_g4basematerials;
    std::map<std::pair<uint64_t,std::string>,G4Index> m_g4finalmaterials;
  };

  static std::mutex s_objectdbmutex;//Mutex
  static G4ObjectProvider s_objectdb;//NB: Only access this after locking mutex!!
  //Cache-clearing in accordance with NCrystal's global clearCaches function
  //(also detects compatibility between libG4NCrystal.so and libNCrystal.so):
  void clearG4ObjCache() {
    std::lock_guard<std::mutex> guard(s_objectdbmutex);
    s_objectdb = G4ObjectProvider();
  }
  void ensureCacheClearFctRegistered() {
    static bool first = false;
    if (first)
      return;
    first = true;
    //Most client code will call this function, this is a good place to detect
    //mis-paired libNCrystal.so/libG4NCrystal.so:
    NC::libClashDetect();//Detect broken installation
    //Register:
    NC::registerCacheCleanupFunction(clearG4ObjCache);
  }
}

G4Material * G4NCrystal::createMaterial( const char * cfgstr )
{
  try {
    NC::MatCfg cfg(cfgstr);
    std::lock_guard<std::mutex> guard(s_objectdbmutex);
    return s_objectdb.getFinalMaterial(cfg);
  } catch ( NC::Error::Exception& e ) {
    Manager::handleError("G4NCrystal::createMaterial",101,e);
  }
  return 0;
}

G4Material * G4NCrystal::createMaterial( const G4String& cfgstr )
{
  //Just delegate to const char * version:
  return createMaterial(cfgstr.c_str());
}

G4Material * G4NCrystal::createMaterial( const NC::MatCfg&  cfg )
{
  try {
    std::lock_guard<std::mutex> guard(s_objectdbmutex);
    return s_objectdb.getFinalMaterial(cfg);
  } catch ( NC::Error::Exception& e ) {
    Manager::handleError("G4NCrystal::createMaterial",101,e);
  }
  return 0;
}
