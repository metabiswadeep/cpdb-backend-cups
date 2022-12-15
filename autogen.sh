#! /bin/sh

mkdir -p build-aux \
&& aclocal \
&& automake --add-missing \
&& autoconf
