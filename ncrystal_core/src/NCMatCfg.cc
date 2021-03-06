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

#include "NCrystal/NCMatCfg.hh"
#include "NCrystal/NCInfo.hh"
#include "NCrystal/NCSCOrientation.hh"
#include "NCrystal/internal/NCString.hh"
#include "NCrystal/NCFile.hh"
#include "NCrystal/internal/NCMath.hh"
#include "NCrystal/internal/NCVector.hh"
#include "NCrystal/internal/NCAtomUtils.hh"
#include <sstream>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include <cstring>
namespace NC = NCrystal;

struct NC::MatCfg::Impl : public NC::RCBase {
  Impl() : RCBase() {
#ifndef NDEBUG
    //Verify that parnames is sorted correctly (NB: when changing order, also
    //update partypes and PARAMETERS enum)!
    static bool first = true;
    if (first) {
      first = false;
      for (int i = 1; i<PAR_NMAX; ++i) {
        nc_assert(parnames[i-1]<parnames[i]);
      }
    }
#endif
  }
  //clone:
  Impl( const Impl& o ) : RCBase() {
    //Make sure o.m_spies is empty! Otherwise it would either (if spies *are
    //not* transferred) be possible to cheat the spy by first causing cow() and
    //then access variables, or it would (if spies *are* transferred), become
    //technically difficult to uninstall the spies later. The intended usage is
    //anyway in factories, which should not be modifying MatCfg objects anyway:
    o.ensureNoSpy();
    //clone parlist:
    for (int i = PAR_FIRST; i < PAR_NMAX; ++i) {
      if (o.m_parlist[i])
        m_parlist[i] = std::unique_ptr<ValBase>(o.m_parlist[i]->clone());
    }
    //easy stuff:
    m_datafile_resolved = o.m_datafile_resolved;
    m_datafile_orig = o.m_datafile_orig;
    m_datafileext = o.m_datafileext;
    m_ignoredfilecfg = o.m_ignoredfilecfg;
  }

  ~Impl() {
    //NB: We don't own anything in m_spies.
  }

  void setOrientation( const SCOrientation& sco );
  void extractFileCfgStr(TextInputStream& input,std::string&);

  mutable std::vector<AccessSpy*> m_spies;
  struct SpyDisabler;
  std::string m_datafile_resolved;//resolved via NCFile
  std::string m_datafile_orig;//as passed to MatCfg constructor (empty if identical to m_datafile_resolved)
  std::string m_datafileext;
  bool m_ignoredfilecfg;


  //Important!: Keep the following list in alphabetical order and synchronised
  //with parnames and partypes further down the file!
  enum PARAMETERS { PAR_FIRST = 0,
                    PAR_absnfactory = 0,
                    PAR_atomdb,
                    PAR_coh_elas,
                    PAR_dcutoff,
                    PAR_dcutoffup,
                    PAR_dir1,
                    PAR_dir2,
                    PAR_dirtol,
                    PAR_incoh_elas,
                    PAR_inelas,
                    PAR_infofactory,
                    PAR_lcaxis,
                    PAR_lcmode,
                    PAR_mos,
                    PAR_mosprec,
                    PAR_overridefileext,
                    PAR_packfact,
                    PAR_scatfactory,
                    PAR_sccutoff,
                    PAR_temp,
                    PAR_vdoslux,
                    PAR_NMAX };

  enum VALTYPE { VALTYPE_DBL, VALTYPE_BOOL, VALTYPE_INT, VALTYPE_STR, VALTYPE_ORIENTDIR, VALTYPE_VECTOR, VALTYPE_ATOMDB };
  static std::string parnames[PAR_NMAX];
  static VALTYPE partypes[PAR_NMAX];

  struct ValBase {
    ValBase(){}
    virtual ~ValBase(){}
    virtual ValBase * clone() const = 0;
    virtual void set_from_strrep(const std::string& s) = 0;
    virtual std::string to_strrep(bool forcache) const = 0;
  };

  //Array where we keep the actual configuration. Notice: Make sure this is
  //never accessed without triggerSpy() or ensureNoSpy() (except in output which
  //is not expected to be parsed, like dump() or toStrCfg())!
  std::unique_ptr<ValBase> m_parlist[PAR_NMAX];

  bool hasPar(PARAMETERS par) const { triggerSpy(par); return m_parlist[par]!=nullptr; }

  struct ValDbl : public ValBase {
    enum UnitType { UnitNone, UnitAngle, UnitTemp, UnitLength };
    typedef double value_type;
    static const VALTYPE value_type_enum = VALTYPE_DBL;
    ValDbl() : ValBase(), unittype(UnitNone) {};
    virtual ~ValDbl(){}
    virtual ValBase * clone() const { return new ValDbl(*this); }
    void set_from_strrep(const std::string& s) final
    {
      static std::string alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
      std::string tmporig = s;
      std::string tmp = s;
      trim(tmp);
      double unitfact = 1.0;
      double unitoffset = 0.0;
      if (unittype!=UnitNone&&tmp.size()>1&&contains(alpha,*(tmp.rbegin()))) {//*.rbegin since .back is c++11 only
        size_t iunit = tmp.size();
        while (iunit>0 && contains(alpha,tmp.at(iunit-1)))
          --iunit;
        std::string unit = tmp.substr(iunit);
        tmp.resize(iunit);
        trim(tmp);
        tmporig = tmp + unit;
        double u = -1.0;
        switch(unittype) {
        case UnitAngle:
          if (unit=="rad") { u = 1.0; }
          else if (unit=="deg") { u = kDeg; }
          else if (unit=="arcmin") { u = kArcMin; }
          else if (unit=="arcsec") { u = kArcSec; }
          break;
        case UnitLength:
          if (unit=="Aa") { u = 1.0; }
          else if (unit=="nm") { u = 10.0; }
          else if (unit=="mm") { u = 1e7;}
          else if (unit=="cm") { u = 1e8;}
          else if (unit=="m") { u = 1e10;}
          break;
        case UnitTemp:
          if (unit=="K") { u = 1.0; }
          else if (unit=="C") { u = 1.0; unitoffset = 273.15; }
          else if (unit=="F") { u = 1/1.8; unitoffset = 273.15-(32/1.8); }
          break;
        case UnitNone:
          break;
        default:
          break;
        }
        if (u<=0.0)
          NCRYSTAL_THROW2(BadInput,"Invalid unit: "<<unit);
        unitfact = u;
      }
      set(unitoffset + unitfact * str2dbl(tmp));//checks nan
      origstrrep = tmporig;
      trim(origstrrep);
    }
    void set(double v) {
      if (ncisnan(v))
        NCRYSTAL_THROW(BadInput,"Attempting to set number to NaN");
      value = v;
      origstrrep.clear();
    }
    std::string to_strrep(bool forcache) const {

      if (!forcache && !origstrrep.empty())
        return origstrrep;
      std::stringstream s;
      if (forcache)
        s << std::setprecision(16);
      s << value;
      return s.str();
    }

    //data:
    double value;
    void setUnitType(UnitType ut) { unittype = ut; }
  private:
    UnitType unittype;
    std::string origstrrep;//original input (if available), for lossless reproduction.
  };

