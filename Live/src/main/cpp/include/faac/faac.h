/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: faac.h,v 1.36 2009/01/25 18:50:32 menno Exp $
 */

#ifndef _FAAC_H_
#define _FAAC_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(_WIN32) && !defined(__MINGW32__)
# ifndef FAACAPI
#  define FAACAPI __stdcall
# endif
#else
# ifndef FAACAPI
#  define FAACAPI
# endif
#endif

#pragma pack(push, 1)

typedef struct {
    void *ptr;
    char *name;
}
        psymodellist_t;

#include "faaccfg.h"


typedef void *faacEncHandle;

#ifndef HAVE_INT32_T
typedef signed int int32_t;
#endif

/*
	Allows an application to get FAAC version info. This is intended
	purely for informative purposes.

	Returns FAAC_CFG_VERSION.
*/
int FAACAPI faacEncGetVersion(char **faac_id_string,
                              char **faac_copyright_string);


faacEncConfigurationPtr FAACAPI faacEncGetCurrentConfiguration(faacEncHandle hEncoder);


/**
 * FAAC编码器的配置参数
 * @param hEncoder
 * @param config  编码器配置参数
 * @return
 */
int FAACAPI faacEncSetConfiguration(faacEncHandle hEncoder, faacEncConfigurationPtr config);


/**
 * 初始化FAAC编码器，并返回一个编码器句柄
 * @param sampleRate
 * @param numChannels
 * @param inputSamples
 * @param maxOutputBytes
 * @return
 */
faacEncHandle FAACAPI faacEncOpen(unsigned long sampleRate,
                                  unsigned int numChannels,
                                  unsigned long *inputSamples,
                                  unsigned long *maxOutputBytes);


/**
 * 获取 AAC 编码器参数中 decoder specific info
 * @param hEncoder
 * @param ppBuffer   ppBuffer 是指针的指针，用于存储 decoder specific info
 * @param pSizeOfDecoderSpecificInfo  ，pSizeOfDecoderSpecificInfo 用于存储 decoder specific info 的长度
 * @return   函数返回解码器特定信息的长度
 */
int FAACAPI faacEncGetDecoderSpecificInfo(faacEncHandle hEncoder, unsigned char **ppBuffer,
                                          unsigned long *pSizeOfDecoderSpecificInfo);


/**
 * 将PCM音频数据编码为AAC格式的音频数据
 * @param hEncoder      编码器句柄
 * @param inputBuffer   输入PCM音频数据的缓冲区指针
 * @param samplesInput  输入PCM音频数据的样本数
 * @param outputBuffer  输出AAC格式音频数据的缓冲区指针
 * @param bufferSize    输出AAC格式音频数据的缓冲区大小
 * @return     编码后的字节数，如果返回0表示编码失败。
 */
int FAACAPI faacEncEncode(faacEncHandle hEncoder, int32_t *inputBuffer, unsigned int samplesInput,
                          unsigned char *outputBuffer,
                          unsigned int bufferSize);


/**
 * 关闭FAAC编码器
 * @param hEncoder  编码器句柄
 * @return
 */
int FAACAPI faacEncClose(faacEncHandle hEncoder);


#pragma pack(pop)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _FAAC_H_ */
