// Neon Velocity -- showcase scene for sgnano_povray
// A wet neon street with a chrome pod, reflections and a night sky.
// License: AGPL-3.0-or-later (same as sgnano_povray).
#version 3.7;
#include "colors.inc"
#include "textures.inc"

global_settings {
  assumed_gamma 1.0
  max_trace_level 12
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
  location <0.0, 1.5, -16.0>
  look_at <0.0, 3.8, 0.0>
  angle 61
}

sky_sphere {
  pigment {
    gradient y
    color_map {
      [0.00 color srgb <0.004, 0.002, 0.016>]
      [0.42 color srgb <0.020, 0.008, 0.070>]
      [0.54 color srgb <0.130, 0.020, 0.180>]
      [0.62 color srgb <0.050, 0.015, 0.100>]
      [1.00 color srgb <0.001, 0.001, 0.003>]
    }
  }
}

// A huge moon behind the city.
sphere {
  <9, 23, 42>, 6.5
  texture {
    pigment { color srgb <1.00, 0.55, 0.80> }
    finish { ambient 1.2 diffuse 0.0 }
  }
}

fog { color srgb <0.020, 0.010, 0.042> distance 32 }

// Wet street.
plane {
  y, 0
  texture {
    pigment { color srgb <0.006, 0.008, 0.014> }
    normal { bumps 0.012 scale 0.18 }
    finish { diffuse 0.06 specular 0.98 roughness 0.002 reflection { 0.92 metallic } }
  }
}

// Building with emissive windows and neon strips.
#macro Building(X, Z, W, H, D, Ar, Ag, Ab, Nr, Ng, Nb)
  union {
    box { <-W/2, 0, -D/2>, <W/2, H, D/2>
      texture {
        pigment { color srgb <Ar, Ag, Ab> }
        finish { ambient 0.08 diffuse 0.30 specular 0.55 roughness 0.05 reflection { 0.08 } }
      }
    }
    // Vertical neon edge (front face, camera side).
    box { <-W*0.42, H*0.06, -D/2-0.10>, <-W*0.36, H*0.92, -D/2-0.02>
      texture { pigment { color srgb <Nr, Ng, Nb> } finish { ambient 1.0 diffuse 0.0 } }
    }
    box { <W*0.36, H*0.06, -D/2-0.10>, <W*0.42, H*0.92, -D/2-0.02>
      texture { pigment { color srgb <Nb, Ng, Nr> } finish { ambient 1.0 diffuse 0.0 } }
    }
    // Horizontal neon band.
    box { <-W*0.30, H*0.72, -D/2-0.10>, <W*0.30, H*0.76, -D/2-0.02>
      texture { pigment { color srgb <Nb, Nr, Ng> } finish { ambient 1.0 diffuse 0.0 } }
    }
    // Window grid on the front face.
    #declare R = 0;
    #while (R < 8)
      #declare C = 0;
      #while (C < 6)
        #declare WX = -W*0.35 + C*(W*0.14);
        #declare WY = H*0.08 + R*(H*0.105);
        #declare WC = mod(R*7 + C*11, 13)/12;
        #if (mod(R*3 + C, 5) = 0)
          #declare WCOL = <1.00, 0.70, 0.38> * (0.40 + 0.60*WC);
        #else
          #declare WCOL = <0.55, 0.75, 1.00> * (0.40 + 0.60*WC);
        #end
        box {
          <WX-W*0.035, WY, -D/2-0.06>, <WX+W*0.035, WY+H*0.022, -D/2-0.03>
          texture {
            pigment { color srgb WCOL }
            finish { ambient 0.9 diffuse 0.1 }
          }
        }
        #declare C = C + 1;
      #end
      #declare R = R + 1;
    #end
    // Window grid on the street-facing sides.
    #declare R = 0;
    #while (R < 8)
      #declare C = 0;
      #while (C < 4)
        #declare WZ = -D*0.30 + C*(D*0.20);
        #declare WY = H*0.08 + R*(H*0.105);
        #declare WC = mod(R*5 + C*7, 11)/10;
        #if (mod(R*2 + C, 4) = 0)
          #declare WCOL = <1.00, 0.70, 0.38> * (0.40 + 0.60*WC);
        #else
          #declare WCOL = <0.55, 0.75, 1.00> * (0.40 + 0.60*WC);
        #end
        box {
          <W/2+0.03, WY, WZ-D*0.055>, <W/2+0.06, WY+H*0.022, WZ+D*0.055>
          texture { pigment { color srgb WCOL } finish { ambient 0.9 diffuse 0.1 } }
        }
        box {
          <-W/2-0.06, WY, WZ-D*0.055>, <-W/2-0.03, WY+H*0.022, WZ+D*0.055>
          texture { pigment { color srgb WCOL } finish { ambient 0.9 diffuse 0.1 } }
        }
        #declare C = C + 1;
      #end
      #declare R = R + 1;
    #end
    translate <X, 0, Z>
  }
