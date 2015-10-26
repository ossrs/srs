#/bin/bash
ip=`ifconfig 2>&1|grep "inet addr"|grep -v "127"|awk '{print $2}'|awk -F ':' '{print $2}'`
for ((;;)); do
	echo "heatbeat at `date`"
	curl 'http://ossrs.net:8085/api/v1/servers' -H 'Content-Type: text/html' --data-binary "{\"ip\":\"${ip}\",\"device_id\":\"respberry-pi2\"}" && echo ""
	sleep 10
done
