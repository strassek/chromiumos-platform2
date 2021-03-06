/*
 * Copyright (C) 2016-2019 Intel Corporation.
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

#define LOG_TAG "InputFrameWorker"

#include "InputFrameWorker.h"
#include <sys/mman.h>

#include "PerformanceTraces.h"
#include "NodeTypes.h"

namespace cros {
namespace intel {

InputFrameWorker::InputFrameWorker(std::shared_ptr<cros::V4L2VideoNode> node,
        int cameraId, size_t pipelineDepth) :
        /* Keep the same number of buffers as ISYS. */
        FrameWorker(node, cameraId, pipelineDepth + EXTRA_CIO2_BUFFER_NUMBER, "InputFrameWorker"),
        mPipeType(GraphConfig::PIPE_MAX),
        mLastRequestId(-1),
        mPipelineDepth(pipelineDepth)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
    mPollMe = true;
}

InputFrameWorker::~InputFrameWorker()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);
}

status_t InputFrameWorker::configure(std::shared_ptr<GraphConfig>& config)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1, LOG_TAG);

    mPipeType = config->getPipeType();
    LOG2("@%s, mPipeType:%d", __FUNCTION__, mPipeType);

    status_t ret = mNode->GetFormat(&mFormat);
    if (ret != OK)
        return ret;

    ret = setWorkerDeviceBuffers(getDefaultMemoryType(IMGU_NODE_INPUT));
    if (ret != OK)
        return ret;

    return OK;
}

void InputFrameWorker::dumpRaw(const cros::V4L2Buffer &v4l2Buf, Camera3Request &request, int lastReqId)
{
    int jpegBufCnt = request.getBufferCountOfFormat(HAL_PIXEL_FORMAT_BLOB);
    int dumpReqId = request.getId();
    /* For STILL pipe, the raw buffer may be queued twice in ImguUnit::ImguPipe::processNextRequest.
     * The lastRawNonScaledBuffer buffer is used for current request,
     * and rawNonScaledBuffer buffer is used for next request. */
    if (dumpReqId == lastReqId) dumpReqId = dumpReqId + 1;

    bool dumpRawAll  = LogHelper::isDumpTypeEnable(CAMERA_DUMP_RAW);
    bool dumpForJpeg = (LogHelper::isDumpTypeEnable(CAMERA_DUMP_RAW_FOR_JPEG) &&
                       mPipeType == GraphConfig::PIPE_STILL &&
                       jpegBufCnt > 0);
    bool dumpForVideoPipe = (LogHelper::isDumpTypeEnable(CAMERA_DUMP_RAW_FOR_VIDEO_PIPE) &&
                            mPipeType == GraphConfig::PIPE_VIDEO);
    bool dumpForStillPipe = (LogHelper::isDumpTypeEnable(CAMERA_DUMP_RAW_FOR_STILL_PIPE) &&
                            mPipeType == GraphConfig::PIPE_STILL);

    if (!dumpRawAll && !dumpForJpeg && !dumpForVideoPipe && !dumpForStillPipe) {
        return;
    }

    CheckError((v4l2Buf.Memory() != V4L2_MEMORY_DMABUF), VOID_VALUE,
               "just support V4L2_MEMORY_DMABUF dump");

    uint32_t size = v4l2Buf.Length(0);
    int v4l2Buffd = v4l2Buf.Fd(0);

    if (mDumpRawImage == nullptr) {
        std::string streamType = (mPipeType == GraphConfig::PIPE_STILL ? "still" : "video");
        mDumpRawImage = std::make_unique<CameraDumpAsync>(streamType, mPipelineDepth,
                                                          mFormat.Width(), mFormat.Height(), size);
    }

    void* addr = mmap(nullptr, size, PROT_READ, MAP_SHARED, v4l2Buffd, 0);
    CheckError((addr == MAP_FAILED), VOID_VALUE, "mmap fails");

    std::string name;
    if (mPipeType == GraphConfig::PIPE_STILL && jpegBufCnt > 0) {
        name = "vector_raw_for_jpeg";
    } else if (mPipeType == GraphConfig::PIPE_VIDEO) {
        name = "vector_raw_for_video_pipe";
    } else if (mPipeType == GraphConfig::PIPE_STILL) {
        name = "vector_raw_for_still_pipe";
    }

    mDumpRawImage->dumpImageToFile(addr, size, dumpReqId, name);

    munmap(addr, size);
}

status_t InputFrameWorker::prepareRun(std::shared_ptr<DeviceMessage> msg)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);

    int memType = mNode->GetMemoryType();
    CheckError(memType != V4L2_MEMORY_DMABUF, BAD_VALUE,
               "@%s unsupported memory type %d.", __func__, memType);

    Camera3Request* request = msg->pMsg.processingSettings->request;
    CheckError(!request, BAD_VALUE, "@%s request is nullptr", __func__);

    LOG2("@%s, mPipeType:%d, request id %d, mLastRequestId %d",
          __FUNCTION__, mPipeType, request->getId(), mLastRequestId);

    std::shared_ptr<cros::V4L2Buffer> rawV4L2Buf = msg->pMsg.rawNonScaledBuffer;
    if (mPipeType == GraphConfig::PIPE_STILL &&
        msg->pMsg.lastRawNonScaledBuffer &&
        request->getId() > (mLastRequestId + 1)) {
        rawV4L2Buf = msg->pMsg.lastRawNonScaledBuffer;
    }

    int index = rawV4L2Buf->Index();
    mBuffers[index].SetFd(rawV4L2Buf->Fd(0), 0);
    int fd = mBuffers[index].Fd(0);
    CheckError(fd < 0, BAD_VALUE, "@%s invalid fd(%d) passed from isys.\n", __func__, fd);

    status_t status = mNode->PutFrame(&mBuffers[index]);

    request->setSeqenceId(rawV4L2Buf->Sequence());
    PERFORMANCE_HAL_ATRACE_PARAM1("seqId", rawV4L2Buf->Sequence());

    dumpRaw(mBuffers[index], *request, mLastRequestId);

    mLastRequestId = request->getId();

    return status;
}

status_t InputFrameWorker::run()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    return OK;
}

status_t InputFrameWorker::postRun()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2, LOG_TAG);
    cros::V4L2Buffer outBuf;
    status_t status = mNode->GrabFrame(&outBuf);

    return (status < 0) ? status : OK;
}

} /* namespace intel */
} /* namespace cros */
