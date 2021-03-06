#ifndef NCrystal_Info_hh
#define NCrystal_Info_hh

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

#include "NCrystal/NCSABData.hh"
#include "NCrystal/NCAtomData.hh"

/////////////////////////////////////////////////////////////////////////////////
// Data class containing information (high level or derived) about a given     //
// material. Instances of the class are typically generated by dedicated       //
// factories, based on interpretation of data files with e.g. crystallographic //
// information. Physics models in the form of for example NCScatter or         //
// NCAbsorption instances, are then initialised from these Info objects, thus  //
// providing a separation between data sources and algorithms working on the   //
// data.                                                                       //
/////////////////////////////////////////////////////////////////////////////////

namespace NCrystal {

  struct NCRYSTAL_API StructureInfo final {
    unsigned spacegroup = 0;//From 1-230 if provided, 0 if information not available
    double lattice_a = 0.0;//angstrom
    double lattice_b = 0.0;//angstrom
    double lattice_c = 0.0;//angstrom
    double alpha = 0.0;//degree
    double beta = 0.0;//degree
    double gamma = 0.0;//degree
    double volume = 0.0;//Aa^3
    unsigned n_atoms = 0;//Number of atoms per unit cell
  };

  struct NCRYSTAL_API HKLInfo final : public MoveOnly {
    double dspacing = 0.0;//angstrom
    double fsquared = 0.0;//barn
    int h = 0;
    int k = 0;
    int l = 0;
    unsigned multiplicity = 0;

    //If the HKLInfo source knows the plane normals, they will be provided in
    //the following list as unit vectors. Only half of the normals should be
    //included in this list, since if n is a normal, so is -n. If demi_normals
    //is not empty, it will be true that multiplicity == 2*demi_normals.size().
    struct NCRYSTAL_API Normal {
      Normal(double a1, double a2, double a3) : x(a1), y(a2), z(a3) {}
      double x, y, z;
    };
    std::vector<Normal> demi_normals;//TODO: vector->pointer saves 16B when not used

    //If eqv_hkl is not a null pointer, it contains the corresponding Miller
    //indices of the demi_normals as three 2-byte integers (short). Thus,
    //eqv_hkl has demi_normal.size()*3 entries:
    std::unique_ptr<short[]> eqv_hkl;
  };

  typedef std::vector<HKLInfo> HKLList;

  struct NCRYSTAL_API AtomIndex {
    unsigned value;
    bool operator<(const AtomIndex& o) const { return value<o.value; }
    bool operator==(const AtomIndex& o) const { return value==o.value; }
  };

  struct NCRYSTAL_API IndexedAtomData {
    //AtomData and associated index. The index is *only* valid in association
    //with a particular Info object. It exists since it is in principle possible
    //to have the same fundamental atom playing more than one role in a given
    //material (for instance, the same atom could have different displacements
    //on different positions in the unit cell).
    AtomDataSP atomDataSP;
    AtomIndex index;

    const AtomData& data() const;

    //Sort by index (comparison should only be performed with objects associated
    //with the same Info object):
    bool operator<(const IndexedAtomData& o) const;
    bool operator==(const IndexedAtomData& o) const;
  };

  struct NCRYSTAL_API AtomInfo final {

    //Element type:
    IndexedAtomData atom;
    const AtomData& data() const;

    //Number in unit cell:
    unsigned number_per_unit_cell = 0;

    //Per-element debye temperature (0.0 if not available, see hasPerElementDebyeTemperature() below):
    double debye_temp = 0.0;

    //Atomic coordinates (vector must be empty or have number_per_unit_cell entries):
    struct NCRYSTAL_API Pos final { Pos(double a, double b, double c) : x(a),y(b),z(c) {}; double x, y, z; };
    std::vector<Pos> positions;

    //Mean-square-displacements in angstrom^2 (0.0 if not available). Note that
    //this is the displacement projected onto a linear axis, for direct usage in
    //isotropic Debye-Waller factors:
    double mean_square_displacement = 0.0;
  };

  typedef std::vector<AtomInfo> AtomList;