  struct ValInt : public ValBase {
    typedef int value_type;
    static const VALTYPE value_type_enum = VALTYPE_INT;
    ValInt() : ValBase(){};
    virtual ~ValInt(){}
    virtual ValBase * clone() const { return new ValInt(*this); }
    void set_from_strrep(const std::string& s) final
    {
      set(str2int(s));//checks nan
    }
    void set(int v) {
      value = v;
    }
    std::string to_strrep(bool) const {
      std::stringstream s;
      s << value;
      return s.str();
    }
    //data:
    int value;
  };

  struct ValBool : public ValBase {
    typedef bool value_type;
    static const VALTYPE value_type_enum = VALTYPE_BOOL;
    ValBool() : ValBase(){}
    virtual ~ValBool(){}
    virtual ValBase * clone() const { return new ValBool(*this); }
    void set_from_strrep(const std::string& s) final
    {
      if (s=="true"||s=="1") { value = true; }
      else if (s=="false"||s=="0") { value = false; }
      else { NCRYSTAL_THROW2(BadInput,"Could not convert \""<<s
                             <<"\" to boolean value (should be \"true\", \"1\", \"false\" or \"0\")") }
    }
    std::string to_strrep(bool) const { return value?"true":"false"; }
    void set(bool v) { value = v; }
    bool value;
  };

  //reduce potential escaping worries in various contexts by making sure we
  //never use these special characters (in addition to a SimpleAscii check):
#define NCMATCFG_FORBIDDEN_CHARS "\"'|><(){}[]"

  struct ValStr : public ValBase {
    typedef std::string value_type;
    static const VALTYPE value_type_enum = VALTYPE_STR;
    ValStr() : ValBase() {}
    virtual ~ValStr(){}
    virtual ValBase * clone() const { return new ValStr(*this); }
    void set_from_strrep(const std::string& s) final { set(s); }
    void set(const std::string& s) {
      if (!isSimpleASCII(s,false,false))
        NCRYSTAL_THROW(BadInput,"Non-ASCII characters or tab/newlines in string value!");
      if (contains_any(s,NCMATCFG_FORBIDDEN_CHARS)||contains_any(s,"=;"))
        NCRYSTAL_THROW(BadInput,"Forbidden characters in string value!");
      value = s;
    }
    std::string to_strrep(bool) const { return value; }
    std::string value;
  };

  struct ValAtomDB : public ValBase {
    typedef std::vector<VectS> value_type;
    static const VALTYPE value_type_enum = VALTYPE_ATOMDB;
    ValAtomDB() : ValBase() {}
    virtual ~ValAtomDB(){}
    virtual ValBase * clone() const { return new ValAtomDB(*this); }
    void set_from_strrep(const std::string& s) final {
      value_type v;
      VectS lines;
      split(lines,s,0,'@');
      for (auto& line: lines) {
        strreplace(line,":"," ");
        v.emplace_back();
        split(v.back(),line);
      }
      set(v);
    }
    void set(const value_type& s) {
      value.clear();
      value.reserve(s.size());
      unsigned iline = 0;
      for (auto& line : s) {
        if (line.empty())
          continue;
        for (auto& word: line) {
          if (!isSimpleASCII(word,false,false))
            NCRYSTAL_THROW(BadInput,"Non-ASCII characters or tab/newlines in atomdb parameter!");
          if (contains_any(word,NCMATCFG_FORBIDDEN_CHARS)||contains_any(word,"=;"))
            NCRYSTAL_THROW(BadInput,"Forbidden characters in atomdb parameter!");
        }
        try {
          validateAtomDBLine( line );
        } catch (Error::BadInput&e) {
          NCRYSTAL_THROW2(BadInput,"Invalid entry in atomdb cfg parameter in the line: \""<<joinstr(line)<<"\". Error is: "<<e.what());
        }

        //Check for position of "nodefaults" keyword:
        if (line.size()==1&&line.at(0)=="nodefaults") {
          if (iline>0)
            NCRYSTAL_THROW2(BadInput,"Invalid entry in atomdb cfg parameter (\"nodefaults\" must be the first line)");
        }
        ++iline;
        value.push_back(std::move(line));
      }
      value_as_string = to_strrep_impl();
    }
    std::string to_strrep(bool) const { return value_as_string; }
    std::string to_strrep_impl() const {
      std::string res;
      if (value.empty())
        return res;
      auto n = value.size();
      for (decltype(n) i = 0; i < n; ++i) {
        res += joinstr( value.at(i), ":" );
        if ( i+1 < n )
          res += "@";
      }
      return res;
    }
    value_type value;
    std::string value_as_string;
  };

  struct ValOrientDir : public ValBase {
    ValOrientDir() : ValBase(){}
    virtual ~ValOrientDir(){}
    virtual ValBase * clone() const { return new ValOrientDir(*this); }
    void set_from_strrep(const std::string& s) final
    {
      std::string st = s; trim(st);
      VectS parts;
      split(parts,st,0,'@');
      if (parts.size()!=3||!parts.at(0).empty())
        NCRYSTAL_THROW2(BadInput,"Bad syntax for orientation: \""<<s<<"\"");
      std::string& c = parts.at(1);
      std::string& l = parts.at(2);
      int c_is_hkl(-1);
      if (startswith(c,"crys:")) { c = c.substr(5); c_is_hkl = 0; }
      else if (startswith(c,"crys_hkl:")) { c = c.substr(9); c_is_hkl = 1; }
      if (c_is_hkl==-1||!startswith(l,"lab:"))
        NCRYSTAL_THROW2(BadInput,"Bad syntax for orientation: \""<<s<<"\"");
      l = l.substr(4);
      trim(c);
      trim(l);
      VectS partsc, partsl;
      split(partsc,c,0,',');
      split(partsl,l,0,',');
      if (partsc.size()!=3||partsl.size()!=3)
        NCRYSTAL_THROW2(BadInput,"Bad syntax for orientation: \""<<s<<"\"");
      set(c_is_hkl,
          str2dbl(partsc.at(0)),str2dbl(partsc.at(1)),str2dbl(partsc.at(2)),
          str2dbl(partsl.at(0)),str2dbl(partsl.at(1)),str2dbl(partsl.at(2)));
      origstrrep = s;
      trim(origstrrep);
    }
    std::string to_strrep(bool) const {
      if (!origstrrep.empty())
        return origstrrep;
      std::stringstream s;
      s.precision(17);//TODO: should we not only have high res if argument bool
                      //is true? (here and elsewhere). In fact, we should
                      //probably make sure that the argument bool can be changed
                      //in toStrCfg, and get high-res cache keys (e.g. in Geant4
                      //hooks).
      s << (crystal_is_hkl?"@crys_hkl:":"@crys:")
        << crystal[0] << "," << crystal[1] << ","
        << crystal[2] << "@lab:" << lab[0] << ","
        << lab[1] << "," << lab[2];
      return s.str();
    }
    void set(bool cishkl, double c1, double c2, double c3, double l1, double l2, double l3)
    {
      if (ncisnan(c1)||ncisnan(c2)||ncisnan(c3)||ncisnan(l1)||ncisnan(l2)||ncisnan(l3))
        NCRYSTAL_THROW(BadInput,"Attempting to set number to NaN");
      crystal_is_hkl = cishkl;
      crystal[0] = c1; crystal[1] = c2; crystal[2] = c3;
      lab[0] = l1; lab[1] = l2; lab[2] = l3;
      origstrrep.clear();
    }
    bool crystal_is_hkl;
    double crystal[3];
    double lab[3];
  private:
    std::string origstrrep;//original input (if available), for lossless reproduction.
  };

