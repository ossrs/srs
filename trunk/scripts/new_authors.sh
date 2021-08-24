#!/bin/bash

AFILE=`dirname $0`/../AUTHORS.txt
if [[ ! -f $AFILE ]]; then echo "No file at $AFILE"; exit -1; fi

authors=`git log --format='%ae'|grep -v localhost|grep -v demo|grep -v none|sort|uniq`
if [[ $? -ne 0 ]]; then echo "no authors"; exit -1; fi

for author in $authors; do
  echo $author| grep -e "winlin" -e 'winterserver' >/dev/null 2>&1 && continue;

  username=$(echo $author |awk -F '@' '{print $1}')
  grep $username $AFILE 1>/dev/null 2>/dev/null && continue;

  echo "* $(git log -1 --author="$author" --format='`%an<%ae>`'| sed 's/ //g')"
done
