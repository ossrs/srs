if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <hls0> <hls1> [keep_ts]"
    echo "      keep_ts to keep the download ts, default is off"
    echo "For example:"
    echo "      $0 http://192.168.1.137:1980/hls/live/stone/live.m3u8 http://192.168.1.137:1984/hls/live/stone/live.m3u8"
    echo "      $0 http://192.168.1.137:1980/hls/live/livestream/live.m3u8 http://192.168.1.137:1984/hls/live/livestream/live.m3u8"
    echo "      $0 http://ossrs.net:1984/hls/live/livestream/live.m3u8 http://ossrs.net:1996/hls/live/livestream/live.m3u8"
    exit 1
fi
 
hls0=$1
hls1=$2
keep_ts=NO
if [[ $# -gt 2 ]]; then
    keep_ts=YES
fi
#echo "check hls backup of $hls0 vs $hls1, keep_ts=$keep_ts"

hls0_tss=`curl $hls0 2>/dev/null |grep "\.ts"`
hls1_tss=`curl $hls1 2>/dev/null |grep "\.ts"`

hls0_prefix=`dirname $hls0`
hls1_prefix=`dirname $hls1`
work_dir="./hbt_temp_`date +%s`"

md5_tool="md5"
`md5sum --version >/dev/null 2>&1` && md5_tool="md5sum"
#echo "use md5 tool: $md5_tool"

CHECKED=NO
OK=YES
for ts in $hls0_tss; do
    match=NO
    for ts1 in $hls1_tss; do
        if [[ $ts == $ts1 ]]; then
            #echo "check ts $ts"
            match=YES
            break
        fi
    done
    #echo "check ts $ts, match=$match"
    
    if [ $match = NO ]; then
        echo "skip $ts"
        continue
    fi
    
    ts0_uri=$hls0_prefix/$ts
    ts1_uri=$hls1_prefix/$ts
    ts0_tmp=$work_dir/hls0/`basename $ts`
    ts1_tmp=$work_dir/hls1/`basename $ts`
    #echo "start check $ts0_uri($ts0_tmp) vs $ts1_uri($ts1_tmp)"
    
    mkdir -p `dirname $ts0_tmp` &&
    curl $ts0_uri >$ts0_tmp 2>/dev/null &&
    ret=$?; if [[ $ret -ne 0 ]]; then echo "download $ts0_uri to $ts0_tmp failed. ret=$ret"; exit $ret; fi
    
    mkdir -p `dirname $ts1_tmp` &&
    curl $ts1_uri >$ts1_tmp 2>/dev/null &&
    ret=$?; if [[ $ret -ne 0 ]]; then echo "download $ts1_uri to $ts1_tmp failed. ret=$ret"; exit $ret; fi
    
    if [[ $md5_tool == "md5" ]]; then
        ts0_cs=`$md5_tool $ts0_tmp|awk '{print $4}'`
    else
        ts0_cs=`$md5_tool $ts0_tmp|awk '{print $1}'`
    fi
    #echo "hls0: md5sum($ts0_tmp)=$ts0_cs"
    
    if [[ $md5_tool == "md5" ]]; then
        ts1_cs=`$md5_tool $ts1_tmp|awk '{print $4}'`
    else
        ts1_cs=`$md5_tool $ts1_tmp|awk '{print $1}'`
    fi
    #echo "hls1: md5sum($ts1_tmp)=$ts1_cs"
    
    if [[ $ts0_cs != $ts1_cs ]]; then
        echo "$ts0_uri($ts0_cs) not equals to $ts1_uri($ts1_cs)"
        OK=NO
    fi
    CHECKED=YES
done

if [ $keep_ts = NO ]; then
    #echo "clenaup work dir $work_dir"
    rm -rf $work_dir
else
    echo "keep work dir $work_dir"
fi

#echo "====================================================="
if [[ $OK = YES && $CHECKED = YES ]]; then
    echo "OK"
    exit 0
else
    echo "FAILED"
    exit 1
fi

exit 0
