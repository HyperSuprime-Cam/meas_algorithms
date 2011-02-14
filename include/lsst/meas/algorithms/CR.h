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
 
#if !defined(LSST_MEAS_ALGORITHMS_CR_H)
#define LSST_MEAS_ALGORITHMS_CR_H
//!
// Handle cosmic rays in a MaskedImage
//
#include <vector>
#include "lsst/base.h"
#include "lsst/afw/image/MaskedImage.h"

namespace lsst { namespace afw {
namespace detection {
    class Footprint;
    class Psf;
}}}

namespace lsst {
namespace meas {
namespace algorithms {

template <typename MaskedImageT>
std::vector<boost::shared_ptr<lsst::afw::detection::Footprint> >
findCosmicRays(MaskedImageT& image,
               lsst::afw::detection::Psf const &psf,
               double const bkgd,
               lsst::pex::policy::Policy const& policy,
               bool const keep = false
              );

}}}

#endif
