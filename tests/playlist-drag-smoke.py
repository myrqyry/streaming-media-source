"""Drive a real X11 drag in the disposable Xvfb Qt playback smoke."""
import sys
import time
from Xlib import X, display
from Xlib.ext import xtest

x, y, tx, ty = map(int, sys.argv[1:])
d = display.Display()
xtest.fake_input(d, X.MotionNotify, x=x, y=y)
d.sync()
time.sleep(0.15)
xtest.fake_input(d, X.ButtonPress, 1)
d.sync()
try:
    for i in range(1, 25):
        xtest.fake_input(d, X.MotionNotify,
                         x=round(x + (tx-x)*i/24),
                         y=round(y + (ty-y)*i/24))
        d.sync()
        time.sleep(0.035)
    time.sleep(0.15)
finally:
    xtest.fake_input(d, X.ButtonRelease, 1)
    d.sync()
    d.close()
