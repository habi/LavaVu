import sys
sys.path.append('bin')
import LavaVu

#LavaVu.execute(["LavaVu", "-I"])

LavaVu.execute(["LavaVu", "-a"])

print LavaVu.image("initial")

#for i in range(1,50):
#  _LavaVu.execute(["LavaVu", "-I", ":", "test"])

LavaVu.command("rotate x 45")
print LavaVu.image("rotated")

imagestr = LavaVu.image()
