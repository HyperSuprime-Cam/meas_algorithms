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
 
#if !defined(LSST_MEAS_ALGORITHMS_ALGORITHM_H)
#define LSST_MEAS_ALGORITHMS_ALGORITHM_H

#include <map>

#include "boost/noncopyable.hpp"
#include "boost/make_shared.hpp"
#include "boost/static_assert.hpp"

#include "lsst/base.h"
#include "lsst/daf/base/PropertyList.h"
#include "lsst/pex/logging/Log.h"
#include "lsst/pex/config.h"
#include "lsst/pex/policy.h"
#include "lsst/pex/exceptions.h"
#include "lsst/afw/table/Source.h"
#include "lsst/afw/image/Exposure.h"

#define LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE_PIXEL(CLASS, PIXEL)        \
    virtual void _applyT(lsst::afw::table::SourceRecord &,              \
                         lsst::afw::image::Exposure< PIXEL > const &,  \
                         lsst::afw::geom::Point2D const & center) const

#define LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION_PIXEL(CLASS, PIXEL)  \
    void CLASS::_applyT(lsst::afw::table::SourceRecord & source,             \
                        lsst::afw::image::Exposure< PIXEL > const & exposure, \
                        lsst::afw::geom::Point2D const & center) const { \
        this->_apply(source, exposure, center);                         \
    }

/**
 *  @brief Declare Algorithm::_applyT virtual function overloads with the correct types.
 *
 *  Should be used in the private or protected section of an Algorithm derived class declaration.
 */
#define LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE(CLASS)    \
    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE_PIXEL(CLASS, float);  \
    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE_PIXEL(CLASS, double)

/**
 *  @brief Implement Algorithm::_applyT virtual function overloads with the correct types.
 *
 *  Should be used at namespace scope in the source file that contains the _apply implementation.
 *  This will automatically instantiate the necessary _apply templates and any other templates it
 *  relies on.
 */
#define LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION(CLASS)       \
    LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION_PIXEL(CLASS, float);      \
    LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION_PIXEL(CLASS, double)

namespace lsst { namespace meas { namespace algorithms {

class Algorithm;

class AlgorithmControl;

typedef std::map<std::string,PTR(Algorithm const)> AlgorithmMap;

/**
 *  @brief Base class for source measurement algorithms.
 *
 *  Algorithm simulates template virtual functions, with the following mechanism:
 *   - Users call the public, templated, non-virtual "apply" member function, which is defined
 *     only in the base class.
 *   - "apply" delegates to the overloaded, non-templated, virtual "_applyT" member functions.
 *   - "_applyT" delegates to the private, templated, non-virtual "_apply" member function,
 *     which must be defined by the derived class.
 *
 *  Algorithm subclasses will generally declare and define "_applyT" using the macro
 *  LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE (in the private or protected section of the class
 *  declaration).  Similarly, LSST_MEAS_ALGORITHM_PRIVATE_IMPLEMENTATION (at namespace scope
 *  in the same source file that contains the implementation of "_apply") will ensure that
 *  the needed versions of "_apply" are instantiated.
 *
 *  Algorithms should generally be immutable; this will prevent letting Python have access
 *  to CONST_PTR(Algorithm) objects from causing problems.
 *
 *  Most algorithms will have a constructor that takes a control object, a non-const
 *  reference to an afw::table::Schema, and a PTR(daf::base::PropertyList).  This is
 *  effectively enforced by the signature of AlgorithmControl::makeAlgorithm.
 */
class Algorithm {
public:

    explicit Algorithm(AlgorithmControl const & ctrl);

    virtual ~Algorithm() {}

    /**
     *  @brief Return a clone of the control object used to construct the algorithm.
     *
     *  The returned reference can be considered completely immutable, and should never be changed.
     *  Subclasses that reimplement to cast to a derived type should use %returnCopy (in p_lsstSwig.i)
     *  to prevent dangling references and keep Python from const-casting the result.
     */
    AlgorithmControl const & getControl() const { return *_ctrl; }

