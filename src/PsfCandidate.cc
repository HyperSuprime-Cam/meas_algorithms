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
 * @brief Implementation of code to determine spatial model of PSF
 *
 * @file
 *
 * @ingroup algorithms
 */
#include "lsst/afw/detection/Footprint.h"
#include "lsst/afw/geom/Point.h"
#include "lsst/afw/geom/Extent.h"
#include "lsst/afw/geom/Box.h"
#include "lsst/afw/image/ImageAlgorithm.h"
#include "lsst/afw/math/offsetImage.h"
#include "lsst/meas/algorithms/PsfCandidate.h"

namespace afwDetection = lsst::afw::detection;
namespace afwGeom      = lsst::afw::geom;
namespace afwImage     = lsst::afw::image;
namespace afwMath      = lsst::afw::math;
namespace measAlg      = lsst::meas::algorithms;

/************************************************************************************************************/
/*
 * PsfCandidate's members
 */
template <typename PixelT>
int measAlg::PsfCandidate<PixelT>::_border = 0;
template <typename PixelT>
int measAlg::PsfCandidate<PixelT>::_defaultWidth = 21;

/************************************************************************************************************/
namespace {
    template<typename T>                // functor used by makeImageFromMask to return inputMask
    struct noop : public afwImage::pixelOp1<T> {
        T operator()(T x) const { return x; }
    };

    template<typename T>                // functor used by makeImageFromMask to return (inputMask & mask)
    struct andMask : public afwImage::pixelOp1<T> {
        andMask(T mask) : _mask(mask) {}
        T operator()(T x) const { return (x & _mask); }
    private:
        T _mask;
    };

    template<typename T>
    andMask<T> makeAndMask(T val) {
        return andMask<T>(val);
    }

    /*
     * Return an Image initialized from a Mask (possibly modified by func)
     */
    template<typename LhsT, typename RhsT>
    typename afwImage::Image<LhsT>::Ptr
    makeImageFromMask(afwImage::Mask<RhsT> const& rhs,     ///< mask to process
                      afwImage::pixelOp1<RhsT> const& func=noop<RhsT>() ///< functor to call
                     )
    {
        typename afwImage::Image<LhsT>::Ptr lhs =
            boost::make_shared<afwImage::Image<LhsT> >(rhs.getDimensions());
        lhs->setXY0(rhs.getXY0());

        for (int y = 0; y != lhs->getHeight(); ++y) {
            typename afwImage::Image<RhsT>::const_x_iterator rhsPtr = rhs.row_begin(y);

            for (typename afwImage::Image<LhsT>::x_iterator lhsPtr = lhs->row_begin(y),
                     lhsEnd = lhs->row_end(y); lhsPtr != lhsEnd; ++rhsPtr, ++lhsPtr) {
                *lhsPtr = func(*rhsPtr);
            }
        }
        
        return lhs;
    }
}

/// Extract an image of the candidate.
///
/// No offsets are applied.
/// The INTRP bit is set for any pixels that are detected but not part of the Source
template <typename PixelT>
PTR(afwImage::MaskedImage<PixelT>)
measAlg::PsfCandidate<PixelT>::extractImage(
    unsigned int width,                 // Width of image
    unsigned int height                 // Height of image
) const {
    afwGeom::Point2I const cen(afwImage::positionToIndex(getXCenter()),
                               afwImage::positionToIndex(getYCenter()));
    afwGeom::Point2I const llc(cen[0] - width/2 - _parentExposure->getX0(), 
                               cen[1] - height/2 - _parentExposure->getY0());
    
    afwGeom::BoxI bbox(llc, afwGeom::ExtentI(width, height));
        
    PTR(MaskedImageT) image;
    try {
        MaskedImageT mimg = _parentExposure->getMaskedImage();
        image.reset(new MaskedImageT(mimg, bbox, afwImage::LOCAL, true)); // a deep copy
    } catch(lsst::pex::exceptions::LengthErrorException &e) {
        LSST_EXCEPT_ADD(e, "Extracting image of PSF candidate");
        throw e;
    }

    /*
     * Set the INTRP bit for any DETECTED pixels other than the one in the center of the object;
     * we grow the Footprint a bit first
     */
    typedef afwDetection::FootprintSet::FootprintList FootprintList;

    afwImage::MaskPixel const detected = MaskedImageT::Mask::getPlaneBitMask("DETECTED");
    PTR(afwImage::Image<int>) mim = makeImageFromMask<int>(*image->getMask(), makeAndMask(detected));
    PTR(afwDetection::FootprintSet) fs =
        boost::make_shared<afwDetection::FootprintSet>(*mim, afwDetection::Threshold(1));
    CONST_PTR(FootprintList) feet = fs->getFootprints();

    if (feet->size() <= 1) {         // only one Footprint, presumably the one we want
        return image;
    }

    // bit to set for bad pixels
    afwImage::MaskPixel const intrp = MaskedImageT::Mask::getPlaneBitMask("INTRP");
    int const ngrow = 3;            // number of pixels to grow bad Footprints
    //
    // Go through Footprints looking for ones that don't contain cen
    //
    for (FootprintList::const_iterator fiter = feet->begin(); fiter != feet->end(); ++fiter) {
        PTR(afwDetection::Footprint) foot = *fiter;
        if (foot->contains(cen)) {
            continue;
        }
        
        PTR(afwDetection::Footprint) bigfoot = afwDetection::growFootprint(foot, ngrow);
        afwDetection::setMaskFromFootprint(image->getMask().get(), *bigfoot, intrp);
    }

    return image;
}


