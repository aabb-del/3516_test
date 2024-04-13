/**
 * @file expand.h
 * @author your name (you@domain.com)
 * @brief 在官方例子上进行最小的修改来扩充一些功能
 * @version 0.1
 * @date 2023-07-19
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef EXPAND_H
#define EXPAND_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include "sample_comm.h"

typedef void (*VENC_STREAM_CALLBACK_FUNC_T)(VENC_CHN VeChn,VENC_STREAM_S* pstStream);

void VENC_SetStreamCallback(VENC_STREAM_CALLBACK_FUNC_T Callback);

/**
 * @brief 回调函数数据处理示例，即怎么去获取编码数据
 * 
for (i = 0; i < pstStream->u32PackCount; i++)
{
    fwrite(pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset,
            pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, pFd);

    fflush(pFd);
}
 *
 *
 */


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */


#endif