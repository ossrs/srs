#!/bin/bash

# when export srs-librtmp project
# set the SRS_WORKDIR and SRS_OBJS, 
# then copy the srs-librtmp needed files.
#
# params:
#     $SRS_WORKDIR the work dir. ie. .
#     $SRS_OBJS the objs directory to store the Makefile. ie. ./objs
#     $SRS_OBJS_DIR the objs directory for Makefile. ie. objs
#     $SRS_EXPORT_LIBRTMP_PROJECT the export srs-librtmp project path. ie. srs-librtmp
#

if [ $SRS_EXPORT_LIBRTMP_PROJECT != NO ]; then
    if [[ -d ${SRS_EXPORT_LIBRTMP_PROJECT} ]]; then
        echo -e "${RED}srs-librtmp target dir exists: ${SRS_EXPORT_LIBRTMP_PROJECT}. ${BLACK}"
        exit 1
    fi
    # create target
    SRS_WORKDIR=${SRS_EXPORT_LIBRTMP_PROJECT} && SRS_OBJS=${SRS_WORKDIR}/${SRS_OBJS_DIR} && mkdir -p ${SRS_OBJS} &&
    # copy src to target
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/research/librtmp && mkdir -p ${_CPT} && cp research/librtmp/*.c research/librtmp/Makefile ${_CPT} &&
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/auto && mkdir -p ${_CPT} && cp auto/generate_header.sh auto/generate-srs-librtmp-single.sh ${_CPT} &&
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/src/core && mkdir -p ${_CPT} && cp src/core/* ${_CPT} &&
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/src/kernel && mkdir -p ${_CPT} && cp src/kernel/* ${_CPT} &&
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/src/protocol && mkdir -p ${_CPT} && cp src/protocol/* ${_CPT} &&
    _CPT=${SRS_EXPORT_LIBRTMP_PROJECT}/src/libs && mkdir -p ${_CPT} && cp src/libs/* ${_CPT}
    # check ret
    ret=$?; if [[ $ret -ne 0 ]]; then echo "export src failed, ret=$ret"; exit $ret; fi
fi
