# CHANGES - Common Print Dialog Backends - CUPS Backend - v2.0b5 - 2023-08-02

## CHANGES IN V2.0b5 (2nd August 2023)

- Remove CPDB backend info file
  The frontend libraries now use only the D-Bus to find available
  backends. This makes sure that everything works also if the frontend
  and/or any of the backends are installed via sanboxed packaging
  (like Snap for example) where the components cannot read each
  other's file systems (PR #26).

- `get_all_media()`: Do not crash on custom page size range entries
  The media-col-database IPP attribute contains one entry for each
  valid combination of page size dimensions, margins, and in some
  cases also media source and media type. Entries for custom page
  sizes contain ranges instead of single values. `get_all_media()`
  crashed on these. Now we let the function simply skip them.

- Build system: Removed unnecessary lines in `tools/Makefile.am`
  Removed the `TESTdir` and `TEST_SCRIPTS` entries in
  `tools/Makefile.am`.  They are not needed and let `make install` try
  to install `run-tests.sh` in the source directory, where it already
  is, causing an error.


## CHANGES IN V2.0b4 (21th March 2023)

- Added test script for `make test`/`make check`

  The script `src/run-tests.sh` starts a private session D-Bus via
  `dbus-run-session` and therein an own copy of CUPS. It uses the CPDB
  CUPS backend with this CUPS and tests it using the
  `cpdb-text-frontend` of cpdb-libs, performing several test tasks on
  the backend.


## CHANGES IN V2.0b3 (20th February 2023)

- Add handler for `GetAllTranslations` method and Bug fixes (PR #22)
  * Fixed bug when backend finds zero printers
  * Add handler for `GetAllTranslations` method
  * `get_printer_translations()` fetches translations for all printer
    strings.
  * Removed `get_human_readable_option_name()` and
    `get_human_readable_choice_name()` functions.


## CHANGES IN V2.0b2 (13th February 2023)

- Return printer list upon activation and subscribe to cups for
  updates (PR #20)
  * Return printer list synchronously upon activation
  * Subscribe to CUPS notifications for printer updates
  * Automatically update frontends about new printers found, old
    printers lost, or printer state changed

- Added the support of cpdb-libs for translations
  * Using general message string catalogs from CUPS and also message
    string catalogs from individual print queues/IPP printers.
  * Message catalog management done by libcupsfilters 2.x, via the
    `cfCatalog...()` API functions (`catalog.h`).

- Option group support

- Added `billing-info` option (PR #19)

- Log messages handled by frontend

- Use pkg-config variables instead of hardcoded paths (PR #17)

- Build system: Let "make dist" also create .tar.bz2 and .tar.xz


## CHANGES IN V2.0b1 (12th December 2022)

- Added function to query for human readable names of options/choices

  With the added functionality of cpdb-libs to poll human-readable
  names for options/attributes and their choices this commit adds a
  simple function to provide human-readable strings for the
  user-settable printer IPP attributes of CUPS queues.

- Added support for common CUPS/cups-filters options

  Options like number-up, page-set, output-order, ... Available for
  all CUPS queues, not specific to particular printer.

- Added get_media_size() function

- Support for media sizes to have multiple margin variants (like
  standard and borderless)

- Do not send media-col attribute to frontend as user-settable option

- Adapted to renamed API functions and data types of cpdb-libs 2.x

- Updated signal names to match those emitted from CPDB frontend

- Made "make dist" generate a complete source tarball

- Fixed some potential crasher bugs following warnings about
  incompatible pointer types.

- Updated README.md

  + On update the old version of the backend needs to get killed
  + Common Print Dialog -> Common Print Dialog Backends
  + Requires cpdb-libs 2.0.0 or newer
  + Updated instructions for running the backend.
  + Added link to Gaurav Guleria's GSoC work, minor fixes
  + Use third person.
