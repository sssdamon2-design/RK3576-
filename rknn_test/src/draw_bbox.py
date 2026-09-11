import cv2

img = cv2.imread("R_640_preview.jpg")

if img is None:
    raise RuntimeError("Failed to read image")

x1 = 140
y1 = 187
x2 = 587
y2 = 450

cv2.rectangle(
    img,
    (x1, y1),
    (x2, y2),
    (0, 255, 0),
    2
)

cv2.putText(
    img,
    "cat 0.889",
    (x1, y1 - 10),
    cv2.FONT_HERSHEY_SIMPLEX,
    0.8,
    (0, 255, 0),
    2
)

cv2.imwrite(
    "R_detect.jpg",
    img
)

print("saved: R_detect.jpg")