#!/bin/bash
$1 -E env CFLAGS="-arch $2 -mmacosx-version-min=$3" LDFLAGS="-mmacosx-version-min=$3" $4 setup $5/third_party/dav1d --buildtype release --default-library=static -Denable_tests=false -Denable_tools=false -Dbitdepths=8 $6
