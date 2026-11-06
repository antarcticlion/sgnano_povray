#version 3.7;
global_settings { assumed_gamma 1.0 }
camera { location <0,2,-6> look_at <0,0,0> angle 45 }
light_source { <3,5,-4> color rgb <1,1,1> }
plane { y, -1 pigment { checker color rgb 0.8 color rgb 0.2 } }
sphere { <0,0,0>, 1 pigment { color rgb <0.9,0.3,0.2> } finish { phong 0.5 } }
sphere { <1.5,0.2,0.5>, 0.6 pigment { color rgb <0.2,0.4,0.9> } }
