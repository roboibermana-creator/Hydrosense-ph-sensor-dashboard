import re

with open('index.html', 'r', encoding='utf-8') as f:
    html = f.read()

html = html.replace(
    'src="gambarpixel/parameter-tss-mgl.svg" onerror="this.src=\'gambarpixel/ph-droplet.svg\'" alt="TSS"',
    'src="gambarpixel/tss_particles.png" alt="TSS"'
)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(html)
