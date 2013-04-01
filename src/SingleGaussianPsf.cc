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
 
/*!
 * Represent a PSF as a circularly symmetrical single Gaussian
 *
 * @file
 *
 * @ingroup algorithms
 */
#include <cmath>
#include "lsst/afw/math/FunctionLibrary.h"
#include "lsst/pex/exceptions.h"
#include "lsst/meas/algorithms/SingleGaussianPsf.h"
#include "lsst/afw/image/ImageUtils.h"
#include "lsst/afw/table/io/InputArchive.h"
#include "lsst/afw/table/io/OutputArchive.h"
#include "lsst/afw/table/io/CatalogVector.h"

namespace lsst { namespace meas { namespace algorithms {

namespace {

// Read-only singleton struct containing the schema and keys that a single-Gaussian Psf is mapped
// to in record persistence.
struct SingleGaussianPsfPersistenceHelper : private boost::noncopyable {
    afw::table::Schema schema;
    afw::table::Key< afw::table::Point<int> > dimensions;
    afw::table::Key<double> sigma;

    static SingleGaussianPsfPersistenceHelper const & get() {
        static SingleGaussianPsfPersistenceHelper instance;
        return instance;
    }

private:
    SingleGaussianPsfPersistenceHelper() :
        schema(),
        dimensions(
            schema.addField< afw::table::Point<int> >(
                "dimensiosn", "width/height of realization of Psf", "pixels"
            )
        ),
        sigma(schema.addField<double>("sigma", "radius of Gaussian", "pixels"))
    {
        schema.getCitizen().markPersistent();
    }
};

class SingleGaussianPsfFactory : public afw::table::io::PersistableFactory {
public:

    virtual PTR(afw::table::io::Persistable)
    read(InputArchive const & archive, CatalogVector const & catalogs) const {
        static SingleGaussianPsfPersistenceHelper const & keys = SingleGaussianPsfPersistenceHelper::get();
        LSST_ARCHIVE_ASSERT(catalogs.size() == 1u);
        LSST_ARCHIVE_ASSERT(catalogs.front().size() == 1u);
        afw::table::BaseRecord const & record = catalogs.front().front();
        LSST_ARCHIVE_ASSERT(record.getSchema() == keys.schema);
        return boost::make_shared<SingleGaussianPsf>(
            record.get(keys.dimensions.getX()),
            record.get(keys.dimensions.getY()),
            record.get(keys.sigma)
        );
    }

    SingleGaussianPsfFactory(std::string const & name) : afw::table::io::PersistableFactory(name) {}

};

SingleGaussianPsfFactory registration("SingleGaussianPsf");

PTR(afw::math::Kernel) makeSingleGaussianKernel(int width, int height, double sigma) {
    if (sigma <= 0) {
        throw LSST_EXCEPT(lsst::pex::exceptions::DomainErrorException,
                          (boost::format("sigma may not be 0: %g") % sigma).str());
    }
    afw::math::GaussianFunction1<double> sg(sigma);
    return boost::make_shared<afw::math::SeparableKernel>(width, height, sg, sg);
}

} // anonymous

SingleGaussianPsf::SingleGaussianPsf(int width, int height, double sigma) :
    KernelPsf(makeSingleGaussianKernel(width, height, sigma)), _sigma(sigma)
{}

PTR(afw::detection::Psf) SingleGaussianPsf::clone() const {
    return boost::make_shared<SingleGaussianPsf>(
        getKernel()->getWidth(), getKernel()->getHeight(),
        _sigma
    );
}

std::string SingleGaussianPsf::getPersistenceName() const { return "SingleGaussianPsf"; }

void SingleGaussianPsf::write(OutputArchiveHandle & handle) const {
    static SingleGaussianPsfPersistenceHelper const & keys = SingleGaussianPsfPersistenceHelper::get();
    afw::table::BaseCatalog catalog = handle.makeCatalog(keys.schema);
    PTR(afw::table::BaseRecord) record = catalog.addNew();
    (*record)[keys.dimensions.getX()] = getKernel()->getWidth();
    (*record)[keys.dimensions.getY()] = getKernel()->getHeight();
    (*record)[keys.sigma] = getSigma();
    handle.saveCatalog(catalog);
}

}}} // namespace lsst::meas::algorithms
