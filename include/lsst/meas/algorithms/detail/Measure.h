// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
 
#if !defined(LSST_MEAS_ALGORITHMS_DETAIL_MEASURE_H)
#define LSST_MEAS_ALGORITHMS_DETAIL_MEASURE_H

#include <list>
#include <cmath>
#include "lsst/base.h"
#include "lsst/afw/detection.h"
#include "lsst/afw/detection/Measurement.h"
#include "lsst/afw/detection/Astrometry.h"
#include "lsst/afw/detection/Photometry.h"
#include "lsst/afw/detection/Shape.h"
#include "lsst/afw/geom/Point.h"
#include "lsst/meas/algorithms/MeasureQuantity.h"
#include "lsst/meas/algorithms/Flags.h"

namespace lsst {
namespace pex {
    namespace policy {
        class Policy;
    }
}

namespace meas {
namespace algorithms {

namespace pexPolicy = lsst::pex::policy;
namespace pexLog = lsst::pex::logging;
namespace afwDet = lsst::afw::detection;
namespace afwGeom = lsst::afw::geom;

namespace {

    /// Return the numeric value of a policy entry as double
    double getNumeric(lsst::pex::policy::Policy const& policy, std::string const& name)
    {
        return policy.isDouble(name) ? policy.getDouble(name) : policy.getInt(name);
    }

    /// Extractors to call the right extraction method 
    template<class Derived>
    class Extractor {
    public:
        static std::string name() { return Derived::derived_name(); }
        template<typename MeasurementT>
        static void extract(afwDet::Source& source, MeasurementT const& meas) {
            return Derived::derived_extract(source, meas);
        }
    };
    class ApPhotExtractor : public Extractor<ApPhotExtractor> {
    public:
        static std::string derived_name() { return "source.apFlux"; }
        static void derived_extract(afwDet::Source& source, afwDet::Photometry const& phot) {
            source.extractApPhotometry(phot);
        }
    };
    class PsfPhotExtractor : public Extractor<PsfPhotExtractor> {
    public:
        static std::string derived_name() { return "source.psfFlux"; }
        static void derived_extract(afwDet::Source& source, afwDet::Photometry const& phot) {
            source.extractPsfPhotometry(phot);
        }
    };
    class ModelPhotExtractor : public Extractor<ModelPhotExtractor> {
    public:
        static std::string derived_name() { return "source.modelFlux"; }
        static void derived_extract(afwDet::Source& source, afwDet::Photometry const& phot) {
            source.extractModelPhotometry(phot);
        }
    };
    class InstPhotExtractor : public Extractor<InstPhotExtractor> {
    public:
        static std::string derived_name() { return "source.instFlux"; }
        static void derived_extract(afwDet::Source& source, afwDet::Photometry const& phot) {
            source.extractInstPhotometry(phot);
        }
    };
    class AstrometryExtractor : public Extractor<AstrometryExtractor> {
    public:
        static std::string derived_name() { return "source.astrom"; }
        static void derived_extract(afwDet::Source& source, afwDet::Astrometry const& astrom) {
            source.extractAstrometry(astrom);
        }
    };
    class ShapeExtractor : public Extractor<ShapeExtractor> {
    public:
        static std::string derived_name() { return "source.shape"; }
        static void derived_extract(afwDet::Source& source, afwDet::Shape const& shape) {
            source.extractShape(shape);
        }
    };

    /// Templated function to extract the correct measurement
    template<typename MeasurementT, typename ExtractorT>
    void extractMeasurements(afwDet::Source& source,
                             MeasurementT const& measurements,
                             pexPolicy::Policy const& policy,
                             ExtractorT const& extractor) {
        std::string const name = extractor.name();
        if (policy.isString(name)) {
            std::string const& alg = policy.getString(name);
            if (alg != "NONE") {
                typename afwDet::Measurement<MeasurementT>::TPtr meas = measurements.find(alg);
                if (!meas) {
                    throw LSST_EXCEPT(lsst::pex::exceptions::RuntimeErrorException,
                                      (boost::format("Can't find measurement from algorithm %s") % alg).str());
                }
                extractor.extract(source, *meas);
            }
        }
    }


    /**
     * @brief Calculate a detected source's moments
     */
    template <typename ExposureT>
    class FootprintCentroid : public afwDet::FootprintFunctor<typename ExposureT::MaskedImageT> {
    public:
        typedef typename ExposureT::MaskedImageT MaskedImageT;
        explicit FootprintCentroid(typename ExposureT::MaskedImageT const& mimage
            ) : afwDet::FootprintFunctor<MaskedImageT>(mimage),
                _n(0), _sum(0), _sumx(0), _sumy(0),
                _min( std::numeric_limits<double>::max()), _xmin(0), _ymin(0),
                _max(-std::numeric_limits<double>::max()), _xmax(0), _ymax(0),
                _bits(0) {}
        
