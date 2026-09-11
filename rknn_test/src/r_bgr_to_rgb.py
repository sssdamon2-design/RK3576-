import cv2
import numpy as np

img=cv2.imread("R.jpg")

print("yuan shape :",img.shape)
print("yuan dtype :",img.dtype)

#按比例缩放
src_h,src_w=img.shape[:2]
dst_w=640
dst_h=640
scale =   min (dst_w/src_w, dst_h/src_h)
print("scale= ", scale)

new_dst_w=int(scale * src_w)
new_dst_h=int(scale * src_h)

print("new_dst_w=",new_dst_w)
print("new_dst_h=",new_dst_h)

resized=cv2.resize(img,(new_dst_w,new_dst_h),interpolation=cv2.INTER_LINEAR)

print(resized.shape)

#开始填充为640x640

left=(dst_w-new_dst_w)//2
top=(dst_h-new_dst_h)//2
print("left =",left)
print("top =",top)

#创建 640×640 的灰色背景
canvas = np.full(
    (dst_h, dst_w, 3),
    114,
    dtype=np.uint8
)

#把resized之后的图片放进灰色背景里   top:top+..  表示取这些行

canvas[top:top+new_dst_h,left:left+new_dst_w] =  resized

#现在已经用灰色填充到640x640了

# 6. OpenCV BGR → RGB
rgb = cv2.cvtColor(
    canvas,
    cv2.COLOR_BGR2RGB
)

print("RKNN input shape :", rgb.shape)
print("RKNN input dtype :", rgb.dtype)


# 7. 保存纯 RGB 原始像素
rgb.tofile("R_640_rgb.raw") #把数组里的数据按照内存顺序，原样写到文件中。tofile 是nuphy的方法
print("RAW bytes        :", rgb.nbytes)
print("Saved            : R_640_rgb.raw")

# 8. 可选：保存一张预览图检查 Letterbox 是否正确
cv2.imwrite(
    "R_640_preview.jpg",
    canvas
)