  struct ValVector : public ValBase {
    ValVector() : ValBase(){}
    virtual ~ValVector(){}
    virtual ValBase * clone() const { return new ValVector(*this); }
    void set_from_strrep(const std::string& s) final
    {
      std::string st = s; trim(st);
      VectS parts;
      split(parts,st,0,',');
      if (parts.size()!=3)
        NCRYSTAL_THROW2(BadInput,"Bad syntax for vector value: \""<<s<<"\"");
      trim(parts.at(0));
      trim(parts.at(1));
      trim(parts.at(2));
      this->set(str2dbl(parts.at(0)),str2dbl(parts.at(1)),str2dbl(parts.at(2)));
      origstrrep = s;
      trim(origstrrep);
    }
    std::string to_strrep(bool) const {
      if (!origstrrep.empty())
        return origstrrep;
      std::stringstream s;
      s.precision(17);
      s << val[0] << "," << val[1] << "," << val[2];
      return s.str();
    }
    void set(double x, double y, double z)
    {
      if (ncisnan(x)||ncisnan(y)||ncisnan(z))
        NCRYSTAL_THROW(BadInput,"Attempting to set number to NaN");
      val[0] = x; val[1] = y; val[2] = z;
      origstrrep.clear();
    }
    double val[3];
  private:
    std::string origstrrep;//original input (if available), for lossless reproduction.
  };

  void ensureNoSpy() const
  {
    if (!m_spies.empty())
      NCRYSTAL_THROW(LogicError,"Modification of configuration object whose access is being monitored is forbidden!");
  }

  void triggerSpy(PARAMETERS par) const
  {
    std::vector<AccessSpy*>::const_iterator it(m_spies.begin()), itE(m_spies.end());
    const std::string& pn = parnames[par];
    for (;it!=itE;++it) {
      (*it)->parAccessed(pn);
    }
  }

  template <class ValType>
  const ValType* getValType(PARAMETERS par) const {
    triggerSpy(par);
    const ValBase * vb = m_parlist[par].get();
    nc_assert( vb==nullptr || dynamic_cast<const ValType*>(vb) );
    return static_cast<const ValType*>(vb);
  }

  template <class ValType>
  const ValType* getValTypeThrowIfNotAvail(PARAMETERS par) const {
    const ValType * vt = getValType<ValType>(par);
    if (!vt)
      NCRYSTAL_THROW2(MissingInfo,"Value for parameter "<<parnames[par]<<" not available");
    return vt;
  }

  template <class ValType>
  void addUnitsForValType(ValType*, PARAMETERS) {}

  template <class ValType>
  ValType* getValTypeForSet(PARAMETERS par) {
    ensureNoSpy();
    ValBase * vb = m_parlist[par].get();
    if (vb) {
      nc_assert( dynamic_cast<ValType*>(vb) );
      return static_cast<ValType*>(vb);
    } else {
      auto vt = std::make_unique<ValType>();
      addUnitsForValType<ValType>(vt.get(),par);//allow certain units for certain parameters
      auto vt_rawptr = vt.get();
      m_parlist[par] = std::move(vt);
      return vt_rawptr;
    }
  }

  template <class ValType>
  const typename ValType::value_type& getVal(PARAMETERS par, const typename ValType::value_type & code_default_val  ) const
  {
    nc_assert( ValType::value_type_enum == partypes[par] );
    const ValType * vt = getValType<ValType>(par);
    return vt ? vt->value : code_default_val;
  }

  template <class ValType>
  const typename ValType::value_type& getValNoFallback(PARAMETERS par) const
  {
    nc_assert( ValType::value_type_enum == partypes[par] );
    return getValTypeThrowIfNotAvail<ValType>(par)->value;
  }

  template <class ValType>
  void setVal(PARAMETERS par, const typename ValType::value_type& val )
  {
    getValTypeForSet<ValType>(par)->set(val);
  }

  int strNameToParIdx(const std::string& name) const {
    const std::string * itB = &parnames[0];
    const std::string * itE = itB+PAR_NMAX;
    const std::string* it = std::lower_bound(itB,itE,name);
    if ( it == itE || *it != name )
      NCRYSTAL_THROW2(BadInput,"Unknown parameter: \""<<name<<"\"");
    nc_assert( it-itB >= 0 && it-itB < PAR_NMAX );
    return it-itB;
  }

  void setValByStr( std::string name, const std::string& value )
  {
#ifndef NDEBUG
    static bool first = true;
    if (first) {
      first = false;
      nc_assert(strNameToParIdx(parnames[0])==0);
      nc_assert(strNameToParIdx(parnames[PAR_NMAX-1])==PAR_NMAX-1);
    }
#endif
    //Handle pseudo-parameters (special aliases and backwards compat.):
    if (name=="bragg") {
      name="coh_elas";
    } else if (name=="elas") {
      ValBool tmp;
      tmp.set_from_strrep(value);
      setVal<Impl::ValBool>(Impl::PAR_coh_elas,tmp.value);
      setVal<Impl::ValBool>(Impl::PAR_incoh_elas,tmp.value);
      return;
    } else if (name=="bkgd") {
      if ( value=="none" || value == "0" ) {
        setVal<Impl::ValBool>(Impl::PAR_incoh_elas,false);
        setVal<Impl::ValStr>(Impl::PAR_inelas,"none");
        return;
      } else {
        NCRYSTAL_THROW(BadInput,"The \"bkgd\" parameter is obsolete and is available for backwards compatibility "
                       "only with the values \"0\" or \"none\". For control of inelastic or incoherent-elastic "
                       "scattering, one must now instead use the parameters \"incoh_elas\" and \"inelas\".");
      }
    }

    int paridx = strNameToParIdx(name);

    if ( value.empty() && partypes[paridx] != VALTYPE_STR )//only string parameters can construct from empty strings
      NCRYSTAL_THROW2( BadInput, "Missing parameter value for parameter \""<<name<<"\"" );

    switch(partypes[paridx]) {
    case VALTYPE_DBL: getValTypeForSet<ValDbl>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_INT: getValTypeForSet<ValInt>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_BOOL: getValTypeForSet<ValBool>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_STR: getValTypeForSet<ValStr>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_ORIENTDIR: getValTypeForSet<ValOrientDir>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_VECTOR: getValTypeForSet<ValVector>((PARAMETERS)paridx)->set_from_strrep(value); return;
    case VALTYPE_ATOMDB: getValTypeForSet<ValAtomDB>((PARAMETERS)paridx)->set_from_strrep(value); return;
    default:
      nc_assert_always(false);
    }
  }

