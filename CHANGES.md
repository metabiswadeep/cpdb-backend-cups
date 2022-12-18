# CHANGES - Common Print Dialog Backends - CUPS Backend - v2.0b1 - 2022-12-12

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
