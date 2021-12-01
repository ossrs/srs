#!/bin/bash

#for file in $(git remote); do echo ""; git push $file $@; done
for file in $(git remote -v|grep -v https|grep push|awk '{print $1}'); do echo ""; echo "git push $file $@"; git push $file $@; done