  static void decodeopts(const std::string& optstr, std::map<std::string,std::string>& opts2val, bool skipname = true )
  {
    opts2val.clear();
    VectS parts;
    split(parts,optstr,0,':');
    VectS::iterator it(parts.begin()), itE(parts.end());
    nc_assert_always(it!=itE);
    if (skipname)
      ++it;//skip main opt name
    VectS subparts;
    subparts.reserve(2);
    static std::string alphalowercase = "abcdefghijklmnopqrstuvwxyz";
    static std::string alphalowercasenumunderscore  = "abcdefghijklmnopqrstuvwxyz0123456789_";
    for (;it!=itE;++it) {
      trim(*it);
      if (it->empty())
        continue;
      subparts.clear();
      if (contains(*it,'@')) {
        split(subparts,*it,0,'@');
        for (std::size_t i = 0; i<subparts.size();++i)
          trim(subparts.at(i));
        if ( subparts.size()!=2||subparts.at(0).empty()||subparts.at(1).empty()||contains_any(subparts.at(1),"<>:=") ) {
          NCRYSTAL_THROW2(BadInput,"Syntax error in options: \""<<optstr<<"\"");
        }
        if ( !contains_only(subparts.at(0),alphalowercasenumunderscore)||!contains(alphalowercase,subparts.at(0)[0]) ) {
          NCRYSTAL_THROW2(BadInput,"Syntax error in options. Invalid option name: \""<<subparts.at(0)<<"\"");
        }
      } else {
        subparts.push_back(*it);
        subparts.push_back("<flag>");
      }
      if ( opts2val.find(subparts.at(0))!=opts2val.end() ) {
        NCRYSTAL_THROW2(BadInput,"Syntax error in options. Option specified multiple times: \""<<subparts.at(0)<<"\"");
      }
      opts2val[subparts.at(0)]=subparts.at(1);
    }
  }

  static std::string decodeopt_name(const std::string& optstr)
  {
    std::string name;
    if (!contains(optstr,':')) {
      name = optstr;
    } else {
      VectS parts;
      split(parts,optstr,1,':');
      nc_assert_always(!parts.empty());
      trim(parts[0]);
      name=parts[0];
    }
    return name;
  }

  static bool decodeopt_flag(const std::string& optstr, const std::string& flagname)
  {
    if (!contains(optstr,':'))
      return false;
    std::map<std::string,std::string> opts2val;
    decodeopts(optstr, opts2val );
    std::map<std::string,std::string>::const_iterator it=opts2val.find(flagname);
    if (it==opts2val.end())
      return false;
    if (it->second!="<flag>")
      NCRYSTAL_THROW2(BadInput,"Syntax error in flag: \""<<flagname<<"\" (takes no value)");
    return true;
  }

  static double decodeopt_dbl(const std::string& optstr, const std::string& parname, double defval)
  {
    if (!contains(optstr,':'))
      return defval;
    std::map<std::string,std::string> opts2val;
    decodeopts(optstr, opts2val);
    std::map<std::string,std::string>::const_iterator it=opts2val.find(parname);
    if (it==opts2val.end())
      return defval;
    return str2dbl(it->second);
  }

  static int decodeopt_int(const std::string& optstr, const std::string& parname, int defval)
  {
    if (!contains(optstr,':'))
      return defval;
    std::map<std::string,std::string> opts2val;
    Impl::decodeopts(optstr, opts2val);
    std::map<std::string,std::string>::const_iterator it=opts2val.find(parname);
    if (it==opts2val.end())
      return defval;
    return str2int(it->second);
  }

  static void decodedopt_validate(const std::string& optstr,
                                  const std::set<std::string>& recognised_opts)
  {
    if (!contains(optstr,':'))
      return;
    std::string name = decodeopt_name(optstr);
    std::map<std::string,std::string> opts2val;
    decodeopts(optstr, opts2val);
    std::map<std::string,std::string>::const_iterator it(opts2val.begin()),itE(opts2val.end());
    for (;it!=itE;++it)
      if (!recognised_opts.count(it->first)) {
        NCRYSTAL_THROW2(BadInput,"The flag \""<<it->first<<"\" is not supported by the chosen"
                        " factory for a mode of \""<<name<<"\"");
      }
  }

private:
  Impl& operator=(const Impl& o);//forbid
};

namespace NCrystal {
  //Need fallback string values here so we can return references to them:
  static const std::string s_matcfg_str_empty = std::string();
  static const std::string s_matcfg_str_auto = std::string("auto");
  static const std::string s_matcfg_str_none = std::string("none");

  //Important!: Keep the following two lists ordered (parnames sorted
  //alphabetically) and synchronised between themselves as well as the
  //PARAMETERS enum earlier in the file!
  std::string MatCfg::Impl::parnames[PAR_NMAX] = { "absnfactory",
                                                   "atomdb",
                                                   "coh_elas",
                                                   "dcutoff",
                                                   "dcutoffup",
                                                   "dir1",
                                                   "dir2",
                                                   "dirtol",
                                                   "incoh_elas",
                                                   "inelas",
                                                   "infofactory",
                                                   "lcaxis",
                                                   "lcmode",
                                                   "mos",
                                                   "mosprec",
                                                   "overridefileext",
                                                   "packfact",
                                                   "scatfactory",
                                                   "sccutoff",
                                                   "temp",
                                                   "vdoslux" };
  MatCfg::Impl::VALTYPE MatCfg::Impl::partypes[PAR_NMAX] = { VALTYPE_STR,
                                                             VALTYPE_ATOMDB,
                                                             VALTYPE_BOOL,
                                                             VALTYPE_DBL,
                                                             VALTYPE_DBL,
                                                             VALTYPE_ORIENTDIR,
                                                             VALTYPE_ORIENTDIR,
                                                             VALTYPE_DBL,
                                                             VALTYPE_BOOL,
                                                             VALTYPE_STR,
                                                             VALTYPE_STR,
                                                             VALTYPE_VECTOR,
                                                             VALTYPE_INT,
                                                             VALTYPE_DBL,
                                                             VALTYPE_DBL,
                                                             VALTYPE_STR,
                                                             VALTYPE_DBL,
                                                             VALTYPE_STR,
                                                             VALTYPE_DBL,
                                                             VALTYPE_DBL,
                                                             VALTYPE_INT };
  struct MatCfg::Impl::SpyDisabler {
    //swaps spies with empty list (disabling spying) and swaps back in destructor
    SpyDisabler(std::vector<AccessSpy*>& spies)
      : m_spies_ptr(spies.empty()?0:&spies)
    {
      if (m_spies_ptr)
        std::swap(*m_spies_ptr,m_stashed_spies);
    }
    ~SpyDisabler() {
      if (m_spies_ptr)
        std::swap(*m_spies_ptr,m_stashed_spies);
    }
  private:
    std::vector<AccessSpy*>* m_spies_ptr;
    std::vector<AccessSpy*> m_stashed_spies;
  };

  template<>
  void MatCfg::Impl::addUnitsForValType(ValDbl* vt, PARAMETERS par) {
    switch(par) {
    case PAR_mos:
    case PAR_dirtol:
      vt->setUnitType(ValDbl::UnitAngle);
      return;
    case PAR_temp:
      vt->setUnitType(ValDbl::UnitTemp);
      return;
    case PAR_dcutoff:
    case PAR_dcutoffup:
      vt->setUnitType(ValDbl::UnitLength);
      return;
    default:
      break;
    }
  }
}


bool NC::MatCfg::ignoredEmbeddedConfig() const
{
  return m_impl->m_ignoredfilecfg;
}

std::string NC::MatCfg::toEmbeddableCfg() const
{
  std::stringstream out;
  out << "NCRYSTALMATCFG[" << toStrCfg(false,0) << ']';
  return out.str();
}