        /// \brief Reset everything for a new Footprint
        void reset() {
            _n = 0;
            _sum = _sumx = _sumy = 0.0;
            _min =  std::numeric_limits<double>::max();
            _xmin = _ymin = 0;
            _max = -std::numeric_limits<double>::max();
            _xmax = _ymax = 0;
            _bits = 0x0;
        }
        virtual void reset(afwDet::Footprint const&) {}

        /// \brief method called for each pixel by apply()
        void operator()(typename MaskedImageT::xy_locator loc, ///< locator pointing at the pixel
                        int x,                                 ///< column-position of pixel
                        int y                                  ///< row-position of pixel
            ) {
            typename MaskedImageT::Image::Pixel val = loc.image(0, 0);

            _n++;
            _sum += val;
            _sumx += afwImage::indexToPosition(x)*val;
            _sumy += afwImage::indexToPosition(y)*val;
            _bits |= loc.mask(0, 0);

            if (val < _min) {
                _min = val;
                _xmin = x;
                _ymin = y;
            }
            if (val > _max) {
                _max = val;
                _xmax = x;
                _ymax = y;
            }
        }

        /// Return the number of pixels
        int getN() const { return _n; }
        /// Return the Footprint's flux
        double getSum() const { return _sum; }
        /// Return the Footprint's column centroid
        double getX() const { return _sumx/_sum; }
        /// Return the Footprint's row centroid
        double getY() const { return _sumy/_sum; }
        /// Return the Footprint's peak pixel
        PTR(afwDet::Peak) makePeak(bool isNegative) const {
            return boost::make_shared<afwDet::Peak>(isNegative ? afwDet::Peak(_xmin, _ymin) :
                                                          afwDet::Peak(_xmax, _ymax));
        }
        /// Return the union of the bits set anywhere in the Footprint
        typename MaskedImageT::Mask::Pixel getBits() const { return _bits; }
    private:
        int _n;
        double _sum, _sumx, _sumy;
        double _min;
        int _xmin, _ymin;
        double _max;
        int _xmax, _ymax;
        typename MaskedImageT::Mask::Pixel _bits;
    };


/// How to make a peak
    template<typename ExposureT>
    struct SinglePeakMaker {
        static CONST_PTR(afwDet::Peak) makePeak(ExposureT const& exp,
                                                afwDet::Source const& source,
                                                FootprintCentroid<ExposureT> const& centroid) {
            return centroid.makePeak(source.getFlagForDetection() & Flags::DETECT_NEGATIVE);
        }
    };
    template<typename ExposureT>
    struct GroupPeakMaker {
        static CONST_PTR(afwDet::Peak) makePeak(ExposureT const& exp,
                                                afwDet::Source const& source,
                                                FootprintCentroid<ExposureT> const& centroid) {
            afwGeom::Point2D pix = exp.getWcs()->skyToPixel(source.getRaDec());
            return boost::make_shared<afwDet::Peak>(static_cast<float>(pix.getX()),
                                                    static_cast<float>(pix.getY()));
        }
    };

