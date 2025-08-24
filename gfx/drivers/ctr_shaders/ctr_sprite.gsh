.gsh
.entry main_gsh

.constf  _N1N1   (-1.0, 1.0, -1.0, 1.0)

.alias rotate           b0
.alias sprite_coords    v0
.alias tex_frame_coords v1

.alias top_left            sprite_coords.xy
.alias bottom_right        sprite_coords.zw
.alias top_right           sprite_coords.zy
.alias bottom_left         sprite_coords.xw

.alias tex_top_left        tex_frame_coords.xy
.alias tex_bottom_right    tex_frame_coords.zw
.alias tex_top_right       tex_frame_coords.xw
.alias tex_bottom_left     tex_frame_coords.zy

.out  pos            position
.out  texcoord       texcoord0

.proc main_gsh
   setemit 0
      mov pos.xy, top_left.xy
      mov pos.zw, _N1N1

      ifu rotate
         mov texcoord.xy, tex_top_right.xy
      .else
         mov texcoord.xy, tex_top_left.xy
      .end
   emit

   setemit 1
      mov pos.xy, bottom_left.xy
      mov pos.zw, _N1N1

      ifu rotate
         mov texcoord.xy, tex_top_left.xy
      .else
         mov texcoord.xy, tex_bottom_left.xy
      .end
   emit

   setemit 2, prim inv
      mov pos.xy, bottom_right.xy
      mov pos.zw, _N1N1

      ifu rotate
         mov texcoord.xy, tex_bottom_left.xy
      .else
         mov texcoord.xy, tex_bottom_right.xy
      .end
   emit

   setemit 1, prim
      mov pos.xy, top_right.xy
      mov pos.zw, _N1N1

      ifu rotate
         mov texcoord.xy, tex_bottom_right.xy
      .else
         mov texcoord.xy, tex_top_right.xy
      .end
   emit

   end
.end