std::string NC::MatCfg::toStrCfg( bool include_datafile, const std::set<std::string> * only_parnames ) const
{
  //disable any spies during invocation of this method (because we assume
  //toStrCfg will be used for things like debug output, not to actually access
  //the parameters):
  Impl::SpyDisabler nospy(m_impl->m_spies);

  std::stringstream out;
  if (include_datafile) {
    out << getDataFileAsSpecified();
    if (m_impl->m_ignoredfilecfg)
      out << ";ignorefilecfg";
  }
  Impl::ValBase* vb;
  bool empty(out.str().empty());
  for (int i = Impl::PAR_FIRST; i<Impl::PAR_NMAX; ++i) {
    if ( ( vb = m_impl->m_parlist[i].get() ) ) {
      if (only_parnames&&!only_parnames->count(m_impl->parnames[i]))
        continue;
      if (!empty)
        out<<';';
      out << Impl::parnames[i]<<"="<<vb->to_strrep(false);
      empty = false;
    }
  }
  return out.str();
}

bool NC::MatCfg::isSingleCrystal() const
{
  return m_impl->hasPar(Impl::PAR_mos) || m_impl->hasPar(Impl::PAR_dir1) ||
    m_impl->hasPar(Impl::PAR_dir2) || m_impl->hasPar(Impl::PAR_dirtol);
}

bool NC::MatCfg::isLayeredCrystal() const
{
  return m_impl->hasPar(Impl::PAR_lcaxis);
}

void NC::MatCfg::checkConsistency() const
{
  Impl::SpyDisabler nospy(m_impl->m_spies);//disable any spies during invocation of this method

  const double parval_temp = get_temp();
  const double parval_dcutoff = get_dcutoff();
  const double parval_dcutoffup = get_dcutoffup();
  const double parval_packfact = get_packfact();
  const double parval_dirtol = get_dirtol();
  const double parval_sccutoff = get_sccutoff();
  if ( parval_temp!=-1.0 && (parval_temp<0.0||parval_temp>1e5) )
    NCRYSTAL_THROW(BadInput,"temp must be -1.0 or in the range (0.0,1e5]");
  if (parval_dcutoff!=-1) {
    if (parval_dcutoff<0.0)
      NCRYSTAL_THROW(BadInput,"dcutoff must be -1.0 or >=0.0");
    if (parval_dcutoff>=parval_dcutoffup)
      NCRYSTAL_THROW(BadInput,"dcutoff must be less than dcutoffup");
    if (!(parval_dcutoff>=1e-3&&parval_dcutoff<=1e5) && parval_dcutoff!=0 )
      NCRYSTAL_THROW(BadInput,"dcutoff must be -1 (hkl lists disabled), 0 (for automatic selection), or in range [1e-3,1e5]");
  }
  if (parval_packfact<=0.0||parval_packfact>1.0)
    NCRYSTAL_THROW(BadInput,"packfact must be in range (0.0,1.0]");
  if (parval_sccutoff<0.0)
    NCRYSTAL_THROW(BadInput,"sccutoff must be >=0.0");
  if (parval_dirtol<=0.0||parval_dirtol>kPi)
    NCRYSTAL_THROW(BadInput,"dirtol must be in range (0.0,pi]");
  const double parval_mosprec = get_mosprec();
  if ( ! (valueInInterval(0.9999e-7,0.10000001,parval_mosprec) ) )
    NCRYSTAL_THROW(BadInput,"mosprec must be in the range [1e-7,1e-1].");

  //inelas:
  std::string parval_inelas = get_inelas();
  if (parval_inelas.empty()||!contains_only(parval_inelas,"abcdefghijklmnopqrstuvwxyz_0123456789"))
    NCRYSTAL_THROW2(BadInput,"invalid inelas name specified: \""<<parval_inelas<<"\"");

  //infofactory:
  std::string parval_infofactory = get_infofactory();
  std::string parval_infofact_name = get_infofact_name();
  if (!contains_only(parval_infofact_name,"abcdefghijklmnopqrstuvwxyz_0123456789"))
    NCRYSTAL_THROW2(BadInput,"invalid infofactory name specified: \""<<parval_infofact_name<<"\"");
  if (parval_infofact_name.empty()&&contains(parval_infofactory,':'))
    NCRYSTAL_THROW2(BadInput,"infofactory options not allowed when not specifying specific factory");
  std::map<std::string,std::string> opts2val;
  Impl::decodeopts(parval_infofactory, opts2val);//decode to trigger any BadInput errors here

  //Now check the 4 SC parameters, only 1 of which has a code fallback value:
  int nOrient = (m_impl->hasPar(Impl::PAR_dir1)?1:0)
    + (m_impl->hasPar(Impl::PAR_dir2)?1:0)
    + (m_impl->hasPar(Impl::PAR_mos)?1:0);
  if (nOrient!=0 && nOrient<3)
    NCRYSTAL_THROW(BadInput,"Must set all or none of mos, dir1 and dir2 parameters");
  if (nOrient==0&&m_impl->hasPar(Impl::PAR_dirtol))
    NCRYSTAL_THROW(BadInput,"mos, dir1 and dir2 parameters must all be set when dirtol is set");

  if (nOrient) {
    //Check the validity of last SC parameters here!
    const double parval_mos = get_mos();

    if (parval_mos<=0.0||parval_mos>1.570796326794896558)// =pi/2
      NCRYSTAL_THROW(BadInput,"mos must be in range (0.0,pi/2]");
    //should be single crystal
    if (parval_packfact!=1.0)
      NCRYSTAL_THROW(BadInput,"Single crystal parameters are set, so packfact must be 1.0");

    //validate orientations:
    const Impl::ValOrientDir * dirs[2];
    dirs[0] = m_impl->getValTypeThrowIfNotAvail<Impl::ValOrientDir>(Impl::PAR_dir1);
    dirs[1] = m_impl->getValTypeThrowIfNotAvail<Impl::ValOrientDir>(Impl::PAR_dir2);

    for (int i = 0; i < 2; ++i) {
      if ( ! asVect(dirs[i]->crystal).mag2() )
        NCRYSTAL_THROW(BadInput, dirs[i]->crystal_is_hkl
                       ? "Specified point in hkl space is a null-vector"
                       : "Specified direction in crystal frame is a null-vector");
      if ( ! asVect(dirs[i]->lab).mag2() )
        NCRYSTAL_THROW(BadInput, "Specified direction in laboratory frame is a null-vector");
    }

    if ( asVect(dirs[0]->lab).isParallel( asVect(dirs[1]->lab), 1.0e-6 ) )
      NCRYSTAL_THROW(BadInput, "Specified primary and secondary lab directions are parallel");

    if ( dirs[0]->crystal_is_hkl == dirs[1]->crystal_is_hkl ) {
      //can only check crystal directions at this point if both are in the same frame:
      if ( asVect(dirs[0]->crystal).isParallel( asVect(dirs[1]->crystal), 1.0e-6 ) ) {
        NCRYSTAL_THROW(BadInput, dirs[0]->crystal_is_hkl
                       ? "Specified primary and secondary hkl points have planes with parallel normals"
                       : "Specified primary and secondary directions in the crystal frame are parallel" );
      }
    }
  } else {
    //should be polycrystal. No extra validation needed for now, packfact was already validated above.
  }

  if (m_impl->hasPar(Impl::PAR_lcaxis)) {
    nc_assert(isLayeredCrystal());
    double v[3];
    get_lcaxis(v);
    nc_assert_always(! ( ncisnan(v[0]) || ncisnan(v[1]) || ncisnan(v[2]) ) );//should have been caught
    double mag = sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if ( ncisinf(mag) || ncisinf(v[0]) || ncisinf(v[1]) || ncisinf(v[2]) )
      NCRYSTAL_THROW(BadInput, "Infinities or too large values specified in lcaxis vector");
    if (!mag)
      NCRYSTAL_THROW(BadInput, "Null vector or too small values specified in lcaxis vector");
  }

  const int parval_vdoslux = get_vdoslux();
  if ( parval_vdoslux < 0 || parval_vdoslux > 5 ) {
    NCRYSTAL_THROW2(BadInput, "Specified invalid vdoslux value of "
                    <<parval_vdoslux<<" (must be integer from 0 to 5)");
  }

}

