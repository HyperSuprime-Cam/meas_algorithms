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
 
#include "lsst/pex/policy.h"
#include "lsst/afw/detection.h"
#include "lsst/meas/algorithms/DoubleGaussianPsf.h"
#include "lsst/afw/image.h"
#include "lsst/afw/math.h"
#include "lsst/afw/geom.h"
#include "lsst/afw/math/Random.h"
#include "lsst/meas/algorithms/Measure.h"
#include "lsst/meas/algorithms/PsfCandidate.h"
#include "lsst/meas/algorithms/SpatialModelPsf.h"
#include "lsst/meas/algorithms/FluxControl.h"
#include "lsst/meas/algorithms/CentroidControl.h"

namespace afwDetection = lsst::afw::detection;
namespace afwImage = lsst::afw::image;
namespace afwMath = lsst::afw::math;
namespace afwGeom = lsst::afw::geom;
namespace afwTable = lsst::afw::table;
namespace algorithms = lsst::meas::algorithms;

// A test case for SpatialModelPsf
int main() {
    int const width = 100;
    int const height = 301;
    afwImage::MaskedImage<float>::Ptr mi(
        new afwImage::MaskedImage<float>(afwGeom::ExtentI(width, height))
    );
    *mi->getImage() = 0;
    float const sd = 3;                 // standard deviation of image
    *mi->getVariance() = sd*sd;
    mi->getMask()->addMaskPlane("DETECTED");
    
    double const FWHM = 5;
    int const ksize = 25;                         // size of desired kernel
    afwMath::Random rand;                         // make these tests repeatable by setting seed
    
    afwMath::randomGaussianImage(mi->getImage().get(), rand); // N(0, 1)
    *mi->getImage() *= sd;                                    // N(0, sd^2)
    
    std::pair<int, int> xy[] = {
        std::pair<int, int>(20, 20),
        std::pair<int, int>(60, 20),
        std::pair<int, int>(30, 35),
        std::pair<int, int>(50, 50),
        std::pair<int, int>(50, 130),
        std::pair<int, int>(70, 80),
        std::pair<int, int>(60, 210),
        std::pair<int, int>(20, 210)
    };
    
    for (int i = 0; i != sizeof(xy)/sizeof(xy[0]); ++i) {
        int x = xy[i].first, y = xy[i].second;

        double const flux = 10000 - 0*x - 10*y;
        
        double const sigma = 3 + 0.005*(y - mi->getHeight()/2);
        afwDetection::Psf::Ptr psf(new algorithms::DoubleGaussianPsf(ksize, ksize, sigma, 1, 0.1));
        afwImage::Image<float> im(*psf->computeImage(), true);
        im *= flux;
        afwGeom::BoxI box(afwGeom::Point2I(x - ksize/2, y - ksize/2), 
                          afwGeom::ExtentI(ksize, ksize));
        afwImage::Image<float> smi(*mi->getImage(), box, afwImage::LOCAL);

        float const dx = rand.uniform() - 0.5;
        float const dy = rand.uniform() - 0.5;
        {
            afwImage::Image<float>::Ptr oim = afwMath::offsetImage(im, dx, dy);
            
            smi += *oim;
        }
    }

#if 0
    mi->writeFits("foo.fits");
#endif

    afwDetection::Psf::Ptr psf(
        new algorithms::DoubleGaussianPsf(ksize, ksize, FWHM/(2*sqrt(2*log(2))), 1, 0.1)
    );

    afwMath::SpatialCellSet cellSet(
        afwGeom::BoxI(afwGeom::Point2I(0, 0), afwGeom::ExtentI(width, height)), 
        100
    );
    afwDetection::FootprintSet fs(*mi, afwDetection::Threshold(100), "DETECTED");
    
    afwImage::Exposure<float>::Ptr exposure = afwImage::makeExposure(*mi);
    exposure->setPsf(psf);

    afwTable::Schema schema = afwTable::SourceTable::makeMinimalSchema();
    algorithms::NaiveFluxControl naiveFluxControl; // use NAIVE (== crude aperture)  photometry
    naiveFluxControl.radius = 3.0;  
    algorithms::MeasureSources measureSources =
        algorithms::MeasureSourcesBuilder()
        .setCentroider(algorithms::SdssCentroidControl())
        .addAlgorithm(naiveFluxControl)
        .build(schema);
    afwTable::SourceCatalog catalog(schema);
    catalog.getTable()->defineCentroid("centroid.sdss");
    catalog.getTable()->definePsfFlux("flux.naive"); // weird, but that's what was in the Policy before
    fs.makeSources(catalog);
    for (afwTable::SourceCatalog::const_iterator i = catalog.begin(); i != catalog.end(); ++i) {
        measureSources.apply(*i, *exposure);
        algorithms::PsfCandidate<float>::Ptr candidate = algorithms::makePsfCandidate(i, exposure);
        cellSet.insertCandidate(candidate);
    }

    // Convert our cellSet to a LinearCombinationKernel

    int const nEigenComponents = 2;
    int const spatialOrder  =    1;
    int const kernelSize =      31;
    int const nStarPerCell =     4;
    int const nIterForPsf =      5;

    algorithms::PsfCandidate<float>::setWidth(kernelSize);
    algorithms::PsfCandidate<float>::setHeight(kernelSize);

    for (int iter = 0; iter != nIterForPsf; ++iter) {
        algorithms::createKernelFromPsfCandidates<float>(cellSet, afwGeom::ExtentI(width, height),
                                                         afwGeom::Point2I(0, 0),
                                                         nEigenComponents, spatialOrder, 
                                                         kernelSize, nStarPerCell);
    }
}
