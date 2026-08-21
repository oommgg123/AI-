from PIL import Image, ImageDraw

# 白色"导出"图标（导入图标方向相反：向上箭头从顶部托盘发出），32×32
# 4x supersampling -> LANCZOS 缩到 32x32
S, Z = 32, 4
img = Image.new('RGBA', (S * Z, S * Z), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
P = lambda x, y: (x * Z, y * Z)
W = (255, 255, 255, 255)

# 1) 箭杆（垂直竖矩形，偏下）
d.polygon([P(13.5, 16), P(18.5, 16), P(18.5, 27), P(13.5, 27)], fill=W)
# 2) 箭头尖端（朝上的实心三角）
d.polygon([P(9, 19), P(23, 19), P(16, 10)], fill=W)
# 3) 顶部托盘（水平条，箭头从托盘"发出"）
d.polygon([P(5, 2.5), P(27, 2.5), P(27, 6.5), P(5, 6.5)], fill=W)

img = img.resize((S, S), Image.LANCZOS)
img.save('assets/export.png')
print('已生成 assets/export.png', img.size)
