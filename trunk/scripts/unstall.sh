#!/bin/bash

if [[ -d /usr/lib/systemd/system ]]; then
  systemctl disable srs
  systemctl stop srs
  rm -f /usr/lib/systemd/system/srs.service
  rm -f /etc/init.d/srs
else
  /sbin/chkconfig srs off
  /sbin/chkconfig --del srs
  /etc/init.d/srs stop
  rm -f /etc/init.d/srs
fi
rm -rf /usr/local/srs
echo "SRS uninstalled"
