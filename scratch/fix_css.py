import re
with open('index.html', 'r', encoding='utf-8') as f:
    content = f.read()

# Remove inline grid styles from page-dashboard-utama
content = content.replace(
    'style="grid-template-columns: repeat(3, minmax(0, 1fr)) minmax(220px, 0.9fr); grid-template-rows: auto 1fr;"',
    ''
)

# Add CSS rules to the <style> block
style_block = '''    <style>
      .page-content { display: none !important; }
      .page-content.active { display: grid !important; }
      #page-dashboard-utama { grid-template-columns: repeat(3, minmax(0, 1fr)) minmax(220px, 0.9fr); }
      @media (max-width: 1100px) {
         #page-dashboard-utama { grid-template-columns: repeat(2, minmax(0, 1fr)); }
         #calibration-summary { grid-column: 1 / -1; }
         #calibration-summary > div { grid-template-columns: 1fr; }
         #calibration-summary > div > div { border-left: none !important; padding-left: 0 !important; border-bottom: 2px solid var(--border-dark); padding-bottom: 10px; }
         #calibration-summary > div > div:last-child { border-bottom: none; }
      }
      @media (max-width: 690px) {
         #page-dashboard-utama { grid-template-columns: 1fr; }
      }
    </style>'''

content = re.sub(r'<style>.*?</style>', style_block, content, flags=re.DOTALL)

with open('index.html', 'w', encoding='utf-8') as f:
    f.write(content)
