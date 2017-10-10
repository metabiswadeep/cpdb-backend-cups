# CUPS

This repository hosts the code for the CUPS **C**ommon **P**rint **D**ialog **B**ackend. This backend manages and provides information about CUPS and IPP printing destinations to the printing dialog.

## Background

The [Common Printing Dialog](https://wiki.ubuntu.com/CommonPrintingDialog) project aims to provide a uniform, GUI toolkit independent printing experience on Linux Desktop Environments.

## Dependencies

- [cpdb-libs](https://github.com/OpenPrinting/cpdb-libs)
- [CUPS](https://github.com/apple/cups/releases) : Version >= 2.2 
 Install bleeding edge release from [here](https://github.com/apple/cups/releases).
 OR
`sudo apt install cups libcups2-dev`

- GLIB 2.0 :
`sudo apt install libglib2.0-dev`

## Build and installation

    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install


## Running

The backend is auto-activated when a frontend runs; So no need to run it explicitly.
However, if you wish to see the debug statements at the backend, you can run  `print_backend_cups`. 
