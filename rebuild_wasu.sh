#!/bin/bash

#
# This script may be executed in different ways:
#  - from current directory, where the script is placed
#  - from another directory and invoking the script using absolute pathname
#  - from another directory and invoking the script using relative pathname
#
# We need to figure out the VIEWROOT and the build-directory correctly
# for all the above cases.
#
MYNAME=`echo $0`;
PWD=`dirname $MYNAME`;
if test "$PWD" == "."; then
    PWD=`pwd`
fi
IS_ARTS_IN_PATH=`echo $PWD | grep "/arts"`
if test "$IS_ARTS_IN_PATH" == ""; then
    VIEWROOT=`dirname $PWD`
    cd $VIEWROOT
    VIEWROOT=`pwd`
	echo `pwd`
    cd build
    PWD=`pwd`
else
    VIEWROOT=`echo $PWD | sed -e 's/\([A-Za-z,/]*\/arts\)\/.*/\1/'`
fi
echo "VIEWROOT: $VIEWROOT"
echo "Build directory: $PWD"
echo ""

export CFLAGS="-fPIC"
export CXXFLAGS="-fPIC"

echo -n "Building Darwin.."
#cd $CNSROOT/external/Darwin/DarwinStreamingSrvr5.5.5-Sourchan-nb
#cd $CNSROOT/external/Darwin/DarwinStreamingSrvr6.0.3-Source-jiuzhou
#cd $PWD/Darwin/DarwinStreamingSrvr6.0.3-Source-relay-sx
cd $PWD/Darwin/DarwinStreamingSrvr6.0.3-Source-relay-wasu
echo "----------------------\n"
echo `pwd`;
echo "----------------------\n"
#./Buildit -B clean 
./Buildit || exit
echo "Done"

cd $PWD
