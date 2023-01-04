#!/bin/bash
shopt -s globstar
gcov_data_dir="."
for x in ./**/*.o; do
  echo "x: $x"
  gcov "$gcov_data_dir/$x"
done
