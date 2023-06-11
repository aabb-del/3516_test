# 说明
基于3516开发板的学习。在其SDK的基础上编写一些测试例子

# 测试1
基于vio和ffmpeg推流

```bash


./ffplay -protocol_whitelist "file,udp,rtp" -i udp://192.168.3.16:1234 -fflags nobuffer

# 下面这两个也能放，但是会花屏，参数可能不对
# # h264
# ./ffplay.exe -f h264 "udp://192.168.3.16:1234" -fflags nobuffer -nofind_stream_info
# # h265 
# ffplay.exe -f hevc "udp://192.168.3.45:1234" -fflags nobuffer -nofind_stream_info
```