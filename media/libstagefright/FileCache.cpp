
#define LOG_TAG "FileCache"
#define LOG_DEBUG 0

#include <media/stagefright/FileCache.h>

#if LOG_DEBUG>=1
#define ALOGD(...)  printf("FileCache: " __VA_ARGS__), printf("  \n")
#define ALOGE(...) ALOGD(__VA_ARGS__)
#endif


namespace android {



RingBuffer::RingBuffer(size_t size):buf_addr(NULL),rd_index(0),
wr_index(0),getbuf_wakeup(0),getdata_wakeup(0),no_more_data(0),datacopy_wakeup(false)
{
    mSize=size;
}

RingBuffer::~RingBuffer()
{
    if(buf_addr){
        free(buf_addr);
        buf_addr = NULL;
    }
}

int32_t   RingBuffer:: init()
{
#if 0
    if(mSize < 0) {
        ALOGD("Error size:%d",mSize);
        return -1;
    }
#endif

    buf_addr=(uint8_t *)malloc(mSize);
    if(buf_addr != NULL) {
        return 0;
    }
    else {
        ALOGD("buffer Malloc ERROR");
        return -1;
    }
}

bool RingBuffer::isFull()
{
    return (dataCount() >= mSize);
}

bool  RingBuffer::isEmpty()
{
    return (wr_index == rd_index );
}

void  RingBuffer::reset()
{
    mLock_wr.lock();
    mLock_rd.lock();
    wr_index = 0;
    rd_index = 0;
    mLock_rd.unlock();
    mLock_wr.unlock();
}


size_t RingBuffer::dataCount()
{
    return (wr_index - rd_index);
}

int  RingBuffer::getBuf( Buffer * buf, uint32_t wait)
{
    uint32_t wr = 0;
    mLock_wr.lock();

    mBufLock.lock();
    if(isFull()) {
        if(wait) {
            if(!getbuf_wakeup) {
                //ALOGD("getBuf Wait. isFull:%d,wr_index:%zd,rd_index:%zd,this %p",isFull(),wr_index,rd_index,this);
                mBufCond.wait(mBufLock);
                //ALOGD("getBuf After wait. isFull:%d,wr_index:%zd,rd_index:%zd this %p",isFull(),wr_index,rd_index,this);
            }
            if(isFull() || getbuf_wakeup) {
                getbuf_wakeup = 0;
                mBufLock.unlock();
                mLock_wr.unlock();
                return -1;
            }
        }
        else {
            buf->mData = NULL;
            buf->mSize = 0;
            getbuf_wakeup = 0;
            mBufLock.unlock();
            mLock_wr.unlock();
            return -1;
        }
    }

    if(getbuf_wakeup){
        getbuf_wakeup = 0;
        mBufLock.unlock();
        mLock_wr.unlock();
        return -1;
    }

    //wr = wr_index & (mSize - 1);
    wr = wr_index % mSize;
    buf->mData= buf_addr + wr;
    if ((mSize - wr) > (mSize - dataCount()))
        buf->mSize = mSize - dataCount();
    else
        buf->mSize = mSize - wr;

    mBufLock.unlock();
    return 0;

}


void  RingBuffer::putData(Buffer *buf,int wake)
{
    mDataLock.lock();
    if(isFull()){
        // to do error
    }
    wr_index += buf->mSize;
    if(wake){
        mDataCond.signal();
        //ALOGD("putData wr signal,wr_index:%zd,rd_index:%zd,this %p",wr_index,rd_index,this);
    }
    mDataLock.unlock();
    mLock_wr.unlock();
    return ;
}

int  RingBuffer::getData(Buffer * buf ,uint32_t wait)
{
    uint32_t rd = 0;
    mLock_rd.lock();
    mDataLock.lock();
    if(isEmpty()){
        if(wait ){
            if(!getdata_wakeup) {
                if(no_more_data) {
                    getdata_wakeup = 0;
                    buf->mSize = 0;
                    mDataLock.unlock();
                    mLock_rd.unlock();
                    return -1;
                }
              //  ALOGD("getData Wait. isEmpty:%d,wr_index:%zd,rd_index:%zd,this %p",isEmpty(),wr_index,rd_index,this);
                mDataCond.wait(mDataLock);
             //   ALOGD("getData After wait. isEmpty:%d,wr_index:%zd,rd_index:%zd, this %p",isEmpty(),wr_index,rd_index,this);
            }
            if(isEmpty() || getdata_wakeup){
                getdata_wakeup = 0;
                buf->mSize = 0;
                mDataLock.unlock();
                mLock_rd.unlock();
                return -1;
            }
        }
        else{
            getdata_wakeup = 0;
            buf->mSize = 0;
            mDataLock.unlock();
            mLock_rd.unlock();
            return -1;
        }
    }

    if(getdata_wakeup) {
        getdata_wakeup = 0;
        buf->mSize = 0;
        mDataLock.unlock();
        mLock_rd.unlock();
        return -1;
    }

    //rd = rd_index & (mSize - 1);
    rd = rd_index % mSize;
    buf->mData = buf_addr + rd;
    if ((mSize - rd) > dataCount())
        buf->mSize = dataCount();
    else
        buf->mSize = mSize - rd;

    mDataLock.unlock();
    return 0;
}



void  RingBuffer::putBuf(Buffer *buf, int wake)
{
    mBufLock.lock();
    if(isFull()){
        // to do error
    }
    rd_index += buf->mSize;
    if(wake){
        mBufCond.signal();
    }
    mBufLock.unlock();
    mLock_rd.unlock();
    return ;
}

int  RingBuffer::dataCopy(uint32_t offset, Buffer *buf, int wait)
{
    uint32_t rd = 0;
    size_t read_size = buf->mSize;
    size_t avail = 0;
    size_t tail_avail = 0;
    uint8_t *cur_buf = (uint8_t *)buf->mData;
    mLock_rd.lock();
    mDataLock.lock();
    if(wait) {
        while(dataCount()<= offset && (!datacopy_wakeup)) {
            if(no_more_data) {
                datacopy_wakeup = 0;
                buf->mSize = 0;
                mDataLock.unlock();
                mLock_rd.unlock();
                return -1;
            }
         //   ALOGD("dataCopy. Need more data. Wait data count:%zd,offset:%d,flag:%d,size:%zd",dataCount(),offset,datacopy_wakeup,read_size);
            mDataCond.wait(mDataLock);
        //    ALOGD("dataCopy. After wait count:%zd,offset:%d,flag:%d",dataCount(),offset,datacopy_wakeup);
        }
    }
    else {
        if(offset >= dataCount()){
            datacopy_wakeup = 0;
            buf->mSize = 0;
            mDataLock.unlock();
            mLock_rd.unlock();
            return -1;
        }
    }
    if(datacopy_wakeup) {
        datacopy_wakeup = 0;
        buf->mSize = 0;
        mDataLock.unlock();
        mLock_rd.unlock();
        return -1;
    }

    rd = rd_index % mSize;
    avail = dataCount();
    mDataLock.unlock();
    tail_avail = mSize - rd;
    read_size = read_size > (avail - offset)? (avail - offset) : read_size;
    if(offset <= tail_avail) {
        if((tail_avail - offset) >= read_size) {
            memcpy(cur_buf, buf_addr + rd + offset, read_size);
        }
        else {
            memcpy(cur_buf, buf_addr + rd + offset, tail_avail - offset);
            memcpy(cur_buf + (tail_avail - offset),buf_addr, read_size - (tail_avail - offset));
        }
    }
    else {
        memcpy(cur_buf, buf_addr + (offset - tail_avail), read_size);
    }
    buf->mSize = read_size;
    mLock_rd.unlock();
    return 0;

}

size_t  RingBuffer::dataRelease(size_t size)
{
    Buffer buf;
    int ret = 0;
    size_t size_to_release = size;
    size_t cur_size = 0;
    while(size) {
        ret = getData(&buf,true);
        if(!ret) {
            cur_size = buf.mSize > size ? size:buf.mSize;
            buf.mSize = cur_size;
            putBuf(&buf, true);
            size -= cur_size;
        }
        else {
            break;
        }
    }

    return (size_to_release - size);
}

void  RingBuffer::wakeUpForGetBuf()
{
    Mutex::Autolock l(mBufLock);
    getbuf_wakeup = 1;
    mBufCond.signal();
}
void  RingBuffer::wakeUpForDataCopy()
{
    Mutex::Autolock l (mDataLock);
    datacopy_wakeup = 1;
    mDataCond.signal();
    ALOGD("wakeUpForDataCopy wr signal");
}

void  RingBuffer::wakeUpForGetData()
{
    Mutex::Autolock l (mDataLock);
    getdata_wakeup = 1;
    mDataCond.signal();
    ALOGD("wakeUpForGetData wr signal");
}

void  RingBuffer::wakeUpAndSetNoMoreData()
{
    Mutex::Autolock l (mDataLock);
    no_more_data = 1;
    mDataCond.signal();
    ALOGD("wakeUpAndSetNomoreData wr signal");
}

void  RingBuffer::cleanNoMoreData()
{
    Mutex::Autolock l (mDataLock);
    no_more_data = 0;
    ALOGD("cleanNomoreData ");
}




FileCache::FileCache(int Fd , int64_t length, size_t cacheSize, size_t maxPerRead, size_t preCacheSize):
mFd(Fd), mOffset(0), mLength(length), mCacheSize(cacheSize), mPreCacheSize(preCacheSize), errorCount(0), mThreadLoop_wakeup(false), mMaxPerRead(maxPerRead)
{
    mCommand = READ;
    mState = OK;
    mcount++;
    count = mcount;
}

FileCache::~FileCache()
{
    if(mRingBuffer) {
        delete mRingBuffer;
        mRingBuffer = NULL;
    }
}




int32_t   FileCache:: init()
{
    if(mFd < 0) {
        return -1;
    }
    if((long long)mCacheSize > (long long)mLength) {
        mCacheSize = mLength;
    }
    if(mMaxPerRead  >  mCacheSize/2) {
        return -1;
    }
    if(mPreCacheSize  >  mCacheSize/2) {
        return -1;
    }

    mRingBuffer = new RingBuffer(mCacheSize);
    ALOGE(" mRingBuffer %p",mRingBuffer);
    if(mRingBuffer != NULL) {
        int32_t status = mRingBuffer->init();
        if(!status) {
            const size_t SIZE = 256;
            char buffer[SIZE];
            snprintf(buffer, SIZE, "mp3 FileCahce Thread %p", this);
            run(buffer, PRIORITY_AUDIO);
            seekTo(mOffset);
            return 0;
        }
        else
            return -1;
    }
    else {
        ALOGD("init Malloc ERROR");
        return -1;
    }
}

