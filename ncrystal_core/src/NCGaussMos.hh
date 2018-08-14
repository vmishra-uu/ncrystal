#ifndef NCrystal_GaussMos_hh
#define NCrystal_GaussMos_hh

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of NCrystal (see https://mctools.github.io/ncrystal/)   //
//                                                                            //
//  Copyright 2015-2017 NCrystal developers                                   //
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

#include "NCGaussOnSphere.hh"
#include "NCVector.hh"
#include <vector>

namespace NCrystal {

  class RandomBase;

  class GaussMos {
  public:

    //Helper class which implements a mosaic model, assuming a (truncated)
    //Gaussian distribution of crystallite orientations. The provided mosaicity
    //should be in radians, and will be interpreted to be either the sigma or
    //FWHM of the (untruncated) Gaussian mosaicity distribution, depending on
    //the value of the mosaicity_is_fhwm parameter. The precision ("prec")
    //parameter is passed on to the internal GaussOnSphere object (see
    //NCGaussOnSphere.hh for details). If ntrunc is 0,
    //GaussOnSphere::estimateNTruncFromPrec will be used to estimate the
    //truncation range from prec, otherwise the provided value will be used
    //directly.
    GaussMos( double mosaicity, bool mosaicity_is_fhwm = true, double prec = 1e-3, double ntrunc=0 );
    ~GaussMos(){}

    //Mosaicity can also be changed after construction:
    void setMosaicity( double mosaicity, bool mosaicity_is_fhwm = true );

    //Modify certain aspects:
    void setTruncationN(double);
    void setPrecision(double);

    void setDSpacingSpread(double);//Enable dspacing deviation in non-ideal crystal [default value of 0.0 means no deviation]

    //Access parameters of Gaussian mosaicity distribution:
    double mosaicityFWHM() const;
    double mosaicityGaussSigma() const;
    double mosaicityGaussNormFact() const;//gauss(x)=normfact*exp(-k*x*x)
    double mosaicityTruncationN() const;
    double mosaicityTruncationAngle() const;
    double mosaicityCosTruncationAngle() const;
    double mosaicitySinTruncationAngle() const;
    double precision() const;
    const GaussOnSphere& gos() const { return m_gos; }

    //Before calculating cross-sections, the relevant interaction parameters for
    //the neutron and plane family must be set in an InteractionPars object
    //(reusing it might save calculations under certain conditions). The xsfact
    //parameter should typically be set to fsquared/(v0*natoms), and it will
    //simply scale the resulting cross-sections. It must hold that
    //neutron_wavelength<=2*dspacing (otherwise cross-sections are trivially
    //zero and GaussMos should not be invoked in any case).
    class InteractionPars;

    //Calculate cross-section, given (the cosine of) an angle between the
    //neutron direction and a given normal. This function does NOT apply any
    //initial truncation cutoff!!  It is assumed that this function will only be
    //called after the caller has verified that the truncation cutoff is
    //satisfied (but note that the truncationN value still plays a role for the
    //normalisation of the mosaicity distribution used):
    double calcRawCrossSectionValue( InteractionPars& ip,
                                     double cos_angle_indir_normal ) const;

    //Cross-sections for a large number of demi-normals can be found in one go
    //(if they otherwise share parameters like d-spacing & fsquared, which must
    //have been first set with a call to setInteractionParameters). This method
    //DOES implement the Gauss truncation internally (and exactly). Return value
    //is the total cross-section for scattering on any of the passed
    //demi-normals. Intermediate results for individual deminormals with
    //non-zero contributions will be appended to the passed-in cache vector,
    //while the commulative values of the corresponding individual
    //cross-sections will be appended to the passed in xs_commul vector. This
    //setup allows the caller to subsequently select a plane to scatter on with
    //a binary search in the xs_commul vector, and subsequently use the
    //ScatCache object at the same index in the cache vector, to generate
    //scatterings:
    class ScatCache;
    double calcCrossSections( InteractionPars& ip,
                              const Vector& neutron_indir,
                              const std::vector<Vector>& deminormals,
                              std::vector<ScatCache>& cache,
                              std::vector<double>& xs_commul) const;

    //Scatterings can only be generated once appropriate info has been found via
    //previous calls to cross-section methods, and with relevant info embedded
    //into ScatCache objects (of course, they will only be relevant for the
    //neutron state for which they were created):
    class ScatCache {
    public:
      ScatCache();//default constructs invalid cache object
      ScatCache(const Vector& pn, double i2d);
      void set(const Vector& pn, double i2d);
      void clear();//invalidate
      ~ScatCache(){}
      bool isValid() const;
      const Vector& plane_normal() const;
      double plane_inv2d() const;
    private:
      Vector m_plane_normal;//signed normal
      double m_plane_inv2d;
    };

    //Generate scatterings. Needs a valid ScatCache object, a source of random
    //numbers, and the neutron state parameters (must be consistent with the
    //ones used when the ScatCache object was set up):
    void genScat( RandomBase* rand, const ScatCache&,
                  double neutron_wavelength, const Vector& neutron_indir,
                  Vector& neutron_outdir ) const;

  private:
    GaussOnSphere m_gos;
    double m_delta_d;
    double m_mos_fwhm;
    double m_mos_truncN;
    double m_mos_sigma;
    double m_prec;
    void updateDerivedValues();
    double calcRawCrossSectionValueInit( InteractionPars&, double ) const;
  };

  class GaussMos::InteractionPars {
  public:
    InteractionPars(double neutron_wavelength, double inv2dsp, double xsfact);
    InteractionPars();
    bool isValid() const;
    void set(double neutron_wavelength, double inv2dsp, double xsfact);
  private:
    friend class GaussMos;
    void calcQSinAlpha();
    double m_wl;
    double m_wl3;
    double m_inv2dsp;
    double m_sin_perfect_theta;
    double m_cos_perfect_theta;
    double m_cos_perfect_theta_sq;
    double m_xsfact;
    //calculated on-demand only by calling calcQAlpha(). Until this happens, m_Q==-1.:
    double m_Q;
    double m_alpha;
  };

}


////////////////////////////
// Inline implementations //
////////////////////////////

namespace NCrystal {
  inline double GaussMos::mosaicityFWHM() const { return m_mos_fwhm; }
  inline double GaussMos::mosaicityGaussSigma() const { return m_gos.getSigma(); }
  inline double GaussMos::mosaicityGaussNormFact() const { return m_gos.getNormFactor(); }
  inline double GaussMos::mosaicityTruncationN() const { return m_mos_truncN; }
  inline double GaussMos::mosaicityTruncationAngle() const { return m_gos.getTruncangle(); }
  inline double GaussMos::mosaicityCosTruncationAngle() const { return m_gos.getCosTruncangle(); }
  inline double GaussMos::mosaicitySinTruncationAngle() const { return m_gos.getSinTruncangle(); }
  inline double GaussMos::precision() const { return m_gos.getPrecisionParameter(); }

  inline GaussMos::InteractionPars::InteractionPars(double wl, double inv2dsp, double xsfact)
    : m_wl(-1), m_inv2dsp(-1)
  {
    set(wl, inv2dsp, xsfact);
  }

  inline GaussMos::InteractionPars::InteractionPars() : m_wl(-1), m_inv2dsp(-1)
  {
  }

  inline bool GaussMos::InteractionPars::isValid() const
  {
    return m_wl>0;
  }

  inline GaussMos::ScatCache::ScatCache(): m_plane_inv2d(0) {}
  inline GaussMos::ScatCache::ScatCache(const Vector& pn, double i2d) : m_plane_normal(pn), m_plane_inv2d(i2d) { nc_assert(i2d>0&&pn.isUnitVector()); }
  inline void GaussMos::ScatCache::set(const Vector& pn, double i2d) { nc_assert(i2d>0&&pn.isUnitVector()); m_plane_normal = pn; m_plane_inv2d = i2d; }
  inline void GaussMos::ScatCache::clear() { m_plane_inv2d=0; }
  inline bool GaussMos::ScatCache::isValid() const { return m_plane_inv2d>0; }
  inline const Vector& GaussMos::ScatCache::plane_normal() const { nc_assert(isValid()); return m_plane_normal; }
  inline double GaussMos::ScatCache::plane_inv2d() const { nc_assert(isValid()); return m_plane_inv2d; }

  inline double GaussMos::calcRawCrossSectionValue( InteractionPars& ip, double cos_angle_indir_normal ) const
  {
    nc_assert(ip.isValid());
    nc_assert(ncabs(cos_angle_indir_normal)<=1.+1e-10);

    if (ip.m_Q<=0.)
      return calcRawCrossSectionValueInit(ip,cos_angle_indir_normal);

    //ip.m_sin_perfect_theta = cosalpha, ip.m_cos_perfect_theta = sinalpha
    double sin_angle_indir_normal = std::sqrt(1.0-cos_angle_indir_normal*cos_angle_indir_normal);//>0 since angle is in 0..pi.
    return ip.m_Q * m_gos.circleIntegral( cos_angle_indir_normal, sin_angle_indir_normal, ip.m_sin_perfect_theta, ip.m_cos_perfect_theta );
  }
}

#endif
