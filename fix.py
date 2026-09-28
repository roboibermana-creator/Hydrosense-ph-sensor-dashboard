import sys  
c = open('src/Network.cpp').read()  
c = c.replace('\" Hydrosense "table\', '\Hydrosense%%20table\')  
open('src/Network.cpp', 'w').write(c)  
