#/bin/bash
for ((;;)); do
	ip=`ifconfig 2>&1|grep "inet addr"|grep -v "127"|awk '{print $2}'|awk -F ':' '{print $2}'`
	echo "heatbeat at `date`, ip is ${ip}"
	curl 'http://ossrs.net:8085/api/v1/servers' -H 'Content-Type: text/html' --data-binary "{\"ip\":\"${ip}\",\"device_id\":\"respberry-pi2\"}" && echo ""
	sleep 10
done
