import sys
from PIL import Image
from collections import deque

def remove_bg(input_path, output_path, tolerance=30):
    img = Image.open(input_path).convert("RGBA")
    data = img.load()
    width, height = img.size
    
    # Sample the 4 corners to find the background color
    corners = [(0,0), (width-1,0), (0,height-1), (width-1,height-1)]
    bg_colors = [data[x, y] for x, y in corners]
    
    visited = set()
    q = deque(corners)
    
    def color_dist(c1, c2):
        # Euclidean distance might be better but Manhattan is fine
        return sum(abs(c1[i] - c2[i]) for i in range(3))
        
    while q:
        x, y = q.popleft()
        if (x, y) in visited:
            continue
        visited.add((x, y))
        
        # Check against all corner colors, if it's close to any, it's background
        is_bg = any(color_dist(data[x, y], bg_c) < tolerance for bg_c in bg_colors)
        
        if is_bg:
            data[x, y] = (255, 255, 255, 0) # Make transparent
            if x > 0: q.append((x-1, y))
            if x < width-1: q.append((x+1, y))
            if y > 0: q.append((x, y-1))
            if y < height-1: q.append((x, y+1))
            
    img.save(output_path, "PNG")
    print(f"Saved transparent image to {output_path}")

if __name__ == "__main__":
    remove_bg(sys.argv[1], sys.argv[2])
