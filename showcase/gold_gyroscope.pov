// Gold Gyroscope -- showcase scene for sgnano_povray
// Polished gold and chrome rings around a glass sphere on a black mirror,
// lit by colored softboxes.
// License: AGPL-3.0-or-later (same as sgnano_povray).
#version 3.7;
#include "colors.inc"

global_settings {
  assumed_gamma 1.0
  max_trace_level 10
}

#declare Chrome_Mirror = texture {
  pigment { color srgb <0.86, 0.90, 1.00> }
  finish { ambient 0.04 diffuse 0.06 specular 1.0 roughness 0.001 reflection { 0.95 metallic } }
}
#declare Gold_Mirror = texture {
  pigment { color srgb <1.00, 0.72, 0.22> }
  finish { ambient 0.05 diffuse 0.10 specular 1.0 roughness 0.002 reflection { 0.92 metallic } }
}
#declare Copper_Mirror = texture {
  pigment { color srgb <1.00, 0.45, 0.20> }
  finish { ambient 0.05 diffuse 0.10 specular 1.0 roughness 0.002 reflection { 0.90 metallic } }
}

camera {
  location <0.0, 3.4, -12.0>
  look_at <0.0, 3.1, 0.0>
  angle 42
}

sky_sphere {
  pigment {
    gradient y
    color_map {
      [0.00 color srgb <0.002, 0.003, 0.010>]
      [0.45 color srgb <0.010, 0.015, 0.045>]
      [0.56 color srgb <0.060, 0.015, 0.090>]
      [0.64 color srgb <0.020, 0.030, 0.070>]
      [1.00 color srgb <0.001, 0.001, 0.003>]
    }
  }
}

// Black mirror floor.
plane {
  y, 0
  texture {
    pigment { color srgb <0.004, 0.005, 0.010> }
    normal { bumps 0.008 scale 0.20 }
    finish { diffuse 0.05 specular 0.98 roughness 0.002 reflection { 0.94 metallic } }
  }
}

// Glowing floor ring around the piece.
torus {
  4.30, 0.035
  translate <0, 0.03, 0>
  texture { pigment { color srgb <0.25, 0.65, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}
torus {
  4.55, 0.022
  translate <0, 0.03, 0>
  texture { pigment { color srgb <1.00, 0.30, 0.75> } finish { ambient 1.0 diffuse 0.0 } }
}

// Pedestal.
cylinder {
  <0, 0.0, 0>, <0, 0.55, 0>, 1.30
  texture { Gold_Mirror }
}
cylinder {
  <0, 0.55, 0>, <0, 0.70, 0>, 1.05
  texture { Chrome_Mirror }
}

// Glass sphere at the core.
// Glowing plasma core inside a glass shell.
sphere {
  <0, 3.25, 0>, 0.32
  texture {
    pigment { color srgb <0.85, 0.97, 1.00> * 1.2 }
    finish { ambient 1.0 diffuse 0.0 }
  }
}
light_source {
  <0, 3.25, 0> color srgb <0.35, 0.80, 1.00> * 2.2
  looks_like { sphere { <0, 0, 0>, 0.001 pigment { color rgbt <1,1,1,1> } } }
}
sphere {
  <0, 3.25, 0>, 1.55
  texture {
    pigment { color rgbt <0.90, 0.96, 1.00, 0.90> }
    finish { specular 1.0 roughness 0.0005 reflection { 0.30 } }
  }
  interior { ior 1.45 }
}

// Gold and copper rings.
torus {
  2.95, 0.105
  rotate <90, 0, 0>
  translate <0, 3.25, 0>
  texture { Gold_Mirror }
}
torus {
  2.95, 0.090
  rotate <38, 0, 0>
  rotate <0, 52, 0>
  translate <0, 3.25, 0>
  texture { Gold_Mirror }
}
torus {
  2.95, 0.075
  rotate <-42, 0, 0>
  rotate <0, -48, 0>
  translate <0, 3.25, 0>
  texture { Copper_Mirror }
}
torus {
  3.35, 0.055
  rotate <12, 0, 0>
  rotate <0, 78, 0>
  translate <0, 3.25, 0>
  texture { Chrome_Mirror }
}

// Floating spheres.
sphere { < 4.4, 1.0,  1.8>, 0.55 texture { Chrome_Mirror } }
sphere { <-4.7, 0.8,  1.2>, 0.42 texture { Gold_Mirror } }
sphere { < 3.6, 5.6,  0.6>, 0.38 texture { Gold_Mirror } }
sphere { <-3.9, 5.9, -0.3>, 0.32 texture { Chrome_Mirror } }
sphere { < 1.9, 0.45, 4.6>, 0.30 texture { Copper_Mirror } }
sphere { <-2.2, 0.4,  4.9>, 0.26 texture { Chrome_Mirror } }

// Softboxes behind the camera: mirrored by the metals.
box {
  <-7.0, 5.5, -16.0>, <-3.0, 8.5, -15.5>
  texture { pigment { color srgb <1.00, 0.98, 0.95> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <3.0, 4.2, -16.0>, <7.0, 7.2, -15.5>
  texture { pigment { color srgb <1.00, 0.98, 0.95> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <-10.5, 0.8, -14.0>, <-10.0, 8.0, -8.0>
  texture { pigment { color srgb <0.90, 0.12, 0.58> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <10.0, 0.8, -14.0>, <10.5, 8.0, -8.0>
  texture { pigment { color srgb <0.10, 0.70, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <-3.5, 9.5, -6.0>, <3.5, 12.0, -2.0>
  texture { pigment { color srgb <1.00, 0.85, 0.60> } finish { ambient 1.0 diffuse 0.0 } }
}

// Lights.
light_source { <0, 9, -7> color srgb <1.00, 0.95, 0.88> * 2.0 }
light_source { <-10, 6, -2> color srgb <1.00, 0.12, 0.65> * 1.2 }
light_source { <9, 6, 2> color srgb <0.10, 0.70, 1.00> * 1.8 }
light_source { <0, 5, 9> color srgb <1.00, 0.50, 0.15> * 0.9 }
light_source { <0, 16, 1> color srgb <0.45, 0.55, 1.00> * 0.7 }

// Sparkles.
#declare Sd = seed(7);
#declare K = 0;
#while (K < 150)
  #declare A = rand(Sd)*6.283185;
  #declare B = 0.25 + rand(Sd)*1.25;
  #declare Rr = 26 + rand(Sd)*16;
  #declare P = <Rr*sin(B)*cos(A), Rr*cos(B)+5, Rr*sin(B)*sin(A)+8>;
  #declare G = rand(Sd);
  sphere {
    P, 0.03 + rand(Sd)*0.05
    texture {
      pigment { color srgb <0.80+0.20*G, 0.82+0.18*G, 1.00> }
      finish { ambient 1.0 diffuse 0.0 }
    }
  }
  #declare K = K + 1;
#end
