from PIL import Image
import sys

img = Image.open('quicky_icon_tight.png')
img.save('quicky.ico', format='ICO', sizes=[(16,16), (32,32), (48,48), (64,64)])
print("Done")
