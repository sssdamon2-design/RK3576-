from rknn.api import RKNN
from pathlib import Path
import sys


# ============================================================
# RK3576 YOLOv8n INT8 PTQ conversion
#
# PTQ = Post-Training Quantization
#     = 训练后量化
#
# Calibration = 校准
#     = 用代表性图片统计各层 Activation（激活值）分布
# ============================================================

WORKSPACE = Path.home() / "rknn_workspace"

ONNX_PATH = WORKSPACE / "models" / "yolov8n.onnx"
DATASET_PATH = WORKSPACE / "dataset.txt"
OUTPUT_RKNN_PATH = WORKSPACE / "models" / "yolov8n_int8_230.rknn"

TARGET_PLATFORM = "rk3576"


def check_file(path: Path, name: str) -> None:
    if not path.is_file():
        print(f"[ERROR] {name} not found: {path}")
        sys.exit(1)


def count_dataset_lines(path: Path) -> int:
    count = 0

    with path.open("r", encoding="utf-8") as f:
        for line in f:
            if line.strip():
                count += 1

    return count


def main():
    print("========== RKNN INT8 PTQ BUILD ==========")
    print(f"ONNX     : {ONNX_PATH}")
    print(f"Dataset  : {DATASET_PATH}")
    print(f"Output   : {OUTPUT_RKNN_PATH}")
    print(f"Platform : {TARGET_PLATFORM}")

    check_file(ONNX_PATH, "ONNX model")
    check_file(DATASET_PATH, "Calibration dataset list")

    dataset_count = count_dataset_lines(DATASET_PATH)

    print(f"Calibration images listed: {dataset_count}")

    if dataset_count == 0:
        print("[ERROR] dataset.txt is empty")
        return 1

    # verbose=True:
    # 打印更完整的 Toolkit Build 日志。
    # 这次我们需要观察 Quantization / outlier / dtype 等信息，
    # 所以故意打开详细日志。
    rknn = RKNN(verbose=True)

    try:
        # ----------------------------------------------------
        # 1. Config
        #
        # 保持与当前 FP16 模型完全一致：
        #
        # input:
        #   RGB UINT8 0~255
        #
        # Toolkit preprocessing:
        #   (x - 0) / 255
        #
        # 这样 FP16 / INT8 的输入语义一致，
        # A/B 对比才有效。
        # ----------------------------------------------------
        print("\n--> Config model")

        ret = rknn.config(
            mean_values=[[0, 0, 0]],
            std_values=[[255, 255, 255]],
            target_platform=TARGET_PLATFORM
        )

        if ret != 0:
            print(f"[ERROR] rknn.config failed: {ret}")
            return ret

        print("done")


        # ----------------------------------------------------
        # 2. Load ONNX
        # ----------------------------------------------------
        print("\n--> Loading ONNX")

        ret = rknn.load_onnx(
            model=str(ONNX_PATH)
        )

        if ret != 0:
            print(f"[ERROR] rknn.load_onnx failed: {ret}")
            return ret

        print("done")


        # ----------------------------------------------------
        # 3. Build + INT8 PTQ
        #
        # do_quantization=True:
        #   开启训练后量化
        #
        # dataset:
        #   Calibration 图片清单
        #
        # Toolkit 会用这些图片进行校准，
        # 统计网络中间 Activation 分布并确定量化参数。
        # ----------------------------------------------------
        print("\n--> Building INT8 RKNN")

        ret = rknn.build(
            do_quantization=True,
            dataset=str(DATASET_PATH)
        )

        if ret != 0:
            print(f"[ERROR] rknn.build failed: {ret}")
            return ret

        print("done")


        # ----------------------------------------------------
        # 4. Export
        # ----------------------------------------------------
        print("\n--> Export RKNN")

        ret = rknn.export_rknn(
            str(OUTPUT_RKNN_PATH)
        )

        if ret != 0:
            print(f"[ERROR] rknn.export_rknn failed: {ret}")
            return ret

        print("done")


        print("\n========== BUILD SUCCESS ==========")
        print(f"INT8 model: {OUTPUT_RKNN_PATH}")

        return 0

    finally:
        rknn.release()


if __name__ == "__main__":
    sys.exit(main())
