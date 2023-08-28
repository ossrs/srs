# Dockers

About SRS Dockerfile:

* Dockerfile: For release and test.
* trunk/Dockerfile.pkg: For package binary.
* trunk/Dockerfile.builds: For test.
* trunk/Dockerfile.test: For test.
* trunk/Dockerfile.cov: For test and coverage.

```bash
docker build -t srs -f Dockerfile .
```

## Dependency Tree

The dependency tree about Dockerfile:

* Dockerfile
  * [ossrs/srs:ubuntu20](https://github.com/ossrs/dev-docker/tree/ubuntu20)
    * [ossrs/srs:ubuntu20-base2](https://github.com/ossrs/dev-docker/blob/ubuntu20/Dockerfile.base2)
      * [ossrs/srs:ubuntu20-base](https://github.com/ossrs/dev-docker/blob/ubuntu20/Dockerfile.base)
        * ubuntu:focal
    * ubuntu:focal
  * [ossrs/srs:ubuntu20-cache](https://github.com/ossrs/dev-docker/tree/ubuntu20-cache)
    * [ossrs/srs:ubuntu20](https://github.com/ossrs/dev-docker/tree/ubuntu20)
      * ubuntu:focal
  * ubuntu:focal
* trunk/Dockerfile.test
  * [ossrs/srs:dev-gcc7-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-gcc7-cache)
    * [ossrs/srs:dev-gcc7](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-gcc7)
      * centos:7
* trunk/Dockerfile.cov
  * [ossrs/srs:dev-gcc7-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-gcc7-cache)
      * [ossrs/srs:dev-gcc7](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-gcc7)
          * centos:7
* trunk/Dockerfile.pkg
  * [ossrs/srs:dev](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev)
    * centos:7
* trunk/Dockerfile.builds
  * [ossrs/srs:dev-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-cache)
    * [ossrs/srs:dev](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev)
      * centos:7
  * [ossrs/srs:dev6-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev6-cache)
    * [ossrs/srs:dev6](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev6)
      * centos:6
  * [ossrs/srs:ubuntu16-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cache)
      * [ossrs/srs:ubuntu16](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16)
          * ubuntu:xenial
  * [ossrs/srs:ubuntu18-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu18-cache)
      * [ossrs/srs:ubuntu18](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu18)
          * ubuntu:bionic
  * [ossrs/srs:ubuntu20-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cache)
      * [ossrs/srs:ubuntu20](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20)
          * ubuntu:focal
  * [ossrs/srs:ubuntu16-cross-arm](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cross-arm)
      * [ossrs/srs:ubuntu16](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16)
          * ubuntu:xenial
  * [ossrs/srs:ubuntu16-cross-aarch64](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cross-aarch64)
      * [ossrs/srs:ubuntu16](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16)
          * ubuntu:xenial
  * [ossrs/srs:ubuntu20-cross-arm](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cross-arm)
      * [ossrs/srs:ubuntu20](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20)
          * ubuntu:focal
  * [ossrs/srs:ubuntu20-cross-aarch64](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cross-aarch64)
      * [ossrs/srs:ubuntu20](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20)
          * ubuntu:focal

## Image for Cache

To speedup the test, we use a set of cache images. 

If need to reconfigure SRS, please update SRS, then update the images:

* [ossrs/srs:dev-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev-cache)
* [ossrs/srs:dev6-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:dev6-cache)
* [ossrs/srs:ubuntu16-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cache)
* [ossrs/srs:ubuntu18-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu18-cache)
* [ossrs/srs:ubuntu20-cache](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cache)
* [ossrs/srs:ubuntu16-cross-arm](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cross-arm)
* [ossrs/srs:ubuntu16-cross-aarch64](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu16-cross-aarch64)
* [ossrs/srs:ubuntu20-cross-arm](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cross-arm)
* [ossrs/srs:ubuntu20-cross-aarch64](https://github.com/ossrs/dev-docker/tree/ossrs/srs:ubuntu20-cross-aarch64)

For example, update the [release.yml](https://github.com/ossrs/dev-docker/blob/ubuntu20-cache/.github/workflows/release.yml) for ubuntu20-cache:

```bash
# Build SRS for cache, never install it.
#     SRS is 2d036c3fd Fix #2747: Support Apple Silicon M1(aarch64). v5.0.41
# Please update this comment, if need to refresh the cached dependencies, like st/openssl/ffmpeg/libsrtp/libsrt etc.
```

Then push to github and the image will be updated automatically.

