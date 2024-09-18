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

# 测试7 opencv

@todo
测试opencv的使用，没有成功调用

# 测试8 2路VI输入

开发板有两个摄像头。

需要在底层驱动中根据传感器型号和设备号（MipiDev）重新配置一下Lane的id。

当MipiDev为0时，表示为设备0，lane id为0，2。

当MipiDev为1时，表示为设备1，lane id为1，3。



## 分屏测试

@todo 二分屏和四分屏的显示效果这里测试是一样的，但是配置确实是2个通道和4个通道，不知道为啥不是左右分屏。



数据流程图：

vi0送到vo的分屏0，同时送到一个venc通道编码。
vi1送到vo的分屏1。
在屏幕上的效果是两个图像分别占四分之一的显示位置。

```bash
vi0 ----| ------> venc
        | ------> vo(MUX)
vi1 ----|
```

### VO_MODE_2MUX


```bash
-----CHN BASIC INFO ---------------------------------------------------------------
 LayerId   ChnId ChnEn  Prio DeFlk  ChnX  ChnY  ChnW  ChnH DispX DispY bSnap Field RotAngle
       0       0     Y     0     N     0     0   960   540    -1    -1     N  both        0
       0       1     Y     0     N   960     0   960   540    -1    -1     N  both        0

-----CHN PLAY INFO 1---------------------------------------------------------------------------------------------------------------------
 LayerId   ChnId   Batch  Show Pause  Step Revrs Refsh Thrshd ChnFrt   ChnGap
       0       0       N     Y     N     N     N     N      3     60    16666
       0       1       N     Y     N     N     N     N      3     60    16666


```


### VO_MODE_4MUX

```bash

-----CHN BASIC INFO ---------------------------------------------------------------
 LayerId   ChnId ChnEn  Prio DeFlk  ChnX  ChnY  ChnW  ChnH DispX DispY bSnap Field RotAngle
       0       0     Y     0     N     0     0   960   540    -1    -1     N  both        0
       0       1     Y     0     N   960     0   960   540    -1    -1     N  both        0
       0       2     Y     0     N     0   540   960   540    -1    -1     N  both        0
       0       3     Y     0     N   960   540   960   540    -1    -1     N  both        0

-----CHN PLAY INFO 1---------------------------------------------------------------------------------------------------------------------
 LayerId   ChnId   Batch  Show Pause  Step Revrs Refsh Thrshd ChnFrt   ChnGap
       0       0       N     Y     N     N     N     N      3     60    16666
       0       1       N     Y     N     N     N     N      3     60    16666
       0       2       N     Y     N     N     N     N      3     60    16666
       0       3       N     Y     N     N     N     N      3     60    16666

```






