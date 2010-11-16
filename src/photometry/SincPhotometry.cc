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
 
#include <numeric>
#include <cmath>
#include <functional>
#include <map>

#include <complex>
#include <gsl/gsl_sf_bessel.h>
#include <fftw3.h>

#include "boost/lambda/lambda.hpp"
#include "boost/lambda/bind.hpp"

#include "lsst/pex/exceptions.h"
#include "lsst/pex/logging/Trace.h"
#include "lsst/afw/geom/Extent.h"
#include "lsst/afw/image.h"
#include "lsst/afw/math/Integrate.h"
#include "lsst/meas/algorithms/Measure.h"
#include "lsst/meas/algorithms/detail/SincPhotometry.h"

#include "lsst/afw/detection/Psf.h"
#include "lsst/afw/detection/Photometry.h"

namespace pexExceptions = lsst::pex::exceptions;
namespace pexLogging = lsst::pex::logging;
namespace afwDetection = lsst::afw::detection;
namespace afwImage = lsst::afw::image;
namespace afwMath = lsst::afw::math;

namespace lsst {
namespace meas {
namespace algorithms {

/**
 * @brief A class that knows how to calculate fluxes using the SINC photometry algorithm
 * @ingroup meas/algorithms
 */
class SincPhotometry : public afwDetection::Photometry
{
public:
    typedef boost::shared_ptr<SincPhotometry> Ptr;
    typedef boost::shared_ptr<SincPhotometry const> ConstPtr;

    /// Ctor
    SincPhotometry(double flux, double fluxErr=std::numeric_limits<double>::quiet_NaN()) :
        afwDetection::Photometry(flux, fluxErr) {}

    /// Add desired fields to the schema
    virtual void defineSchema(afwDetection::Schema::Ptr schema ///< our schema; == _mySchema
                     ) {
        Photometry::defineSchema(schema);
    }

    static bool doConfigure(lsst::pex::policy::Policy const& policy);

    template<typename ImageT>
    static Photometry::Ptr doMeasure(typename ImageT::ConstPtr im, afwDetection::Peak const*);

    /// Set the aperture radius to use
    static void setRadius(double radius) { _radius = radius; }
    static void setInnerRadius(double iradius) { _innerRadius = iradius; }

    /// Return the aperture radius to use
    static double getRadius() { return _radius; }
    static double getInnerRadius() { return _innerRadius; }

private:
    static double _radius;
    static double _innerRadius;
    SincPhotometry(void) : afwDetection::Photometry() { }
    LSST_SERIALIZE_PARENT(afwDetection::Photometry)
};

LSST_REGISTER_SERIALIZER(SincPhotometry)

double SincPhotometry::_radius = 0;      // radius to use for sinc photometry
double SincPhotometry::_innerRadius = 0;

    
/************************************************************************************************************/
namespace {

// sinc function
template<typename T>
inline T sinc(T const x) {
    return (x != 0.0) ? (std::sin(x) / x) : 1.0;
}

/******************************************************************************/
/**
 * @brief Define a circular aperture function object g_i, cos-tapered?
 */
template<typename CoordT>            
class CircularAperture {
public:
    
