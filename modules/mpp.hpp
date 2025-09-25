#pragma once

#include "sample_comm.h"
#include "expand.h"

#include <atomic>

class Mpp{

public:
    Mpp():is_vi_inited_(false),is_vo_inited_(false)
    {
        memset(&stViConfig, 0x00, sizeof(stViConfig));
    }

    ~Mpp()
    {
        vo_exit();
        vi_exit();
        // 退出MPP系统
        HI_MPI_SYS_Exit();
    }


    HI_S32 vi_init()
    {
        HI_S32             s32Ret = HI_SUCCESS;
        HI_S32             s32ViCnt       = 2;
        VI_DEV             ViDev          = 0;
        VI_PIPE            ViPipe         = 0;
        VI_CHN             ViChn          = 0;
        HI_S32             s32WorkSnsId   = 0;
        
        SIZE_S             stSize;
        VB_CONFIG_S        stVbConf;
        
        HI_U32             u32BlkSize;

   
        WDR_MODE_E         enWDRMode      = WDR_MODE_NONE;
        
        PIXEL_FORMAT_E     enPixFormat    = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        VIDEO_FORMAT_E     enVideoFormat  = VIDEO_FORMAT_LINEAR;
        COMPRESS_MODE_E    enCompressMode = COMPRESS_MODE_NONE;
        VI_VPSS_MODE_E     enMastPipeMode = VI_OFFLINE_VPSS_OFFLINE;

        /*config vi*/
        SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
        stViConfig.astViInfo[0].stSnsInfo.enSnsType = GC2053_TEST;
        stViConfig.astViInfo[1].stSnsInfo.enSnsType = GC2053_TEST;
        
        stViConfig.s32WorkingViNum                                   = s32ViCnt;
        stViConfig.as32WorkingViId[0]                                = 0;
        stViConfig.astViInfo[s32WorkSnsId].stSnsInfo.MipiDev         = 0;
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

        stViConfig.as32WorkingViId[1]                                = 1;
        stViConfig.astViInfo[s32WorkSnsId+1].stSnsInfo.MipiDev         = ViDev+1;
        stViConfig.astViInfo[s32WorkSnsId+1].stSnsInfo.s32BusId        = 1;
        stViConfig.astViInfo[s32WorkSnsId+1].stDevInfo.ViDev           = ViDev+1;
        stViConfig.astViInfo[s32WorkSnsId+1].stDevInfo.enWDRMode       = enWDRMode;
        stViConfig.astViInfo[s32WorkSnsId+1].stPipeInfo.enMastPipeMode = VI_OFFLINE_VPSS_OFFLINE;
        stViConfig.astViInfo[s32WorkSnsId+1].stPipeInfo.aPipe[0]       = ViPipe+1;
        stViConfig.astViInfo[s32WorkSnsId+1].stPipeInfo.aPipe[1]       = -1;
        stViConfig.astViInfo[s32WorkSnsId+1].stPipeInfo.aPipe[2]       = -1;
        stViConfig.astViInfo[s32WorkSnsId+1].stPipeInfo.aPipe[3]       = -1;
        stViConfig.astViInfo[s32WorkSnsId+1].stChnInfo.ViChn           = ViChn;
        stViConfig.astViInfo[s32WorkSnsId+1].stChnInfo.enPixFormat     = enPixFormat;
        stViConfig.astViInfo[s32WorkSnsId+1].stChnInfo.enDynamicRange  = enDynamicRange;
        stViConfig.astViInfo[s32WorkSnsId+1].stChnInfo.enVideoFormat   = enVideoFormat;
        stViConfig.astViInfo[s32WorkSnsId+1].stChnInfo.enCompressMode  = enCompressMode;

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
        stVbConf.astCommPool[0].u32BlkCnt   = 20;

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

        VI_DUMP_ATTR_S stDumpAttr;
        stDumpAttr.bEnable = HI_TRUE;
        stDumpAttr.u32Depth = 2;
        stDumpAttr.enDumpType = VI_DUMP_TYPE_YUV;

        // for(int i=0; i<s32ViCnt; i++)
        // {
        //     HI_MPI_VI_SetPipeDumpAttr(i, &stDumpAttr);
        // }

        for(int i=0; i<s32ViCnt; i++)
        {
            VI_CHN_ATTR_S stChnAttr;
            s32Ret = HI_MPI_VI_GetChnAttr(i, 0, &stChnAttr);
            if (HI_SUCCESS != s32Ret)
            {
                SAMPLE_PRT("HI_MPI_VI_GetChnAttr failed s32Ret:0x%x !\n", s32Ret);
            }
            else
            {
                stChnAttr.u32Depth = 1;
                s32Ret = HI_MPI_VI_SetChnAttr(i, 0, &stChnAttr);
                if (HI_SUCCESS != s32Ret)
                {
                    SAMPLE_PRT("HI_MPI_VI_SetChnAttr failed s32Ret:0x%x !\n", s32Ret);
                }
            }

            HI_MPI_VI_SetChnRotation(i, 0, ROTATION_180);
        }



        is_vi_inited_ = true;
        
        return s32Ret;

EXIT:
        SAMPLE_COMM_SYS_Exit();
        return s32Ret;

    }


    void vi_exit()
    {
        if(is_vi_inited_)
        {
            SAMPLE_COMM_VI_StopVi(&stViConfig);
        }
    }



    HI_S32 vo_init()
    {
        HI_S32             s32Ret = HI_SUCCESS;
        
        /*config vo*/
        SAMPLE_COMM_VO_GetDefConfig(&stVoConfig);

        stVoConfig.enVoMode = VO_MODE_4MUX;
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
            return s32Ret;
        }

        is_vo_inited_ = true;

        return s32Ret;

    }


    void vo_exit()
    {
        if(is_vo_inited_)
        {
            SAMPLE_COMM_VO_StopVO(&stVoConfig);
        }
    }
   



private:
    enum VO_INTERFACE_TYPE{
        VO_INTERFACE_TYPE_HDMI,
        VO_INTERFACE_TYPE_BT1120

    };

    std::atomic<bool> is_vi_inited_ ;        // VI是否初始化
    std::atomic<bool> is_vo_inited_ ;        // VO是否初始化

    SAMPLE_VI_CONFIG_S stViConfig;          // VI配置结构体

    SAMPLE_VO_CONFIG_S stVoConfig;

    HI_U32 u32VoIntfType = VO_INTERFACE_TYPE_HDMI;

    DYNAMIC_RANGE_E    enDynamicRange = DYNAMIC_RANGE_SDR8;
    PIC_SIZE_E         enPicSize;

};





