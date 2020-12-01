#ifndef NETDATALOG_H
#define NETDATALOG_H
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <direct.h>
#include <string>
#include <io.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
 
using namespace std;
enum TIMEFORMAT
{
	NETLOG = 0,	//	[yyyy\mm\dd hh.MM.ss]
	LOGINLOG=1,	//	mm-dd hh:MM:ss
};
 
class NetDataLog
{
public:
	NetDataLog(string strDir = "log",string filename = "record",int maxfilesize=0,int filecount=0,int timeformat=0);
	~NetDataLog();
 
	void addLog(string log);	//添加日志记录到日志文件
	void fileSizeLimit();		//判断文件大小是否达到限定值
	int getCurrentLogFileSize();//获取当前日志文件的大小
	string getLogFileName();	//获取日志文件名称
	void setMaxFileSize(int);//设置文件最大大小
	void setFileName(string); //设置日志文件名
	void setFileCount(int);	//设置日志文件的个数
	void setLogDir(string strDir);		//设置日志文件目录
private:
	void fileOffset();		//文件名称进行偏移
	bool checkFolderExist(const string &strPath);
	string getCurrentTime();
	
 
private:
	string m_LogFileName;	//文件名
	int m_MaxFileSize;		//文件大小
	int m_FileCount;		//文件个数
	fstream *m_outputFile;	//输出文件流
	string m_strDir;		//目录
	int m_timeFormat;
	
 
};
#endif