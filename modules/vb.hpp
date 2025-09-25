#pragma once

#include <queue>

using namespace std;

class VB{
public:
    VB(){

    }
    ~VB(){
        while(!m_vb_handle_queue.empty())
        {
           auto VbHandle = m_vb_handle_queue.front();
           HI_MPI_VB_ReleaseBlock(VbHandle);
           m_vb_handle_queue.pop();
        }
        SAMPLE_PRT("vb quit!\n");
    }

    int get_frame_vb(VIDEO_FRAME_INFO_S *pstFrameInfo,  HI_U32 u32Width = 1920, HI_U32 u32Height = 1080)
    {
        HI_U32 u32VBSize = u32Width * u32Height * 2;
        VB_BLK VbHandle = HI_MPI_VB_GetBlock(VB_INVALID_POOLID, u32VBSize, HI_NULL);
    
        if (VB_INVALID_HANDLE == VbHandle)
        {
            SAMPLE_PRT("HI_MPI_VB_GetBlock failed!\n");
            return HI_FAILURE;
        }

        HI_U64 u64PhyAddr = HI_MPI_VB_Handle2PhysAddr(VbHandle);
        if (0 == u64PhyAddr)
        {
            SAMPLE_PRT("HI_MPI_VB_Handle2PhysAddr failed!.\n");
            HI_MPI_VB_ReleaseBlock(VbHandle);
            return HI_FAILURE;
        }

        SAMPLE_PRT("HI_MPI_SYS_Mmap u64PhyAddr %llx!.\n", u64PhyAddr);

        HI_U8* pu8VirAddr = (HI_U8*)HI_MPI_SYS_Mmap(u64PhyAddr, u32VBSize);
        if (HI_NULL == pu8VirAddr)
        {
            SAMPLE_PRT("HI_MPI_SYS_Mmap failed!.\n");
            HI_MPI_VB_ReleaseBlock(VbHandle);
            
            return HI_FAILURE;
        }


        pstFrameInfo->enModId = HI_ID_USER;
        pstFrameInfo->u32PoolId = HI_MPI_VB_Handle2PoolId(VbHandle);

        pstFrameInfo->stVFrame.u32Width       = u32Width;
        pstFrameInfo->stVFrame.u32Height      = u32Height;
        pstFrameInfo->stVFrame.enField        = VIDEO_FIELD_FRAME;
        pstFrameInfo->stVFrame.enPixelFormat  = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        pstFrameInfo->stVFrame.enVideoFormat  = VIDEO_FORMAT_LINEAR;
        pstFrameInfo->stVFrame.enCompressMode = COMPRESS_MODE_NONE;
        pstFrameInfo->stVFrame.enDynamicRange = DYNAMIC_RANGE_SDR8;
        pstFrameInfo->stVFrame.enColorGamut   = COLOR_GAMUT_BT601;

        pstFrameInfo->stVFrame.u32HeaderStride[0]  = u32Width;
        pstFrameInfo->stVFrame.u32HeaderStride[1]  = u32Width;
        pstFrameInfo->stVFrame.u32HeaderStride[2]  = u32Width;
        pstFrameInfo->stVFrame.u64HeaderPhyAddr[0] = u64PhyAddr;
        pstFrameInfo->stVFrame.u64HeaderPhyAddr[1] = pstFrameInfo->stVFrame.u64HeaderPhyAddr[0] + u32Width*u32Height;
        pstFrameInfo->stVFrame.u64HeaderPhyAddr[2] = pstFrameInfo->stVFrame.u64HeaderPhyAddr[1];
        pstFrameInfo->stVFrame.u64HeaderVirAddr[0] = (HI_U64)(HI_UL)pu8VirAddr;
        pstFrameInfo->stVFrame.u64HeaderVirAddr[1] = pstFrameInfo->stVFrame.u64HeaderVirAddr[0] + u32Width*u32Height;
        pstFrameInfo->stVFrame.u64HeaderVirAddr[2] = pstFrameInfo->stVFrame.u64HeaderVirAddr[1];



        pstFrameInfo->stVFrame.u32Stride[0]  = u32Width;
        pstFrameInfo->stVFrame.u32Stride[1]  = u32Width;
        pstFrameInfo->stVFrame.u32Stride[2]  = u32Width;
        pstFrameInfo->stVFrame.u64PhyAddr[0] = u64PhyAddr;
        pstFrameInfo->stVFrame.u64PhyAddr[1] = pstFrameInfo->stVFrame.u64PhyAddr[0] + u32Width*u32Height;
        pstFrameInfo->stVFrame.u64PhyAddr[2] = pstFrameInfo->stVFrame.u64PhyAddr[1];
        pstFrameInfo->stVFrame.u64VirAddr[0] = pu8VirAddr;
        pstFrameInfo->stVFrame.u64VirAddr[1] = pstFrameInfo->stVFrame.u64VirAddr[0] + u32Width*u32Height;
        pstFrameInfo->stVFrame.u64VirAddr[2] = pstFrameInfo->stVFrame.u64VirAddr[1];

        m_vb_handle_queue.push(VbHandle);
        return 0;
    }   
private:
    std::queue<VB_BLK> m_vb_handle_queue;
};