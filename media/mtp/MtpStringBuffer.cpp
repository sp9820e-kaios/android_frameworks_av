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

#define LOG_TAG "MtpStringBuffer"

#include <string.h>
#include <ctype.h>

#include "MtpDataPacket.h"
#include "MtpStringBuffer.h"

namespace android {

MtpStringBuffer::MtpStringBuffer()
    :   mCharCount(0),
        mByteCount(1)
{
    mBuffer[0] = 0;
}

MtpStringBuffer::MtpStringBuffer(const char* src)
    :   mCharCount(0),
        mByteCount(1)
{
    set(src);
}

MtpStringBuffer::MtpStringBuffer(const uint16_t* src)
    :   mCharCount(0),
        mByteCount(1)
{
    set(src);
}

MtpStringBuffer::MtpStringBuffer(const MtpStringBuffer& src)
    :   mCharCount(src.mCharCount),
        mByteCount(src.mByteCount)
{
    memcpy(mBuffer, src.mBuffer, mByteCount);
}


MtpStringBuffer::~MtpStringBuffer() {
}

void MtpStringBuffer::set(const char* src) {
    // count the characters
    int count = 0;
    char ch;
    char* dest = (char*)mBuffer;

    while ((ch = *src++) != 0 && count < MTP_STRING_MAX_CHARACTER_NUMBER) {
        if ((ch & 0x80) == 0) {
            // single byte character
            *dest++ = ch;
        } else if ((ch & 0xE0) == 0xC0) {
            // two byte character
            char ch1 = *src++;
            if (! ch1) {
                // last character was truncated, so ignore last byte
                break;
            }

            *dest++ = ch;
            *dest++ = ch1;
        } else if ((ch & 0xF0) == 0xE0) {
            // 3 byte char
            char ch1 = *src++;
            if (! ch1) {
                // last character was truncated, so ignore last byte
                break;
            }
            char ch2 = *src++;
            if (! ch2) {
                // last character was truncated, so ignore last byte
                break;
            }

            *dest++ = ch;
            *dest++ = ch1;
            *dest++ = ch2;
        }
        count++;
    }

    *dest++ = 0;
    mByteCount = dest - (char*)mBuffer;
    mCharCount = count;
}

void MtpStringBuffer::set(const uint16_t* src) {
    int count = 0;
    uint16_t ch;
    uint8_t* dest = mBuffer;

    while ((ch = *src++) != 0 && count < MTP_STRING_MAX_CHARACTER_NUMBER) {
        if (ch >= 0x0800) {
            *dest++ = (uint8_t)(0xE0 | (ch >> 12));
            *dest++ = (uint8_t)(0x80 | ((ch >> 6) & 0x3F));
            *dest++ = (uint8_t)(0x80 | (ch & 0x3F));
        } else if (ch >= 0x80) {
            *dest++ = (uint8_t)(0xC0 | (ch >> 6));
            *dest++ = (uint8_t)(0x80 | (ch & 0x3F));
        } else {
            *dest++ = ch;
        }
        count++;
    }
    *dest++ = 0;
    mCharCount = count;
    mByteCount = dest - mBuffer;
}

bool MtpStringBuffer::readFromPacket(MtpDataPacket* packet) {
    uint8_t count;
    if (!packet->getUInt8(count))
        return false;

    uint8_t* dest = mBuffer;
    for (int i = 0; i < count; i++) {
        uint16_t ch;

        if (!packet->getUInt16(ch))
            return false;
        if (ch >= 0x0800) {
            *dest++ = (uint8_t)(0xE0 | (ch >> 12));
            *dest++ = (uint8_t)(0x80 | ((ch >> 6) & 0x3F));
            *dest++ = (uint8_t)(0x80 | (ch & 0x3F));
        } else if (ch >= 0x80) {
            *dest++ = (uint8_t)(0xC0 | (ch >> 6));
            *dest++ = (uint8_t)(0x80 | (ch & 0x3F));
        } else {
            *dest++ = ch;
        }
    }
    *dest++ = 0;
    mCharCount = count;
    mByteCount = dest - mBuffer;
    return true;
}

void MtpStringBuffer::writeToPacket(MtpDataPacket* packet) const {
    int count = mCharCount;
    const uint8_t* src = mBuffer;
    packet->putUInt8(count > 0 ? count + 1 : 0);

    // expand utf8 to 16 bit chars
    for (int i = 0; i < count; i++) {
        uint16_t ch;
        uint16_t ch1 = *src++;
        if ((ch1 & 0x80) == 0) {
            // single byte character
            ch = ch1;
        } else if ((ch1 & 0xE0) == 0xC0) {
            // two byte character
            uint16_t ch2 = *src++;
            ch = ((ch1 & 0x1F) << 6) | (ch2 & 0x3F);
        } else {
            // three byte character
            uint16_t ch2 = *src++;
            uint16_t ch3 = *src++;
            ch = ((ch1 & 0x0F) << 12) | ((ch2 & 0x3F) << 6) | (ch3 & 0x3F);
        }
        packet->putUInt16(ch);
    }
    // only terminate with zero if string is not empty
    if (count > 0)
        packet->putUInt16(0);
}

void MtpStringBuffer::trim() {
    size_t max_count = MTP_STRING_MAX_CHARACTER_NUMBER * 3 + 1;
    uint8_t buf[MTP_STRING_MAX_CHARACTER_NUMBER * 3 + 1] = {0};
    size_t size;
    uint8_t* start = mBuffer;
    uint8_t *end;
    const uint8_t *sc;

    for (sc = start; max_count-- && *sc != '\0'; ++sc)
        /* nothing */;
    size = sc - start;

    if (size) {
        end = start + size - 1;
        while (end >= start && isspace(*end))
            end--;
        *(end + 1) = '\0';

        while (isspace(*start) && start <= end)
            ++start;

        memcpy(buf, (const char*)(start), end - start + 1);
        set((const char*)buf);
    }
}

}  // namespace android
