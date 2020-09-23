ip=`ifconfig 2>&1|grep 'inet addr'|grep -v '127.0.0.1'|awk 'NR==1 {print $2}'|awk -F ':' '{print $2}'`
if [[ -z $ip ]]; then 
    echo "127.0.0.1"
else
    echo $ip
fi