    CircularAperture(
                     CoordT const radius1,    ///< inner radius of the aperture
                     CoordT const radius2,    ///< outer radius of the aperture
                     CoordT const taperwidth ///< width to cosine taper from 1.0 to 0.0 (ie. 0.5*cosine period)
                    ):
        _radius1(radius1),
        _radius2(radius2),
        _taperwidth1(taperwidth),
        _taperwidth2(taperwidth),
        _k1(1.0/(2.0*taperwidth)),
        _k2(1.0/(2.0*taperwidth)),
        _taperLo1(radius1 - 0.5*taperwidth),
        _taperHi1(radius1 + 0.5*taperwidth),
        _taperLo2(radius2 - 0.5*taperwidth),
        _taperHi2(radius2 + 0.5*taperwidth) {

        // if we're asked for a radius smaller than our taperwidth,
        // adjust the taper width smaller so it fits exactly
        // with smooth derivative=0 at r=0

        if (_radius1 > _radius2) {
            throw LSST_EXCEPT(pexExceptions::InvalidParameterException,
                              (boost::format("rad2 less than rad1: (rad1=%.2f, rad2=%.2f) ") %
                               _radius1 % _radius2).str());
        }
        if (_radius1 < 0.0 || _radius2 < 0.0) {
            throw LSST_EXCEPT(pexExceptions::InvalidParameterException,
                              (boost::format("radii must be > 0 (rad1=%.2f, rad2=%.2f) ") %
                               _radius1 % _radius2).str());
        }
        
        if (_radius1 == 0) {
            _taperwidth1 = 0.0;
            _k1 = 0.0;
        }
        
        // if we don't have room to taper at r=0
        if ( _radius1 < 0.5*_taperwidth1) {
            _taperwidth1 = 2.0*_radius1;
            _k1 = 1.0/(2.0*_taperwidth1);
        }
            
        // if we don't have room to taper between r1 and r2
        if ((_radius2 - _radius1) < 0.5*(_taperwidth1+_taperwidth2)) {
            
            // if we *really* don't have room ... taper1 by itself is too big
            // - set taper1,2 to be equal and split the r2-r1 range
            if ((_radius2 - _radius2) < 0.5*_taperwidth1) {
                _taperwidth1 = _taperwidth2 = 0.5*(_radius2 - _radius1);
                _k1 = _k2 = 1.0/(2.0*_taperwidth1);
                
                // if there's room for taper1, but not taper1 and 2
            } else {
                _taperwidth2 = _radius2 - _radius1 - _taperwidth1;
                _k2 = 1.0/(2.0*_taperwidth2);
            }                
                
            _taperLo1 = _radius1 - 0.5*_taperwidth1; 
            _taperHi1 = _radius1 + 0.5*_taperwidth1;
            _taperLo2 = _radius2 - 0.5*_taperwidth2; 
            _taperHi2 = _radius2 + 0.5*_taperwidth2;
        }
    }
    

    // When called, return the throughput at the requested x,y
    // todo: replace the sinusoid taper with a band-limited
    CoordT operator() (CoordT const x, CoordT const y) const {
        CoordT const xyrad = std::sqrt(x*x + y*y);
        if ( xyrad < _taperLo1 ) {
            return 0.0;
        } else if (xyrad >= _taperLo1 && xyrad <= _taperHi1 ) {
            return 0.5*(1.0 + std::cos(  (2.0*M_PI*_k1)*(xyrad - _taperHi1)));
        } else if (xyrad > _taperHi1 && xyrad <= _taperLo2 ) {
            return 1.0;
        } else if (xyrad > _taperLo2 && xyrad <= _taperHi2 ) {
            return 0.5*(1.0 + std::cos(  (2.0*M_PI*_k2)*(xyrad - _taperLo2)));
        } else {
            return 0.0;
        }
    }
    
    CoordT getRadius1() { return _radius1; }
    CoordT getRadius2() { return _radius2; }
    
private:
    CoordT _radius1, _radius2;
    CoordT _taperwidth1, _taperwidth2;
    CoordT _k1, _k2;      // the angular wavenumber corresponding to a cosine with wavelength 2*taperwidth
    CoordT _taperLo1, _taperHi1;
    CoordT _taperLo2, _taperHi2;
};


template<typename CoordT>            
class CircApPolar : public std::unary_function<CoordT, CoordT> {
public:
    CircApPolar(double radius, double taperwidth) : _ap(CircularAperture<CoordT>(0.0, radius, taperwidth)) {}
    CoordT operator() (double r) const {
        return r*_ap(r, 0.0);
    }
private:
    CircularAperture<CoordT> _ap;
};
    
/******************************************************************************/

/**
 * Define a Sinc functor to be integrated over for Sinc interpolation
 */
template<typename IntegrandT>
class SincAperture : public std::binary_function<IntegrandT, IntegrandT, IntegrandT> {
public:
    
    SincAperture(
                 CircularAperture<IntegrandT> const &ap,
                 int const ix,        // sinc center x
                 int const iy         // sinc center y
                )
        : _ap(ap), _ix(ix), _iy(iy) {
        _xtaper = 10.0;
        _ytaper = 10.0;
    }
    
