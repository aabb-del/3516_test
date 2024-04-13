#ifdef __cplusplus
extern "C"
{
#endif
 
#include "libavutil/mathematics.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/mem.h"
#include "libavutil/audio_fifo.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/time.h"


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ffmpeg_udp.h"

#ifdef __cplusplus
}
#endif



#if 1
#define UDP 1
#else
#define RTSP 1
#endif

#define UDP_TARGET_IP "192.168.3.16"

#define READ_DATA_LEN (1920*1080)
#define true 1
#define false 0
int fd;
int hflv_audio = 0;


int read_init(char *filename)
{
    fd = open(filename,O_RDONLY);
    if(fd < 0)
    {
        perror("open file failed");
        return -1;
    }
    return 1;
}

int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
   return  read(fd,buf,buf_size);
}



int mem_udp_main()
{
    printf("ffmpeg udp stream started!\n");
#if UDP
    char *out_filename = "udp://"UDP_TARGET_IP":1234";
#elif RTSP
    char *out_filename = "rtsp://"UDP_TARGET_IP":1234";
#else
    char *out_filename = "rtmp://192.168.3.45/live/livestream";
#endif

#if 0
    char *in_filename = "stream_chn0.h264";
#else
    char *in_filename = "/tmp/stream_chn0_fifo";
    while(access(in_filename,F_OK) < 0)
    {
        usleep(1000);
    }
#endif


    read_init(in_filename);

    int ret = 0;
    int videoindex = -1;
    AVPacket pkt;
    int frame_index = 0;
    AVStream *in_stream, *out_stream;
 
    AVFormatContext *ictx = NULL;
    const AVInputFormat* ifmt = NULL;
 
    AVFormatContext *octx = NULL;
    AVOutputFormat *ofmt = NULL;
    AVIOContext *avio = NULL;


    unsigned char * iobuffer = (unsigned char *)av_malloc(READ_DATA_LEN);
    avio = avio_alloc_context(iobuffer, READ_DATA_LEN, 0, NULL, read_packet, NULL, NULL);
    if (!avio)
    {
        printf( "avio_alloc_context for input failed\n");
        goto end;
    }
 
    //探测流封装格式
    ret = av_probe_input_buffer(avio, &ifmt, "", NULL, 0, 0);
    if (ret < 0)
    {
        printf("av_probe_input_buffer failed\n");
        goto end;
    }
    printf("av_probe_input_buffer format:%s[%s]\n",ifmt->name, ifmt->long_name);
 
    ictx = avformat_alloc_context();
    ictx->pb = avio;
    ictx->flags=AVFMT_FLAG_CUSTOM_IO;
 
    ret = avformat_open_input(&ictx, "", NULL, NULL);
    if (ret < 0)
    {
        printf("avformat_open_input failed\n");
        goto end;
    }
 
    //获取音频视频的信息
    ictx->probesize = 1024*1024*3;
    ictx->max_analyze_duration = 3000000; //最大分析3秒
    ictx->flags |= AVFMT_FLAG_NOBUFFER; //不缓存, 减小直播延时
 
    ret = avformat_find_stream_info(ictx, NULL);
    if (ret < 0)
    {
        printf("avformat_find_stream_info failed\n");
        goto end;
    }
 
    for(unsigned int i = 0; i < ictx->nb_streams; i++)
    {
        if (ictx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)
        {
            //HTTP—FLV只支持 AAC和MP3 音频格式
            if ((ictx->streams[i]->codecpar->codec_id != AV_CODEC_ID_AAC
                && ictx->streams[i]->codecpar->codec_id != AV_CODEC_ID_MP3)
                || ictx->streams[i]->codecpar->sample_rate == 0)
            {
                hflv_audio = false;
            }
            else
            {
                hflv_audio = true;
            }
        }
        else if (ictx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
        {
            // //HTTP—FLV只支持 h264 视频格式
            // if (ictx->streams[i]->codecpar->codec_id != AV_CODEC_ID_H264)
            // {
            //     goto end;
            // }
 
            videoindex = i;
        }
        av_opt_set(ictx->streams[i]->priv_data,"tune","zerolatency",0);
        ictx->streams[i]->avg_frame_rate.num = 30;
        ictx->streams[i]->r_frame_rate.num = 30;
        ictx->streams[i]->r_frame_rate.den = 1;
        
    }
 
    if (videoindex == -1)
    {
        printf("videoindex is -1\n");
        goto end;
    }
 
    av_dump_format(ictx, 0, "", 0);
 
#if UDP
    avformat_alloc_output_context2(&octx, NULL, "mpegts", out_filename);
#elif RTSP
    avformat_alloc_output_context2(&octx, NULL, "RTSP", out_filename);
    //使用tcp协议传输
    av_opt_set(octx->priv_data, "rtsp_transport", "udp", 0);
    //检查所有流是否都有数据，如果没有数据会等待max_interleave_delta微秒
    octx->max_interleave_delta = 1000000;
#else 
    avformat_alloc_output_context2(&octx, 0, "flv", out_filename);
#endif

    if (!octx)
    {
        printf( "Could not create output context\n");
        goto end;
    }
    printf("avformat_alloc_output_context2\n");
 
    //将输入流音视频编码信息复制到输出流中
    for (unsigned int i = 0; i < ictx->nb_streams; i++)
    {
        if (!hflv_audio)
        {
            if(ictx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)
            {
                continue;
            }
        }
 
        AVStream *in_stream = ictx->streams[i];
 
        AVStream *out_stream = avformat_new_stream(octx, NULL);
        if (!out_stream)
        {
            printf("avformat_new_stream failed\n");
            goto end;
        }
 
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
        {
            printf("avcodec_parameters_copy failed\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
        // octx->streams[i]->avg_frame_rate.num = 30;
    }
    printf("copy codec context \n");
 
    av_dump_format(octx, 0, out_filename, 1);
 
    //打开输出URL，准备推流
    if (!(octx->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&octx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf( "Could not open output URL '%s'", out_filename);
            goto end;
        }
        printf("avio_open \n");
    }
 
    ret = avformat_write_header(octx, NULL);
    if (ret < 0)
    {
        printf( "Error occurred when opening output URL\n");
        goto end;
    }
 
    printf("start push stream \n");
 
    while (1)
    {
        //获取每一帧数据
        ret = av_read_frame(ictx, &pkt);
        if (ret < 0)
        {
            break;
        }
 
        if (!hflv_audio && pkt.stream_index != videoindex)
        {
            av_packet_unref(&pkt);
            continue;
        }
 
        in_stream = ictx->streams[pkt.stream_index];
 
        if (!hflv_audio && pkt.stream_index == videoindex)
        {
            out_stream = octx->streams[0];
            pkt.stream_index = 0;
        }
        else
        {
            out_stream = octx->streams[pkt.stream_index];
        }
 
        if (pkt.pts == AV_NOPTS_VALUE)
        {
            //Write PTS
            AVRational time_base1 = in_stream->time_base;
            //Duration between 2 frames (us)
            int64_t calc_duration = (int64_t)((double)AV_TIME_BASE / av_q2d(in_stream->r_frame_rate));
            //Parameters
            pkt.pts = (int64_t)((double)(frame_index * calc_duration) / (double)(av_q2d(time_base1)*AV_TIME_BASE));
            pkt.dts = pkt.pts;
            pkt.duration = (int64_t)((double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE));
        }
 
        //指定时间戳
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = (int)av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
 
        if (pkt.stream_index == videoindex)
        {            
            //printf("Send %8d video frames to output URL, [%d]\n",frame_index, pkt.flags);
            frame_index++;
        }
 
        ret = av_interleaved_write_frame(octx, &pkt);
 
        if (ret < 0)
        {
            printf("Error muxing packet.error code %d\n", ret);
            break;
        }

        //释放 packet，否则会内存泄露
        av_packet_unref(&pkt);

        // 读取文件的时候需要加延时
        // av_usleep(40000);
    }
 
    av_write_trailer(octx);
 
end:
 
    // 该函数会释放用户自定义的IO buffer
    // 上面不再释放，否则会corrupted double-linked list
    avformat_close_input(&ictx);
    avformat_free_context(ictx);
 
    if (octx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(octx->pb);
 
    if (octx)
    {
        avformat_free_context(octx);
    }
    return 0;
}
 