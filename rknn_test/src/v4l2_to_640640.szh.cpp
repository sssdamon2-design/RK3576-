#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>
#include <linux/dma-heap.h>

#include <rga/im2d.h>
#include <rga/rga.h>

#define CAM_W       3840
#define CAM_H       2160

#define RESIZE_W    640
#define RESIZE_H    360

#define MODEL_W     640
#define MODEL_H     640

#define PAD_TOP     140
#define PAD_VALUE   114

struct CameraBuffer
{
    int dma_fd;
};

int main()
{
int camera_fd =open("/dev/video11", O_RDWR);

if (camera_fd < 0)
{
    perror("open camera");
    return -1;
}

struct v4l2_format fmt;
memset(&fmt, 0, sizeof(fmt));

fmt.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

fmt.fmt.pix_mp.width =CAM_W;

fmt.fmt.pix_mp.height =CAM_H;

fmt.fmt.pix_mp.pixelformat =V4L2_PIX_FMT_NV12;

fmt.fmt.pix_mp.field =V4L2_FIELD_NONE;

if (ioctl(camera_fd,VIDIOC_S_FMT,&fmt) < 0)
{
    perror("VIDIOC_S_FMT");
    return -1;
}

struct v4l2_requestbuffers req;
memset(&req, 0, sizeof(req));

req.count = 4;

req.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

req.memory =V4L2_MEMORY_MMAP;

ioctl(camera_fd,VIDIOC_REQBUFS,&req);

CameraBuffer *buffers =(CameraBuffer *)calloc(req.count,sizeof(CameraBuffer));   //动态申请结构体保存dma-buf fd

for (unsigned int i = 0;i < req.count;i++)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    buf.memory =V4L2_MEMORY_MMAP;

    buf.index = i;

    buf.m.planes =planes;

    buf.length = 1;

    ioctl(camera_fd,VIDIOC_QUERYBUF,&buf);

    struct v4l2_exportbuffer expbuf;

    memset(&expbuf,0,sizeof(expbuf));

    expbuf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    expbuf.index = i;
    expbuf.plane = 0;

    ioctl(camera_fd,VIDIOC_EXPBUF,&expbuf);

    buffers[i].dma_fd =expbuf.fd;





}











}