  class NCRYSTAL_API DynamicInfo : public UniqueID {
  public:
    DynamicInfo(double fraction, IndexedAtomData, double temperature);
    virtual ~DynamicInfo() = 0;//Make abstract
    double fraction() const;
    void changeFraction(double f) { m_fraction = f; }
    double temperature() const;//same as on associated Info object

    const IndexedAtomData& atom() const { return m_atom; }
    AtomDataSP atomDataSP() const { return m_atom.atomDataSP; }
    const AtomData& atomData() const { return m_atom.data(); }

  private:
    double m_fraction;
    IndexedAtomData m_atom;
    double m_temperature;
  };

  typedef std::vector<std::unique_ptr<const DynamicInfo>> DynamicInfoList;

  class NCRYSTAL_API DI_Sterile final : public DynamicInfo {
    //Class indicating elements for which inelastic neutron scattering is absent
    //or disabled.
  public:
    using DynamicInfo::DynamicInfo;
    virtual ~DI_Sterile();
  };

  class NCRYSTAL_API DI_FreeGas final : public DynamicInfo {
    //Class indicating elements for which inelastic neutron scattering should be
    //modelled as scattering on a free gas.
  public:
    using DynamicInfo::DynamicInfo;
    virtual ~DI_FreeGas();
  };

  class NCRYSTAL_API DI_ScatKnl : public DynamicInfo {
  public:
    //Base class for dynamic information which can, directly or indirectly,
    //result in a SABData scattering kernel. The class is mostly semantic, as no
    //SABData access interface is provided on this class, as some derived
    //classes (e.g. VDOS) need dedicated algorithms in order to create the
    //SABData object. This class does, however, provide a unified interface for
    //associated data which is needed in order to use the SABData for
    //scattering. Currently this is just the grid of energy points for which SAB
    //integration will perform analysis and caching.
    virtual ~DI_ScatKnl();

    //If source dictated what energy grid to use for caching cross-sections,
    //etc., it can be returned here. It is ok to return a null ptr, leaving the
    //decision entirely to the consuming code. Grids must have at least 3
    //entries, and grids of size 3 actually indicates [emin,emax,npts], where
    //any value can be 0 to leave the choice for the consuming code. Grids of
    //size >=4 most be proper grids.
    typedef std::shared_ptr<const VectD> EGridShPtr;
    virtual EGridShPtr energyGrid() const = 0;
  protected:
    using DynamicInfo::DynamicInfo;
  };

  class NCRYSTAL_API DI_ScatKnlDirect : public DI_ScatKnl {
  public:
    //Pre-calculated scattering kernel which at most needs a conversion to
    //SABData format before it is available. For reasons of efficiency, this
    //conversion is actually not required to be carried out before calling code
    //calls the MT-safe ensureBuildThenReturnSAB().
    using DI_ScatKnl::DI_ScatKnl;
    virtual ~DI_ScatKnlDirect();

    //Use ensureBuildThenReturnSAB to access the scattering kernel:
    std::shared_ptr<const SABData> ensureBuildThenReturnSAB() const;

    //check if SAB is already built:
    bool hasBuiltSAB() const;

  protected:
    //Implement in derived classes to build the completed SABData object (will
    //only be called once and in an MT-safe context, protected by per-object
    //mutex):
    virtual std::shared_ptr<const SABData> buildSAB() const = 0;
  private:
    mutable std::shared_ptr<const SABData> m_sabdata;
    mutable std::mutex m_mutex;
  };

  class NCRYSTAL_API DI_VDOS : public DI_ScatKnl {
  public:
    //For a solid material, a phonon spectrum in the form of a Vibrational
    //Density Of State (VDOS) parameterisation, can be expanded into a full
    //scattering kernel. The calling code is responsible for doing this,
    //including performing choices as to grid layout, expansion order, etc.
    using DI_ScatKnl::DI_ScatKnl;
    virtual ~DI_VDOS();
    virtual const VDOSData& vdosData() const = 0;

    //The above vdosData() function returns regularised VDOS. The following
    //functions provide optional access to the original curves (returns empty
    //vectors if not available):
    virtual const VectD& vdosOrigEgrid() const = 0;
    virtual const VectD& vdosOrigDensity() const = 0;
  };

