/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AVC_UTILS_H_

#define AVC_UTILS_H_

#include <media/stagefright/foundation/ABuffer.h>
#include <utils/Errors.h>

namespace android {

struct ABitReader;

enum {
    kAVCProfileBaseline      = 0x42,
    kAVCProfileMain          = 0x4d,
    kAVCProfileExtended      = 0x58,
    kAVCProfileHigh          = 0x64,
    kAVCProfileHigh10        = 0x6e,
    kAVCProfileHigh422       = 0x7a,
    kAVCProfileHigh444       = 0xf4,
    kAVCProfileCAVLC444Intra = 0x2c
};

struct vc_params_t
{
    uint32_t width,height;
    uint32_t profile, level;
    uint64_t nal_length_size;
    void clear()
    {
        memset(this, 0, sizeof(*this));
    }
};

class NALBitstream
{
public:
    NALBitstream() : m_data(NULL), m_len(0), m_idx(0), m_bits(0), m_byte(0), m_zeros(0) {
    };
    NALBitstream(void * data, int len) {
        Init(data, len);
    };
    void Init(void * data, int len) {
        m_data = (uint8_t*)data;
        m_len = len;
        m_idx = 0;
        m_bits = 0;
        m_byte = 0;
        m_zeros = 0;
    };

    uint8_t GetBYTE(){
        if ( m_idx >= m_len )
        return 0;
        uint8_t b = m_data[m_idx++];

        // to avoid start-code emulation, a byte 0x03 is inserted
        // after any 00 00 pair. Discard that here.
        if ( b == 0 ){
            m_zeros++;
            if ( (m_idx < m_len) && (m_zeros == 2) && (m_data[m_idx] == 0x03) )
            {
                m_idx++;
                m_zeros=0;
            }
        } else {
            m_zeros = 0;
        }
        return b;
    };

    uint32_t GetBit()
    {
        if (m_bits == 0)
        {
            m_byte = GetBYTE();
            m_bits = 8;
        }
        m_bits--;
        return (m_byte >> m_bits) & 0x1;
    };

    uint32_t GetWord(int bits)
    {
        uint32_t u = 0;
        while ( bits > 0 ){
            u <<= 1;
            u |= GetBit();
            bits--;
        }
        return u;
    };

    uint32_t GetUE()
    {
        // Exp-Golomb entropy coding: leading zeros, then a one, then
        // the data bits. The number of leading zeros is the number of
        // data bits, counting up from that number of 1s as the base.
        // That is, if you see
        //      0001010
        // You have three leading zeros, so there are three data bits (010)
        // counting up from a base of 111: thus 111 + 010 = 1001 = 9
        int zeros = 0;
        while (m_idx < m_len && GetBit() == 0 ) zeros++;
        return GetWord(zeros) + ((1 << zeros) - 1);
    };

    int32_t GetSE()
    {
        // same as UE but signed.
        // basically the unsigned numbers are used as codes to indicate signed numbers in pairs
        // in increasing value. Thus the encoded values
        //      0, 1, 2, 3, 4
        // mean
        //      0, 1, -1, 2, -2 etc
        uint32_t UE = GetUE();
        bool positive = UE & 1;
        int32_t SE = (UE + 1) >> 1;
        if ( !positive )
        {
            SE = -SE;
        }
        return SE;
    };

    private:
        uint8_t* m_data;
        int m_len;
        int m_idx;
        int m_bits;
        uint8_t m_byte;
        int m_zeros;
};

bool ParseHEVCSPS(uint8_t* data,int size, vc_params_t& params);
void FindHEVCDimensions(const sp<ABuffer> &seqParamSet,
        int32_t *width, int32_t *height);

// Optionally returns sample aspect ratio as well.
void FindAVCDimensions(
        const sp<ABuffer> &seqParamSet,
        int32_t *width, int32_t *height,
        int32_t *sarWidth = NULL, int32_t *sarHeight = NULL);

unsigned parseUE(ABitReader *br);

status_t getNextNALUnit(
        const uint8_t **_data, size_t *_size,
        const uint8_t **nalStart, size_t *nalSize,
        bool startCodeFollows = false);

// Return SPS/PPS
void MakeAVCCodecSpecificData(const sp<ABuffer> &accessUnit,
        sp<ABuffer> &seqParamSet, sp<ABuffer> &picParamSet);

void MakeHEVCCodecSpecificData(const sp<ABuffer> &accessUnit,
    sp<ABuffer> &videoParamSet, sp<ABuffer> &seqParamSet, sp<ABuffer> &picParamSet);

struct MetaData;
sp<MetaData> MakeAVCCodecSpecificData(const sp<ABuffer> &accessUnit);

bool IsIDR(const sp<ABuffer> &accessUnit);
bool IsAVCReferenceFrame(const sp<ABuffer> &accessUnit);

const char *AVCProfileToString(uint8_t profile);

sp<MetaData> MakeAACCodecSpecificData(
        unsigned profile, unsigned sampling_freq_index,
        unsigned channel_configuration);

// Given an MPEG4 video VOL-header chunk (starting with 0x00 0x00 0x01 0x2?)
// parse it and fill in dimensions, returns true iff successful.
bool ExtractDimensionsFromVOLHeader(
        const uint8_t *data, size_t size, int32_t *width, int32_t *height);

bool GetMPEGAudioFrameSize(
        uint32_t header, size_t *frame_size,
        int *out_sampling_rate = NULL, int *out_channels = NULL,
        int *out_bitrate = NULL, int *out_num_samples = NULL);

void ProcessRawImage(char* rawFilePath, int width, int height, const char* yuvFilePath);
int ConvertARGB2YUV(int width,int height,unsigned char *bmp,unsigned int *yuv);
void InitLookupTable();

}  // namespace android

#endif  // AVC_UTILS_H_
