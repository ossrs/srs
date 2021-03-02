#!/bin/sh
# 
# usage: undos <file>
# 
# strips CRs from a file - useful when moving DOS-created files
# onto UN*X machines

cat $1 | tr -d "\r" > $1.tmp
mv $1.tmp $1

