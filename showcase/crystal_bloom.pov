// Crystal Bloom -- showcase scene for sgnano_povray
// A cluster of faceted crystals on a dark mirror floor, with caustics,
// neon rim lights and sparkles.
// License: AGPL-3.0-or-later (same as sgnano_povray).
#version 3.7;
#include "colors.inc"
#include "textures.inc"

global_settings {
  assumed_gamma 1.0
  max_trace_level 8
}

// Mirror-like metals (the stock Chrome_Texture is only 0.15 reflection).
#declare Chrome_Mirror = texture {
  pigment { color srgb <0.86, 0.90, 1.00> }
  finish { ambient 0.04 diffuse 0.06 specular 1.0 roughness 0.001 reflection { 0.95 metallic } }
}
#declare Gold_Mirror = texture {
  pigment { color srgb <1.00, 0.72, 0.22> }
  finish { ambient 0.05 diffuse 0.10 specular 1.0 roughness 0.002 reflection { 0.92 metallic } }
}

camera {
  location <0.0, 4.8, -14.0>
  look_at <0.0, 2.2, 0.0>
  angle 40
}

sky_sphere {
  pigment {
    gradient y
    color_map {
      [0.00 color srgb <0.002, 0.004, 0.012>]
      [0.30 color srgb <0.020, 0.040, 0.100>]
      [0.45 color srgb <0.150, 0.025, 0.220>]
      [0.55 color srgb <0.400, 0.070, 0.360>]
      [0.65 color srgb <0.070, 0.140, 0.260>]
      [1.00 color srgb <0.001, 0.001, 0.004>]
    }
  }
}

// Dark mirror floor.
plane {
  y, 0
  texture {
    pigment { color srgb <0.004, 0.006, 0.012> }
    normal { bumps 0.012 scale 0.35 }
    finish { diffuse 0.04 specular 0.85 roughness 0.002 reflection { 0.78 metallic } }
  }
}

// Gold ring around the base.
torus {
  2.9, 0.035
  translate <0, 0.045, 0>
  texture { Gold_Mirror }
}

// Faceted crystal: hexagonal prism + two cones, standing along +y.
#macro Crystal(H, R, Cr, Cg, Cb, RotY)
  union {
    prism {
      -H*0.30, H*0.30, 7,
      <0, R>, <R*0.866, R*0.5>, <R*0.866, -R*0.5>,
      <0, -R>, <-R*0.866, -R*0.5>, <-R*0.866, R*0.5>, <0, R>
    }
    cone { <0, 0, H*0.30>, R*0.98, <0, 0, H*0.50>, 0 }
    cone { <0, 0, -H*0.30>, R*0.90, <0, 0, -H*0.50>, 0 }
    sphere {
      <0, H*0.15, 0>, R*0.24
      texture {
        pigment { color srgb <1.00, 0.82, 0.45> }
        finish { ambient 1.0 diffuse 0.0 }
      }
    }
    texture {
      pigment { color rgbt <Cr, Cg, Cb, 0.88> }
      finish { specular 1.0 roughness 0.0005 reflection { 0.30 } }
    }
    interior { ior 1.70 }
    rotate <0, RotY, 0>
    rotate <-90, 0, 0>
    translate <0, H*0.5, 0>
  }
#end

object { Crystal(5.8, 0.62, 0.55, 0.90, 1.00,   0) translate < 0.0, 0.0,  0.0> }
object { Crystal(4.2, 0.48, 1.00, 0.25, 0.75,  22) translate < 2.5, 0.0,  0.8> }
object { Crystal(3.4, 0.42, 0.20, 0.95, 0.65,  48) translate <-2.6, 0.0,  1.0> }
object { Crystal(2.8, 0.38, 1.00, 0.70, 0.15,  72) translate < 1.3, 0.0,  2.7> }
object { Crystal(2.2, 0.34, 0.35, 0.65, 1.00,  95) translate <-1.7, 0.0,  2.9> }
object { Crystal(1.8, 0.28, 0.85, 0.35, 1.00, 130) translate < 3.9, 0.0, -1.0> }
object { Crystal(1.5, 0.24, 1.00, 0.55, 0.55, 165) translate <-4.0, 0.0, -0.6> }

// Chrome accents.
sphere {
  <0.0, 1.1, 5.6>, 1.10
  texture { Chrome_Mirror }
}
sphere {
  <-3.6, 0.55, 4.2>, 0.48
  texture { Chrome_Mirror }
}
torus {
  1.18, 0.055
  rotate <24, 0, 34>
  translate <0, 2.65, 0>
  texture { Chrome_Mirror }
}

// Lights.
light_source {
  <0, 11, -3>
  color srgb <1.0, 0.95, 0.88> * 2.2
}
light_source {
  <0, 14, 0>
  color srgb <1.0, 1.0, 1.0> * 1.6
  spotlight
  point_at <0, 1.0, 0>
  radius 14
  falloff 20
  tightness 5
}
light_source {
  <0, 5, 12>
  color srgb <1.00, 0.20, 0.75> * 2.0
}
light_source {
  <0, 4, -13>
  color srgb <0.20, 0.75, 1.00> * 1.8
}
light_source {
  <-9, 7, -9>
  color srgb <1.0, 0.12, 0.65> * 1.2
  area_light <4, 0, 0>, <0, 0, 4>, 4, 4
  adaptive 1
  jitter
}
light_source {
  <9, 6, 7>
  color srgb <0.10, 0.65, 1.0> * 1.2
  area_light <4, 0, 0>, <0, 0, 4>, 4, 4
  adaptive 1
  jitter
}
light_source {
  <0, 6, 12>
  color srgb <1.0, 0.45, 0.15> * 0.9
}

// A translucent glow pool on the floor.
plane {
  y, 0.002
  texture {
    pigment {
      radial
      color_map {
        [0.00 color rgbt <0.30, 0.65, 1.00, 0.45>]
        [0.35 color rgbt <0.55, 0.15, 0.75, 0.65>]
        [1.00 color rgbt <0.00, 0.00, 0.00, 1.00>]
      }
      scale 4.0
    }
    finish { ambient 1.0 diffuse 0.0 }
  }
  no_shadow
}

// Sparkles / stars.
#declare S = seed(19);
#declare I = 0;
#while (I < 120)
  #declare P = <rand(S)*30-15, rand(S)*12+0.2, rand(S)*22-8>;
  #declare G = rand(S);
  sphere {
    P, 0.025 + rand(S)*0.055
    texture {
      pigment { color srgb <1.0, 0.75+0.25*G, 0.35+0.65*G> }
      finish { ambient 1.0 diffuse 0.0 }
    }
  }
  #declare I = I + 1;
#end
