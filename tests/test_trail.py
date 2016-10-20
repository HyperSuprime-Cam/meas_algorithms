from __future__ import print_function, division

import numpy as np

import unittest
import lsst.utils.tests

import lsst.afw.display
import lsst.afw.image
import lsst.afw.detection
import lsst.afw.geom

import lsst.meas.algorithms

from lsst.pipe.base import Struct
from lsst.pex.config import Config, Field


class Trail(Struct):
    def __init__(self, peak, width, slope, intercept):
        Struct.__init__(self, peak=peak, width=width, slope=slope, intercept=intercept)

    def func(self, distance):
        raise NotImplementedError("Subclasses should define")

    def makeImage(self, width, height):
        xx, yy = np.meshgrid(np.arange(width), np.arange(height))
        distance = (self.slope*xx - yy + self.intercept)/np.hypot(self.slope, -1.0)
        return self.peak*self.func(distance)

class Satellite(Trail):
    def func(self, distance):
        return np.exp(-0.5*distance**2/self.width**2)

class WingedSatellite(Satellite):
    def func(self, distance):
        return Satellite.func(distance) + 0.1*self.peak*np.exp(-0.5*distance**2/(2.0*self.width)**2)

class Aircraft(Trail):
    def func(self, distance):
        return np.where(np.abs(distance) < self.width, 1.0, 0.0)


class CheckConfig(Config):
    width = Field(dtype=int, default=2048, doc="Width of image")
    height = Field(dtype=int, default=2048, doc="Height of image")
    x0 = Field(dtype=int, default=12345, doc="x offset for image")
    y0 = Field(dtype=int, default=6789, doc="y offset for image")
    psfSigma = Field(dtype=float, default=4.321, doc="Gaussian sigma for PSF")
    noise = Field(dtype=float, default=10.0, doc="Noise to add to image")
    seed = Field(dtype=int, default=987654321, doc="Seed for RNG")
    meanFactor = Field(dtype=float, default=0.01, doc="Factor of noise for threshold on cleaned image mean")
    stdFactor = Field(dtype=float, default=1.1, doc="Factor of noise for threshold on cleaned image stdev")

class TrailTestCase(lsst.utils.tests.TestCase):
    """A test case for trail detection"""

#    @lsst.utils.tests.debugger(Exception)
    def checkTrails(self, trailList, config=None, check=None):
        if config is None:
            config = lsst.meas.algorithms.TrailConfig()
        if check is None:
            check = CheckConfig()

        exposure = lsst.afw.image.ExposureF(check.width, check.height)
        exposure.setXY0(lsst.afw.geom.PointI(check.x0, check.y0))
        psfSize = 2*int(5.0*check.psfSigma) + 1
        exposure.setPsf(lsst.afw.detection.GaussianPsf(psfSize, psfSize, check.psfSigma))
        image = exposure.getMaskedImage().getImage().getArray()
        rng = np.random.RandomState(check.seed)
        image[:] = check.noise*rng.normal(size=image.shape)
        exposure.getMaskedImage().getVariance().set(check.noise**2)

        for trail in trailList:
            image += trail.makeImage(check.width, check.height)

        task = lsst.meas.algorithms.TrailTask(config=config, name="trail")
        found = task.run(exposure)

        mask = exposure.getMaskedImage().getMask()
        maskVal = mask.getPlaneBitMask("TRAIL")
        clean = mask.getArray() & maskVal == 0
        self.assertLess(np.abs(image[clean].mean()), check.meanFactor*check.noise)
        self.assertLess(image[clean].std(), check.stdFactor*check.noise)

    def testSatellite(self):
        check = CheckConfig()
        trails = [
            Satellite(100.0, check.psfSigma, 1.2345, -123.45),
            Satellite(30.0, check.psfSigma, -1.2345, 2345.67),
            Satellite(10.0, check.psfSigma, 0.12345, 432.1),
            Satellite(5.0, check.psfSigma, 5.4321, -2345.67),
        ]

        self.checkTrails(trails)


    def testAircraft(self):
        trails = [
            Aircraft(300.0, 32.1, 1.02030, 123.45),
            Aircraft(100.0, 32.1, -1.2345, 2345.67),
        ]

        config = lsst.meas.algorithms.TrailConfig()
        config.widths = [40.0, 70.0, 100.0]
        config.sigmaSmooth = 2.0
        config.kernelSigma = 9.0
        config.kernelWidth = 15
        config.bins = 8
        config.maxTrailWidth = 50.0

        self.checkTrails(trails, config)

class TestMemory(lsst.utils.tests.MemoryTestCase):
    pass


def setup_module(module, backend="virtualDevice"):
    lsst.utils.tests.init()
    try:
        lsst.afw.display.setDefaultBackend(backend)
    except:
        print("Unable to configure display backend: %s" % backend)


if __name__ == "__main__":
    import sys

    from argparse import ArgumentParser
    parser = ArgumentParser()
    parser.add_argument('--backend', type=str, default="virtualDevice",
                        help="The backend to use, e.g. 'ds9'. Be sure to 'setup display_<backend>'")
    parser.add_argument('unittest', nargs='*')
    args = parser.parse_args()
    sys.argv[1:] = args.unittest

    setup_module(sys.modules[__name__], backend=args.backend)
    unittest.main()
