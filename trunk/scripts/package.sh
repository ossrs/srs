#!/bin/bash

# user can config the following configs, then package.
INSTALL=/usr/local/srs

##################################################################################
##################################################################################
##################################################################################
# discover the current work dir, the log and access.
echo "argv[0]=$0"
if [[ ! -f $0 ]]; then 
    echo "directly execute the scripts on shell.";
    work_dir=`pwd`
else 
    echo "execute scripts in file: $0";
    work_dir=`dirname $0`; work_dir=`(cd ${work_dir} && pwd)`
fi
work_dir=`(cd ${work_dir}/.. && pwd)`
product_dir=$work_dir
build_objs=${work_dir}/objs
package_dir=${build_objs}/package

log="${build_objs}/logs/package.`date +%s`.log" && . ${product_dir}/scripts/_log.sh && check_log
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi

# check os version
os_name=`lsb_release --id|awk '{print $3}'` &&
os_release=`lsb_release --release|awk '{print $2}'` &&
os_major_version=`echo $os_release|awk -F '.' '{print $1}'` &&
os_machine=`uname -i`
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "lsb_release get os info failed."; exit $ret; fi
ok_msg "target os is ${os_name}-${os_major_version} ${os_release} ${os_machine}"

# build srs
# @see https://github.com/winlinvip/simple-rtmp-server/wiki/Build
ok_msg "start build srs"
(
    cd $work_dir && 
    ./configure --with-ssl --with-hls --with-nginx --with-ffmpeg --with-http-callback --prefix=$INSTALL &&
    make && rm -rf $package_dir && make DESTDIR=$package_dir install
) >> $log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "build srs failed"; exit $ret; fi
ok_msg "build srs success"

# copy extra files to package.
ok_msg "start copy extra files to package"
(
    cp $work_dir/scripts/install.sh $package_dir/INSTALL &&
    sed -i "s|^INSTALL=.*|INSTALL=${INSTALL}|g" $package_dir/INSTALL &&
    mkdir -p $package_dir/scripts &&
    cp $work_dir/scripts/_log.sh $package_dir/scripts/_log.sh &&
    chmod +x $package_dir/INSTALL
) >> $log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "copy extra files failed"; exit $ret; fi
ok_msg "copy extra files success"

# generate zip dir and zip filename
srs_version=`${build_objs}/srs -v 2>/dev/stdout 1>/dev/null` &&
zip_dir="SRS-${os_name}${os_major_version}-${os_machine}-${srs_version}"
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "generate zip filename failed"; exit $ret; fi
ok_msg "generate zip filename success"

# zip package.
ok_msg "start zip package"
(
    mv $package_dir ${build_objs}/${zip_dir} &&
    cd ${build_objs} && rm -rf ${zip_dir}.zip && zip -q -r ${zip_dir}.zip ${zip_dir} &&
    mv ${build_objs}/${zip_dir} $package_dir
) >> $log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "zip package failed"; exit $ret; fi
ok_msg "zip package success"

ok_msg "srs package success"
echo ""
echo "package: ${build_objs}/${zip_dir}.zip"
echo "install:"
echo "      unzip -q ${zip_dir}.zip &&"
echo "      cd ${zip_dir} &&"
echo "      sudo bash INSTALL"

exit 0
