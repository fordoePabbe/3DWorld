uniform mat4 fg_ViewMatrixInv;
uniform vec4 color = vec4(1.0);
uniform float depth_bias = 0.0;

out vec3 vpos, eye_norm, normal, world_space_pos;
out vec4 epos;
out vec2 tc; // declared in bump_map.part.frag, but unused for asteroids

#ifdef USE_CUSTOM_XFORM
in mat4 inst_xform_matrix;
#endif

void main()
{
#ifdef USE_CUSTOM_XFORM
	epos     = inst_xform_matrix * fg_Vertex;
	eye_norm = normalize(transpose(inverse(mat3(inst_xform_matrix))) * fg_Normal);
#else
	epos     = fg_ModelViewMatrix * fg_Vertex;
	eye_norm = normalize(fg_NormalMatrix * fg_Normal); // for lighting
#endif
#ifdef ENABLE_SHADOWS
	world_space_pos = (fg_ViewMatrixInv * epos).xyz;
#endif
	gl_Position   = fg_ProjectionMatrix * epos;
	gl_Position.z += depth_bias;
	fg_Color_vf   = color;
	normal        = fg_Normal; // world space normal, for triplanar texturing
	vpos          = fg_Vertex.xyz;
} 