void NC::MatCfg::getCacheSignature(std::string& out, const std::set<std::string>& pns) const
{
  std::stringstream s;
  std::set<std::string>::const_iterator itB(pns.begin()), itE(pns.end());
  for (std::set<std::string>::const_iterator it = itB;it!=itE;++it) {
    Impl::PARAMETERS paridx = (Impl::PARAMETERS)m_impl->strNameToParIdx(*it);
    if (it!=itB)
      s << ';';
    s << *it << '=' << (m_impl->hasPar(paridx)?m_impl->m_parlist[paridx]->to_strrep(true):"<>");
  }
  out = s.str();
}

void NC::MatCfg::set_lcaxis( const double (&axis)[3] )
{
  cow();
  Impl::ValVector * vv = m_impl->getValTypeForSet<Impl::ValVector>(Impl::PAR_lcaxis);
  vv->set(axis[0],axis[1],axis[2]);
}

void NC::MatCfg::get_lcaxis( double (&axis)[3] ) const
{
  const Impl::ValVector * vv = m_impl->getValTypeThrowIfNotAvail<Impl::ValVector>(Impl::PAR_lcaxis);
  axis[0] = vv->val[0];
  axis[1] = vv->val[1];
  axis[2] = vv->val[2];
}

void NC::MatCfg::set_dir1( bool cishkl,
                                 const double (&cdir)[3],
                                 const double (&ldir)[3] )
{
  cow();
  Impl::ValOrientDir * dir = m_impl->getValTypeForSet<Impl::ValOrientDir>(Impl::PAR_dir1);
  dir->set(cishkl,
           cdir[0],cdir[1],cdir[2],
           ldir[0],ldir[1],ldir[2]);
}

void NC::MatCfg::get_dir1( bool& cishkl,
                                 double (&cdir)[3],
                                 double (&ldir)[3] )
{
  const Impl::ValOrientDir * dir = m_impl->getValTypeThrowIfNotAvail<Impl::ValOrientDir>(Impl::PAR_dir1);
  cishkl = dir->crystal_is_hkl;
  for ( int i=0; i<3; ++i ) {
    cdir[i] = dir->crystal[i];
    ldir[i] = dir->lab[i];
  }
}

void NC::MatCfg::set_dir2( bool cishkl,
                                 const double (&cdir)[3],
                                 const double (&ldir)[3] )
{
  cow();
  Impl::ValOrientDir * dir = m_impl->getValTypeForSet<Impl::ValOrientDir>(Impl::PAR_dir2);
  dir->set(cishkl,
           cdir[0],cdir[1],cdir[2],
           ldir[0],ldir[1],ldir[2]);
}

void NC::MatCfg::get_dir2( bool& cishkl,
                                 double (&cdir)[3],
                                 double (&ldir)[3] )
{
  const Impl::ValOrientDir * dir = m_impl->getValTypeThrowIfNotAvail<Impl::ValOrientDir>(Impl::PAR_dir2);
  cishkl = dir->crystal_is_hkl;
  for ( int i=0; i<3; ++i ) {
    cdir[i] = dir->crystal[i];
    ldir[i] = dir->lab[i];
  }
}

void NC::MatCfg::setOrientation( const SCOrientation& sco )
{
  if (!sco.isComplete())
    NCRYSTAL_THROW(BadInput,"setOrientation called with incomplete SCOrientation object");
  cow();
  m_impl->setOrientation(sco);
  nc_assert(isSingleCrystal());
}

void NC::MatCfg::Impl::setOrientation( const SCOrientation& sco )
{
  ValOrientDir* p[2];
  p[0] = getValTypeForSet<ValOrientDir>(PAR_dir1);
  p[1] = getValTypeForSet<ValOrientDir>(PAR_dir2);
  nc_assert(p[0]&&p[1]);
  for ( int i = 0; i < 2; ++i ) {
    p[i]->set(sco.getCrysIsHKL(i),
              sco.getCrysDir(i)[0],sco.getCrysDir(i)[1],sco.getCrysDir(i)[2],
              sco.getLabDir(i)[0],sco.getLabDir(i)[1],sco.getLabDir(i)[2]);
  }
  setVal<ValDbl>(PAR_dirtol,sco.getTolerance());
}

NC::SCOrientation NC::MatCfg::createSCOrientation() const
{
  checkConsistency();
  if (!isSingleCrystal())
    NCRYSTAL_THROW(MissingInfo,"Can not supply SCOrientation object for poly crystals");
  if ( ! m_impl->hasPar(Impl::PAR_dir1) )
    NCRYSTAL_THROW(MissingInfo,"Can not supply SCOrientation object without the dir1 parameter set");
  if ( ! m_impl->hasPar(Impl::PAR_dir2) )
    NCRYSTAL_THROW(MissingInfo,"Can not supply SCOrientation object without the dir2 parameter set");
  double tolerance = get_dirtol();

  SCOrientation out;
  const Impl::ValOrientDir * dir1 = m_impl->getValType<Impl::ValOrientDir>(Impl::PAR_dir1);
  const Impl::ValOrientDir * dir2 = m_impl->getValType<Impl::ValOrientDir>(Impl::PAR_dir2);
  nc_assert(dir1&&dir2);

  if (dir1->crystal_is_hkl)
    out.setPrimaryDirection( dir1->crystal[0],dir1->crystal[1],dir1->crystal[2],dir1->lab);
  else
    out.setPrimaryDirection( dir1->crystal,dir1->lab);
  if (dir2->crystal_is_hkl)
    out.setSecondaryDirection( dir2->crystal[0],dir2->crystal[1],dir2->crystal[2],dir2->lab,tolerance);
  else
    out.setSecondaryDirection( dir2->crystal,dir2->lab,tolerance);
  return out;
}

void NC::MatCfg::applyStrCfg( const std::string& str )
{
  if (!isSimpleASCII(str,true,true))
    NCRYSTAL_THROW(BadInput,"Non-ASCII characters in parameter specification!");

  if (contains_any(str,NCMATCFG_FORBIDDEN_CHARS))
    NCRYSTAL_THROW(BadInput,"Forbidden characters in parameter specification!");

  VectS parts;
  VectS par_and_val;
  split(parts,str,0,';');
  for (size_t i = 0; i<parts.size();++i) {
    trim(parts.at(i));
    if (parts.at(i).empty()) {
      //be flexible and simply ignore missing parts (so for instance
      //MatCfg("myfile.ncmat;") will still work).
      continue;
    }
    if (parts.at(i)=="ignorefilecfg") {
      NCRYSTAL_THROW2(BadInput,"The \"ignorefilecfg\" keyword can only be used in the MatCfg "
                      "constructor (and only directly after the filename)");
    }
    split(par_and_val,parts.at(i),0,'=');
    if (par_and_val.size()!=2) {
      NCRYSTAL_THROW2(BadInput,"Bad syntax in parameter specification: \""<<parts.at(i)<<"\"");
    }
    trim(par_and_val.at(0));
    trim(par_and_val.at(1));
    if (par_and_val.at(0).empty())
      NCRYSTAL_THROW(BadInput,"Missing parameter name");
    cow();
    m_impl->setValByStr(par_and_val.at(0),par_and_val.at(1));
  }
}