    IntegrandT operator() (IntegrandT const x, IntegrandT const y) const {
        double const fourierConvention = 1.0*M_PI;
        double const dx = fourierConvention*(x - _ix);
        double const dy = fourierConvention*(y - _iy);
        double const fx = 0.5*(1.0 + std::cos(dx/_xtaper)) * sinc<double>(dx);
        double const fy = 0.5*(1.0 + std::cos(dy/_ytaper)) * sinc<double>(dy);
        return (1.0 + _ap(x, y)*fx*fy);
    }
    
private: 
    CircularAperture<IntegrandT> const &_ap;
    double _ix, _iy;
    double _xtaper, _ytaper; // x,y distances over which to cos-taper the sinc to zero
};
    


/******************************************************************************/
    
template <typename MaskedImageT, typename WeightImageT>
class FootprintWeightFlux : public afwDetection::FootprintFunctor<MaskedImageT> {
public:
    FootprintWeightFlux(
                        MaskedImageT const& mimage, ///< The image the source lives in
                        typename WeightImageT::Ptr wimage    ///< The weight image
                       ) :
        afwDetection::FootprintFunctor<MaskedImageT>(mimage),
        _wimage(wimage),
        _sum(0.0), _sumVar(0.0),
        _x0(wimage->getX0()), _y0(wimage->getY0()) {}
    
    /// @brief Reset everything for a new Footprint
    void reset() {}        
    void reset(afwDetection::Footprint const& foot) {
        _sum = 0.0;
        _sumVar = 0.0;

        afwImage::BBox const& bbox(foot.getBBox());
        _x0 = bbox.getX0();
        _y0 = bbox.getY0();

        if (bbox.getDimensions() != _wimage->getDimensions()) {
            throw LSST_EXCEPT(pexExceptions::LengthErrorException,
                              (boost::format("Footprint at %d,%d -- %d,%d is wrong size "
                                             "for %d x %d weight image") %
                               bbox.getX0() % bbox.getY0() % bbox.getX1() % bbox.getY1() %
                               _wimage->getWidth() % _wimage->getHeight()).str());
        }
    }
    
    /// @brief method called for each pixel by apply()
    void operator()(typename MaskedImageT::xy_locator iloc, ///< locator pointing at the image pixel
                    int x,                                 ///< column-position of pixel
                    int y                                  ///< row-position of pixel
                   ) {
        typename MaskedImageT::Image::Pixel ival = iloc.image(0, 0);
        typename MaskedImageT::Image::Pixel vval = iloc.variance(0, 0);
        typename WeightImageT::Pixel wval = (*_wimage)(x - _x0, y - _y0);
        _sum    += wval*ival;
        _sumVar += wval*wval*vval;
    }

    /// Return the Footprint's flux
    double getSum() const { return _sum; }
    double getSumVar() const { return _sumVar; }
    
private:
    typename WeightImageT::Ptr const& _wimage;        // The weight image
    double _sum;                                      // our desired sum
    double _sumVar;                                   // sum of the variance
    int _x0, _y0;                                     // the origin of the current Footprint
};

/*
 * A comparison function that doesn't require equality closer than machine epsilon
 */
template <typename T>
struct fuzzyCompare {
    bool operator()(T x,
                    T y) const
    {
        if (::fabs(x - y) < std::numeric_limits<T>::epsilon()) {
            return false;
        } else {
            return (x - y < 0) ? true : false;
        }
    }
};
    
} // end of anonymous namespace

namespace {
    template<typename PixelT>
    class SincCoeffs : private boost::noncopyable {
        typedef std::map<float, typename afwImage::Image<PixelT>::Ptr, fuzzyCompare<float> > _coeffImageMap;
        typedef std::map<float, _coeffImageMap, fuzzyCompare<float> > _coeffImageMapMap;
    public:
        static SincCoeffs &getInstance();

        typename afwImage::Image<PixelT>::Ptr getImage(float r1, float r2, double taperwidth=1.0) {
            return _calculateImage(r1, r2, taperwidth);
        }
        typename afwImage::Image<PixelT>::ConstPtr getImage(float r1, float r2, double taperwidth=1.0) const {
            return _calculateImage(r1, r2, taperwidth);
        }
    private:
        static typename afwImage::Image<PixelT>::Ptr _calculateImage(double const innerRadius,
                                                                     double const radius,
                                                                     double const taperwidth=1.0);
        
        static _coeffImageMapMap _coeffImages;
    };

    
    template<typename PixelT>
    SincCoeffs<PixelT>& SincCoeffs<PixelT>::getInstance()
    {
        static SincCoeffs<PixelT> instance;
        return instance;
    }
    
    template<typename PixelT>
    typename SincCoeffs<PixelT>::_coeffImageMapMap SincCoeffs<PixelT>::_coeffImages =
        typename SincCoeffs<PixelT>::_coeffImageMapMap();

