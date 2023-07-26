# 说明

基于3516开发板的学习。在其SDK的基础上编写一些测试例子

# 测试1

基于vio和ffmpeg推流，264或者265。

官方获取编码数据的例子加了一个回调函数。

```bash


./ffplay -protocol_whitelist "file,udp,rtp" -i udp://192.168.3.16:1234 -fflags nobuffer

# 下面这两个也能放，但是会花屏，参数可能不对
# # h264
# ./ffplay.exe -f h264 "udp://192.168.3.16:1234" -fflags nobuffer -nofind_stream_info
# # h265 
# ffplay.exe -f hevc "udp://192.168.3.45:1234" -fflags nobuffer -nofind_stream_info
```

# 测试2

mjpeg和jpeg编码。

jpeg的编码是保存为一个图片，提供的例子里面是抓拍。实际测试全部保存的话非常占用编码的性能，有丢帧问题。

这里实现1s保存1张的功能，注释官方jpeg的保存，后续和http服务器实现预览的功能。