#ifdef NCRYSTAL_STDCMAKECFG_EMBED_DATA_ON
namespace NCrystal {
  namespace internal {
    void ensureInMemDBReadyMTSafe();//fwd declare NCFactory.cc function
  }
}
#endif

NC::MatCfg::MatCfg( const std::string& datafile_and_parameters )
  : m_impl(0)
{
  RCHolder<Impl> guard(new Impl());//refs now and releases in destructor, ensuring memory
                                   //cleanup in case of bad input leading to exceptions.
  m_impl = guard.obj();//set now, but only ref at end of constructor

  //Trim and split on ';', throwing away empty parts:
  std::string input(datafile_and_parameters);
  trim( input );
  VectS parts;
  split(parts,input,1,';');
  for (std::size_t i = 0; i<parts.size(); ++i)
    trim(parts.at(i));
  //First and only required parameter is the datafile:
  if ( parts.empty() || parts.at(0).empty() )
    NCRYSTAL_THROW(MissingInfo,"Please supply name of data file");
  if (contains(parts.at(0),'='))
    NCRYSTAL_THROW2(BadInput,"Filename contains a forbidden character ('='): "<<parts.at(0));//catch typical user error

#ifdef NCRYSTAL_STDCMAKECFG_EMBED_DATA_ON
  //make sure embedded data files are ready by calling this NCFactory.cc function:
  internal::ensureInMemDBReadyMTSafe();
#endif

  //Don't just open files, use input streams -- we support e.g. in-memory files.
  auto inputstream = createTextInputStream( parts.at(0) );
  if (!inputstream)
    NCRYSTAL_THROW2(FileNotFound,"Could not find specified datafile: "<<parts.at(0));
  m_impl->m_datafile_resolved = inputstream->onDiskResolvedPath();//empty in case of e.g. in-mem files.
  if (parts.at(0)!=m_impl->m_datafile_resolved)
    m_impl->m_datafile_orig = parts.at(0);
  m_impl->m_datafileext = getfileext(parts.at(0));

  nc_assert_always(parts.size()<=2);
  m_impl->m_ignoredfilecfg = false;
  std::string extracfgstr;
  if (parts.size()==2) {
    //First check if there is actually an "ignorefilecfg" part (can contain spaces)
    VectS parts2;
    split(parts2,parts.at(1),1,';');
    for (std::size_t i = 0; i<parts2.size(); ++i)
      trim(parts2.at(i));
    for (std::size_t i = 0; i<parts2.size(); ++i)
    if (!parts2.empty() && parts2.at(0)=="ignorefilecfg") {
      m_impl->m_ignoredfilecfg = true;
      if (parts2.size()==2)
        extracfgstr = parts2.at(1);
    } else {
      extracfgstr = parts.at(1);
    }
  }
  if (!m_impl->m_ignoredfilecfg) {
    std::string filecfgstr;
    nc_assert(inputstream!=nullptr);
    m_impl->extractFileCfgStr(*inputstream,filecfgstr);
    if (!filecfgstr.empty())
      applyStrCfg( filecfgstr );
  }
  if (!extracfgstr.empty())
    applyStrCfg( extracfgstr );

  if (getDataFileExtension().empty())
    NCRYSTAL_THROW2(BadInput,"Unsupported data file (can not determine extension): "<<getDataFileAsSpecified());

  //Done - no more exceptions can be thrown, time to actually increase the
  //refcount of m_impl (just before it is released by the guard):
  m_impl->ref();
}

void NC::MatCfg::Impl::extractFileCfgStr( TextInputStream& input,
                                          std::string&res )
{


  res.clear();
  std::string line;
  std::string pattern="NCRYSTALMATCFG";
  while (input.getLine(line)) {
    std::size_t pos = line.find(pattern);
    if ( pos == std::string::npos )
      continue;
    if (!contains(line,pattern))
      continue;
    if (!res.empty())
      NCRYSTAL_THROW2(BadInput,"Input file contains more than one "<<pattern<<" specification: "<<m_datafile_resolved);
    line = line.substr(pos+pattern.size());
    if (line.empty()||line.at(0)!='[')
      NCRYSTAL_THROW2(BadInput,"Input file contains "<<pattern<<" which is not followed by a '[' character: "<<m_datafile_resolved);
    if (line.find(pattern)!=std::string::npos)
      NCRYSTAL_THROW2(BadInput,"Input file contains more than one "<<pattern<<" specification on a single line: "<<m_datafile_resolved);
    line = line.substr(1);
    pos = line.find(']');
    if ( pos == std::string::npos )
      NCRYSTAL_THROW2(BadInput,"Input file contains "<<pattern<<" without a closing ']' character: "<<m_datafile_resolved);
    res = line.substr(0,pos);
    if (res.empty())
      res = " ";//for detection of multiple occurances
  }
  trim(res);
}

void NC::MatCfg::cow()
{
  if (m_impl->refCount()==1)
    return;
  Impl * newimpl = new Impl(*m_impl);
  newimpl->ref();//ref new
  std::swap(newimpl,m_impl);
  newimpl->unref();//unref old
  nc_assert(m_impl->refCount()==1);
}

NC::MatCfg::~MatCfg()
{
  if (m_impl)
    m_impl->unref();
}


NC::MatCfg::MatCfg(const MatCfg& o)
  : m_impl(0)
{
  *this = o;
}

NC::MatCfg& NC::MatCfg::operator=(const MatCfg& o)
{
  o.m_impl->ref();
  if (m_impl)
    m_impl->unref();
  m_impl = o.m_impl;
  return *this;
}

NC::MatCfg& NC::MatCfg::operator=(MatCfg&& o)
{
  if (m_impl)
    m_impl->unref();
  m_impl = 0;
  std::swap(m_impl,o.m_impl);
  return *this;
}
NC::MatCfg::MatCfg(MatCfg&& o)
  : m_impl(0)
{
  std::swap(m_impl,o.m_impl);
}

void NC::MatCfg::dump( std::ostream& out, bool add_endl ) const
{
  std::string strcfg = toStrCfg( false );
  out << "MatCfg(\""<<basename(m_impl->m_datafile_resolved);
  if (m_impl->m_ignoredfilecfg)
    out << ";ignorefilecfg";
  if (!strcfg.empty())
    out << (strcfg[0]==';'?"":";") << strcfg;
  out<<"\")";
  if (add_endl)
    out<<std::endl;
}

const std::string& NC::MatCfg::getDataFileAsSpecified() const
{
  return m_impl->m_datafile_orig.empty() ? m_impl->m_datafile_resolved : m_impl->m_datafile_orig;
}

const std::string& NC::MatCfg::getDataFile() const
{
  return m_impl->m_datafile_resolved;
}

const std::string& NC::MatCfg::getDataFileExtension() const
{
  const std::string& s=get_overridefileext();
  return s.empty() ? m_impl->m_datafileext : s;
}