    template<typename PixelT>
    typename afwImage::Image<PixelT>::Ptr
    SincCoeffs<PixelT>::_calculateImage(double const innerRadius, double const radius,
                                        double const taperwidth) {
        typename _coeffImageMapMap::const_iterator rmap = _coeffImages.find(innerRadius);
        if (rmap != _coeffImages.end()) {
            // we've already calculated the coefficients for this radius
            typename _coeffImageMap::const_iterator cImage = rmap->second.find(radius);
            if (cImage != rmap->second.end()) {
                return cImage->second;
            }
        }
        
        // @todo this should be in a .paf file with radius
        //double const taperwidth = 1.0;
        double const bufferWidth = 10.0;

        PixelT initweight = 0.0; // initialize the coeff values

        double const xdwidth = 2.0*(radius + bufferWidth);
        double const ydwidth = 2.0*(radius + bufferWidth);
        int const xwidth     = static_cast<int>(xdwidth) + 1;
        int const ywidth     = static_cast<int>(ydwidth) + 1;

        int const x0 = -xwidth/2;
        int const y0 = -ywidth/2;
    
        // create an image to hold the coefficient image
        typename afwImage::Image<PixelT>::Ptr coeffImage =
            boost::make_shared<afwImage::Image<PixelT> >(xwidth, ywidth, initweight);
        coeffImage->markPersistent();
        coeffImage->setXY0(x0, y0);

        // create the aperture function object
        // determine the radius to use that makes 'radius' the effective radius of the aperture
        double tolerance = 1.0e-12;
        double dr = 1.0e-6;
        double err = 2.0*tolerance;
        double apEff = M_PI*radius*radius;
        double radIn = radius;
        int maxIt = 20;
        int i = 0;
        while (err > tolerance && i < maxIt) {
            CircApPolar<double> apPolar1(radIn, taperwidth);
            CircApPolar<double> apPolar2(radIn+dr, taperwidth); 
            double a1 = M_PI*2.0*afwMath::integrate(apPolar1, 0.0, radIn+taperwidth, tolerance);
            double a2 = M_PI*2.0*afwMath::integrate(apPolar2, 0.0, radIn+dr+taperwidth, tolerance);
            double dadr = (a2 - a1)/dr;
            double radNew = radIn - (a1 - apEff)/dadr;
            err = (a1 - apEff)/apEff;
            radIn = radNew;
            i++;
        }
        CircularAperture<double> ap(innerRadius, radius, taperwidth);

        
        /* ******************************* */
        // integrate over the aperture
        
        // the limits of the integration over the sinc aperture
        double const limit = radius + taperwidth;
        double const x1 = -limit;
        double const x2 =  limit;
        double const y1 = -limit;
        double const y2 =  limit;
        
        for (int iY = y0; iY != y0 + coeffImage->getHeight(); ++iY) {
            int iX = x0;
            typename afwImage::Image<PixelT>::x_iterator end = coeffImage->row_end(iY-y0);
            for (typename afwImage::Image<PixelT>::x_iterator ptr = coeffImage->row_begin(iY-y0); ptr != end; ++ptr) {
                
                // create a sinc function in the CircularAperture at our location
                SincAperture<double> sincAp(ap, iX, iY);
                
                // integrate the sinc
                PixelT integral = afwMath::integrate2d(sincAp, x1, x2, y1, y2, 1.0e-8);
                
                // we actually integrated function+1.0 and now must subtract the excess volume
                // - just force it to zero in the corners
                double const dx = iX;
                double const dy = iY;
                *ptr = (std::sqrt(dx*dx + dy*dy) < xwidth/2) ?
                    integral - (x2 - x1)*(y2 - y1) : 0.0;
                ++iX;
            }
        }


        double sum = 0.0;
        for (int iY = y0; iY != y0 + coeffImage->getHeight(); ++iY) {
            typename afwImage::Image<PixelT>::x_iterator end = coeffImage->row_end(iY-y0);
            for (typename afwImage::Image<PixelT>::x_iterator ptr = coeffImage->row_begin(iY-y0); ptr != end; ++ptr) {
                sum += *ptr;
            }
        }
        //printf("%.15f %.15f  %.15g  %.10f %.10f %d\n", sum, M_PI*radius*radius, sum/(M_PI*radius*radius), radius, radIn, i);
        
#if 0                           // debugging
        coeffImage->writeFits("cimage.fits");
#endif
        _coeffImages[innerRadius][radius] = coeffImage;

        return coeffImage;
    }
}

namespace detail {

template<typename PixelT>
typename afwImage::Image<PixelT>::Ptr getCoeffImage(double const innerRadius, double const radius,
                                                   double const taperwidth) {
    SincCoeffs<PixelT> &coeffs = SincCoeffs<PixelT>::getInstance();
    return coeffs.getImage(innerRadius, radius, taperwidth);
}


class Airy : public std::binary_function<double, double, double> {
public:
    Airy(double const r) : _r(r) {}
    double operator()(double kx, double ky) const {
        double k = ::sqrt(kx*kx + ky*ky);
        return k ? gsl_sf_bessel_J1(2.0*M_PI*_r*k)/k : 0.0; }
private:
    double _r;
};
    
// note you can only call this on double because we pass a pointer to the fft
// and it expects double ... so don't template it!
afwImage::Image<double>::Ptr getCoeffImageFft(double const innerRadius, double const radius) {

    // determine the airy function in k-space
    Airy jxx(radius);
    
    // integrate the airy function for each pixel
    int const bufferWidth = 10.0;
    int width = 2*(bufferWidth + static_cast<int>(radius)) + 1;
    int y0 = -width/2;
    int x0 = -width/2;

    boost::shared_ptr<std::complex<double> > cimg(new std::complex<double>[width*width]);
    std::complex<double> *c = cimg.get();
    //afwImage::Image<double>::Ptr cimg(new afwImage::Image<double>(width, width));
    for (int iY = 0; iY < width; ++iY) {
        for (int iX = 0; iX < width; ++iX) {
            double x = static_cast<double>(iX - x0);
            double y = static_cast<double>(iY - y0);
            double integral = afwMath::integrate2d(jxx, x-0.5, x+0.5, y-0.5, y+0.5, 1.0e-8);
            c[iY*width+iX].real() = integral;
            c[iY*width+iX].imag() = 0.0;
        }
    }
    
    // ifft2 to real space
    boost::shared_ptr<std::complex<double> > icimg(new std::complex<double>[width*width]);
    std::complex<double> *ic = icimg.get();

    //construct a backward-transform plan
    fftw_plan plan = fftw_plan_dft_2d(
                                      width, width, //image dimensions
                                      reinterpret_cast<fftw_complex*>(cimg.get()),   // input ptr
                                      reinterpret_cast<fftw_complex*>(icimg.get()),  // output ptr
                                      FFTW_BACKWARD, // direction to transform
                                      FFTW_MEASURE
                                     );
   
    fftw_execute(plan);
    fftw_destroy_plan(plan);


    afwImage::Image<double>::Ptr coeffImage(new afwImage::Image<double>(width, width));
    for (int iY = y0; iY != y0 + coeffImage->getHeight(); ++iY) {
        int iX = 0;
        afwImage::Image<double>::x_iterator end = coeffImage->row_end(iY-y0);
        for (afwImage::Image<double>::x_iterator ptr = coeffImage->row_begin(iY-y0); ptr != end; ++ptr) {
            int x = iX - x0;
            int y = iY - y0;
            *ptr = ic[y*width+x].real();
            iX++;
        }
    }
    
    return coeffImage;
    //return getCoeffImage<PixelT>(innerRadius, radius);
}
    
}

/************************************************************************************************************/
/**
 * Set parameters controlling how we do measurements
 */
bool SincPhotometry::doConfigure(lsst::pex::policy::Policy const& policy)
{
    if (policy.isDouble("radius")) {   
        double const radius = policy.getDouble("radius"); // remember the radius we're using
        setRadius(radius);
        double iradius = (policy.isDouble("innerRadius")) ? policy.getDouble("innerRadius") : 0.0;
        setInnerRadius(iradius);                          // remember the inner radius we're using
        SincCoeffs<float>::getInstance().getImage(iradius, radius); // calculate the needed coefficients
    } 
    return true;
}
    
/************************************************************************************************************/
/**
 * Calculate the desired aperture flux using the sinc algorithm
 */
template<typename ExposureT>
afwDetection::Photometry::Ptr SincPhotometry::doMeasure(typename ExposureT::ConstPtr exposure,
                                                        afwDetection::Peak const* peak
                                                       ) {
    
    double flux = std::numeric_limits<double>::quiet_NaN();
    double fluxErr = std::numeric_limits<double>::quiet_NaN();
    if (!peak) {
        return boost::make_shared<SincPhotometry>(flux, fluxErr);
    }
    
    typedef typename ExposureT::MaskedImageT MaskedImageT;
    typedef typename MaskedImageT::Image Image;
    typedef typename Image::Pixel Pixel;
    typedef typename Image::Ptr ImagePtr;

    MaskedImageT const& mimage = exposure->getMaskedImage();
    
    double const xcen = peak->getFx();   ///< object's column position
    double const ycen = peak->getFy();   ///< object's row position
    
    afwImage::BBox imageBBox(afwImage::PointI(mimage.getX0(), mimage.getY0()),
                             mimage.getWidth(), mimage.getHeight()); // BBox for data image

    /* ********************************************************** */
    // Aperture photometry
    {
        // make the coeff image
        // compute c_i as double integral over aperture def g_i(), and sinc()
        ImagePtr cimage0 = detail::getCoeffImage<Pixel>(getInnerRadius(), getRadius());

        // as long as we're asked for the same radius, we don't have to recompute cimage0
        // shift to center the aperture on the object being measured
        ImagePtr cimage = afwMath::offsetImage(*cimage0, xcen, ycen);
        afwImage::BBox bbox(cimage->getXY0(), cimage->getWidth(), cimage->getHeight());

        
        // ***************************************
        // bounds check for the footprint
#if 0
        // I think this should work, but doesn't.
        // For the time being, I'll do the bounds check here
        // ... should determine why bbox/image behaviour not as expected.
        afwImage::BBox mbbox(mimage.getXY0(), mimage.getWidth(), mimage.getHeight());
        bbox.clip(mbbox);
        afwImage::PointI cimXy0(cimage->getXY0());
        bbox.shift(-cimage->getX0(), -cimage->getY0());
        cimage = typename Image::Ptr(new Image(*cimage, bbox, false));
        cimage->setXY0(cimXy0);
#else
        int x1 = (cimage->getX0() < mimage.getX0()) ? mimage.getX0() : cimage->getX0();
        int y1 = (cimage->getY0() < mimage.getY0()) ? mimage.getY0() : cimage->getY0();
        int x2 = (cimage->getX0() + cimage->getWidth() > mimage.getX0() + mimage.getWidth()) ?
            mimage.getX0() + mimage.getWidth() - 1 : cimage->getX0() + cimage->getWidth() - 1;
        int y2 = (cimage->getY0() + cimage->getHeight() > mimage.getY0() + mimage.getHeight()) ?
            mimage.getY0() + mimage.getHeight() - 1 : cimage->getY0() + cimage->getHeight() - 1; 

        // if the dimensions changed, put the image in a smaller bbox
        if ( (x2 - x1 + 1 != cimage->getWidth()) || (y2 - y1 + 1 != cimage->getHeight()) ) {
            // must be zero origin or we'll throw in Image copy constructor
            bbox = afwImage::BBox(afwImage::PointI(x1 - cimage->getX0(), y1 - cimage->getY0()),
                                                 x2 - x1 + 1, y2 - y1 + 1);
            cimage = ImagePtr(new Image(*cimage, bbox, false));

            // shift back to correct place
            cimage = afwMath::offsetImage(*cimage, x1, y1);
            bbox = afwImage::BBox(afwImage::PointI(x1, y1), x2 - x1 + 1, y2 - y1 + 1);
        }
#endif
        // ****************************************
        
        
        
        // pass the image and cimage into the wfluxFunctor to do the sum
        FootprintWeightFlux<MaskedImageT, Image> wfluxFunctor(mimage, cimage);
        
        afwDetection::Footprint foot(bbox, imageBBox);
        wfluxFunctor.apply(foot);
        flux = wfluxFunctor.getSum();
        fluxErr = ::sqrt(wfluxFunctor.getSumVar());
    }
    return boost::make_shared<SincPhotometry>(flux, fluxErr);
}

//
// Explicit instantiations
//
// \cond
#define INSTANTIATE(T) \
    template lsst::afw::image::Image<T>::Ptr detail::getCoeffImage<T>(double const, double const, double const)
    
/*
 * Declare the existence of a "SINC" algorithm to MeasurePhotometry
 */
#define MAKE_PHOTOMETRYS(TYPE)                                          \
    MeasurePhotometry<afwImage::Exposure<TYPE> >::declare("SINC", \
        &SincPhotometry::doMeasure<afwImage::Exposure<TYPE> >, \
        &SincPhotometry::doConfigure \
    )

namespace {
    volatile bool isInstance[] = {
        MAKE_PHOTOMETRYS(float)
#if 0
        ,MAKE_PHOTOMETRYS(double)
#endif
    };
}

INSTANTIATE(float);
INSTANTIATE(double);
    
// \endcond

}}}
