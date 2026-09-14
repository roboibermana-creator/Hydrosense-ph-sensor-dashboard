import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Fix the dashboard-grid column count (which we know was reset or applied correctly)
html = html.replace('#page-dashboard-utama { grid-template-columns: repeat(3, minmax(0, 1fr)) minmax(220px, 0.9fr); }', '#page-dashboard-utama { grid-template-columns: repeat(4, minmax(0, 1fr)); }')

# Manually replace all the specific images based on their location
# Sidebar
html = html.replace('gambarpixel/sidebar_dashboard.png', 'gambarpixel/real-time-icon.svg') # Did they provide an SVG for dashboard? No, let's keep PNG if no SVG
html = html.replace('gambarpixel/sidebar_ph.png', 'gambarpixel/ph-drop-small.svg')
html = html.replace('gambarpixel/sidebar_tss.png', 'gambarpixel/tss-cluster-small.svg')
html = html.replace('gambarpixel/sidebar_waterlevel.png', 'gambarpixel/level-wave-status.svg')

# Remove any onerror attributes entirely to clean up the HTML
html = re.sub(r'\s*onerror="[^"]+"\s*', ' ', html)

# Fix #page-water-level specifically
html = html.replace('gambarpixel/parameter-water-level.svg', 'gambarpixel/level-main-tank.svg')
html = html.replace('gambarpixel/voltage-symbol.svg', 'gambarpixel/parameter-voltage-petir.svg')
html = html.replace('gambarpixel/echo_distance.png', 'gambarpixel/level-wave-status.svg')
html = html.replace('gambarpixel/log-book-2.svg', 'gambarpixel/ikon-statistik-sesi.svg')
html = html.replace('gambarpixel/settings-gear.svg', 'gambarpixel/ikon-pengaturan-kecil.svg')

# Check other sensor pages
html = html.replace('gambarpixel/parameter-tss-mgl.svg', 'gambarpixel/tss-cluster-large.svg')
html = html.replace('gambarpixel/ph-droplet.svg', 'gambarpixel/ph-drop-large.svg')

# Fix small calibration icons
html = html.replace('gambarpixel/calib_droplet_small.png', 'gambarpixel/ph-drop-small.svg')
html = html.replace('gambarpixel/calib_particles_small.png', 'gambarpixel/tss-cluster-small.svg')
html = html.replace('gambarpixel/calib_tank_small.png', 'gambarpixel/level-calibration.svg')

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
