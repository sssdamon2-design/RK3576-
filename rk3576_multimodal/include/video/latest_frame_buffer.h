#pragma once

#include <memory>
#include <mutex>

#include "common/types.h"

class LatestFrameBuffer   //定义一个类  创建一个“最新帧仓库”。
{
public:   //外面的代码可以访问。
    using FramePtr = std::shared_ptr<const ImageFrame>;  //共享智能指针   多个地方同时用一个指针   都使用完毕之后自动释放内存
 //给一个很长的类型取一个简单名字。  using 表示定义FramePtr 就等于后面这一串  “别名”   const表示不允许通过这个指针修改这帧
    void update(FramePtr frame);   //把最新帧存入 `latest_frame_`。
//FramePtr frame = std::shared_ptr<const ImageFrame>  frame    创建一个智能指针变量 frame，它可以指向一个 ImageFrame 对象。
    FramePtr getLatest() const;  //拿到最新图像帧的共享智能指针。  返回一个图片智能指 针。
    // const表示这个函数承诺不会修改 LatestFrameBuffer 对象的正常成员状态。

private:  //只能 LatestFrameBuffer 自己内部使用。
    mutable std::mutex mutex_;          //互斥锁  类的私有成员变量末尾加 _     mutable表示即使在 const 函数里面，这个成员也允许修改

    FramePtr latest_frame_;
};