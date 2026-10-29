#!/bin/bash

# Check the dependency cache of an ossrs/srs:*-cache image, which configure copies with --build-cache=on,
# see auto/depends.sh. The cache must have every library built for this SRS major version, from the same
# source as this tree, or the build silently rebuilds them or tests the old ones. Run it from trunk in the
# image, before the build:
#     docker run --rm -v $(pwd):/srs -w /srs/trunk ossrs/srs:ubuntu20-cache bash scripts/check_build_cache.sh
# On a miss or a stale library, refresh the image, see https://github.com/ossrs/dev-docker/tree/ubuntu20-cache

work_dir=$(cd -P $(dirname $0) && cd .. && pwd) && cd $work_dir
if [[ ! -f src/core/srs_core_version.hpp ]]; then echo "no SRS trunk at ${work_dir}"; exit 1; fi
refresh="Refresh the image, see https://github.com/ossrs/dev-docker/tree/ubuntu20-cache"

# The image builds the cache of each major version from its branch, and keeps the source tree of the last
# one, which must be this major version.
major=$(grep -o 'srs_core_version[0-9]*\.hpp' src/core/srs_core_version.hpp | grep -o '[0-9]*')
source=/usr/local/srs-cache/srs/trunk
cache=$(ls -d ${source}/objs/Platform-SRS${major}-* 2>/dev/null | head -n 1)
if [[ ! -d $cache ]]; then
    echo "Cache miss: no ${source}/objs/Platform-SRS${major}-* in this image."
    echo "$refresh"; exit 1
fi
source_major=$(grep -o 'srs_core_version[0-9]*\.hpp' ${source}/src/core/srs_core_version.hpp | grep -o '[0-9]*')
if [[ $source_major != $major ]]; then
    echo "Cache source is SRS ${source_major}, not SRS ${major}, so its libraries can't be checked."
    echo "$refresh"; exit 1
fi
echo "Cache at ${cache}"

# Each library, as the directory configure installs it to in the cache, and its source in 3rdparty. The
# patches change the sources at build time, so they are a source too.
failed=()
for pair in st:st-srs openssl:openssl-1.1-fit srtp2:libsrtp-2-fit ffmpeg:ffmpeg-4-fit srt:srt-1-fit \
        opus:opus-1.3.1.tar.gz patches:patches; do
    lib=${pair%%:*} src=${pair#*:}
    if [[ $lib != patches && ! -d ${cache}/3rdparty/${lib} ]]; then
        failed+=(${src}); echo "Cache miss ${src}, configure would build it from source."; continue
    fi
    changed=$(diff -rq 3rdparty/${src} ${source}/3rdparty/${src} 2>&1)
    if [[ -n $changed ]]; then
        failed+=(${src})
        echo "Stale ${src}, $(echo "$changed" | wc -l | tr -d ' ') files differ from this tree, for example:"
        echo "$changed" | head -n 5 | sed 's/^/    /'
    else
        echo "The ${src} is ok."
    fi
done

if [[ ${#failed[@]} -ne 0 ]]; then
    echo "Cache stale or missed: ${failed[*]}. The build would use old libraries or rebuild them."
    echo "$refresh"; exit 1
fi
echo "Cache ok for SRS ${major}."
