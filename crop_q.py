import sys
from PIL import Image

def extract_q_and_crop(input_path, output_path):
    img = Image.open(input_path).convert("RGBA")
    data = img.load()
    width, height = img.size
    
    min_x, min_y = width, height
    max_x, max_y = 0, 0
    
    for y in range(height):
        for x in range(width):
            r, g, b, a = data[x, y]
            
            # The background is white/light grey (low saturation/difference between colors)
            # The gold Q has a high difference between red/green and blue.
            color_diff = max(r, g, b) - min(r, g, b)
            
            # If color difference is low, OR it's extremely bright white, it's background
            if color_diff < 40 or (r > 245 and g > 245 and b > 245):
                data[x, y] = (255, 255, 255, 0)
            else:
                # This is part of the Q
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)
                
    # Crop tightly around the Q, adding a small 20px padding
    if max_x >= min_x and max_y >= min_y:
        padding = 20
        crop_box = (
            max(0, min_x - padding),
            max(0, min_y - padding),
            min(width, max_x + padding),
            min(height, max_y + padding)
        )
        img = img.crop(crop_box)
        print(f"Cropped to box: {crop_box}")
    else:
        print("Warning: Could not find the Q bounds.")
        
    img.save(output_path, "PNG")
    print(f"Saved tightly cropped Q to {output_path}")

if __name__ == "__main__":
    extract_q_and_crop(sys.argv[1], sys.argv[2])
