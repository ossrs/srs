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
product_dir=$work_dir

log="${work_dir}/logs/package.`date +%s`.log" && . ${product_dir}/scripts/_log.sh && check_log
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi

# user must stop service first.
ok_msg "check previous install"
if [[ -f /etc/init.d/srs ]]; then
    /etc/init.d/srs status >/dev/null 2>&1
    ret=$?; if [[ 0 -eq ${ret} ]]; then 
        failed_msg "you must stop the service first: sudo /etc/init.d/srs stop"; 
        exit 1; 
    fi
fi
ok_msg "previous install checked"

# backup old srs
ok_msg "backup old srs"
install_root=$INSTALL
install_bin=$install_root/objs/srs
if [[ -d $install_root ]]; then
    version="unknown"
    if [[ -f $install_bin ]]; then
        version=`$install_bin -v 2>/dev/stdout 1>/dev/null`
    fi
    
    backup_dir=${install_root}.`date "+%Y-%m-%d_%H-%M-%S"`.v-$version
    ok_msg "backup installed dir, version=$version"
    ok_msg "    to=$backup_dir"
    mv $install_root $backup_dir >>$log 2>&1
    ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "backup installed dir failed"; exit $ret; fi
    ok_msg "backup installed dir success"
fi
ok_msg "old srs backuped"

# prepare files.
ok_msg "prepare files"
(
    sed -i "s|^ROOT=.*|ROOT=\"${INSTALL}\"|g" $work_dir/${INSTALL}/etc/init.d/srs
) >> $log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "prepare files failed"; exit $ret; fi
ok_msg "prepare files success"

# copy core files
ok_msg "copy core components"
(
    mkdir -p $install_root
    cp -r $work_dir/${INSTALL}/conf $install_root &&
    cp -r $work_dir/${INSTALL}/etc $install_root &&
    cp -r $work_dir/${INSTALL}/objs $install_root
) >>$log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "copy core components failed"; exit $ret; fi
ok_msg "copy core components success"

# install init.d scripts
ok_msg "install init.d scripts"
(
    rm -rf /etc/init.d/srs &&
    ln -sf $install_root/etc/init.d/srs /etc/init.d/srs
) >>$log 2>&1
ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "install init.d scripts failed"; exit $ret; fi
ok_msg "install init.d scripts success"

# install system service
lsb_release --id|grep "CentOS" >/dev/null 2>&1; os_id_centos=$?
lsb_release --id|grep "Ubuntu" >/dev/null 2>&1; os_id_ubuntu=$?
lsb_release --id|grep "Debian" >/dev/null 2>&1; os_id_debian=$?
lsb_release --id|grep "Raspbian" >/dev/null 2>&1; os_id_rasabian=$?
if [[ 0 -eq $os_id_centos ]]; then
    ok_msg "install system service for CentOS"
    /sbin/chkconfig --add srs && /sbin/chkconfig srs on
    ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "install system service failed"; exit $ret; fi
    ok_msg "install system service success"
elif [[ 0 -eq $os_id_ubuntu ]]; then
    ok_msg "install system service for Ubuntu"
    update-rc.d srs defaults
    ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "install system service failed"; exit $ret; fi
    ok_msg "install system service success"
elif [[ 0 -eq $os_id_debian ]]; then
    ok_msg "install system service for Debian"
    update-rc.d srs defaults
    ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "install system service failed"; exit $ret; fi
    ok_msg "install system service success"
elif [[ 0 -eq $os_id_rasabian ]]; then
    ok_msg "install system service for RaspberryPi"
    update-rc.d srs defaults
    ret=$?; if [[ 0 -ne ${ret} ]]; then failed_msg "install system service failed"; exit $ret; fi
    ok_msg "install system service success"
else
    warn_msg "ignore and donot install system service for `lsb_release --id|awk '{print $3}'`."
fi

echo ""
echo "see: https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_LinuxService"
echo "install success, you can:"
echo -e "${GREEN}      sudo /etc/init.d/srs start${BLACK}"
echo "srs root is ${INSTALL}"

exit 0
