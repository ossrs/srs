#!/usr/bin/python
'''
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
'''

#################################################################################
# to stat the code and comments lines
#################################################################################
import sys

def trace(msg):
    print msg
    pass
def info(msg):
    print msg
    pass
def verbose(msg):
    #print msg
    pass

def process(f, code_file):
    info("process file success")
    (stat_code, stat_block_comments, stat_line_comments) = (0, 0, 0)
    is_block_comments = False
    is_line_comments = False
    for line in f.readlines():
        line = line.strip()
        if is_block_comments:
            if "*/" in line:
                verbose("[block][end] %s"%line)
                is_block_comments = False
                is_line_comments = False
            else:
                verbose("[block][cont] %s"%line)
            stat_block_comments += 1
            continue
        if line.startswith("/*"):
            verbose("[block][start] %s"%line)
            is_block_comments = True
            is_line_comments = False
            stat_block_comments += 1
            # inline block comments
            if is_block_comments:
                if "*/" in line:
                    verbose("[block][end] %s"%line)
                    is_block_comments = False
                    is_line_comments = False
            continue
        if line.startswith("//"):
            verbose("[line] %s"%line)
            is_block_comments = False
            is_line_comments = True
            stat_line_comments += 1
            continue
        verbose("[code] %s"%line)
        is_block_comments = False
        is_line_comments = False
        stat_code += 1
    total = stat_code + stat_block_comments + stat_line_comments
    comments = stat_block_comments + stat_line_comments
    trace("total:%s code:%s comments:%s block:%s line:%s file:%s"%(total, stat_code, comments, stat_block_comments, stat_line_comments, code_file))
    return (0, total, stat_code, comments, stat_block_comments, stat_line_comments, code_file)

def do_stat(code_file):
    f = None
    try:
        f = open(code_file, "r")
        info("open file success");
        return process(f, code_file)
    finally:
        if f is not None:
            f.close()
            info("close file success")
    return (-1, 0, 0, 0, 0, 0, None)
    
code_file = None
if __name__ == "__main__":
    if len(sys.argv) <= 1:
        print "to stat the code and comments lines"
        print "Usage: python %s <code_file>"%(sys.argv[0])
        print "     code_file: the code(header or source) file to stat"
        print "Example:"
        print "     python %s src/core/srs_core.hpp"%(sys.argv[0])
        sys.exit(-1)
        
    code_file = sys.argv[1]
    info("stat %s"%(code_file))
    do_stat(code_file)
