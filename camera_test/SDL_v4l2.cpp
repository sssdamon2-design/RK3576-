#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h> 
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <SDL2/SDL.h>


int main()
{
    const char *device ="/dev/video11";
    int fd;

    fd=open(device,O_RDWR);
    
    if (fd<0)
    {
        perror("open");
        close(fd);
        return -1;
    }
    
    printf("open %s successfully\n",device);

    struct v4l2_capability cap;
    
    memset(&cap,0,sizeof(cap));

    if(ioctl(fd,VIDIOC_QUERYCAP,&cap))
    {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }

    printf("driver=%s\n",cap.driver);
    printf("card=%s\n",cap.card);
    printf("capabilities=0x%x\n",cap.capabilities);

    if(!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        printf("do not support V4L2_CAP_VIDEO_CAPTURE_MPLANE\n");
    }

     if (!(cap.device_caps & V4L2_CAP_STREAMING))   
    {
        printf("do not support V4L2_CAP_STREAMING\n");

    }


    struct v4l2_format fmt ;
    memset(&fmt,0,sizeof(fmt));
    fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width=3840;
    fmt.fmt.pix_mp.height=2160;
    fmt.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field=V4L2_FIELD_NONE;
    if(ioctl(fd,VIDIOC_S_FMT,&fmt))
    {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    printf("width=%u\n",fmt.fmt.pix_mp.width);
    printf("height=%u\n",fmt.fmt.pix_mp.height);
    printf("Number of planes: %u\n",fmt.fmt.pix_mp.num_planes);
    

    struct v4l2_requestbuffers req;
    memset(&req,0,sizeof(req));
    req.count=4;
    req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory=V4L2_MEMORY_MMAP;

    if(ioctl(fd,VIDIOC_REQBUFS,&req))
    {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    printf("we have %d buffers\n",req.count);
    
    struct Buffer
    {
        void *start;
        size_t length;   
    };

    struct Buffer *buffers;

    buffers=(struct Buffer *)calloc(req.count,sizeof(struct Buffer));
    
    if (buffers == NULL)
{
    perror("calloc");
    close(fd);
    return -1;
}

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];


    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    unsigned int i;

    for(i=0;i<req.count;i++)
    {
        buf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; 
        buf.memory =V4L2_MEMORY_MMAP; 
        buf.index = i; 
        buf.m.planes = planes;
        buf.length = 1;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);  
        buffers[i].length =planes[0].length;
        buffers[i].start=mmap(
    NULL,
    planes[0].length,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd,
    planes[0].m.mem_offset
);
        if (buffers[i].start == MAP_FAILED)
{
    perror("mmap");
    
}

    printf("Buffer %u: address=%p length=%zu\n",
       i,
       buffers[i].start,
       buffers[i].length);
    }


    for (i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;                              
        struct v4l2_plane planes[1]; 
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory =V4L2_MEMORY_MMAP;
        buf.index = i;                   
        buf.m.planes = planes;             
        buf.length = 1;                    
        if (ioctl(fd, VIDIOC_QBUF,&buf) < 0)
        {
            perror("VIDIOC_QBUF");
            return 1;
        }


        printf("入队 %u\n", i);
    }   
    
    /*
 * 第 7 步：
 * 启动视频流
 */
enum v4l2_buf_type type;

type =
    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

if (ioctl(fd,
          VIDIOC_STREAMON,
          &type) < 0)
{
    perror("VIDIOC_STREAMON");
    return 1;
}

printf("Streaming started\n");

//开始取帧

int number;

for(number=0;number<100;number++)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[1];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type =V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    buf.memory =V4L2_MEMORY_MMAP;

    buf.m.planes = planes;

    buf.length = 1;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
    {
        perror("VIDIOC_DQBUF");
        break;
    }

    printf("buffer = %u, bytesused = %u\n",buf.index,planes[0].bytesused);

    if(ioctl(fd, VIDIOC_QBUF, &buf)<0)
    {
        perror("VIDIOC_QBUF");
        break;
    }
}




if (ioctl(fd,VIDIOC_STREAMOFF,&type) < 0)
{
    perror("VIDIOC_STREAMOFF");
}

printf("Streaming stopped\n");









    for (i = 0; i < req.count; i++)
{
    munmap(
        buffers[i].start,       
          buffers[i].length       
    );
}
    free(buffers);
    close(fd);
    return 0;
}