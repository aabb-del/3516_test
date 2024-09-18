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

#include "rtsp_server.h"

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


/**
 * @brief 编码数据处理回调，写到FIFO里面去
 * 
 * @param VeChn 
 * @param pstStream 
 */
void VENC_STREAM_CALLBACK_TO_RTSP_SERVER(VENC_CHN VeChn,VENC_STREAM_S* pstStream)
{
    int i = VeChn;
    if(i == 0)
    {
        /* 写入数据到FIFO */
        for (i = 0; i < pstStream->u32PackCount; i++)
        {
            // SAMPLE_PRT("========%d==%d=========\n",i,pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset);
            rtsp_server_push_frame_264((char*)(pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset),
                   (int)(pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset),0);
        }
    }
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
    PAYLOAD_TYPE_E     enType      = PT_H264;
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

    /*config vpss*/
    memset_s(&stVpssGrpAttr, sizeof(VPSS_GRP_ATTR_S), 0, sizeof(VPSS_GRP_ATTR_S));
    stVpssGrpAttr.stFrameRate.s32SrcFrameRate    = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate    = -1;
    stVpssGrpAttr.enDynamicRange                 = DYNAMIC_RANGE_SDR8;
    stVpssGrpAttr.enPixelFormat                  = enPixFormat;
    stVpssGrpAttr.u32MaxW                        = stSize.u32Width;
    stVpssGrpAttr.u32MaxH                        = stSize.u32Height;
    stVpssGrpAttr.bNrEn                          = HI_TRUE;
    stVpssGrpAttr.stNrAttr.enCompressMode        = COMPRESS_MODE_FRAME;
    stVpssGrpAttr.stNrAttr.enNrMotionMode        = NR_MOTION_MODE_NORMAL;

    astVpssChnAttr[VpssChn].u32Width                    = stSize.u32Width;
    astVpssChnAttr[VpssChn].u32Height                   = stSize.u32Height;
    astVpssChnAttr[VpssChn].enChnMode                   = VPSS_CHN_MODE_USER;
    astVpssChnAttr[VpssChn].enCompressMode              = enCompressMode;
    astVpssChnAttr[VpssChn].enDynamicRange              = enDynamicRange;
    astVpssChnAttr[VpssChn].enVideoFormat               = enVideoFormat;
    astVpssChnAttr[VpssChn].enPixelFormat               = enPixFormat;
    astVpssChnAttr[VpssChn].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr[VpssChn].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr[VpssChn].u32Depth                    = 0;
    astVpssChnAttr[VpssChn].bMirror                     = HI_FALSE;
    astVpssChnAttr[VpssChn].bFlip                       = HI_FALSE;
    astVpssChnAttr[VpssChn].stAspectRatio.enMode        = ASPECT_RATIO_NONE;

    /*start vpss*/
    abChnEnable[0] = HI_TRUE;
    s32Ret = SAMPLE_COMM_VPSS_Start(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        goto EXIT1;
    }

// #if 1
//     /*vi bind vpss*/
//     s32Ret = SAMPLE_COMM_VI_Bind_VPSS(ViPipe, ViChn, VpssGrp);
//     if (HI_SUCCESS != s32Ret)
//     {
//         SAMPLE_PRT("vi bind vpss failed. s32Ret: 0x%x !\n", s32Ret);
//         goto EXIT2;
//     }
// #endif

    /*config venc */
    stGopAttr.enGopMode  = VENC_GOPMODE_SMARTP;
    stGopAttr.stSmartP.s32BgQpDelta  = 7;
    stGopAttr.stSmartP.s32ViQpDelta  = 2;
    stGopAttr.stSmartP.u32BgInterval = 1200;

    // stGopAttr.enGopMode  = VENC_GOPMODE_NORMALP;
    // stGopAttr.stNormalP.s32IPQpDelta = 10;


    s32Ret = SAMPLE_COMM_VENC_Start(VencChn[0], enType, enPicSize, enRcMode, u32Profile,bRcnRefShareBuf, &stGopAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start venc failed. s32Ret: 0x%x !\n", s32Ret);
        goto EXIT2;
    }

    s32Ret = SAMPLE_COMM_VPSS_Bind_VENC(VpssGrp, VpssChn, VencChn[0]);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Venc bind Vpss failed. s32Ret: 0x%x !n", s32Ret);
        goto EXIT3;
    }

    /*config vo*/
    SAMPLE_COMM_VO_GetDefConfig(&stVoConfig);
    stVoConfig.enDstDynamicRange = enDynamicRange;
    if (1 == u32VoIntfType)
    {
        stVoConfig.enVoIntfType = VO_INTF_BT1120;
        stVoConfig.enIntfSync   = VO_OUTPUT_1080P25;
    }
    else
    {
        stVoConfig.enVoIntfType = VO_INTF_HDMI;
    }
    stVoConfig.enPicSize = enPicSize;

    /*start vo*/
    s32Ret = SAMPLE_COMM_VO_StartVO(&stVoConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vo failed. s32Ret: 0x%x !\n", s32Ret);
        goto EXIT4;
    }


    /*vpss bind vo*/
    s32Ret = SAMPLE_COMM_VPSS_Bind_VO(VpssGrp, VpssChn, stVoConfig.VoDev, VoChn);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("vo bind vpss failed. s32Ret: 0x%x !\n", s32Ret);
        goto EXIT5;
    }

    rtsp_server_init((char*)"live",9999,NULL);
    VENC_SetStreamCallback(VENC_STREAM_CALLBACK_TO_RTSP_SERVER);
    s32Ret = SAMPLE_COMM_VENC_StartGetStream(VencChn, sizeof(VencChn)/sizeof(VENC_CHN));
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Get venc stream failed!\n");
        goto EXIT6;
    }



    PAUSE();

    SAMPLE_COMM_VENC_StopGetStream();

EXIT6:
    SAMPLE_COMM_VPSS_UnBind_VO(VpssGrp, VpssChn, stVoConfig.VoDev, VoChn);
EXIT5:
    SAMPLE_COMM_VO_StopVO(&stVoConfig);
EXIT4:
    SAMPLE_COMM_VPSS_UnBind_VENC(VpssGrp, VpssChn, VencChn[0]);
EXIT3:
    SAMPLE_COMM_VENC_Stop(VencChn[0]);
EXIT2:
    SAMPLE_COMM_VPSS_Stop(VpssGrp, abChnEnable);
EXIT1:
    SAMPLE_COMM_VI_StopVi(&stViConfig);
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