    template<typename ExposureT, class PeakMaker>
    void checkPixels(ExposurePatch<ExposureT>& patch,
                     afwDet::Source const& source) {
        CONST_PTR(ExposureT) exp = patch.getExposure();

        FootprintCentroid<ExposureT> centroidFunctor(exp->getMaskedImage());
        centroidFunctor.apply(*patch.getFootprint());

        CONST_PTR(afwDet::Peak) peak = PeakMaker::makePeak(*exp, source, centroidFunctor);
        patch.setPeak(peak);

        // Check for bits set in the Footprint
        if (centroidFunctor.getBits() & ExposureT::MaskedImageT::Mask::getPlaneBitMask("EDGE")) {
            patch.orFlag(ExposurePatch<ExposureT>::EDGE);
        }
        if (centroidFunctor.getBits() & ExposureT::MaskedImageT::Mask::getPlaneBitMask("INTRP")) {
            patch.orFlag(ExposurePatch<ExposureT>::INTERP);
        }
        if (centroidFunctor.getBits() & ExposureT::MaskedImageT::Mask::getPlaneBitMask("SAT")) {
            patch.orFlag(ExposurePatch<ExposureT>::SAT);
        }

        // Check for bits set near the centroid
        afwGeom::Point2I llc(afwImage::positionToIndex(peak->getFx()) - 1,
                             afwImage::positionToIndex(peak->getFy()) - 1);
        afwDet::Footprint const middle(afwGeom::BoxI(llc, afwGeom::ExtentI(3))); // central 3x3
        centroidFunctor.apply(middle);
        if (centroidFunctor.getBits() & ExposureT::MaskedImageT::Mask::getPlaneBitMask("INTRP")) {
            patch.orFlag(ExposurePatch<ExposureT>::INTERP_CENTER);
        }
        if (centroidFunctor.getBits() & ExposureT::MaskedImageT::Mask::getPlaneBitMask("SAT")) {
            patch.orFlag(ExposurePatch<ExposureT>::SAT_CENTER);
        }
    }


/// Measurers for use with _measure
///
/// These make up for the lack of support for passing method names: we simply
/// template on the Measurer class, and call a class (static) method of a
/// standard name.  The optimised compiled code should end up calling the
/// appropriate method name directly.
template<typename ExposureT>
struct SingleMeasurer {
    typedef ExposureT ExposureContainerT;
    typedef afwDet::Source SourceContainerT;
    static void footprints(ExposurePatch<ExposureT>& exp,
                           afwDet::Source const& source,
                           afwImage::Wcs const& sourceWcs) {
        exp.setFootprint(source.getFootprint());
    }
    static void check(ExposurePatch<ExposureT>& exp, afwDet::Source &source) {
        return checkPixels<ExposureT, SinglePeakMaker<ExposureT> >(exp, source);
    }
    template<typename MeasurementT>
    static PTR(MeasurementT) measure(MeasureQuantity<MeasurementT, ExposureT> const& measureQuantity,
                                     ExposurePatch<ExposureT> const& exp,
                                     afwDet::Source const& source) {
        return measureQuantity.measureOne(exp, source);
    }
    template<typename MeasurementT, typename ExtractorT>
    static void extract(afwDet::Source& target, MeasurementT const& meas, pexPolicy::Policy const& policy,
                        ExtractorT const& extractor) {
        extractMeasurements(target, meas, policy, extractor);
    }
    static void nullAstrom(afwDet::Source& target, afwDet::Source const& source,
                           ExposurePatch<ExposureT> const& exp) {
        CONST_PTR(afwDet::Peak) peak = exp.getPeak();
        target.setXAstrom(peak->getIx());
        target.setYAstrom(peak->getIy());
        target.setFlagForDetection(target.getFlagForDetection() | Flags::PEAKCENTER);
    }
    static void astrom(afwDet::Source& target, afwDet::Source const& source,
                       ExposurePatch<ExposureT> const& exp) {
        if (lsst::utils::isnan(target.getXAstrom()) || lsst::utils::isnan(target.getYAstrom())) {
            CONST_PTR(afwDet::Peak) peak = exp.getPeak();
            target.setXAstrom(peak->getFx());
            target.setYAstrom(peak->getFy());
            target.setFlagForDetection(target.getFlagForDetection() | Flags::PEAKCENTER);
        }
    }
    static void photom(afwDet::Source& target, afwDet::Photometry const& phot,
                       pexPolicy::Policy const& policy) {
        // Set photometry flags
        boost::int64_t flag = target.getFlagForDetection();
        for (afwDet::Measurement<afwDet::Photometry>::const_iterator i = phot.begin();
             i != phot.end(); ++i) {
            flag |= (*i)->getFlag();
        }
        target.setFlagForDetection(flag);

        // Add some star/galaxy information.  The "extendedness" parameter is supposed to be the
        // probability of being extended
        std::vector<float> fac(3);// Fiddle factors for star/galaxy separation
        fac[0] = getNumeric(policy, "classification.sg_fac1");
        fac[1] = getNumeric(policy, "classification.sg_fac2");
        fac[2] = getNumeric(policy, "classification.sg_fac3");

        bool const isStar = ((fac[0]*target.getInstFlux() + fac[1]*target.getInstFluxErr()) <
                             (target.getPsfFlux() + fac[2]*target.getPsfFluxErr()) ? 0.0 : 1.0);
#if 0
        target.setExtendedness(isStar ? 0.0 : 1.0);
#else
        target.setApDia(isStar ? 0.0 : 1.0);
#endif
    }
};
template<typename ExposureT>
struct GroupMeasurer {
    typedef ExposureGroup<ExposureT> ExposureContainerT;
    typedef afwDet::Source SourceContainerT;
    static void footprints(ExposureGroup<ExposureT>& group,
                           afwDet::Source const& source,
                           afwImage::Wcs const& sourceWcs) {
        group.setFootprints(*source.getFootprint(), sourceWcs);
    }
    static void check(ExposureGroup<ExposureT>& group,
                      afwDet::Source const& source) {
        for (typename ExposureGroup<ExposureT>::iterator iter = group.begin(); iter != group.end(); ++iter) {
            checkPixels<ExposureT, GroupPeakMaker<ExposureT> >(*iter, source);
        }
    }
    template<typename MeasurementT>
    static PTR(MeasurementT) measure(MeasureQuantity<MeasurementT, ExposureT> const& measureQuantity,
                                     ExposureGroup<ExposureT> const& group,
                                     afwDet::Source const& source) {
        return measureQuantity.measureGroup(group, source);
    }
    template<typename MeasurementT, typename ExtractorT>
    static void extract(afwDet::Source& target, MeasurementT const& meas, pexPolicy::Policy const& policy,
                        ExtractorT const& extractor) {
        extractMeasurements(target, meas, policy, extractor);
    }
    static void nullAstrom(afwDet::Source& target, afwDet::Source const& source,
                           ExposureGroup<ExposureT> const& group) {
        target.setXAstrom(source.getXAstrom());
        target.setYAstrom(source.getYAstrom());
        target.setFlagForDetection(target.getFlagForDetection() | Flags::PEAKCENTER);
    }
    static void astrom(afwDet::Source& target, afwDet::Source const& source,
                       ExposureGroup<ExposureT> const& exp) {
        if (lsst::utils::isnan(target.getXAstrom()) || lsst::utils::isnan(target.getYAstrom())) {
            target.setXAstrom(source.getXAstrom());
            target.setYAstrom(source.getYAstrom());
            target.setFlagForDetection(target.getFlagForDetection() | Flags::PEAKCENTER);
        }
    }
    static void photom(afwDet::Source& target, afwDet::Photometry const& phot,
                       pexPolicy::Policy const& policy) {
        SingleMeasurer<ExposureT>::photom(target, phot, policy);
    }
};
template<typename ExposureT>
struct GroupsMeasurer {
    typedef std::vector<ExposureGroup<ExposureT> > ExposureContainerT;
    typedef std::vector<afwDet::Source> SourceContainerT;
    static void footprints(std::vector<ExposureGroup<ExposureT> >& groups,
                           afwDet::Source const& source,
                           afwImage::Wcs const& sourceWcs) {
        for (typename std::vector<ExposureGroup<ExposureT> >::iterator iter = groups.begin(); 
             iter != groups.end(); ++ iter) {
            iter->setFootprints(*source.getFootprint(), sourceWcs);
        }
    }
    static void check(std::vector<ExposureGroup<ExposureT> >& groups,
                      std::vector<afwDet::Source> const& sources) {
        typename std::vector<afwDet::Source>::const_iterator source = sources.begin();
        for (typename std::vector<ExposureGroup<ExposureT> >::iterator group = groups.begin();
             group != groups.end(); ++group, ++source) {
            ExposureGroup<ExposureT> g = *group;
            afwDet::Source s = *source;
            GroupMeasurer<ExposureT>::check(g, s);
        }
    }
    template<typename MeasurementT>
    static PTR(MeasurementT) measure(MeasureQuantity<MeasurementT, ExposureT> const& measureQuantity,
                                     std::vector<ExposureGroup<ExposureT> > const& groups,
                                     afwDet::Source const& source) {
        return measureQuantity.measureGroups(groups, source);
    }
    template<typename MeasurementT, typename ExtractorT>
    static void extract(std::vector<afwDet::Source>& targets, MeasurementT const& meas,
                        pexPolicy::Policy const& policy, ExtractorT const& extractor) {
        for (typename std::vector<afwDet::Source>::iterator target = targets.begin();
             target != targets.end(); ++target) {
            extractMeasurements(*target, meas, policy, extractor);
        }
    }
    static void nullAstrom(std::vector<afwDet::Source>& targets, afwDet::Source const& source,
                           std::vector<ExposureGroup<ExposureT> > const& groups) {
        typename std::vector<afwDet::Source>::iterator target = targets.begin();
        for (typename std::vector<ExposureGroup<ExposureT> >::const_iterator group = groups.begin();
             group != groups.end(); ++group, ++target) {
            ExposureGroup<ExposureT> g = *group;
            afwDet::Source t = *target;
            GroupMeasurer<ExposureT>::nullAstrom(t, source, g);
        }
    }
    static void astrom(std::vector<afwDet::Source>& targets, afwDet::Source const& source,
                       std::vector<ExposureGroup<ExposureT> > const& groups) {
        typename std::vector<afwDet::Source>::iterator target = targets.begin();
        for (typename std::vector<ExposureGroup<ExposureT> >::const_iterator group = groups.begin();
             group != groups.end(); ++group, ++target) {
            GroupMeasurer<ExposureT>::astrom(*target, source, *group);
        }
    }
    static void photom(std::vector<afwDet::Source>& targets, afwDet::Photometry const& phots,
                       pexPolicy::Policy const& policy) {
        typename afwDet::Measurement<afwDet::Photometry>::const_iterator phot = phots.begin();
        for (typename std::vector<afwDet::Source>::iterator target = targets.begin(); 
             target != targets.end(); ++target, ++phot) {
            GroupMeasurer<ExposureT>::photom(*target, **phot, policy);
        }
    }
};




} // anonymous namespace


}}} // lsst::meas::algorithms
#endif