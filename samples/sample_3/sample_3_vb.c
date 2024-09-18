#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "sample_comm.h"
#include "expand.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


HI_VOID SAMPLE_VIO_MsgInit(HI_VOID)
{
}

HI_VOID SAMPLE_VIO_MsgExit(HI_VOID)
{
}

void SAMPLE_VIO_HandleSig(HI_S32 signo)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_VENC_StopGetStream();
        SAMPLE_COMM_All_ISP_Stop();
        SAMPLE_COMM_VO_HdmiStop();
        SAMPLE_COMM_SYS_Exit();
        SAMPLE_PRT("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}


HI_S32 SAMPLE_VIO_TEST(HI_U32 u32VoIntfType)
{
    HI_S32             s32Ret = HI_SUCCESS;

    HI_S32             s32ViCnt       = 1;
    VI_DEV             ViDev          = 0;
    VI_PIPE            ViPipe         = 0;
    VI_CHN             ViChn          = 0;
    HI_S32             s32WorkSnsId   = 0;
    SAMPLE_VI_CONFIG_S stViConfig;

    SIZE_S             stSize;
    VB_CONFIG_S        stVbConf;
    PIC_SIZE_E         enPicSize;
    HI_U32             u32BlkSize;

    VO_CHN             VoChn          = 0;
    SAMPLE_VO_CONFIG_S stVoConfig;

    WDR_MODE_E         enWDRMode      = WDR_MODE_NONE;
    DYNAMIC_RANGE_E    enDynamicRange = DYNAMIC_RANGE_SDR8;
    PIXEL_FORMAT_E     enPixFormat    = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    VIDEO_FORMAT_E     enVideoFormat  = VIDEO_FORMAT_LINEAR;
    COMPRESS_MODE_E    enCompressMode = COMPRESS_MODE_NONE;
    VI_VPSS_MODE_E     enMastPipeMode = VI_ONLINE_VPSS_ONLINE;

    VPSS_GRP           VpssGrp        = 0;
    VPSS_GRP_ATTR_S    stVpssGrpAttr;
    VPSS_CHN           VpssChn        = VPSS_CHN0;
    HI_BOOL            abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};
    VPSS_CHN_ATTR_S    astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM];

    VENC_CHN           VencChn[1]  = {0};
    PAYLOAD_TYPE_E     enType      = PT_H265;
    SAMPLE_RC_E        enRcMode    = SAMPLE_RC_CBR;
    HI_U32             u32Profile  = 0;
    HI_BOOL            bRcnRefShareBuf = HI_FALSE;
    VENC_GOP_ATTR_S    stGopAttr;

    /*config vi*/
    SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
    stViConfig.astViInfo[0].stSnsInfo.enSnsType = GC2053_TEST;
    stViConfig.astViInfo[1].stSnsInfo.enSnsType = GC2053_TEST;

    stViConfig.s32WorkingViNum                                   = s32ViCnt;
    stViConfig.as32WorkingViId[0]                                = 0;
    stViConfig.astViInfo[s32WorkSnsId].stSnsInfo.MipiDev         = ViDev;
    stViConfig.astViInfo[s32WorkSnsId].stSnsInfo.s32BusId        = 0;
    stViConfig.astViInfo[s32WorkSnsId].stDevInfo.ViDev           = ViDev;
    stViConfig.astViInfo[s32WorkSnsId].stDevInfo.enWDRMode       = enWDRMode;
    stViConfig.astViInfo[s32WorkSnsId].stPipeInfo.enMastPipeMode = enMastPipeMode;
    stViConfig.astViInfo[s32WorkSnsId].stPipeInfo.aPipe[0]       = ViPipe;
    stViConfig.astViInfo[s32WorkSnsId].stPipeInfo.aPipe[1]       = -1;
    stViConfig.astViInfo[s32WorkSnsId].stPipeInfo.aPipe[2]       = -1;
    stViConfig.astViInfo[s32WorkSnsId].stPipeInfo.aPipe[3]       = -1;
    stViConfig.astViInfo[s32WorkSnsId].stChnInfo.ViChn           = ViChn;
    stViConfig.astViInfo[s32WorkSnsId].stChnInfo.enPixFormat     = enPixFormat;
    stViConfig.astViInfo[s32WorkSnsId].stChnInfo.enDynamicRange  = enDynamicRange;
    stViConfig.astViInfo[s32WorkSnsId].stChnInfo.enVideoFormat   = enVideoFormat;
    stViConfig.astViInfo[s32WorkSnsId].stChnInfo.enCompressMode  = enCompressMode;

    /*get picture size*/
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(stViConfig.astViInfo[s32WorkSnsId].stSnsInfo.enSnsType, &enPicSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("get picture size by sensor failed!\n");
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("get picture size failed!\n");
        return s32Ret;
    }

    /*config vb*/
    memset_s(&stVbConf, sizeof(VB_CONFIG_S), 0, sizeof(VB_CONFIG_S));
    stVbConf.u32MaxPoolCnt              = 2;

    u32BlkSize = COMMON_GetPicBufferSize(stSize.u32Width, stSize.u32Height, SAMPLE_PIXEL_FORMAT, DATA_BITWIDTH_8, COMPRESS_MODE_SEG, DEFAULT_ALIGN);
    stVbConf.astCommPool[0].u64BlkSize  = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt   = 10;

    u32BlkSize = VI_GetRawBufferSize(stSize.u32Width, stSize.u32Height, PIXEL_FORMAT_RGB_BAYER_16BPP, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    stVbConf.astCommPool[1].u64BlkSize  = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt   = 4;

    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        return s32Ret;
    }

    /*start vi*/
    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed.s32Ret:0x%x !\n", s32Ret);
        goto EXIT;
    }

    /* 不设置无法获取 */
    VI_DUMP_ATTR_S stDumpAttr;
    VI_DUMP_ATTR_S *pstDumpAttr = &stDumpAttr;
    s32Ret = HI_MPI_VI_GetPipeDumpAttr(ViPipe, pstDumpAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("error:0x%x !\n", s32Ret);
    }

    pstDumpAttr->bEnable = HI_TRUE;
    pstDumpAttr->u32Depth = 8;
    /* 两个类型都能获取到 */
    // pstDumpAttr->enDumpType = VI_DUMP_TYPE_RAW;
    pstDumpAttr->enDumpType = VI_DUMP_TYPE_YUV;

    s32Ret = HI_MPI_VI_SetPipeDumpAttr(ViPipe, pstDumpAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("error:0x%x !\n", s32Ret);
    }


    VIDEO_FRAME_INFO_S stVideoFrame;
    VIDEO_FRAME_INFO_S *pstVideoFrame = &stVideoFrame;
    HI_S32 s32MilliSec = 100;

    s32Ret = HI_MPI_VI_GetPipeFrame(ViPipe, pstVideoFrame, s32MilliSec);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_VI_GetPipeFrame:0x%x !\n", s32Ret);
    }
    else
    {
        SAMPLE_PRT("pstVideoFrame->stVFrame.enPixelFormat %d\n",pstVideoFrame->stVFrame.enPixelFormat);
        s32Ret = HI_MPI_VI_ReleasePipeFrame(ViPipe, pstVideoFrame);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_VI_ReleasePipeFrame:0x%x !\n", s32Ret);
        }
    }

    getchar();

    while(1)
    {
        s32Ret = HI_MPI_VI_GetPipeFrame(ViPipe, pstVideoFrame, s32MilliSec);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_VI_GetPipeFrame:0x%x !\n", s32Ret);
            continue;
        }


        HI_U32 u32Size = 1024;
        
        // HI_U64* u64VirtAddr = (HI_U64*)HI_MPI_SYS_Mmap(pstVideoFrame->stVFrame.u64PhyAddr[0], u32Size);

        SAMPLE_PRT("pstVideoFrame->stVFrame.u64VirAddr[0] %d\n",pstVideoFrame->stVFrame.u64VirAddr[0]);
        #define DEVICE_NAME "/dev/mydevice"
        int fd;
        fd = open(DEVICE_NAME, O_RDWR);
        write(fd, (void*)(pstVideoFrame->stVFrame.u64VirAddr[0]),1024);

        // HI_MPI_SYS_Munmap((HI_VOID*)u64VirtAddr,u32Size);


        s32Ret = HI_MPI_VI_ReleasePipeFrame(ViPipe, pstVideoFrame);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("HI_MPI_VI_ReleasePipeFrame:0x%x !\n", s32Ret);
        }


    }

  


    PAUSE();

EXIT:
    SAMPLE_COMM_SYS_Exit();
    return s32Ret;
}




int main()
{
    HI_S32 s32Ret = HI_FAILURE;
    HI_S32 s32Index;
    HI_U32 u32VoIntfType = 0;
    HI_U32  u32ChipId;

    signal(SIGINT, SAMPLE_VIO_HandleSig);
    signal(SIGTERM, SAMPLE_VIO_HandleSig);

    HI_MPI_SYS_GetChipId(&u32ChipId);
    SAMPLE_PRT("chip id: 0x%x\n", u32ChipId);
    if (HI3516C_V500 == u32ChipId)
    {
        u32VoIntfType = 1;
    }
    else
    {
        u32VoIntfType = 0;
    }
    
    SAMPLE_VIO_MsgInit();

    SAMPLE_VIO_TEST(u32VoIntfType);

    if (HI_SUCCESS == s32Ret)
    {
        SAMPLE_PRT("sample_vio exit success!\n");
    }
    else
    {
        SAMPLE_PRT("sample_vio exit abnormally!\n");
    }

    SAMPLE_VIO_MsgExit();

}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
