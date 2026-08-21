from PIL import Image, ImageDraw

# 白色"导入"图标（Windows 导入参考：向下箭头进入底部托盘），32×32
# 4x supersampling -> LANCZOS 缩到 32x32
S, Z = 32, 4
img = Image.new('RGBA', (S * Z, S * Z), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
P = lambda x, y: (x * Z, y * Z)
W = (255, 255, 255, 255)

# 1) 箭杆（垂直竖矩形）
d.polygon([P(13.5, 5), P(18.5, 5), P(18.5, 16), P(13.5, 16)], fill=W)
# 2) 箭头尖端（朝下的实心三角）
d.polygon([P(9, 13), P(23, 13), P(16, 22)], fill=W)
# 3) 底部托盘（水平条，箭头"进入"托盘）
d.polygon([P(5, 25.5), P(27, 25.5), P(27, 29.5), P(5, 29.5)], fill=W)

img = img.resize((S, S), Image.LANCZOS)
img.save('assets/import.png')
print('已生成 assets/import.png', img.size)
