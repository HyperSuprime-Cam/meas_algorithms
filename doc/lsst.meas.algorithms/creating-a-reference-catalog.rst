.. _creating-a-reference-catalog:

#########################################
How to generate an LSST reference catalog
#########################################

The LSST Data Management Science Pipeline uses external reference catalogs to fit astrometric and photometric calibration models.
In order to use these external catalogs with our software, we have to convert them into a common format.
This page describes how to "ingest" an external catalog for use as a reference catalog for LSST.

The process for generating an LSST-style HTM indexed reference catalog is similar to that of running other LSST Tasks: write an appropriate ``Config`` and run :lsst-task:`~lsst.meas.algorithms.ingestIndexReferenceTask.IngestIndexedReferenceTask`
The differences are in how you prepare the input data, what goes into that ``Config``, how you go about running the ``Task``, and what you do with the final output.

Ingesting a large reference catalog can be a slow process.
Ingesting all of Gaia DR2 took a weekend on a high-performance workstation running with 8 parallel processes, for example.

This page uses `Gaia DR2`_ as an example.

.. _Gaia DR2: https://www.cosmos.esa.int/web/gaia/dr2

1. Gathering data
=================

:lsst-task:`~lsst.meas.algorithms.ingestIndexReferenceTask.IngestIndexedReferenceTask` reads reference catalog data from one or more text or FITS files representing an external catalog (e.g. :file:`GaiaSource*.csv.gz`).
In order to ingest these files, you must have a copy of them on a local disk.
Network storage (such as NFS and GPFS) are not recommended for this work, due to performance issues involving tens of thousands of small files.
Ensure that you have sufficient storage capacity.
For example, the GaiaSource DR2 files take 550 GB of space, and the ingested LSST reference catalog takes another 200 GB.

To write the config, you will need to have a document that describes the columns in the input data, including their units and any caveats that may apply (for example, Gaia DR2 does not supply magnitude errors).

If the files are text files of some sort, check that you can read one of them with `astropy.io.ascii.read`, which is what the ingester uses to read text files. For example:

.. code-block:: python

    import astropy.io.ascii
    data = astropy.io.ascii.read('gaia_source/GaiaSource_1000172165251650944_1000424567594791808.csv.gz', format='csv')
    print(data)

The default Config assumes that the files are readable with ``format="csv"``; you can change that to a different ``format`` if necessary (see `~lsst.meas.algorithms.ReadTextCatalogConfig` for how to configure the file reader).

2. Write a Config for the ingestion
===================================

`~lsst.meas.algorithms.IngestIndexedReferenceConfig` specifies what fields in the input files get translated to the output data, and how they are converted along the way.
See the :ref:`IngestIndexedReferenceTask configuration documentation <lsst.meas.algorithms.IngestIndexedReferenceTask-configs>` docs for the different available options.

This is an example configuration that was used to ingest the Gaia DR2 catalog:

.. code-block:: python

    # The name of the output reference catalog dataset.
    config.dataset_config.ref_dataset_name = "gaia_dr2"

    # Gen3 butler wants all of our refcats have the same indexing depth.
    config.dataset_config.indexer['HTM'].depth = 7

    # Ingest the data in parallel with this many processes.
    config.n_processes = 8

    # These define the names of the fields from the gaia_source data model:
    # https://gea.esac.esa.int/archive/documentation/GDR2/Gaia_archive/chap_datamodel/sec_dm_main_tables/ssec_dm_gaia_source.html

    config.id_name = "source_id"
    config.ra_name = "ra"
    config.dec_name = "dec"
    config.ra_err_name = "ra_error"
    config.dec_err_name = "dec_error"

    config.parallax_name = "parallax"
    config.parallax_err_name = "parallax_error"

    config.pm_ra_name = "pmra"
    config.pm_ra_err_name = "pmra_error"
    config.pm_dec_name = "pmdec"
    config.pm_dec_err_name = "pmdec_error"

    config.epoch_name = "ref_epoch"
    config.epoch_format = "jyear"
    config.epoch_scale = "tcb"

    # NOTE: these names have `_flux` appended to them when the output Schema is created,
    # while the Gaia-specific class handles the errors.
    config.mag_column_list = ["phot_g_mean", "phot_bp_mean", "phot_rp_mean"]

    config.extra_col_names = ["astrometric_excess_noise", "phot_variable_flag"]