#end

// Left and right rows.
#declare I = 0;
#while (I < 8)
  #declare Z = -10 + I*5.2;
  #declare H1 = 8 + mod(I*7, 15);
  #declare H2 = 7 + mod(I*11 + 3, 17);
  Building(-11.5 - mod(I*3, 2), Z, 5.5, H1, 5.0, 0.020, 0.030, 0.065, 0.15, 0.95, 1.00)
  Building( 11.5 + mod(I*5, 2), Z+2.2, 5.0, H2, 5.0, 0.025, 0.025, 0.060, 1.00, 0.15, 0.70)
  #declare I = I + 1;
#end

// Street-level neon signs across the view.
#declare SignPos = array[6] {
  < -9.0, 4.2, -6.0>, < 8.0, 6.8, -2.0>, < -7.5, 8.6, 4.0>,
  <  9.0, 10.0, 9.0>, < -10.0, 3.2, 11.0>, < 7.0, 3.6, 15.0>
}
#declare SignSize = array[6] {
  <4.0, 1.00>, <3.2, 0.90>, <3.8, 0.90>,
  <3.0, 0.85>, <2.6, 0.80>, <2.4, 0.75>
}
#declare SignColor = array[6] {
  <1.00, 0.20, 0.80>, <0.10, 0.85, 1.00>, <1.00, 0.55, 0.10>,
  <0.75, 0.20, 1.00>, <0.20, 1.00, 0.55>, <1.00, 0.20, 0.55>
}
#declare J = 0;
#while (J < 6)
  #declare P = SignPos[J];
  #declare Sz = SignSize[J];
  #declare C = SignColor[J];
  box {
    <P.x-Sz.x/2, P.y-Sz.y/2, P.z-0.05>, <P.x+Sz.x/2, P.y+Sz.y/2, P.z+0.05>
    texture { pigment { color srgb C } finish { ambient 1.0 diffuse 0.0 } }
  }
  #declare J = J + 1;
#end

