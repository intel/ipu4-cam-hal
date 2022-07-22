#
#  Copyright (C) 2014-2016 Intel Corporation
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

#!/bin/sh
RELEASE_VERSION="1.0.0-2"
if [ $# -ne 1 ]; then
    echo "Error: invalid command line, please specify the release version like'./release.sh 1.0.0-2'\n"
    exit 1
else
    RELEASE_VERSION=$1
fi
if [ -d release -o -f release ]; then
    # Not automatically remove it, since it may be created by user for other purpose.
    echo "Error: a directory or file named 'release' already exists, please remove it firstly...\n"
    exit 1
fi
autoreconf --install
if [ $? -ne 0 ]; then
    echo "Error: failed in autoreconf.\n"
    exit 1
fi
./configure --sysconfdir=${PWD}/release/etc --libdir=${PWD}/release/usr/lib64 --includedir=${PWD}/release/usr/include
if [ $? -ne 0 ]; then
    echo "Error: failed in configure.\n"
    exit 1
fi
make clean
make -j
if [ $? -ne 0 ]; then
    echo "Error: failed in make.\n"
    exit 1
fi
make install
if [ $? -ne 0 ]; then
    echo "Error: failed in local install for release.\n"
    exit 1
fi
cp MODULE_LICENSE_APACHE2 release/
git log > release/release_note
cd release
tar cvfj libcamhal-${RELEASE_VERSION}.x86_64.tar.bz2 usr etc
if [ $? -ne 0 ]; then
    echo "Error: failed in tar release package.\n"
    cd -
    exit 1
else
    mv libcamhal-${RELEASE_VERSION}.x86_64.tar.bz2 ../
    echo "Release package ready.\n"
fi
cd -
rm -rf release