  class NCRYSTAL_API DI_VDOSDebye final : public DI_ScatKnl {
  public:
    //An idealised VDOS spectrum, based on the Debye Model in which the spectrum
    //rises quadratically with phonon energy below a cutoff value, kT, where T
    //is the Debye temperature (which must be available on the associated Info
    //object).
    DI_VDOSDebye( double fraction,
                  IndexedAtomData,
                  double temperature,
                  double debyeTemperature );
    virtual ~DI_VDOSDebye();
    double debyeTemperature() const;
    EGridShPtr energyGrid() const override { return nullptr; }
  private:
    double m_dt;
  };

  class NCRYSTAL_API Info final : public RCBase {
  public:

    //////////////////////////
    // Check if crystalline //
    //////////////////////////

    //Materials can be crystalline (i.e. at least one of structure info, atomic
    //positions and hkl info) must be present. Non-crystalline materials must
    //always have dynamic info present.
    bool isCrystalline() const;

    /////////////////////////////////////////
    // Information about crystal structure //
    /////////////////////////////////////////

    bool hasStructureInfo() const;
    const StructureInfo& getStructureInfo() const;

    //Convenience method, calculating the d-spacing of a given Miller
    //index. Calling this incurs the overhead of creating a reciprocal lattice
    //matrix from the structure info:
    double dspacingFromHKL( int h, int k, int l ) const;

    /////////////////////////////////////////
    // Information about material dynamics //
    /////////////////////////////////////////

    bool hasDynamicInfo() const;
    const DynamicInfoList& getDynamicInfoList() const;

    /////////////////////////////////////////////
    // Information about cross-sections [barn] //
    /////////////////////////////////////////////

    //absorption cross-section (at 2200m/s):
    bool hasXSectAbsorption() const;
    double getXSectAbsorption() const;

    //saturated scattering cross-section (high E limit):
    bool hasXSectFree() const;
    double getXSectFree() const;

    /////////////////////////////////////////////////////////////////////////////////
    // Provides calculation of "background" (non-Bragg diffraction) cross-sections //
    /////////////////////////////////////////////////////////////////////////////////

    bool providesNonBraggXSects() const;
    double xsectScatNonBragg(double lambda) const;

    ///////////////////////////
    // Temperature [kelvin]  //
    ///////////////////////////

    bool hasTemperature() const;
    double getTemperature() const;

    /////////////////////////////////
    // Debye temperature [kelvin]  //
    /////////////////////////////////

    //Global Debye temperature:
    bool hasAnyDebyeTemperature() const;
    bool hasGlobalDebyeTemperature() const;
    double getGlobalDebyeTemperature() const;

    //Whether AtomInfo objects have per-element Debye temperatures available:
    bool hasPerElementDebyeTemperature() const;

    //Convenience function for accessing Debye temperatures, whether global or per-element:
    double getDebyeTemperatureByElement(const AtomIndex&) const;

    ///////////////////////////////////
    // Atom Information in unit cell //
    ///////////////////////////////////

    bool hasAtomInfo() const;
    AtomList::const_iterator atomInfoBegin() const;
    AtomList::const_iterator atomInfoEnd() const;

    //Whether AtomInfo objects have atomic coordinates available:
    bool hasAtomPositions() const;

    //Whether AtomInfo objects have mean-square-displacements available:
    bool hasAtomMSD() const;

    //See also hasPerElementDebyeTemperature() above.

    /////////////////////
    // HKL Information //
    /////////////////////

    bool hasHKLInfo() const;
    unsigned nHKL() const;
    HKLList::const_iterator hklBegin() const;//first (==end if empty)
    HKLList::const_iterator hklLast() const;//last (==end if empty)
    HKLList::const_iterator hklEnd() const;
    //The limits:
    double hklDLower() const;
    double hklDUpper() const;
    //The largest/smallest (both returns inf if nHKL=0):
    double hklDMinVal() const;
    double hklDMaxVal() const;

    //////////////////////////////
    // Expanded HKL Information //
    //////////////////////////////

    //Whether HKLInfo objects have demi_normals available:
    bool hasHKLDemiNormals() const;