double NC::MatCfg::get_temp() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_temp,-1.0); }
double NC::MatCfg::get_dcutoff() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_dcutoff,0.0); }
double NC::MatCfg::get_dcutoffup() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_dcutoffup,kInfinity); }
double NC::MatCfg::get_packfact() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_packfact,1.0); }
double NC::MatCfg::get_mos() const { return m_impl->getValNoFallback<Impl::ValDbl>(Impl::PAR_mos); }
double NC::MatCfg::get_mosprec() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_mosprec,1e-3); }
double NC::MatCfg::get_sccutoff() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_sccutoff,0.4); }
double NC::MatCfg::get_dirtol() const { return m_impl->getVal<Impl::ValDbl>(Impl::PAR_dirtol,1e-4); }
bool NC::MatCfg::get_coh_elas() const { return m_impl->getVal<Impl::ValBool>(Impl::PAR_coh_elas,true); }
bool NC::MatCfg::get_incoh_elas() const { return m_impl->getVal<Impl::ValBool>(Impl::PAR_incoh_elas,true); }
const std::string& NC::MatCfg::get_inelas() const {
  const std::string& ss = m_impl->getVal<Impl::ValStr>(Impl::PAR_inelas,s_matcfg_str_auto);
  if (isOneOf(ss,"none","0","sterile","false"))
    return s_matcfg_str_none;
  return ss;
 }
const std::string& NC::MatCfg::get_overridefileext() const { return m_impl->getVal<Impl::ValStr>(Impl::PAR_overridefileext,s_matcfg_str_empty); }
const std::string& NC::MatCfg::get_infofactory() const { return m_impl->getVal<Impl::ValStr>(Impl::PAR_infofactory,s_matcfg_str_empty); }
const std::string& NC::MatCfg::get_scatfactory() const { return m_impl->getVal<Impl::ValStr>(Impl::PAR_scatfactory,s_matcfg_str_empty); }
const std::string& NC::MatCfg::get_absnfactory() const { return m_impl->getVal<Impl::ValStr>(Impl::PAR_absnfactory,s_matcfg_str_empty); }
void NC::MatCfg::set_temp( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_temp,v); }
void NC::MatCfg::set_dcutoff( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_dcutoff,v); }
void NC::MatCfg::set_dcutoffup( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_dcutoffup,v); }
void NC::MatCfg::set_packfact( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_packfact,v); }
void NC::MatCfg::set_mos( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_mos,v); }
void NC::MatCfg::set_mosprec( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_mosprec,v); }
void NC::MatCfg::set_sccutoff( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_sccutoff,v); }
void NC::MatCfg::set_dirtol( double v ) { cow(); m_impl->setVal<Impl::ValDbl>(Impl::PAR_dirtol,v); }
void NC::MatCfg::set_coh_elas( bool v ) { cow(); m_impl->setVal<Impl::ValBool>(Impl::PAR_coh_elas,v); }
void NC::MatCfg::set_incoh_elas( bool v ) { cow(); m_impl->setVal<Impl::ValBool>(Impl::PAR_incoh_elas,v); }
void NC::MatCfg::set_inelas( const std::string& v ) { cow(); m_impl->setVal<Impl::ValStr>(Impl::PAR_inelas,v); }
void NC::MatCfg::set_overridefileext( const std::string& v ) { cow(); m_impl->setVal<Impl::ValStr>(Impl::PAR_overridefileext,v); }
void NC::MatCfg::set_infofactory( const std::string& v ) { cow(); m_impl->setVal<Impl::ValStr>(Impl::PAR_infofactory,v); }
void NC::MatCfg::set_scatfactory( const std::string& v ) { cow(); m_impl->setVal<Impl::ValStr>(Impl::PAR_scatfactory,v); }
void NC::MatCfg::set_absnfactory( const std::string& v ) { cow(); m_impl->setVal<Impl::ValStr>(Impl::PAR_absnfactory,v); }
void NC::MatCfg::set_lcmode( int v ) { cow(); m_impl->setVal<Impl::ValInt>(Impl::PAR_lcmode,v); }
int NC::MatCfg::get_lcmode() const { return m_impl->getVal<Impl::ValInt>(Impl::PAR_lcmode,0); }
void NC::MatCfg::set_vdoslux( int v ) { cow(); m_impl->setVal<Impl::ValInt>(Impl::PAR_vdoslux,v); }
int NC::MatCfg::get_vdoslux() const { return m_impl->getVal<Impl::ValInt>(Impl::PAR_vdoslux,3); }

const std::string& NC::MatCfg::get_atomdb() const {
  const Impl::ValAtomDB * vt = m_impl->getValType<Impl::ValAtomDB>(Impl::PAR_atomdb);
  return vt ? vt->value_as_string : s_matcfg_str_empty;
}

const std::vector<NC::VectS>& NC::MatCfg::get_atomdb_parsed() const
{
  const Impl::ValAtomDB * vt = m_impl->getValType<Impl::ValAtomDB>(Impl::PAR_atomdb);
  static decltype(vt->value) s_empty_atomdb;
  return vt ? vt->value : s_empty_atomdb;
}

void NC::MatCfg::set_atomdb( const std::string& v ) {
  cow();
  m_impl->getValTypeForSet<Impl::ValAtomDB>(Impl::PAR_atomdb)->set_from_strrep(v);
}

bool NC::MatCfg::isPolyCrystal() const { return !isSingleCrystal(); }

bool NC::MatCfg::hasAccessSpy(AccessSpy* spy) const
{
  return std::find(m_impl->m_spies.begin(), m_impl->m_spies.end(),spy) != m_impl->m_spies.end();
}

void NC::MatCfg::addAccessSpy(AccessSpy* spy) const
{
  if (!spy)
    NCRYSTAL_THROW(BadInput,"NULL access spy provided");
  if (hasAccessSpy(spy))
    NCRYSTAL_THROW(BadInput,"Attempt to install the same access spy more than once");
  m_impl->m_spies.push_back(spy);
}

void NC::MatCfg::removeAccessSpy(AccessSpy* spy) const
{
  size_t n = m_impl->m_spies.size();
  m_impl->m_spies.erase(std::remove(m_impl->m_spies.begin(), m_impl->m_spies.end(), spy), m_impl->m_spies.end());
  std::vector<AccessSpy*>(m_impl->m_spies).swap(m_impl->m_spies);//shrink to fit
  if (n==m_impl->m_spies.size())
    NCRYSTAL_THROW(BadInput,"Could not remove access spy which was never installed");

}

std::string NC::MatCfg::get_infofact_name() const
{
  return Impl::decodeopt_name( get_infofactory() );
}

bool NC::MatCfg::get_infofactopt_flag(const std::string& flagname) const
{
  return Impl::decodeopt_flag(get_infofactory(),flagname);
}

double NC::MatCfg::get_infofactopt_dbl(const std::string& flagname, double defval) const
{
  return Impl::decodeopt_dbl(get_infofactory(),flagname,defval);
}

int NC::MatCfg::get_infofactopt_int(const std::string& flagname, int defval) const
{
  return Impl::decodeopt_int(get_infofactory(),flagname,defval);
}

void NC::MatCfg::infofactopt_validate(const std::set<std::string>& recognised_opts) const
{
  Impl::decodedopt_validate(get_infofactory(),recognised_opts);
}
