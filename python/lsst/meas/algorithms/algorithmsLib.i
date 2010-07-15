// -*- lsst-c++ -*-
%define meas_algorithmsLib_DOCSTRING
"
Python bindings for meas/algorithms module
"
%enddef

%feature("autodoc", "1");
%module(package="lsst.meas.algorithms",docstring=meas_algorithmsLib_DOCSTRING) algorithmsLib

// Suppress swig complaints
// I had trouble getting %warnfilter to work; hence the pragmas
#pragma SWIG nowarn=362                 // operator=  ignored

%{
#   include <exception>
#   include <list>
#   include <map>
#   include <boost/cstdint.hpp>
#   include <boost/shared_ptr.hpp>
#   include "lsst/pex/logging/Log.h"
#   include "lsst/pex/logging/ScreenLog.h"
#   include "lsst/pex/logging/DualLog.h"
#   include "lsst/pex/logging/Debug.h"
#   include "lsst/afw.h"
#   include "lsst/afw/detection/Psf.h"
#   include "lsst/meas/algorithms/CR.h"
#   include "lsst/meas/algorithms/Interp.h"
#   include "lsst/meas/algorithms/PSF.h"
#   include "lsst/meas/algorithms/SpatialModelPsf.h"
#   include "lsst/meas/algorithms/Measure.h"
%}

%inline %{
namespace boost { namespace filesystem { } }
namespace lsst { namespace afw {
        namespace detection { }
        namespace image { }
        namespace math { }
} }
namespace lsst { namespace meas { namespace algorithms { namespace interp {} } } }
namespace lsst { namespace daf { namespace data { } } }
    
using namespace lsst;
using namespace lsst::afw::image;
using namespace lsst::afw::detection;
using namespace lsst::meas::algorithms;
using namespace lsst::meas::algorithms::interp;
using namespace lsst::daf::data;
%}

%ignore boost::noncopyable;
namespace boost {
    class noncopyable {};
}

%init %{
%}

%include "lsst/p_lsstSwig.i"
%include "lsst/base.h"                  // PTR(); should be in p_lsstSwig.i
%include "lsst/daf/base/persistenceMacros.i"

%lsst_exceptions();

%import "lsst/daf/data/dataLib.i"
%import "lsst/afw/image/imageLib.i"
%import "lsst/afw/detection/detectionLib.i"
%import "lsst/afw/math/mathLib.i"

%include "lsst/afw/image/lsstImageTypes.i"     // Image/Mask types and typedefs

%pythoncode %{
def version(HeadURL = r"$HeadURL$"):
    """Return a version given a HeadURL string; default: afw's version"""
    return guessSvnVersion(HeadURL)
%}

/************************************************************************************************************/

%include "psf.i"
%include "lsst/meas/algorithms/CR.h"

/************************************************************************************************************/

SWIG_SHARED_PTR(DefectPtrT, lsst::meas::algorithms::Defect);
SWIG_SHARED_PTR(DefectListT,  std::vector<lsst::meas::algorithms::Defect::Ptr>);

%include "lsst/meas/algorithms/Interp.h"

/************************************************************************************************************/
//
// We need this macro so as to avoid having commas in the 2nd argument to SWIG_SHARED_PTR_DERIVED,
// which confuses the swig parser.
//
%define %Exposure(PIXTYPE)
    lsst::afw::image::Exposure<PIXTYPE, lsst::afw::image::MaskPixel, lsst::afw::image::VariancePixel>
%enddef

%define %MeasureQuantity(ALGORITHM, PIXTYPE)
    lsst::afw::detection::MeasureQuantity<lsst::afw::detection::ALGORITHM,
                                          %Exposure(PIXTYPE), lsst::afw::detection::Peak>
%enddef

%define %MeasureQuantityAstrometry(PIXTYPE)
    %MeasureQuantity(Astrometry, PIXTYPE)
%enddef
%define %MeasureQuantityPhotometry(PIXTYPE)
    %MeasureQuantity(Photometry, PIXTYPE)
%enddef
%define %MeasureQuantityShape(PIXTYPE)
    %MeasureQuantity(Shape, PIXTYPE)
%enddef

%define %MeasureQuantityPtrs(SUFFIX, ALGORITHM, PIXTYPE)
    SWIG_SHARED_PTR(MeasureQuantity##ALGORITHM##SUFFIX, %MeasureQuantity##ALGORITHM(PIXTYPE));
    SWIG_SHARED_PTR_DERIVED(Measure##ALGORITHM##SUFFIX,
                            %MeasureQuantity##ALGORITHM(PIXTYPE),
                            lsst::meas::algorithms::Measure##ALGORITHM<%Exposure(PIXTYPE)>
        )
%enddef

%define %MeasureSources(SUFFIX, PIXTYPE)
    SWIG_SHARED_PTR(MeasureSources##SUFFIX,
                    lsst::meas::algorithms::MeasureSources<%Exposure(PIXTYPE)>);

    %MeasureQuantityPtrs(SUFFIX, Astrometry, PIXTYPE);
    %MeasureQuantityPtrs(SUFFIX, Photometry, PIXTYPE);
    %MeasureQuantityPtrs(SUFFIX, Shape, PIXTYPE);
%enddef

%MeasureSources(F, float);
%MeasureSources(I, int);

%include "lsst/meas/algorithms/Measure.h"

/************************************************************************************************************/
/*
 * Now %template declarations
 */
%define %MeasureAlgorithm(SUFFIX, ALGORITHM, PIXTYPE)
    %template(MeasureQuantity##ALGORITHM##SUFFIX) %MeasureQuantity##ALGORITHM(PIXTYPE);
    %template(Measure##ALGORITHM##SUFFIX) lsst::meas::algorithms::Measure##ALGORITHM<%Exposure(PIXTYPE)>;
    %template(makeMeasure##ALGORITHM) lsst::meas::algorithms::makeMeasure##ALGORITHM<%Exposure(PIXTYPE)>;
%enddef

%define %instantiate_templates(SUFFIX, PIXTYPE, UTILITIES)
#if UTILITIES
    %template(findCosmicRays) findCosmicRays<lsst::afw::image::MaskedImage<PIXTYPE,
                                                                           lsst::afw::image::MaskPixel> >;
    %template(interpolateOverDefects) interpolateOverDefects<lsst::afw::image::MaskedImage<PIXTYPE> >;
#endif

    %template(MeasureSources##SUFFIX) lsst::meas::algorithms::MeasureSources<%Exposure(PIXTYPE)>;
    %template(makeMeasureSources) lsst::meas::algorithms::makeMeasureSources<%Exposure(PIXTYPE)>;

    %MeasureAlgorithm(SUFFIX, Astrometry, PIXTYPE);
    %MeasureAlgorithm(SUFFIX, Photometry, PIXTYPE);
    %MeasureAlgorithm(SUFFIX, Shape, PIXTYPE);
%enddef

%instantiate_templates(F, float, 1)
%instantiate_templates(I, int, 0)

%template(DefectListT) std::vector<lsst::meas::algorithms::Defect::Ptr>;

/************************************************************************************************************/

%template(xyAndError) std::pair<double, double>;

/******************************************************************************/
// Local Variables: ***
// eval: (setq indent-tabs-mode nil) ***
// End: ***
