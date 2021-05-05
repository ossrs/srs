#!/bin/bash

for file in $(git remote); do echo ""; git push $file $@; done

