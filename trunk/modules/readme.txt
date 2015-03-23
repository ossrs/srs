SRS模块规则：
1. 一个模块一个目录
2. 目录下放一个config文件
3. 所有的configure中的变量模块中可以使用

模块中需要定义变量，例如：
1. SRS_MODULE_NAME：模块名称，用来做Makefile的phony以及执行binary文件名。
2. SRS_MODULE_MAIN：模块的main函数所在的cpp文件，在src/main目录。
3. SRS_MODULE_APP：模块在src/app目录的源文件列表。
4. SRS_MODULE_DEFINES: 模块编译时的额外宏定义。

winlin, 2015.3
