# 说明

基于3516开发板的学习。在其SDK的基础上编写一些测试例子

## sample

官方的示例，只学习了一部分。

### hifb

海思fb的使用，例子里面画了十字交叉线，叠加了位图。

fb使用基本步骤是打开设备，mmap后进行操作，还有一些ioctl的操作，需要了解下linux的framebuffer，是比较类似的。

## 测试1

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

## 测试2

mjpeg和jpeg编码。

jpeg的编码是保存为一个图片，提供的例子里面是抓拍。实际测试全部保存的话非常占用编码的性能，有丢帧问题。

这里实现1s保存1张的功能，注释官方jpeg的保存，后续和http服务器实现预览的功能。

## 测试3

获取图像后怎么访问原始数据，带cache的mmap速度会快一些。

## 测试4

开发环境搭建

[【海思篇】【Hi3516DV300】十一、qt5移植_树下棋缘的博客-CSDN博客](https://blog.csdn.net/cocoron/article/details/105662856?spm=1001.2014.3001.5502)

```bash
#!/bin/sh
# S99_qt_env_set
export QT_PATH=/mnt/TF/shared
export QT_QPA_PLATFORM_PLUGIN_PATH=$QT_PATH/plugins
export LD_LIBRARY_PATH=$QT_PATH/lib:$LD_LIBRARY_PATH
export QT_QPA_PLATFORM=linuxfb:tty=/dev/fb0
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=320x240:offset=0x0:nographicsmodeswitch
```

需要先进行下fb的初始化后，测试程序才可以正常运行。

## 测试5

RTSP 推流测试，使用一个开源项目，也用了git子模块的功能。

github的一个项目：

[PHZ76/RtspServer: RTSP Server , RTSP Pusher (github.com)](https://github.com/PHZ76/RtspServer/tree/master)

# 测试6

IVS 使用。当前 IVS 支持的智能应用有：MD（Motion Detection，移动侦测）。

这里把ive的例子拷贝过来用。

简单过程是从VPSS获取图像，调用接口做处理后得到多个检测到移动的区域，使用VGS进行画线。这里也将图像进行了保存，同时使用了DMA加速拷贝。
