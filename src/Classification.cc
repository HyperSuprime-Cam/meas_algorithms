// -*- lsst-c++ -*-

#include "boost/make_shared.hpp"

#include "lsst/meas/algorithms/Classification.h"

namespace lsst { namespace meas { namespace algorithms {

namespace {

class ClassificationAlgorithm : public Algorithm {
public:

    ClassificationAlgorithm(ClassificationControl const & ctrl, afw::table::Schema & schema);

public:

    template <typename PixelT>
    void _apply(
        afw::table::SourceRecord & source,
        afw::image::Exposure<PixelT> const & exposure,
        afw::geom::Point2D const & center
    ) const;

private:
    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE(ClassificationAlgorithm);

    afw::table::Key<double> _key;
};

ClassificationAlgorithm::ClassificationAlgorithm(
    ClassificationControl const & ctrl, afw::table::Schema & schema
) :
    Algorithm(ctrl),
    _key(schema.addField<double>(ctrl.name, "probability of being extended"))
{}

template <typename PixelT>
void ClassificationAlgorithm::_apply(
    afw::table::SourceRecord & source,
    afw::image::Exposure<PixelT> const & exposure,
    afw::geom::Point2D const & center
) const {
    ClassificationControl const & ctrl = static_cast<ClassificationControl const &>(getControl());
    source[_key] = (ctrl.fluxRatio*source.getModelFlux() + ctrl.modelErrFactor*source.getModelFluxErr())
        < (source.getPsfFlux() + ctrl.psfErrFactor*source.getPsfFluxErr()) ? 0.0 : 1.0;
}

LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION(ClassificationAlgorithm);

} // anonymous

PTR(AlgorithmControl) ClassificationControl::_clone() const {
    return boost::make_shared<ClassificationControl>(*this);
}

PTR(Algorithm) ClassificationControl::_makeAlgorithm(
    afw::table::Schema & schema,
    PTR(daf::base::PropertyList) const & metadata
) const {
    return boost::make_shared<ClassificationAlgorithm>(*this, boost::ref(schema));
}

}}} // namespace lsst::meas::algorithms
