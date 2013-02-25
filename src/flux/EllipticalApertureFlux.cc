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
#include <cmath>
#include <limits>
#include <numeric>
#include "lsst/pex/exceptions.h"
#include "lsst/pex/logging/Trace.h"
#include "lsst/afw/image.h"
#include "lsst/afw/geom.h"
#include "lsst/afw/geom/ellipses.h"
#include "lsst/afw/detection/Psf.h"
#include "lsst/meas/algorithms/Photometry.h"
#include "lsst/meas/algorithms/FluxControl.h"
#include "lsst/meas/algorithms/ApertureFlux.h"

namespace lsst {
namespace meas {
namespace algorithms {

/**
 * Implement "EllipticalAperture" photometry.
 * @brief A class that knows how to calculate fluxes as a simple sum over a Footprint
 */
class EllipticalApertureFlux : public ApertureFlux {
public:
    EllipticalApertureFlux(EllipticalApertureFluxControl const & ctrl, afw::table::Schema & schema) :
        ApertureFlux(ctrl, schema)
        {}

private:
    
    template <typename PixelT>
    void _apply(
        afw::table::SourceRecord & source,
        afw::image::Exposure<PixelT> const & exposure,
        afw::geom::Point2D const & center
    ) const;

    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE(EllipticalApertureFlux);
};

/************************************************************************************************************/
/**
 * @brief Given an image and a source position, calculate a set of fluxes in elliptical apertures
 */
template <typename PixelT>
void EllipticalApertureFlux::_apply(
    afw::table::SourceRecord & source,
    afw::image::Exposure<PixelT> const& exposure,
    afw::geom::Point2D const & center
) const {
    source.set(_flagKey, true);         // say we've failed so that's the result if we throw
    source.set(_nProfileKey, 0);        // no points measured

    if (source.getShapeFlag()) {        // the shape's bad; give up now
        return;
    }
    afw::geom::ellipses::Axes const shape = source.getShape();

    VectorD const & radii = static_cast<EllipticalApertureFluxControl const &>(getControl()).radii;
    int const nradii = radii.size();

    typename afw::image::Exposure<PixelT>::MaskedImageT const& mimage = exposure.getMaskedImage();
    double oradius = 0.0;                // old size of shape
    double const fac = 1.0/::sqrt(shape.getA()*shape.getB()); // conversion between radii and semi-major axis
    for (int i = 0; i != nradii; ++i) {
        afw::geom::ellipses::Axes outer(shape);
        outer.scale(fac*radii[i]);

        std::pair<double, double> flux =
            algorithms::photometry::calculateSincApertureFlux(mimage,
                                                              afw::geom::ellipses::Ellipse(outer, center),
                                                              oradius/outer.getA());
        oradius = outer.getA();

        source.set(_fluxKey[i], flux.first);
        source.set(_errKey[i],  flux.second);
    }
    source.set(_nProfileKey, nradii);
    source.set(_flagKey, false);
}

LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION(EllipticalApertureFlux);

PTR(AlgorithmControl) EllipticalApertureFluxControl::_clone() const {
    return boost::make_shared<EllipticalApertureFluxControl>(*this);
}

PTR(Algorithm) EllipticalApertureFluxControl::_makeAlgorithm(
    afw::table::Schema & schema,
    PTR(daf::base::PropertyList) const & metadata
) const {
    if (metadata) {
        std::string key = this->name + ".radii";
        std::replace(key.begin(), key.end(), '.', '_');
        metadata->add(key, radii, "Radii for aperture flux measurement");
    }
    return boost::make_shared<EllipticalApertureFlux>(*this, boost::ref(schema));
}

}}}