3. Ingest the files
===================

:lsst-task:`~lsst.meas.algorithms.ingestIndexReferenceTask.IngestIndexedReferenceTask` takes three important parameters:

- The name of a Butler repository.

  This repository is only used to initialize the Butler, and doesn't have to contain any useful data.
  You can point to any repository you have available, or you could create a temporary one like this:

  .. prompt:: bash

    mkdir /path/to/my_repo
    echo "lsst.obs.test.TestMapper" > /path/to/my_repo/_mapper

- The name(s) of the input FITS or text files.
- The path to the configuration file (say, :file:`/path/to/my_config.cfg`).

The task could then be invoked from the command line as:

.. prompt:: bash

  ingestReferenceCatalog.py /path/to/my_repo input_catalog.txt --configfile /path/to/my_config.cfg

However, be aware that external catalogs may be split across tens of thousands of files: attempting to specify the full list on the command line is likely to be impossible due to limits imposed by the underlying operating system and shell.
Instead, you can write a small Python script that finds files with the `glob` package and then runs the :lsst-task:`~lsst.meas.algorithms.ingestIndexReferenceTask.IngestIndexedReferenceTask` task for you.

Here is a sample script that was used to generate the Gaia DR2 refcat.
In order to deal with the way that Gaia released their photometric data, we have subclassed :lsst-task:`~lsst.meas.algorithms.ingestIndexReferenceTask.IngestIndexedReferenceTask` as `~lsst.meas.algorithms.ingestIndexReferenceTask.IngestGaiaReferenceTask`, and also subclassed the ingestion manager with `lsst.meas.algorithms.ingestIndexManager.IngestGaiaManager`.
This class special-cases the calculation of the flux and flux errors from the values in the Gaia DR2 catalog, which cannot be handled via the simple Config system used above.
Note the lines that should be modified at the top, specifying the config, input, output and an existing butler repo:

.. code-block:: python

    import glob
    from lsst.meas.algorithms import IngestGaiaReferenceTask

    # Modify these lines to run with your data and config:
    #
    # The config file that gives the field name mappings
    configFile = 'gaia_dr2_config.py'
    # The path to the input data
    inputGlob="/project/shared/data/gaia_dr2/gaia_source/csv/GaiaSource*"
    # path to where the output will be written
    outpath = "refcat"
    # This repo itself doesn't matter: it can be any valid butler repository.
    # It just provides something for the Butler to construct itself with.
    repo="/datasets/hsc/repo/"

    # These lines generate the list of files and do the work:
    files = glob.glob(inputGlob)
    # Sorting the glob list lets you specify `*files[:10]` in the argument
    # list below to test the ingestion with a small set of files.
    files.sort()

    config = IngestGaiaReferenceTask.ConfigClass()
    config.load(configFile)

    # Replace `*files` with e.g. `*files[:10]` to only ingest the first 10
    # files, and then run `test_ingested_reference_catalog.py` on the output
    # with a glob pattern that matches the first 10 files to check that the
    # ingest worked.
    args = [repo, "--output", outpath, *files]
    IngestGaiaReferenceTask.parseAndRun(args=args, config=config)

To run it, first ``setup meas_algorithms``, and, assuming the file above is
saved as ``ingestGaiaDr2.py``, run it and send the output to a log file:

.. code-block:: sh

    python ingestGaiaDr2.py &> ingest.log

Monitor the log file in a new terminal with:

.. code-block:: sh

    tail -f ingest.log

Check the log ouput after several hours.
``IngestIndexedReferenceTask`` reports progress in 1% intervals.

4. Check the ingested files
===========================

Once you have ingested the reference catalog, you can spot check the output to see if the objects were transfered.
To do this, ``setup meas_algorithms`` and run ``check_ingested_reference_catalog.py``.
See its help (specify ``-h`` on the commandline) for details about options and an example command.
If you only ingested a subset of the catalog, you can specify just the files you ran the ingest step on to only check those specific files.

5. Move the output to the correct location
==========================================

Once you have successfully ingested the refcat, it needs to be moved into an existing Gen2 butler repository's ``ref_cats`` directory (instructions for Gen3 will be provided once they are available).
For LSST staff using ``lsst-dev``, see the `Reference catalogs policy <https://developer.lsst.io/services/datasets.html#reference-catalogs>`_ in the Developer Guide.