    /**
     *  @brief Run the algorithm, filling appropriate fields in the given source.
     *
     *  This is the public interface to the algorithm; it delegates to virtual functions
     *  that are overloaded for all the allowed template types.  These in turn delegate
     *  the templated _apply function.
     */
    template <typename PixelT>
    void apply(
        afw::table::SourceRecord & source,
        afw::image::Exposure<PixelT> const & exposure,
        afw::geom::Point2D const & center
    ) const {
        this->_applyT(source, exposure, center);
    }

protected:

    /// @brief Simulated virtual function that all algorithms must implement.
    template <typename PixelT>
    void _apply(
        afw::table::SourceRecord & source,
        afw::image::Exposure<PixelT> const & exposure, 
        afw::geom::Point2D const & center
    ) const {
        // This declaration is exposition only; subclasses must implement to allow the 
        // LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE macro to do its work.
        BOOST_STATIC_ASSERT(sizeof(PixelT) < 0);
    }

private:

    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE_PIXEL(CLASS, float) = 0;
    LSST_MEAS_ALGORITHM_PRIVATE_INTERFACE_PIXEL(CLASS, double) = 0;

    CONST_PTR(AlgorithmControl) _ctrl;
};

/**
 *  @brief Base class for measurement algorithm control objects.
 *
 *  This is a polymorphic class hierarchy because control objects are also factories
 *  for algorithms - but this is considered an implementation detail, and only matters
 *  to algorithm writers, who must implement the protected algorithm factory functions.
 *  The advantage of this approach is that we don't have to SWIG all the algorithm classes.
 */
class AlgorithmControl {
public:

    /**
     *  @brief Name of the algorithm.
     *
     *  We don't want this to be a Field in the corresponding Python Config class, because then it
     *  would be very confusing to allow it to be the different from the name in the config registry.
     *  Instead, we'll make the registry set the name on the control object when it makes it.
     */
    std::string name;

    LSST_CONTROL_FIELD(
        priority, double,
        "Parameter that sets the sort order for algorithms; lower numbers go first. "
        "Typically, priority=0 for centroids, 1 for shapes, and 2 for fluxes."
    );

    PTR(AlgorithmControl) clone() const { return _clone(); }

    /**
     *  @brief Construct a new algorithm configured with *this.
     *
     *  @param[in,out] schema    A Schema the algorithm should register its outputs with and use to
     *                           obtain the keys for any input fields for other algorithms it depends on.
     *  @param[in,out] metadata  Flexible metadata for additional descriptive information the algorithm
     *                           might want to pass onto a source table.  May be null.
     *  @param[in]     others    A map of Algorithm objects for measurement algorithms that
     *                           have already been registered with the schema.  Only algorithms with
     *                           priority < this->priority will be present.
     */
    PTR(Algorithm) makeAlgorithm(
        afw::table::Schema & schema,
        PTR(daf::base::PropertyList) const & metadata = PTR(daf::base::PropertyList)(),
        AlgorithmMap const & others = AlgorithmMap()
    ) const { return _makeAlgorithm(schema, metadata, others); }

    virtual ~AlgorithmControl() {}
    
protected:

    virtual PTR(AlgorithmControl) _clone() const = 0;

    virtual PTR(Algorithm) _makeAlgorithm(
        afw::table::Schema & schema,
        PTR(daf::base::PropertyList) const & metadata
    ) const {
        throw LSST_EXCEPT(
            pex::exceptions::LogicErrorException,
            "Algorithm subclasses must override one of the _makeAlgorithm member function overloads."
        );
    }

    virtual PTR(Algorithm) _makeAlgorithm(
        afw::table::Schema & schema,
        PTR(daf::base::PropertyList) const & metadata,
        AlgorithmMap const & others
    ) const {
        return _makeAlgorithm(schema, metadata);
    }

    explicit AlgorithmControl(std::string const & name_, double priority_) :
        name(name_), priority(priority_) {}

private:
    void operator=(AlgorithmControl const &) { assert(false); } // prevent slicing 
};

inline Algorithm::Algorithm(AlgorithmControl const & ctrl) : _ctrl(ctrl.clone()) {}

}}} // namespace lsst::meas::algorithms

#endif