    //Whether HKLInfo objects have eqv_hkl available:
    bool hasExpandedHKLInfo() const;

    //Search eqv_hkl lists for specific (h,k,l) value. Returns hklEnd() if not found:
    HKLList::const_iterator searchExpandedHKL(short h, short k, short l) const;

    /////////////////////
    // Density [g/cm^3] //
    /////////////////////

    bool hasDensity() const;
    double getDensity() const;

    /////////////////////////////////
    // Number density [atoms/Aa^3] //
    /////////////////////////////////

    bool hasNumberDensity() const;
    double getNumberDensity() const;

    ////////////////////////////////////////////////////////////////////////////
    // Basic composition (always consistent with AtomInfo/DynInfo if present) //
    ////////////////////////////////////////////////////////////////////////////

    bool hasComposition() const;
    struct NCRYSTAL_API CompositionEntry {
      double fraction = -1.0;
      IndexedAtomData atom;
    };
    typedef std::vector<CompositionEntry> Composition;
    const Composition& getComposition() const;

    /////////////////////////////////////////////////////////////////////////////
    // Display labels associated with atom data. Needs index, so that for      //
    // instance an Al atom playing two different roles in the material will be //
    // labelled "Al-a" and "Al-b" respectively.                                //
    /////////////////////////////////////////////////////////////////////////////

    const std::string& displayLabel(const AtomIndex& ai) const;

    //////////////////////////////////////////////////////
    // All AtomData instances connected to object, by   //
    // index (allows efficient AtomDataSP<->index map.  //
    // (this is used to build the C/Python interfaces). //
    //////////////////////////////////////////////////////

    AtomDataSP atomDataSP( const AtomIndex& ai ) const;
    const AtomData& atomData( const AtomIndex& ai ) const;
    IndexedAtomData indexedAtomData( const AtomIndex& ai ) const;

    /////////////////////////////////////////////////////////////////////////////
    // Custom information for which the core NCrystal code does not have any   //
    // specific treatment. This is primarily intended as a place to put extra  //
    // data needed while developing new physics models. The core NCrystal code //
    // should never make use of such custom data.                              //
    /////////////////////////////////////////////////////////////////////////////

    //Data is stored as an ordered list of named "sections", with each section
    //containing "lines" which can consist of 1 or more "words" (strings).
    typedef VectS CustomLine;
    typedef std::vector<CustomLine> CustomSectionData;
    typedef std::string CustomSectionName;
    typedef std::vector<std::pair<CustomSectionName,CustomSectionData>> CustomData;
    const CustomData& getAllCustomSections() const;

    //Convenience (count/access specific section):
    unsigned countCustomSections(const CustomSectionName& sectionname ) const;
    const CustomSectionData& getCustomSection( const CustomSectionName& name,
                                               unsigned index=0 ) const;

    //////////////////////////////
    // Internals follow here... //
    //////////////////////////////

  public:
    //Methods used by factories when setting up an Info object:
    Info();
    void addAtom(const AtomInfo& ai) {ensureNoLock(); m_atomlist.push_back(ai); }
    void addAtom(AtomInfo&& ai) {ensureNoLock(); m_atomlist.push_back(std::move(ai)); }
    void enableHKLInfo(double dlower, double dupper);
    void addHKL(HKLInfo&& hi) { ensureNoLock(); m_hkllist.emplace_back(std::move(hi)); }
    void setHKLList(HKLList&& hkllist) { ensureNoLock(); m_hkllist = std::move(hkllist); }
    void setStructInfo(const StructureInfo& si) { ensureNoLock(); nc_assert_always(si.spacegroup!=999999); m_structinfo = si; }
    void setXSectFree(double x) { ensureNoLock(); m_xsect_free = x; }
    void setXSectAbsorption(double x) { ensureNoLock(); m_xsect_absorption = x; }
    void setTemperature(double t) { ensureNoLock(); m_temp = t; }
    void setGlobalDebyeTemperature(double dt) { ensureNoLock(); m_debyetemp = dt; }
    void setDensity(double d) { ensureNoLock(); m_density = d; }
    void setNumberDensity(double d) { ensureNoLock(); m_numberdensity = d; }
    void setXSectProvider(std::function<double(double)> xsp) { ensureNoLock(); nc_assert(!!xsp); m_xsectprovider = std::move(xsp); }
    void addDynInfo(std::unique_ptr<DynamicInfo> di) { ensureNoLock(); nc_assert(di); m_dyninfolist.push_back(std::move(di)); }
    void setComposition(Composition&& c) { ensureNoLock(); m_composition = std::move(c); }
    void setCustomData(CustomData&& cd) { ensureNoLock(); m_custom = std::move(cd); }

