import zlib
import struct
import math
import os

def create_car_icon(size, output_path):
    # 创建一个带有圆角汽车徽章的 PNG 图像 (纯 Python 标准库，无外部依赖)
    width = size
    height = size
    pixels = []

    center_x = width / 2.0
    center_y = height / 2.0
    corner_radius = width * 0.22

    for y in range(height):
        row = []
        for x in range(width):
            # 计算圆角矩形边界
            dx = max(0.0, abs(x - center_x) - (width / 2.0 - corner_radius))
            dy = max(0.0, abs(y - center_y) - (height / 2.0 - corner_radius))
            dist = math.sqrt(dx * dx + dy * dy)

            if dist > corner_radius:
                # 外部透明
                row.extend([0, 0, 0, 0])
                continue

            # 内部渐变背景：深蓝到靛蓝渐变
            ratio_y = y / float(height)
            bg_r = int(37 + ratio_y * (30 - 37))
            bg_g = int(99 + ratio_y * (58 - 99))
            bg_b = int(235 + ratio_y * (138 - 235))

            # 简易车轮与车身图案数学坐标
            nx = (x - center_x) / (width * 0.38)
            ny = (y - center_y) / (height * 0.38)

            is_car = False
            # 车身主体
            if -0.85 <= nx <= 0.85 and -0.15 <= ny <= 0.35:
                is_car = True
            # 车顶挡风玻璃流线
            if -0.55 <= nx <= 0.55 and -0.55 <= ny <= -0.15:
                # 梯形车顶
                allowed_width = 0.55 - (-ny - 0.15) * 0.5
                if abs(nx) <= allowed_width:
                    is_car = True
            # 车轮镂空与轮胎
            wheel1_dist = math.sqrt((nx - (-0.5))**2 + (ny - 0.4)**2)
            wheel2_dist = math.sqrt((nx - 0.5)**2 + (ny - 0.4)**2)
            if wheel1_dist < 0.22 or wheel2_dist < 0.22:
                if wheel1_dist > 0.08 and wheel2_dist > 0.08:
                    is_car = False # 轮胎
                else:
                    is_car = True # 轮毂

            if is_car:
                # 白色车身图案
                row.extend([255, 255, 255, 255])
            else:
                row.extend([bg_r, bg_g, bg_b, 255])

        pixels.append(bytes(row))

    # 生成 PNG 格式二进制数据
    raw_data = b"".join(b"\x00" + p for p in pixels)
    compressed = zlib.compress(raw_data, 9)

    png_bytes = b"\x89PNG\r\n\x1a\n"
    # IHDR
    png_bytes += struct.pack(">I", 13) + b"IHDR" + struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    crc = zlib.crc32(b"IHDR" + struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png_bytes += struct.pack(">I", crc)

    # IDAT
    png_bytes += struct.pack(">I", len(compressed)) + b"IDAT" + compressed
    crc = zlib.crc32(b"IDAT" + compressed)
    png_bytes += struct.pack(">I", crc)

    # IEND
    png_bytes += struct.pack(">I", 0) + b"IEND"
    crc = zlib.crc32(b"IEND")
    png_bytes += struct.pack(">I", crc)

    with open(output_path, "wb") as f:
        f.write(png_bytes)
    print(f"Icon generated: {output_path} ({size}x{size})")

if __name__ == "__main__":
    static_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "static")
    create_car_icon(192, os.path.join(static_dir, "icon-192.png"))
    create_car_icon(512, os.path.join(static_dir, "icon-512.png"))
