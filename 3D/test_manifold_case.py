#!/usr/bin/env python3
import math
import struct
import os

from build_gps_magsafe_case import STLWriter, earcut, make_circle_loop, make_rounded_rect_loop, make_outer_unibody_profile

def test_mesh():
    stl = STLWriter()
    print("Testing clean topology...")

test_mesh()
