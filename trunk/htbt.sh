#/bin/bash
for ((;;)); do
	echo "heatbeat at `date`"
	curl 'http://ossrs.net:8085/api/v1/servers' -H 'Content-Type: text/html' --data-binary "{\"ip\":\"${ip}\",\"device_id\":\"respberry-pi2\"}" && echo ""
	sleep 30
done
