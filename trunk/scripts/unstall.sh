#!/bin/bash

systemctl disable srs
systemctl stop srs
rm -rf /usr/local/srs
rm -f /etc/init.d/srs
echo "SRS uninstalled"
