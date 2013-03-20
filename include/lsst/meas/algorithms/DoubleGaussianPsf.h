// -*- lsst-c++ -*-
/*
 * LSST Data Management System
 * Copyright 2008-2013 LSST Corporation.
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

#ifndef LSST_MEAS_ALGORITHMS_DoubleGaussianPsf_h_INCLUDED
#define LSST_MEAS_ALGORITHMS_DoubleGaussianPsf_h_INCLUDED

#include "lsst/meas/algorithms/KernelPsf.h"

#include "boost/serialization/nvp.hpp"
#include "boost/serialization/void_cast.hpp"

namespace lsst { namespace meas { namespace algorithms {

/// Represent a Psf as a circularly symmetrical double Gaussian
class DoubleGaussianPsf : public afw::table::io::PersistableFacade<DoubleGaussianPsf>, public KernelPsf {
public:

    /**
     *  Constructor for a DoubleGaussianPsf
     *
     *  @param[in] width    Number of columns in realisations of Psf
     *  @param[in] height   Number of rows in realisations of Psf
     *  @param[in] sigma1   Radius of inner Gaussian
     *  @param[in] sigma2   Radius of outer Gaussian
     *  @param[in] b        Peak-amplitude
     */
    DoubleGaussianPsf(int width, int height, double sigma1, double sigma2=0.0, double b=0.0);

    /// Polymorphic deep copy.  Usually unnecessary, as Psfs are immutable.
    virtual PTR(afw::detection::Psf) clone() const;

    /// Return the radius of the inner Gaussian.
    double getSigma1() const { return _sigma1; }

    /// Return the radius of the outer Gaussian.
    double getSigma2() const { return _sigma2; }

    /// Return the peak-amplitude ratio of the outer Gaussian to the inner Gaussian.
    double getB() const { return _b; }

    /// Whether this Psf is persistable (always true for DoubleGaussianPsf).
    virtual bool isPersistable() const { return true; }

protected:

    virtual std::string getPersistenceName() const;

    virtual void write(OutputArchiveHandle & handle) const;

private:
    double _sigma1;
    double _sigma2;
    double _b;

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive&, unsigned int const) {
        boost::serialization::void_cast_register<DoubleGaussianPsf, Psf>(
            static_cast<DoubleGaussianPsf*>(0), static_cast<Psf*>(0)
        );
    }
};

}}} // namespace lsst::meas::algorithms

namespace boost { namespace serialization {

template <class Archive>
inline void save_construct_data(
    Archive& ar, lsst::meas::algorithms::DoubleGaussianPsf const* p,
    unsigned int const
) {
    int width = p->getKernel()->getWidth();
    int height = p->getKernel()->getHeight();
    double sigma1 = p->getSigma1();
    double sigma2 = p->getSigma2();
    double b = p->getB();
    ar << make_nvp("width", width);
    ar << make_nvp("height", height);
    ar << make_nvp("sigma1", sigma1);
    ar << make_nvp("sigma2", sigma2);
    ar << make_nvp("b", b);
}

template <class Archive>
inline void load_construct_data(
        Archive& ar, lsst::meas::algorithms::DoubleGaussianPsf* p,
        unsigned int const
) {
    int width;
    int height;
    double sigma1;
    double sigma2;
    double b;
    ar >> make_nvp("width", width);
    ar >> make_nvp("height", height);
    ar >> make_nvp("sigma1", sigma1);
    ar >> make_nvp("sigma2", sigma2);
    ar >> make_nvp("b", b);
    ::new(p) lsst::meas::algorithms::DoubleGaussianPsf(width, height, sigma1, sigma2, b);
}

}} // namespace boost::serialization

#endif // !LSST_MEAS_ALGORITHMS_DoubleGaussianPsf_h_INCLUDED