 int  FileCache::mcount = 0;

void  FileCache::wakeUpForThreadLoop()
{
    Mutex::Autolock l (mLock_thread);
    mThreadLoop_wakeup = true;
    mCond_thread.signal();
}
void FileCache::exit()
{
    requestExit();
    wakeUpForThreadLoop();
    mRingBuffer->wakeUpForGetBuf();
    mRingBuffer->wakeUpForDataCopy();
    mRingBuffer->wakeUpForGetData();
    requestExitAndWait();
}


void FileCache:: seekTo(off64_t offset)
{
    mLock.lock();
    mOffset = offset;
    //ALOGD("SEEK offset:%lld,mOffset:%lld, %d",(long long)offset,(long long)mOffset, count);
    mCommand = SEEK;
    mRingBuffer->cleanNoMoreData();
    wakeUpForThreadLoop();
    mRingBuffer->wakeUpForGetBuf();
    ALOGD("seekTo Wait count %d",count);
    mWaitCond.wait(mLock);
    ALOGD("seekTo After Wait %d",count);
    mLock.unlock();
}

ssize_t FileCache::readAt(off64_t offset, void *data, size_t size)
{
    RingBuffer::Buffer  buf;
    size_t readSize;
    int ret = 0;
    size_t size_release = 0;
    if (mLength >= 0) {
        if (offset >= mLength) {
            return 0;  // read beyond EOF.
        }
        int64_t numAvailable = mLength - offset;
        if ((int64_t)size > numAvailable) {
            size = numAvailable;
        }
    }
    readSize = size;
    if((long long)offset >= (long long)mOffset&&offset<(long long)(mOffset+mCacheSize)) {
        buf.mData = data;
        while(readSize >0 ) {
            buf.mSize = readSize > mCacheSize? mCacheSize: readSize;
            if((long long)(offset - mOffset) >= (long long)(mMaxPerRead+mPreCacheSize)) {
                size_release = mRingBuffer->dataRelease(mMaxPerRead);
                mOffset += size_release;
            }
            ret = mRingBuffer->dataCopy(offset - mOffset,&buf,true);
            if(ret < 0) {
               // ALOGD("readAt offset:%lld,mOffset:%lld,count %d",(long long)offset,(long long)mOffset,count);
                return (size - readSize);
            }
            buf.mData = (uint8_t *)buf.mData + buf.mSize;
            readSize -= buf.mSize;
            offset += buf.mSize;
        }
        return size;
    }
    else {
        if((long long)offset>(long long)mPreCacheSize) {
            seekTo(offset-mPreCacheSize);
        }
        else {
            seekTo(0);
        }
        if(mState == OK) {
            return readAt(offset, data, size);
        }
        else {
            return SEEK_ERROR;
        }
    }
return 0;
}
void FileCache:: commandProcess()
{
    mLock.lock();
    switch(mCommand) {
        case NOP:
            break;
        case SEEK:
            off64_t result = lseek64(mFd, mOffset, SEEK_SET);
            if (result == -1) {
                ALOGD("seek to %lld failed",  (long long)mOffset);
                mState = SEEK_ERROR;
            }
            else {
                mState = OK;
            }
            mRingBuffer->reset();
            errorCount = 0;
            mCommand = NOP;
            mWaitCond.signal();
            break;
    }
    mLock.unlock();
}

bool FileCache:: threadLoop()
{
    sp <FileCache> strongMe = this;
    //uint8_t * trackBuf = NULL;
    int ret = 0;
    size_t preread_size = 0;
    RingBuffer::Buffer buffer;

    ALOGD("threadLoop start %d",count);
    while (!exitPending()) {
        commandProcess();
        ret = mRingBuffer->getBuf(&buffer,true);
        if(0==ret) {
            preread_size = buffer.mSize > mMaxPerRead ? mMaxPerRead: buffer.mSize;
            buffer.mSize = ::read(mFd, buffer.mData, preread_size);
            if(buffer.mSize <= preread_size /*&& buffer.mSize >= 0*/) {
                mRingBuffer->putData(&buffer,true);
                errorCount = 0;
                if(buffer.mSize < preread_size) {
                    mLock_thread.lock();
                    if(mThreadLoop_wakeup) {
                        mThreadLoop_wakeup = false;
                    }
                    else {
                        //ALOGD("threadLoop read EOS! Real read Size:%zd. And wait,%d",buffer.mSize,count);
                        mRingBuffer->wakeUpAndSetNoMoreData();
                        mCond_thread.wait(mLock_thread);
                        mRingBuffer->cleanNoMoreData();
                        ALOGD("threadLoop read EOS! After Wait, %d", count);
                    }
                    mLock_thread.unlock();
                }
            }
            else {
                errorCount++;
                if(errorCount>=10) {
                   // ALOGD("threadLoop read data ERROR size:%zd, count %d",buffer.mSize,count);
                    buffer.mSize = preread_size;
                    mRingBuffer->putData(&buffer,true);
                }
                else {
                  //  ALOGD("threadLoop read data ERROR 0 size:%zd, %d",buffer.mSize,count);
                    buffer.mSize = 0;
                    mRingBuffer->putData(&buffer,true);
                }
            }
        }
        else
            ALOGD("threadLoop getBuf SEEK,count %d",count);
    }
    ALOGD("thread loop exit refcount is %d,count %d",this->getStrongCount(), count);
    return false;
}

}


