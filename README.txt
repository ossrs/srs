学习SRS源码

一、 软件运行
1、 编译
  config --full && make -j3
  
2、 RTMP媒体端口监听
  ./obj/srs -c conf/rtmp.confg
  
3、 查看实时日志
  tail ./obj/srs.log
  
4、 用FFmpeg的方式推流（略）
  ffmpeg -re -i night.mp4 -c copy -f flv rtmp://192.168.31.53/live2/night
  
二、 代码分析
1、 目录
3rdparty 	第三方包，zip格式。估计是一些公共的方法。
auto 		sh脚本。服务运维使用。
conf		sh脚本。配置和修改配置。
doc			英文文档。
etc			略。编译使用。
ide			略。调试工具。
modules		空。扩展模块。HLS音频和MP4音视频。
research	其他平台接入SRS。Golong、arm、python、player
scripts		脚本。
src			主要源码。
	|
	|-----	app 	API层（主要逻辑）
	|-----	core	工具类通用 
	|-----	kernel	音视频、文本输入输出、网络连接
	|-----	libs	略。就是普通代码
	|-----	main	控制台、单元测试。
	|-----	protool	协议的字符解析。json、avc、握手、rtmp、rtsp
	|-----	server	处理服务。线程消息相关。
	
先主要看下RTMP的协议和处理。

2、 代码跟踪
源码文件：/truck/src/main/src_main_server.cpp
这个src_main_server.cpp里面有个 int main() 入口。程序会走到 /truck/src/app/srs_app_server.cpp
srs_app_server.cpp里面有listen监听和Handler消息。通过订阅设计模式实现。

srs_app_server.cpp 文件夹下accept_client() 、fd2conn()接口。RTMP媒体端口接听从这里开始。
*pconn = new SrsRtmpConn(this, stfd, ip);
这里开始RTMP的协议流处理。

SrsRtmpConn类里面新建SrsRtmpServer()类。SrsHandshakeBytes()握手后绑定当前通信接口。

SrsProtocol（）是解析协议的。

主要看下srs_app_rtmp_conn.cpp 类，这里面是协议通信相关。

编解码的先不需要看，这个SRS本来只是开RTMP端口和会议室的。

3、 二次开发技巧
 自己打日志，改方法和改类的重载。梳理流程后，想怎么接入都不难。SRS服务器对接GB28181的一个标准开源平台。