// Glowing ring behind the pod.
torus {
  5.2, 0.07
  rotate <90, 0, 0>
  translate <0, 5.2, 15.0>
  texture { pigment { color srgb <0.20, 0.90, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}
torus {
  4.0, 0.05
  rotate <90, 0, 0>
  translate <0, 5.2, 14.6>
  texture { pigment { color srgb <1.00, 0.25, 0.80> } finish { ambient 1.0 diffuse 0.0 } }
}
torus {
  6.4, 0.035
  rotate <90, 0, 0>
  translate <0, 5.2, 15.4>
  texture { pigment { color srgb <0.60, 0.25, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}
torus {
  7.6, 0.028
  rotate <90, 0, 0>
  translate <0, 5.2, 15.8>
  texture { pigment { color srgb <1.00, 0.45, 0.15> } finish { ambient 1.0 diffuse 0.0 } }
}

// Softboxes behind the camera: mirrored by the chrome pod.
box {
  <-7.0, 6.5, -22.0>, <-3.0, 9.5, -21.5>
  texture { pigment { color srgb <1.00, 0.98, 0.95> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <3.0, 5.0, -22.0>, <7.0, 8.0, -21.5>
  texture { pigment { color srgb <1.00, 0.98, 0.95> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <-12.2, 1.0, -20.0>, <-11.7, 9.0, -14.0>
  texture { pigment { color srgb <1.00, 0.12, 0.65> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <11.7, 1.0, -20.0>, <12.2, 9.0, -14.0>
  texture { pigment { color srgb <0.10, 0.70, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}

// Chrome pod / sphere.
sphere {
  <0, 3.4, 2.5>, 2.6
  texture { Chrome_Mirror }
}
// Gold ring around the pod.
torus {
  3.35, 0.065
  rotate <90, 0, 18>
  translate <0, 3.4, 2.5>
  texture { Gold_Mirror }
}
torus {
  3.05, 0.055
  rotate <68, 18, 12>
  translate <0, 3.4, 2.5>
  texture {
    pigment { color srgb <0.10, 0.85, 1.00> }
    finish { ambient 0.6 diffuse 0.2 specular 1.0 roughness 0.01 reflection { 0.3 } }
  }
}

// A second chrome sphere for depth.
sphere {
  <6.2, 1.2, 8.0>, 1.2
  texture { Chrome_Mirror }
}

// Neon tubes behind the pod, reflected in the chrome and the wet floor.
#declare TubeCols = array[5] {
  <1.00, 0.15, 0.70>, <0.10, 0.80, 1.00>, <1.00, 0.50, 0.10>,
  <0.55, 0.20, 1.00>, <0.20, 1.00, 0.55>
}
#declare T = 0;
#while (T < 5)
  #declare TC = TubeCols[T];
  #declare TX = -9.0 + T*4.5;
  #declare TH = 5.0 + mod(T*3, 5);
  #declare TZ = 7.0 + mod(T*4, 6);
  cylinder {
    <TX, 0.02, TZ>, <TX, TH, TZ>, 0.05
    texture { pigment { color srgb TC } finish { ambient 1.0 diffuse 0.0 } }
  }
  #declare T = T + 1;
#end

// Long glowing strips along the street edge.
box {
  <-11.0, 0.005, -12.0>, <-10.6, 0.05, 24.0>
  texture { pigment { color srgb <1.00, 0.10, 0.65> } finish { ambient 1.0 diffuse 0.0 } }
}
box {
  <10.6, 0.005, -12.0>, <11.0, 0.05, 24.0>
  texture { pigment { color srgb <0.10, 0.75, 1.00> } finish { ambient 1.0 diffuse 0.0 } }
}

// Lights.
light_source { <0, 14, -8> color srgb <0.55, 0.65, 1.00> * 0.9 }
light_source { <-11, 7, -4> color srgb <1.00, 0.10, 0.65> * 1.8 }
light_source { <11, 9, 4> color srgb <0.10, 0.70, 1.00> * 1.8 }
light_source { <0, 3, 12> color srgb <1.00, 0.45, 0.10> * 0.9 }
light_source { <0, 6, 2> color srgb <0.35, 0.55, 1.00> * 0.5 }
light_source { <0, 22, -15> color srgb <0.30, 0.35, 0.60> * 1.2 }
light_source { <0, 7, 30> color srgb <0.60, 0.25, 0.55> * 0.8 }

// Stars.
#declare Sd = seed(41);
#declare K = 0;
#while (K < 220)
  #declare A = rand(Sd)*6.283185;
  #declare B = 0.15 + rand(Sd)*1.35;
  #declare Rr = 38 + rand(Sd)*14;
  #declare P = <Rr*sin(B)*cos(A), Rr*cos(B)+8, Rr*sin(B)*sin(A)+12>;
  #declare G = rand(Sd);
  sphere {
    P, 0.035 + rand(Sd)*0.055
    texture {
      pigment { color srgb <0.75+0.25*G, 0.80+0.20*G, 1.00> }
      finish { ambient 1.0 diffuse 0.0 }
    }
  }
  #declare K = K + 1;
#end
