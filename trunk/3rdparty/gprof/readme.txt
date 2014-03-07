gprof图形化输出工具: gprof2dot.py graphviz-2.18.tar.gz build_gprof2dot.sh

dot:
    graphviz-2.18.tar.gz 绘图工具
    build_gprof2dot.sh 编译graphviz，命令为dot。
    要求是sudoer，需要sudo make install。
    
gprof2dot.py:
    将gprof的日志绘图。
    
使用方法：
1. srs配置时，指定 --with-pg
    脚本会加入编译参数"-pg -lc_p"，gcc -g -pg -lc_p -c xxx -o xxx.o，即在configure中打开 Performance="-pg -lc_p"
    链接时，加入链接选项"-pg"，否则无法工作：gcc -pg -o srs xxxx.o，即在configure中打开 PerformanceLink="-pg"
2. 编译和启动程序：make && ./objs/srs -c conf/srs.conf
    退出程序，按CTRL+C，可以看到生成了gmon.out，这个就是性能的统计数据。
3. gprof生成报表：
    用gprof生成报表：gprof -b ./objs/srs gmon.out > ~/t.log
4. 将报表生成图片：
    ../3rdparty/gprof/gprof2dot.py ~/t.log | dot -Tpng -o ~/out.png

缩写语句：
    # 生成 ~/winlin.log ~/winlin.png
    rm -f gmon.out; ./objs/srs -c conf/srs.conf
    # 用户按CTRL+C
    file="winlin";gprof -b ./objs/srs gmon.out > ~/${file}.log; ../thirdparty/gprof2dot.py ~/${file}.log| dot -Tpng -o ~/${file}.png

备注：
    其实gprof生成的日志就可以看，不一定要图形化。
    也就是dot和gprof2dot都不用执行。
