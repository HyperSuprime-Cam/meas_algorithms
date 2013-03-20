#!/usr/bin/env python

# 
# LSST Data Management System
# Copyright 2008, 2009, 2010 LSST Corporation.
# 
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the LSST License Statement and 
# the GNU General Public License along with this program.  If not, 
# see <http://www.lsstcorp.org/LegalNotices/>.
#

"""
Tests for bad pixel interpolation code

Run with:
   python Interp.py
or
   python
   >>> import Interp; Interp.run()
"""

import os
from math import *
import unittest
import eups
import math, numpy
import lsst.utils.tests as tests
import lsst.pex.logging as logging
import lsst.afw.detection as afwDetection
import lsst.afw.image as afwImage
import lsst.afw.geom as afwGeom
import lsst.afw.display.ds9 as ds9
import lsst.meas.algorithms as algorithms
import lsst.meas.algorithms.defects as defects

try:
    type(verbose)
except NameError:
    verbose = 0
    logging.Trace_setVerbosity("algorithms.Interp", verbose)

try:
    type(display)
except NameError:
    display = False

class interpolationTestCase(unittest.TestCase):
    """A test case for interpolation"""
    def setUp(self):
        self.FWHM = 5
        self.psf = algorithms.DoubleGaussianPsf(15, 15, self.FWHM/(2*sqrt(2*log(2))))
        maskedImageFile = os.path.join(eups.productDir("afwdata"), "CFHT", "D4", "cal-53535-i-797722_1.fits")
            
        self.mi = afwImage.MaskedImageF(maskedImageFile)
        if False:                       # use sub-image?
            self.mi = self.mi.Factory(self.mi, afwImage.BBox(afwImage.PointI(760, 20), 256, 256))
        self.mi.getMask().addMaskPlane("INTERP")

        self.badPixels = defects.policyToBadRegionList(os.path.join(eups.productDir("meas_algorithms"),
                                                                    "policy", "BadPixels.paf"))

    def tearDown(self):
        del self.mi
        del self.psf
        del self.badPixels

    def XXXtestDetection(self):
        """Test Interp algorithms"""

        if display:
            frame = 0
            ds9.mtv(self.mi, frame=frame, title="Original")

        algorithms.interpolateOverDefects(self.mi, self.psf, self.badPixels)

        if display:
            ds9.mtv(self.mi, frame = frame + 1, title="Interpolated")
            ds9.mtv(self.mi.getVariance(), frame = frame + 2, title="Variance")

    def XXXtest818(self):
        """A test case for #818; the full test is in /lsst/DC3root/ticketFiles/818"""

        badPixels = algorithms.DefectListT()
        defects = [((82, 663), 6, 8),
                   ((83, 659), 9, 6),
                   ((85, 660), 10, 11),
                   ((87, 669), 3, 3),
                   ]

        for xy0, width, height in defects:
            x0, y0 = xy0
            bbox = afwImage.BBox(afwImage.PointI(x0, y0), width, height)
            badPixels.push_back(algorithms.Defect(bbox))

        mi = afwImage.MaskedImageF(517, 800)

        algorithms.interpolateOverDefects(mi, self.psf, badPixels)

    def test1295(self):
        """A test case for #1295 (failure to interpolate over groups of defects"""

        im = afwImage.ImageF(afwGeom.ExtentI(100, 100))
        mi = afwImage.makeMaskedImage(im)
        mi.set(100)
        flat = afwImage.ImageF(im.getDimensions())
        flat.set(1)
        for i in range(100):
            for j in range(100):
                if i == 50 or i == 55 or i == 58:
                    flat.set(i,j,0)
                if i < 60 and i > 50 and j > 50:
                    flat.set(i,j,0)

        mi /= flat

        if display:
            ds9.mtv(mi, frame=0, title="Raw")

        defectList = algorithms.DefectListT()
        bbox = afwGeom.BoxI(afwGeom.PointI(50,0), afwGeom.ExtentI(1,100))
        defectList.append(algorithms.Defect(bbox))
        bbox = afwGeom.BoxI(afwGeom.PointI(55,0), afwGeom.ExtentI(1,100))
        defectList.append(algorithms.Defect(bbox))
        bbox = afwGeom.BoxI(afwGeom.PointI(58,0), afwGeom.ExtentI(1,100))
        defectList.append(algorithms.Defect(bbox))
        bbox = afwGeom.BoxI(afwGeom.PointI(51,51), afwGeom.ExtentI(9,49))
        defectList.append(algorithms.Defect(bbox))

        psf = algorithms.DoubleGaussianPsf(15, 15, 1./(2*math.sqrt(2*math.log(2))))
        algorithms.interpolateOverDefects(mi, psf, defectList, 50.)
        
        if display:
            ds9.mtv(mi, frame=1, title="Interpolated")

        self.assertTrue(numpy.isfinite(mi.getImage().get(56, 51)))

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

def suite():
    """Returns a suite containing all the test cases in this module."""
    tests.init()

    suites = []
    if eups.productDir("afwdata"):
        suites += unittest.makeSuite(interpolationTestCase)
    else:
        print "Skipping interpolation test case as afwdata isn't set up"
    suites += unittest.makeSuite(tests.MemoryTestCase)
    return unittest.TestSuite(suites)


def run(exit = False):
    """Run the tests"""
    tests.run(suite(), exit)

if __name__ == "__main__":
    run(True)
