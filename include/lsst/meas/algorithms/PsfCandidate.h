// -*- LSST-C++ -*-
#if !defined(LSST_MEAS_ALGORITHMS_PSFCANDIDATE_H)
#define LSST_MEAS_ALGORITHMS_PSFCANDIDATE_H

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
 
/**
 * @file
 *
 * @brief Class used by SpatialCell for spatial PSF fittig
 *
 * @ingroup algorithms
 */
#include <vector>

#include "boost/make_shared.hpp"

#include "lsst/pex/policy.h"

#include "lsst/afw/image/Exposure.h"
#include "lsst/afw/detection/Psf.h"
#include "lsst/afw/detection/FootprintSet.h"
#include "lsst/afw/table/Source.h"
#include "lsst/afw/math/SpatialCell.h"

namespace lsst {
namespace meas {
namespace algorithms {
    /** 
     * @brief Class stored in SpatialCells for spatial Psf fitting
     * 
     * PsfCandidate is a detection that may turn out to be a PSF. We'll
     * assign them to sets of SpatialCells; these sets will then be used to fit
     * a spatial model to the PSF.
     */
    template <typename PixelT>
    class PsfCandidate : public lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT> {
        using lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT>::_image;
    public:
        using lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT>::getXCenter;
        using lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT>::getYCenter;
        using lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT>::getWidth;
        using lsst::afw::math::SpatialCellMaskedImageCandidate<PixelT>::getHeight;
    
        typedef boost::shared_ptr<PsfCandidate<PixelT> > Ptr;
        typedef boost::shared_ptr<const PsfCandidate<PixelT> > ConstPtr;
        typedef std::vector<Ptr > PtrList;

        typedef lsst::afw::image::MaskedImage<PixelT> MaskedImageT;

        /**
         * Construct a PsfCandidate from a specified source and image.
         *
         * The x/yCenter is set to source.getX/YAstrom()
         */
        PsfCandidate(
            PTR(afw::table::SourceRecord) const& source, ///< The detected Source
            CONST_PTR(afw::image::Exposure<PixelT>) parentExposure ///< The image wherein lie the Sources
        ) :
            afw::math::SpatialCellMaskedImageCandidate<PixelT>(source->getX(), source->getY()),
            _parentExposure(parentExposure),
            _offsetImage(),
            _source(source),
            _haveImage(false),
            _amplitude(0.0), _var(1.0)
        {}
        
        /**
         * Construct a PsfCandidate from a specified source, image and xyCenter.
         */
        PsfCandidate(
            PTR(afw::table::SourceRecord) const& source, ///< The detected Source
            CONST_PTR(afw::image::Exposure<PixelT>) parentExposure, ///< The image wherein lie the Sources
            double xCenter,    ///< the desired x center
            double yCenter     ///< the desired y center
        ) :
            afw::math::SpatialCellMaskedImageCandidate<PixelT>(xCenter, yCenter),
            _parentExposure(parentExposure),
            _offsetImage(),
            _source(source),
            _haveImage(false),
            _amplitude(0.0), _var(1.0)
        {}
        
        /// Destructor
        virtual ~PsfCandidate() {};
        
        /**
         * Return Cell rating
         * 
         * @note Required method for use by SpatialCell
         */
        double getCandidateRating() const { return _source->getPsfFlux(); }
        
        /// Return the original Source
        PTR(afw::table::SourceRecord) getSource() const { return _source; }
        
        /// Return the best-fit amplitude
        double getAmplitude() const { return _amplitude; }
    
        /// Set the best-fit amplitude
        void setAmplitude(double amplitude) { _amplitude = amplitude; }

        /// Return the variance in use when fitting this object
        double getVar() const { return _var; }
    
        /// Set the variance to use when fitting this object
        void setVar(double var) { _var = var; }
    
        CONST_PTR(afw::image::MaskedImage<PixelT>) getMaskedImage() const;
        CONST_PTR(afw::image::MaskedImage<PixelT>) getMaskedImage(int width, int height) const;
        PTR(afw::image::MaskedImage<PixelT>) getOffsetImage(std::string const algorithm,
                                                            unsigned int buffer) const;

        /// Return the number of pixels being ignored around the candidate image's edge
        static int getBorderWidth() { return _border; }
    
        /// Set the number of pixels to ignore around the candidate image's edge
        static void setBorderWidth(int border) { _border = border; }

    private:
        CONST_PTR(lsst::afw::image::Exposure<PixelT>) _parentExposure; // the %image that the Sources are found in
        
        PTR(afw::image::MaskedImage<PixelT>)
        offsetImage(
            PTR(afw::image::MaskedImage<PixelT>) img,
            std::string const algorithm,
            unsigned int buffer
        );
        
        PTR(afw::image::MaskedImage<PixelT>)
        extractImage(unsigned int width, unsigned int height) const;

        PTR(afw::image::MaskedImage<PixelT>) mutable _offsetImage; // %image offset to put center on a pixel
        PTR(afw::table::SourceRecord) _source; // the Source itself

        bool mutable _haveImage;                    // do we have an Image to return?
        double _amplitude;                          // best-fit amplitude of current PSF model
        double _var;                                // variance to use when fitting this candidate
        static int _border;                         // width of border of ignored pixels around _image
        afw::geom::Point2D _xyCenter;
        static int _defaultWidth;
    };
    
    /**
     * Return a PsfCandidate of the right sort
     *
     * Cf. std::make_pair
     */
    template <typename PixelT>
    boost::shared_ptr<PsfCandidate<PixelT> >
    makePsfCandidate(PTR(afw::table::SourceRecord) const& source, ///< The detected Source
                     PTR(afw::image::Exposure<PixelT>) image    ///< The image wherein lies the object
                    )
    {
        return boost::make_shared< PsfCandidate<PixelT> >(source, image);
    }
   
}}}

#endif
