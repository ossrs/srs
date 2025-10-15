#!/bin/bash

# work_dir is the root directory of the project
work_dir=$(cd -P $(dirname $0) && cd ../.. && pwd) && cd $work_dir && echo "Run clang-format in ${work_dir}"

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo "clang-format could not be found, please install it first."
    exit 1
fi

# Check if the trunk directory exists
if [ ! -d "trunk" ]; then
    echo "trunk directory does not exist, please run this script from the project root."
    exit 1
fi
# Find all .cpp, .hpp, and .h files in the trunk directory, excluding 3rdparty
# and format them using clang-format with the style defined in .clang-format file
if [ ! -f ".clang-format" ]; then
    echo ".clang-format file does not exist, please create one in the project root."
    exit 1
fi

echo "Formatting source files in trunk directory..."
# Exclude the 3rdparty directory and format all .cpp, and .hpp
# Use -i to edit files in place
# Use xargs -P N to run N clang-format processes in parallel
find trunk/src -name "*.*pp" | xargs -P 16 -n 1 clang-format -style=file -i
