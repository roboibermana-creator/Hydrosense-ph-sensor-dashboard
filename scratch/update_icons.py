import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

# 1. Header & Sidebar
html = html.replace(
    '<span class="brand-mark">H</span>',
    '<img class="brand-mark" src="gambarpixel/logo_h.png" alt="H" style="width:35px;height:35px;object-fit:contain;"/>'
)
html = html.replace(
    'src="gambarpixel/user-avatar.svg"',
    'src="gambarpixel/operator_avatar.png"'
)
html = html.replace(
    'src="gambarpixel/calibration-tool.svg"',
    'src="gambarpixel/calibrate_probe.png"'
)
html = html.replace(
    'src="gambarpixel/real-time-icon.svg"',
    'src="gambarpixel/sidebar_dashboard.png"'
)
html = html.replace(
    'src="gambarpixel/parameter-ph-tetesan.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt=""',
    'src="gambarpixel/sidebar_ph.png" alt=""'
)
html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/history-icon.svg\'" alt=""',
    'src="gambarpixel/sidebar_tss.png" alt=""'
)
html = html.replace(
    'src="gambarpixel/parameter-water-level.svg" onerror="this.src=\'gambarpixel/settings-gear.svg\'" alt=""',
    'src="gambarpixel/sidebar_waterlevel.png" alt=""'
)
html = html.replace(
    'src="gambarpixel/ph-probe-gear.svg"',
    'src="gambarpixel/tools_settings.png"'
)

# 2. Main content panels (DASHBOARD UTAMA)
html = html.replace(
    'src="gambarpixel/parameter-ph-tetesan.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt="pH droplet"',
    'src="gambarpixel/droplet_ph.png" alt="pH droplet"'
)
html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/voltage-symbol.svg\'" alt="TSS"',
    'src="gambarpixel/tss_particles.png" alt="TSS"'
)
html = html.replace(
    'src="gambarpixel/parameter-water-level.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt="Water Level"',
    'src="gambarpixel/water_tank.png" alt="Water Level"'
)
html = html.replace(
    'src="gambarpixel/log-book-2.svg"',
    'src="gambarpixel/book_session.png"'
)
html = html.replace(
    'src="gambarpixel/settings-gear.svg"',
    'src="gambarpixel/gear_settings.png"'
)

# Calib row icons (Dashboard utama)
html = html.replace(
    'src="gambarpixel/parameter-ph-tetesan.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'"',
    'src="gambarpixel/calib_droplet_small.png"'
)
html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/history-icon.svg\'"',
    'src="gambarpixel/calib_particles_small.png"'
)
html = html.replace(
    'src="gambarpixel/parameter-water-level.svg" onerror="this.src=\'gambarpixel/settings-gear.svg\'"',
    'src="gambarpixel/calib_tank_small.png"'
)

# Subpages reading icons
html = html.replace(
    'src="gambarpixel/ph-droplet.svg" alt="pH droplet"',
    'src="gambarpixel/droplet_ph.png" alt="pH droplet"'
)
html = html.replace(
    'src="gambarpixel/voltage-symbol.svg" alt="Voltage"',
    'src="gambarpixel/probe_voltage_bolt.png" alt="Voltage"'
)
html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/voltage-symbol.svg\'" alt="NTU"',
    'src="gambarpixel/sensor_turbidity.png" alt="NTU"'
)
html = html.replace(
    'src="gambarpixel/voltage-symbol.svg" alt="Distance"',
    'src="gambarpixel/echo_distance.png" alt="Distance"'
)

# Subpages calib icons
html = html.replace(
    'src="gambarpixel/ph-droplet.svg" alt=""',
    'src="gambarpixel/calib_droplet_small.png" alt=""'
)
html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt=""',
    'src="gambarpixel/calib_particles_small.png" alt=""'
)
html = html.replace(
    'src="gambarpixel/parameter-water-level.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt=""',
    'src="gambarpixel/calib_tank_small.png" alt=""'
)

# Actually let's manually modify the wifi JS just to be safe
html = html.replace(
    'document.querySelector("#wifiIcon").className = "pixel-icon wifi-icon is-online";',
    'document.querySelector("#wifiIcon").className = "pixel-icon wifi-icon is-online"; document.querySelector("#wifiIcon").src = "gambarpixel/wifi-icon.svg";'
)
html = html.replace(
    'document.querySelector("#wifiIcon").className = "pixel-icon wifi-icon is-offline";',
    'document.querySelector("#wifiIcon").className = "pixel-icon wifi-icon is-offline"; document.querySelector("#wifiIcon").src = "gambarpixel/wifi_disconnected.png";'
)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
