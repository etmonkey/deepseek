# deepseek外挂
## 安装
make后在当前文件夹下生成文件ds，将该文件移到PATH所指目录下 
配置文件在 ~/.ds/ 目录下 
## 使用方法
ds -h ：可查看帮助 
echo "hello" | ds ：通过管道传入对话 
ds [你要询问的语句] ：单轮对话 
ds -c ：进入多轮对话模式 
ds ：不带询问语句的ds命令进入多轮对话模式 
ds -t a ：使能工具调用功能 
## 多轮对话中命令
/bye ：退出当前回话 
/save [file] ：将当前会话历史保存到file文件中 
/load [file] ：从file中载入会话历史 
/print history ：打印当前会话历史 
/print debug ：打印调试信息 
