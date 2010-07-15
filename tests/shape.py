#!/usr/bin/env python
import re, os, sys
import glob
import math
import unittest

import lsst.pex.policy as pexPolicy
import lsst.pex.exceptions as pexExceptions
import lsst.afw.image as afwImage
import lsst.afw.math as afwMath
import lsst.meas.algorithms as algorithms
import lsst.utils.tests as utilsTests
import lsst.afw.detection as afwDetection

import lsst.afw.display.ds9 as ds9

try:
    type(verbose)
except NameError:
    verbose = 0
    display = False

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

class ShapeTestCase(unittest.TestCase):
    """A test case for centroiding"""

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def testCleanup(self):
        """Test that tearDown does"""
        pass

    def testInvalidmeasureShape(self):
        """Test that we cannot instantiate an unknown measureShape"""

        def getInvalid():
            shapeFinder = algorithms.makeMeasureShape(None)
            shapeFinder.addAlgorithm("XXX")

        try:
            utilsTests.assertRaisesLsstCpp(self, pexExceptions.NotFoundException, getInvalid)
        except Exception, e:
            print >> sys.stderr, "Failed to convert pexExceptions.NotFoundException; proceeding"
        else:
            self.assertEqual(e, "")

    def do_testmeasureShape(self, algorithmName):
        """Test that we can instantiate and play with a measureShape"""

        im = afwImage.ImageF(100, 100)

        #-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        bkgd = 100; im.set(bkgd)
        x, y = 30, 40
        im.set(x, y, 1000 + bkgd)

        #
        # Add a Gaussian to the image
        #
        sigma_xx, sigma_xy, sigma_yy, ksize = math.pow(1.5, 2), 0, math.pow(2.5, 2), 15

        if True:
            k = afwMath.AnalyticKernel(ksize, ksize,
                                       afwMath.GaussianFunction2D(math.sqrt(sigma_xx), math.sqrt(sigma_yy)))
            cim = im.Factory(im.getDimensions())
            afwMath.convolve(cim, im, k, True)
            im = cim
        else:
            for dx in range(-ksize/2, ksize/2 + 1):
                for dy in range(-ksize/2, ksize/2 + 1):
                    I = 1000*math.exp(-0.5*(dx*dx/sigma_xx + dy*dy/sigma_yy))
                    im.set(x + dx, y + dy, bkgd + I)

        msk = afwImage.MaskU(im.getDimensions()); msk.set(0)
        var = afwImage.ImageF(im.getDimensions()); var.set(10)
        im = afwImage.MaskedImageF(im, msk, var)
        del msk; del var

        shapeFinder = algorithms.makeMeasureShape(afwImage.makeExposure(im))
        shapeFinder.addAlgorithm(algorithmName)
        shapeFinder.configure(pexPolicy.Policy(pexPolicy.PolicyString("SDSS.background: %f" % bkgd)))
            
        if display:
            ds9.mtv(im)

        s = shapeFinder.measure(afwDetection.Peak(x, y)).find(algorithmName)
        self.assertAlmostEqual(x, s.getX(), 6)
        self.assertAlmostEqual(y, s.getY(), 6)

        if False:
            print "I_xx:  %.5f %.5f" % (s.getIxx(), sigma_xx)
            print "I_xy:  %.5f %.5f" % (s.getIxy(), sigma_xy)
            print "I_yy:  %.5f %.5f" % (s.getIyy(), sigma_yy)
        self.assertTrue(abs(s.getIxx() - sigma_xx) < 1e-3*(1 + sigma_xx))
        self.assertTrue(abs(s.getIxy() - sigma_xy) < 1e-3*(1 + sigma_xy))
        self.assertTrue(abs(s.getIyy() - sigma_yy) < 1e-3*(1 + sigma_yy))

    def testSDSSmeasureShape(self):
        """Test that we can instantiate and play with SDSSmeasureShape"""

        self.do_testmeasureShape("SDSS")

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

def suite():
    """Returns a suite containing all the test cases in this module."""
    utilsTests.init()

    suites = []
    suites += unittest.makeSuite(ShapeTestCase)
    suites += unittest.makeSuite(utilsTests.MemoryTestCase)

    return unittest.TestSuite(suites)

def run(exit = False):
    """Run the tests"""
    utilsTests.run(suite(), exit)
 
if __name__ == "__main__":
    run(True)