    void objectDone();//Finish up (sorts hkl list (by dspacing first), and atom info list (by Z first)). This locks the instance.
    bool isLocked() const { return m_lock; }

    UniqueIDValue getUniqueID() const { return m_uid.getUniqueID(); }

  private:
    void ensureNoLock();
    UniqueID m_uid;
    StructureInfo m_structinfo;
    AtomList m_atomlist;
    HKLList m_hkllist;//sorted by dspacing first
    DynamicInfoList m_dyninfolist;
    double m_hkl_dlower, m_hkl_dupper, m_density, m_numberdensity, m_xsect_free, m_xsect_absorption, m_temp, m_debyetemp;
    std::function<double(double)> m_xsectprovider;
    Composition m_composition;
    CustomData m_custom;
    bool m_lock;
    std::vector<AtomDataSP> m_atomDataSPs;
    VectS m_displayLabels;
  protected:
    virtual ~Info();
  };
}


////////////////////////////
// Inline implementations //
////////////////////////////

namespace NCrystal {
  inline bool Info::isCrystalline() const { return hasStructureInfo() || hasAtomPositions() || hasHKLInfo(); }
  inline bool Info::hasStructureInfo() const { return m_structinfo.spacegroup!=999999; }
  inline const StructureInfo& Info::getStructureInfo() const  { nc_assert(hasStructureInfo()); return m_structinfo; }
  inline bool Info::hasXSectAbsorption() const { return m_xsect_absorption >= 0.0; }
  inline bool Info::hasXSectFree() const { return m_xsect_free >= 0.0; }
  inline double Info::getXSectAbsorption() const { nc_assert(hasXSectAbsorption()); return m_xsect_absorption; }
  inline double Info::getXSectFree() const { nc_assert(hasXSectFree()); return m_xsect_free; }
  inline bool Info::providesNonBraggXSects() const { return !!m_xsectprovider; }
  inline double Info::xsectScatNonBragg(double lambda) const  { nc_assert(!!m_xsectprovider); return m_xsectprovider(lambda); }
  inline bool Info::hasTemperature() const { return m_temp > 0.0; }
  inline bool Info::hasAnyDebyeTemperature() const { return hasGlobalDebyeTemperature() || hasPerElementDebyeTemperature(); }
  inline bool Info::hasGlobalDebyeTemperature() const { return m_debyetemp > 0.0; }
  inline double Info::getTemperature() const { nc_assert(hasTemperature()); return m_temp; }
  inline double Info::getGlobalDebyeTemperature() const
  {
    if (!hasGlobalDebyeTemperature())
      NCRYSTAL_THROW(BadInput,"getGlobalDebyeTemperature called but no Debye temperature is available");
    return m_debyetemp;
  }
  inline bool Info::hasPerElementDebyeTemperature() const { return hasAtomInfo() && m_atomlist.front().debye_temp > 0.0; }
  inline bool Info::hasAtomPositions() const { return hasAtomInfo() && !m_atomlist.front().positions.empty(); }
  inline bool Info::hasAtomMSD() const { return hasAtomInfo() && m_atomlist.front().mean_square_displacement>0.0; }
  inline bool Info::hasAtomInfo() const  { return !m_atomlist.empty(); }
  inline AtomList::const_iterator Info::atomInfoBegin() const { nc_assert(hasAtomInfo()); return m_atomlist.begin(); }
  inline AtomList::const_iterator Info::atomInfoEnd() const { nc_assert(hasAtomInfo()); return m_atomlist.end(); }
  inline bool Info::hasHKLInfo() const { return m_hkl_dupper>=m_hkl_dlower; }
  inline bool Info::hasExpandedHKLInfo() const { return hasHKLInfo() && !m_hkllist.empty() && m_hkllist.front().eqv_hkl; }
  inline bool Info::hasHKLDemiNormals() const { return hasHKLInfo() && !m_hkllist.empty() && ! m_hkllist.front().demi_normals.empty(); }
  inline unsigned Info::nHKL() const { nc_assert(hasHKLInfo()); return m_hkllist.size(); }
  inline HKLList::const_iterator Info::hklBegin() const { nc_assert(hasHKLInfo()); return m_hkllist.begin(); }
  inline HKLList::const_iterator Info::hklLast() const
  {
    nc_assert(hasHKLInfo());
    return m_hkllist.empty() ? m_hkllist.end() : std::prev(m_hkllist.end());
  }
  inline HKLList::const_iterator Info::hklEnd() const { nc_assert(hasHKLInfo()); return m_hkllist.end(); }
  inline double Info::hklDLower() const { nc_assert(hasHKLInfo()); return m_hkl_dlower; }
  inline double Info::hklDUpper() const { nc_assert(hasHKLInfo()); return m_hkl_dupper; }
  inline bool Info::hasDensity() const { return m_density > 0.0; }
  inline double Info::getDensity() const { nc_assert(hasDensity()); return m_density; }
  inline bool Info::hasNumberDensity() const { return m_numberdensity > 0.0; }
  inline double Info::getNumberDensity() const { nc_assert(hasNumberDensity()); return m_numberdensity; }
  inline bool Info::hasDynamicInfo() const { return !m_dyninfolist.empty(); }
  inline const DynamicInfoList& Info::getDynamicInfoList() const  { return m_dyninfolist; }
  inline double DynamicInfo::fraction() const { return m_fraction; }
  inline double DynamicInfo::temperature() const { return m_temperature; }
  inline bool Info::hasComposition() const { return !m_composition.empty(); }
  inline const Info::Composition& Info::getComposition() const { return m_composition; }
  inline DI_VDOSDebye::DI_VDOSDebye( double fr, IndexedAtomData atom, double tt,double dt )
    : DI_ScatKnl(fr,std::move(atom),tt),m_dt(dt) { nc_assert(m_dt>0.0); }
  inline double DI_VDOSDebye::debyeTemperature() const { return m_dt; }
  inline const Info::CustomData& Info::getAllCustomSections() const { return m_custom; }
  inline const AtomData& IndexedAtomData::data() const
  {
    nc_assert(atomDataSP!=nullptr); return *atomDataSP;
  }
  inline bool IndexedAtomData::operator<(const IndexedAtomData& o) const {
    //Sanity check (same index means same AtomData instance):
    nc_assert( atomDataSP == o.atomDataSP || index.value != o.index.value );
    return index.value < o.index.value;
  }
  inline bool IndexedAtomData::operator==(const IndexedAtomData& o) const {
    //Sanity check (same index means same AtomData instance):
    nc_assert( atomDataSP == o.atomDataSP || index.value != o.index.value );
    return index.value == o.index.value;
  }
  inline const std::string& Info::displayLabel(const AtomIndex& ai) const
  {
    nc_assert(ai.value<m_displayLabels.size());
    return m_displayLabels[ai.value];
  }

  inline AtomDataSP Info::atomDataSP( const AtomIndex& ai ) const
  {
    nc_assert( ai.value < m_atomDataSPs.size());
    return m_atomDataSPs[ai.value];
  }

  inline const AtomData& Info::atomData( const AtomIndex& ai ) const
  {
    nc_assert( ai.value < m_atomDataSPs.size());
    return *m_atomDataSPs[ai.value];
  }

  inline IndexedAtomData Info::indexedAtomData( const AtomIndex& ai ) const
  {
    nc_assert( ai.value < m_atomDataSPs.size());
    return IndexedAtomData{m_atomDataSPs[ai.value],{ai.value}};
  }
  inline const AtomData& AtomInfo::data() const
  {
    nc_assert(!!atom.atomDataSP);
    return *atom.atomDataSP;
  }
}

#endif