/**
 * Return the %image at the position of the Source, without any sub-pixel shifts to put the centre of the
 * object in the centre of a pixel (for that, use getOffsetImage())
 *
 */
template <typename PixelT>
CONST_PTR(afwImage::MaskedImage<PixelT>)
measAlg::PsfCandidate<PixelT>::getMaskedImage(int width, int height) const {


    if (_haveImage && (width != _image->getWidth() || height != _image->getHeight())) {
        _haveImage = false;
    }

    if (!_haveImage) {
        _image = extractImage(width, height);
        _haveImage = true;
    }
    
    return _image;
}

/**
 * Return the %image at the position of the Source, without any sub-pixel shifts to put the centre of the
 * object in the centre of a pixel (for that, use getOffsetImage())
 *
 */
template <typename PixelT>
CONST_PTR(afwImage::MaskedImage<PixelT>) measAlg::PsfCandidate<PixelT>::getMaskedImage() const {

    int const width = getWidth() == 0 ? _defaultWidth : getWidth();
    int const height = getHeight() == 0 ? _defaultWidth : getHeight();

    return getMaskedImage(width, height);
    
}

/**
 * @brief Return an offset version of the image of the source.
 * The returned image has been offset to put the centre of the object in the centre of a pixel.
 *
 */
template <typename PixelT>
PTR(afwImage::MaskedImage<PixelT>)
measAlg::PsfCandidate<PixelT>::getOffsetImage(
    std::string const algorithm,        // Warping algorithm to use
    unsigned int buffer                 // Buffer for warping
) const {
    unsigned int const width = getWidth() == 0 ? _defaultWidth : getWidth();
    unsigned int const height = getHeight() == 0 ? _defaultWidth : getHeight();
    if (_offsetImage && static_cast<unsigned int>(_offsetImage->getWidth()) == width + 2*buffer && 
        static_cast<unsigned int>(_offsetImage->getHeight()) == height + 2*buffer) {
        return _offsetImage;
    }

    PTR(MaskedImageT) image = extractImage(width + 2*buffer, height + 2*buffer);

    double const xcen = getXCenter(), ycen = getYCenter();
    double const dx = afwImage::positionToIndex(xcen, true).second;
    double const dy = afwImage::positionToIndex(ycen, true).second;

    PTR(MaskedImageT) offset = afwMath::offsetImage(*image, -dx, -dy, algorithm);
    afwGeom::Point2I llc(buffer, buffer);
    afwGeom::Extent2I dims(width, height);
    afwGeom::Box2I box(llc, dims);
    _offsetImage.reset(new MaskedImageT(*offset, box, afwImage::LOCAL, true)); // Deep copy

    return _offsetImage;
}




/************************************************************************************************************/
//
// Explicit instantiations
//
/// \cond
typedef float Pixel;
//template class measAlg::PsfCandidate<afwImage::MaskedImage<Pixel> >;
template class measAlg::PsfCandidate<Pixel>;
/// \endcond
