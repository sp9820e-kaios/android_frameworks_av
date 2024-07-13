/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "MP3FileSource"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MP3FileSource.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <media/stagefright/FileCache.h>

#define CACHE_SIZE 1024*320
#define PRE_SIZE 1024*4
#define PER_READ 1024*16
namespace android {

MP3FileSource::MP3FileSource(int fd, int64_t offset, int64_t length)
    : mFd(fd),
      mOffset(offset),
      mLength(length),
      mFileCache_flag(-1) {
    CHECK(offset >= 0);
    CHECK(length >= 0);

    //ALOGD("use  MP3FileSource fd:%d,offset:%lld,length:%lld",fd,(long long)offset,(long long)length);
    int64_t cacheSize = CACHE_SIZE;
    size_t perRead = PER_READ;
    size_t preSize = PRE_SIZE;
    if(length < CACHE_SIZE) {
        cacheSize = length;
    }
    if(PER_READ > length/2) {
        perRead = length/2;
    }
    if(PRE_SIZE > length/2) {
        preSize = length/2;
    }
    //ALOGD("use  MP3FileSource after change   cacheSize:%lld,perRead:%zd,preSize:%zd",(long long)cacheSize,perRead,preSize);

    mFilecache = new FileCache(fd,length+mOffset,cacheSize,perRead,preSize);
    mFileCache_flag = mFilecache->init();

}

MP3FileSource::~MP3FileSource() {
//ALOGD("~MP3filesource fd:%d,length:%lld",mFd,(long long)mLength);
    if(!(mFilecache == NULL)) {
        mFilecache->exit();
    }
    mFilecache.clear();
}

status_t MP3FileSource::initCheck() const {
    return mFd >= 0 ? OK : NO_INIT;
}
ssize_t MP3FileSource::readAt(off64_t offset, void *data, size_t size) {
    if (mFd < 0) {
        return NO_INIT;
    }
    Mutex::Autolock autoLock(mLock);

    if (mLength >= 0) {
        if (offset >= mLength) {
            return 0;  // read beyond EOF.
        }
        int64_t numAvailable = mLength - offset;
        if ((int64_t)size > numAvailable) {
            size = numAvailable;
        }
    }

    if(mFileCache_flag < 0) {
        off64_t result = lseek64(mFd, offset + mOffset, SEEK_SET);
        if (result == -1) {
            ALOGE("seek to failed");
            return UNKNOWN_ERROR;
        }
        return ::read(mFd, data, size);
    }
    else {
        ssize_t readsize = mFilecache->readAt(offset+ mOffset,data,size);
        if(readsize>=0)
            return readsize;
        else
            return UNKNOWN_ERROR;
    }

}

status_t MP3FileSource::getSize(off64_t *size) {
    Mutex::Autolock autoLock(mLock);

    if (mFd < 0) {
        return NO_INIT;
    }
    *size = mLength;
    return OK;
}


}  // namespace android

