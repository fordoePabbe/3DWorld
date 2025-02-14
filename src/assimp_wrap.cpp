// 3D World - AssImp Reader Wrapper
// by Frank Gennari
// 10/23/2014
// Reference: https://github.com/assimp/assimp

#include "3DWorld.h"
#include "model3d.h"

extern string assimp_alpha_exclude_str;

string get_base_filename(string const &filename);

void model_anim_t::anim_data_t::init(unsigned np, unsigned nr, unsigned ns) {
	assert(pos.empty() && scale.empty() && rot.empty()); // can only call init() once
	assert(np > 0);
	assert(nr > 0);
	assert(ns > 0);
	pos.reserve(np);
	rot.reserve(nr);
	scale.reserve(ns);
}

unsigned model_anim_t::get_bone_id(string const &bone_name) {
	auto it(bone_name_to_index_map.find(bone_name));
	if (it != bone_name_to_index_map.end()) {return it->second;}
	unsigned const bone_id(bone_name_to_index_map.size()); // allocate an index for a new bone
	bone_name_to_index_map[bone_name] = bone_id;
	return bone_id;
}
vector3d model_anim_t::calc_interpolated_position(float anim_time, anim_data_t const &A) const {
	assert(!A.pos.empty());
	if (A.pos.size() == 1) {return A.pos[0].v;} // single value, no interpolation

	for (unsigned i = 0; i+1 < A.pos.size(); ++i) {
		anim_vec3_val_t const &cur(A.pos[i]), &next(A.pos[i+1]);
		if (anim_time >= next.time) continue; // not yet
		float const t((anim_time - cur.time) / (next.time - cur.time));
		assert(t >= 0.0f && t <= 1.0f);
		return cur.v + t*(next.v - cur.v);
	} // for i
	assert(0);
	return zero_vector; // never gets here
}
glm::quat model_anim_t::calc_interpolated_rotation(float anim_time, anim_data_t const &A) const {
	assert(!A.rot.empty());
	if (A.rot.size() == 1) {return A.rot[0].q;} // single value, no interpolation

	for (unsigned i = 0; i+1 < A.rot.size(); ++i) {
		anim_quat_val_t const &cur(A.rot[i]), &next(A.rot[i+1]);
		if (anim_time >= next.time) continue; // not yet
		float const t((anim_time - cur.time) / (next.time - cur.time));
		assert(t >= 0.0f && t <= 1.0f);
		return glm::normalize(glm::slerp(cur.q, next.q, t));
	} // for i
	assert(0);
	return glm::quat(); // never gets here
}
vector3d model_anim_t::calc_interpolated_scale(float anim_time, anim_data_t const &A) const {
	assert(!A.scale.empty());
	if (A.scale.size() == 1) {return A.scale[0].v;} // single value, no interpolation

	for (unsigned i = 0; i+1 < A.scale.size(); ++i) {
		anim_vec3_val_t const &cur(A.scale[i]), &next(A.scale[i+1]);
		if (anim_time >= next.time) continue; // not yet
		float const t((anim_time - cur.time) / (next.time - cur.time));
		assert(t >= 0.0f && t <= 1.0f);
		return cur.v + t*(next.v - cur.v);
	} // for i
	assert(0);
	return zero_vector; // never gets here
}
xform_matrix model_anim_t::apply_anim_transform(float anim_time, animation_t const &animation, anim_node_t const &node) const {
	auto it(animation.anim_data.find(node.name)); // found about half the time
	if (it == animation.anim_data.end()) {return node.transform;} // defaults to node transform
	anim_data_t const &A(it->second);
	xform_matrix node_transform(glm::translate(glm::mat4(1.0), vec3_from_vector3d(calc_interpolated_position(anim_time, A))));
	node_transform *= glm::toMat4(calc_interpolated_rotation(anim_time, A));
	if (A.uses_scale) {node_transform *= glm::scale(glm::mat4(1.0), vec3_from_vector3d(calc_interpolated_scale(anim_time, A)));} // only scale when needed (rarely)
	return node_transform;
}
void model_anim_t::transform_node_hierarchy_recur(float anim_time, animation_t const &animation, unsigned node_ix, xform_matrix const &parent_transform) {
	assert(node_ix < anim_nodes.size());
	anim_node_t const &node(anim_nodes[node_ix]);
	xform_matrix const node_transform(apply_anim_transform(anim_time, animation, node));
	xform_matrix const global_transform(parent_transform * node_transform);

	if (node.bone_index >= 0) {
		assert((size_t)node.bone_index < bone_transforms.size() && (size_t)node.bone_index < bone_offset_matrices.size());
		bone_transforms[node.bone_index] = global_inverse_transform * global_transform * bone_offset_matrices[node.bone_index];
	}
	for (unsigned i : node.children) {transform_node_hierarchy_recur(anim_time, animation, i, global_transform);}
}
void model_anim_t::get_bone_transforms(unsigned anim_id, float cur_time) {
	assert(anim_id < animations.size());
	animation_t const &animation(animations[anim_id]);
	float const time_in_ticks(cur_time * animation.ticks_per_sec);
	float const anim_time(fmod(time_in_ticks, animation.duration));
	transform_node_hierarchy_recur(anim_time, animation, 0, root_transform); // root node is 0
}

void model_anim_t::blend_animations_simple(unsigned anim_id1, unsigned anim_id2, float blend_factor, float cur_time1, float cur_time2) {
	assert(anim_id1 != anim_id2); // this would work, but it doesn't make sense and is inefficient
	animation_t const &animation1(animations[anim_id1]);
	animation_t const &animation2(animations[anim_id2]);
	float const anim_time1(fmod(cur_time1 * animation1.ticks_per_sec, animation1.duration));
	float const anim_time2(fmod(cur_time2 * animation2.ticks_per_sec, animation2.duration));
	get_blended_bone_transforms(anim_time1, anim_time2, animation1, animation2, 0, root_transform, blend_factor); // root node is 0
}
// https://stackoverflow.com/questions/69860756/how-do-i-correctly-blend-between-skeletal-animations-in-opengl-from-a-walk-anima/69917701#69917701
void model_anim_t::blend_animations(unsigned anim_id1, unsigned anim_id2, float blend_factor, float delta_time, float &cur_time1, float &cur_time2) {
	assert(anim_id1 != anim_id2); // this would work, but it doesn't make sense and is inefficient
	animation_t const &animation1(animations[anim_id1]);
	animation_t const &animation2(animations[anim_id2]);
	// speed multipliers to correctly transition from one animation to another
	float const anim_speed_mult_up  ((1.0f - blend_factor) + (animation1.duration/animation2.duration) * blend_factor); // lerp
	float const anim_speed_mult_down((1.0f - blend_factor) * (animation2.duration/animation1.duration) + blend_factor); // lerp
	// current time of each animation, "scaled" by the above speed multiplier variables
	cur_time1 += animation1.ticks_per_sec*delta_time*anim_speed_mult_up;
	cur_time1  = fmod(cur_time1, animation1.duration);
	cur_time2 += animation2.ticks_per_sec*delta_time*anim_speed_mult_down;
	cur_time2  = fmod(cur_time2, animation2.duration);
	get_blended_bone_transforms(cur_time1, cur_time2, animation1, animation2, 0, root_transform, blend_factor); // root node is 0
}
void model_anim_t::get_blended_bone_transforms(float anim_time1, float anim_time2, animation_t const &animation1, animation_t const &animation2,
	unsigned node_ix, xform_matrix const &parent_transform, float blend_factor)
{
	assert(node_ix < anim_nodes.size());
	anim_node_t const &node(anim_nodes[node_ix]);
	xform_matrix const node_transform1(apply_anim_transform(anim_time1, animation1, node));
	xform_matrix const node_transform2(apply_anim_transform(anim_time2, animation2, node));
	// blend two matrices
	glm::quat const rot0(glm::quat_cast(node_transform1)), rot1(glm::quat_cast(node_transform2));
	glm::quat const final_rot(glm::slerp(rot0, rot1, blend_factor));
	glm::mat4 blended_matrix(glm::mat4_cast(final_rot));
	blended_matrix[3] = (1.0f - blend_factor) * node_transform1[3] + node_transform2[3] * blend_factor;
	xform_matrix const global_transform(parent_transform * blended_matrix);

	if (node.bone_index >= 0) {
		assert((size_t)node.bone_index < bone_transforms.size() && (size_t)node.bone_index < bone_offset_matrices.size());
		bone_transforms[node.bone_index] = global_inverse_transform * global_transform * bone_offset_matrices[node.bone_index];
	}
	for (unsigned i : node.children) {get_blended_bone_transforms(anim_time1, anim_time2, animation1, animation2, i, global_transform, blend_factor);}
}

void model_anim_t::merge_from(model_anim_t const &anim) {
	if (animations.empty()) { // first animation added - copy from the incoming class
		assert(anim_nodes.empty());
		*this = anim;
	}
	else {
		// check that all the various bone transforms, mappings, and anim_nodes are compatible; or at least do the easy checks here
		if (anim.global_inverse_transform != global_inverse_transform) {
			cout << "Warning: Mismatching global_inverse_transform values for animation source vs. destination model" << endl;
		}
		if (anim.root_transform != root_transform) {
			cout << "Warning: Mismatching root_transform values for animation source vs. destination model" << endl;
		}
		// anim.bone_name_to_index_map can have fewer entries than bone_name_to_index_map, but the names must match
		for (auto const &kv : anim.bone_name_to_index_map) {
			if (bone_name_to_index_map.find(kv.first) == bone_name_to_index_map.end()) {cout << "Warning: Merging animation with unknown bone name '" << kv.first << "'";}
		}
		// what about bone_transforms, bone_offset_matrices, and bone_name_to_index_map values? they're different in my test models but still work, so maybe they don't need to agree
		vector_add_to(anim.animations, animations); // just combine the animations, and we're done, right?
	}
}
int model_anim_t::get_animation_id_by_name(string const &anim_name) const {
	for (auto a = animations.begin(); a != animations.end(); ++a) {
		if (a->name == anim_name) {return (a - animations.begin());}
	}
	return -1; // not found
}

#ifdef ENABLE_ASSIMP

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>


bool stb_image_enabled(); // from image_io.cpp

string fix_path_slashes(string const &filename) {
  string ret(filename);
#ifdef _WIN32
  std::replace(ret.begin(), ret.end(), '/', '\\');
#else // linux
  std::replace(ret.begin(), ret.end(), '\\', '/');
#endif
  return ret;
}

vector3d  aiVector3D_to_vector3d(aiVector3D const &v) {return vector3d (v.x, v.y, v.z);}
colorRGBA aiColor4D_to_colorRGBA(aiColor4D  const &c) {return colorRGBA(c.r, c.g, c.b, c.a);}
glm::vec3 aiVector3D_to_glm_vec3(aiVector3D const &v) {return glm::vec3(v.x, v.y, v.z);}
xform_matrix aiMatrix4x4_to_xform_matrix(aiMatrix4x4  const &m) {return xform_matrix(glm::transpose(glm::make_mat4(&m.a1)));}
glm::mat3    aiMatrix3x3_to_glm_mat3    (aiMatrix3x3  const &m) {return glm::transpose(glm::make_mat3(&m.a1));}
glm::quat    aiQuaternion_to_glm_quat   (aiQuaternion const &q) {return glm::quat(q.w, q.x, q.y, q.z);}
void print_assimp_matrix(aiMatrix4x4 const &m) {aiMatrix4x4_to_xform_matrix(m).print();}


// For reference, see: https://learnopengl.com/Model-Loading/Model
// Also: https://github.com/emeiri/ogldev
// Also: http://www.xphere.me/2019/05/bones-animation-with-openglassimpglm/

class file_reader_assimp {
	model3d &model;
	geom_xform_t cur_xf;
	string model_dir, anim_name;
	bool load_animations=0, had_vertex_error=0, had_comp_tex_error=0;
	unsigned temp_image_ix=0;

	// texture loading
	struct texture_load_work_item_t {
		aiTexture const *texture;
		unsigned tid;
		texture_load_work_item_t(aiTexture const *const texture_, unsigned tid_) : texture(texture_), tid(tid_) {}
	};
	vector<texture_load_work_item_t> to_load;
	set<unsigned> unique_tids;

	void load_embedded_textures() {
		timer_t timer("Load Embedded Textures", !to_load.empty()); // 1.57s (0.55s) avg across 5 people and 5 zombie models

#pragma omp parallel for schedule(dynamic)
		for (int i = 0; i < (int)to_load.size(); ++i) {
			texture_load_work_item_t const &wi(to_load[i]);
			texture_t &t(model.tmgr.get_texture(wi.tid));

			if (!t.load_stb_image(wi.tid, 1, 0, (unsigned char const *)wi.texture->pcData, wi.texture->mWidth)) { // width is number of bytes
				cerr << "Error: Failed to load embedded texture with stb_image" << endl;
				exit(1); // fatal
			}
			t.init(); // calls calc_color()
		} // for i
		to_load.clear();
	}
	int load_texture(aiScene const *const scene, aiMaterial const* const mat, aiTextureType const type, bool is_normal_map=0) {
		unsigned const count(mat->GetTextureCount(type));
		if (count == 0) return -1; // no texture
		// load only the first texture, as that's all we support
		aiString fn; // absolute path, not relative to the model file
		if (mat->GetTexture(type, 0, &fn) != AI_SUCCESS) return -1;
		char const *const filename(fn.C_Str());
		aiTexture const *const texture(scene->GetEmbeddedTexture(filename));
		string full_path(model_dir + filename);
		bool is_temp_image(0);

		if (texture) {
			assert(texture->pcData);
			// try to read from memory
			// is_alpha_mask=0, verbose=0, invert_alpha=0, wrap=1, mirror=0, force_grayscale=0
			unsigned const tid(model.tmgr.create_texture(full_path, 0, 0, 0, 1, 0, 0, is_normal_map, 1));
			texture_t &t(model.tmgr.get_texture(tid));
			//cout << TXT(width) << TXT(height) << TXT(tid) << TXT(t.is_allocated()) << endl;
			if (t.is_allocated()) return tid; // duplicate

			if (texture->mHeight > 0) { // texture stored uncompressed, size is {width, height}
				// Note: I don't have a test case for this, so it's untested; maybe we need to invert Y?
				// manually allocate and copy texture data; stored as BGRA, but we want RGBA, so can't directly memcpy() it
				t.width = texture->mWidth; t.height = texture->mHeight; t.ncolors = 4;
				t.alloc();
				unsigned char *tdata(t.get_data());
				unsigned const num_pixels(t.num_pixels());

				for (unsigned i = 0; i < num_pixels; ++i) {
					tdata[4*i+0] = texture->pcData[i].r;
					tdata[4*i+1] = texture->pcData[i].g;
					tdata[4*i+2] = texture->pcData[i].b;
					tdata[4*i+3] = texture->pcData[i].a;
				}
				t.init(); // calls calc_color()
				return tid; // done
			}
			// else texture stored compressed
			if (stb_image_enabled()) {
				// defer load until later so that it can be done in parallel; will exit if loading fails
				if (unique_tids.insert(tid).second) {to_load.emplace_back(texture, tid);} // only add once per unique tid
				return tid; // done
			}
			model.tmgr.remove_last_texture(); // not using this texture
			// write as a temporary image file that we can read back in; reuse filename across images
			full_path = "temp_assimp_embedded_image." + get_file_extension(filename);
			ofstream out(full_path, ios::binary);
			out.write((const char *)texture->pcData, texture->mWidth);
			is_temp_image = 1;
		}
		else if (!check_texture_file_exists(full_path)) {
			string const fn(filename);
			bool found(0);

			if (fn.size() > 3 && (fn[0] >= 'A' && fn[0] <= 'Z') && fn[1] == ':' && (fn[2] == '\\' || fn[2] == '/')) {
				// looks like a Windows path that's invalid; try stripping off the path and looking in the current directory
				string const local_path(model_dir + get_base_filename(fn));
				if (check_texture_file_exists(local_path)) {full_path = local_path; found = 1;}
			}
			if (!found) {
				cerr << "Error: Can't find texture file for assimp model: " << full_path << endl;
				return -1;
			}
		}
		// is_alpha_mask=0, verbose=0, invert_alpha=0, wrap=1, mirror=0, force_grayscale=0, invert_y=0, no_cache=is_temp_image, load_now=is_temp_image
		return model.tmgr.create_texture(full_path, 0, 0, 0, 1, 0, 0, is_normal_map, 0, is_temp_image, is_temp_image);
	}

	aiNodeAnim const *find_node_anim(aiAnimation const *const pAnimation, string const &node_name) {
		for (unsigned i = 0; i < pAnimation->mNumChannels; i++) {
			aiNodeAnim const *const anim(pAnimation->mChannels[i]);
			if (string(anim->mNodeName.data) == node_name) return anim;
		}
		return NULL;
	}
	unsigned extract_animation_data_recur(aiScene const *const scene, aiNode const *const node, model_anim_t &model_anim) {
		string const node_name(node->mName.data);
		unsigned const node_ix(model_anim.anim_nodes.size());
		int bone_index(-1); // starts unset
		auto it(model_anim.bone_name_to_index_map.find(node_name));
		if (it != model_anim.bone_name_to_index_map.end()) {bone_index = it->second;} // found
		model_anim.anim_nodes.emplace_back(node_name, aiMatrix4x4_to_xform_matrix(node->mTransformation), bone_index);

		for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
			aiAnimation const *const animation(scene->mAnimations[a]);
			aiNodeAnim  const *const node_anim(find_node_anim(animation, node_name));
			if (!node_anim) continue; // no animation for this node
			model_anim_t::anim_data_t& A(model_anim.animations[a].anim_data[node_name]);
			A.init(node_anim->mNumPositionKeys, node_anim->mNumRotationKeys, node_anim->mNumScalingKeys);
			
			for (unsigned i = 0; i < node_anim->mNumPositionKeys; i++) { // position
				A.pos.emplace_back(node_anim->mPositionKeys[i].mTime, aiVector3D_to_vector3d(node_anim->mPositionKeys[i].mValue));
			}
			for (unsigned i = 0; i < node_anim->mNumRotationKeys; i++) { // rotation
				A.rot.emplace_back(node_anim->mRotationKeys[i].mTime, aiQuaternion_to_glm_quat(node_anim->mRotationKeys[i].mValue));
			}
			for (unsigned i = 0; i < node_anim->mNumScalingKeys; i++) { // scaling
				vector3d const scale(aiVector3D_to_vector3d(node_anim->mScalingKeys[i].mValue));
				A.scale.emplace_back(node_anim->mScalingKeys[i].mTime, scale);
				// scale is used if any component is more than a small tolerance from 1.0
				A.uses_scale |= (fabs(scale.x - 1.0) > 0.0001 || fabs(scale.y - 1.0) > 0.0001 || fabs(scale.z - 1.0) > 0.0001);
			}
		} // for a
		for (unsigned i = 0; i < node->mNumChildren; i++) {
			unsigned const child_ix(extract_animation_data_recur(scene, node->mChildren[i], model_anim));
			model_anim.anim_nodes[node_ix].children.push_back(child_ix);
		}
		return node_ix;
	}
	void extract_animation_data(aiScene const *const scene, model_anim_t &model_anim) {
		assert(scene && scene->mRootNode);
		model_anim.root_transform = cur_xf.create_xform_matrix();
		model_anim.global_inverse_transform = aiMatrix4x4_to_xform_matrix(scene->mRootNode->mTransformation).inverse();
		model_anim.animations.reserve(scene->mNumAnimations);

		for (unsigned a = 0; a < scene->mNumAnimations; ++a) {
			//cout << "adding animation '" << anim_name << "'" << endl; // TESTING
			model_anim.animations.emplace_back(anim_name);
			aiAnimation const *const anim(scene->mAnimations[a]);
			assert(anim);
			read_missing_bones(anim, model_anim);
			if (anim->mTicksPerSecond) {model_anim.animations[a].ticks_per_sec = anim->mTicksPerSecond;} // defaults to 25
			model_anim.animations[a].duration = anim->mDuration;
		}
		extract_animation_data_recur(scene, scene->mRootNode, model_anim);
	}

	// Note: unclear if this is actually needed; at least it seems to do nothing for the models I've tested this on
	void read_missing_bones(aiAnimation const *const anim, model_anim_t &model_anim) {
		// https://learnopengl.com/Guest-Articles/2020/Skeletal-Animation
		// reading channels (bones engaged in an animation and their keyframes)
		for (unsigned i = 0; i < anim->mNumChannels; i++) {
			unsigned const bone_id(model_anim.get_bone_id(anim->mChannels[i]->mNodeName.C_Str()));

			if (bone_id == model_anim.bone_transforms.size()) { // maybe add a new bone with identity transforms
				model_anim.bone_transforms.push_back(xform_matrix());
				model_anim.bone_offset_matrices.push_back(xform_matrix());
			}
		}
	}
	void parse_single_bone(int bone_index, aiBone const *const pBone, mesh_bone_data_t &bone_data, model_anim_t &model_anim, unsigned first_vertex_offset) {
		unsigned const bone_id(model_anim.get_bone_id(pBone->mName.C_Str()));

		if (bone_id == model_anim.bone_transforms.size()) { // maybe add a new bone
			model_anim.bone_transforms.push_back(xform_matrix());
			model_anim.bone_offset_matrices.push_back(aiMatrix4x4_to_xform_matrix(pBone->mOffsetMatrix));
		}
		for (unsigned i = 0; i < pBone->mNumWeights; i++) {
			aiVertexWeight const &vw(pBone->mWeights[i]);
			unsigned const vertex_id(first_vertex_offset + vw.mVertexId);
			assert(vertex_id < bone_data.vertex_to_bones.size());
			bone_data.vertex_to_bones[vertex_id].add(bone_id, vw.mWeight, had_vertex_error);
		}
	}
	void parse_mesh_bones(aiMesh const *const mesh, mesh_bone_data_t &bone_data, model_anim_t &model_anim, unsigned first_vertex_offset) {
		for (unsigned int i = 0; i < mesh->mNumBones; i++) {parse_single_bone(i, mesh->mBones[i], bone_data, model_anim, first_vertex_offset);}
	}
	void process_mesh(aiMesh const *const mesh, aiScene const *const scene, model_anim_t &model_anim) {
		assert(mesh != nullptr);
		if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) return; // not a triangle mesh - skip for now (can be removed using options)
		vector<vert_norm_tc> verts(mesh->mNumVertices);
		vector<unsigned> indices;
		indices.reserve(3*mesh->mNumFaces);
		cube_t mesh_bcube;

		for (unsigned i = 0; i < mesh->mNumVertices; i++) { // process vertices
			vert_norm_tc &v(verts[i]);
			assert(mesh->mVertices != nullptr); // vertices are required
			assert(mesh->mNormals  != nullptr); // we specified normal creation, so these shouldbe non-null
			v.v = aiVector3D_to_vector3d(mesh->mVertices[i]); // position
			v.n = aiVector3D_to_vector3d(mesh->mNormals [i]); // normals

			if (!load_animations) { // not legal to apply model transform here; must be applied after bone transforms
				cur_xf.xform_pos   (v.v);
				cur_xf.xform_pos_rm(v.n);
			}
			if (mesh->mTextureCoords != nullptr && mesh->mTextureCoords[0] != nullptr) { // TCs are optional and default to (0,0); we only use the first of 8
				v.t[0] = mesh->mTextureCoords[0][i].x; 
				v.t[1] = mesh->mTextureCoords[0][i].y;
			}
			point bcube_pt(v.v);
			if (load_animations) {cur_xf.xform_pos(bcube_pt);} // if we didn't transform the point above, transform it now to compute a (hopefully more accurate) bcube
			if (i == 0) {mesh_bcube.set_from_point(bcube_pt);} else {mesh_bcube.union_with_pt(bcube_pt);}
		} // for i
		assert(mesh->mFaces != nullptr);
		assert(mesh->mNumFaces > 0); // if there were verts, there must be faces

		for (unsigned i = 0; i < mesh->mNumFaces; i++) { // process faces/indices
			aiFace const& face(mesh->mFaces[i]);
			assert(face.mNumIndices == 3); // must be triangles
			for (unsigned j = 0; j < face.mNumIndices; j++) {indices.push_back(face.mIndices[j]);}
		}
		if (!mesh_bcube.is_all_zeros()) {
			// if load_animations, the computed bcube will be in the default pose (T-pose for people models), and therefore it won't be usable for VFC
			// can we transform the bcube or individual vertices by the animated bone transforms efficiently?
			if (load_animations) {}
			model.union_bcube_with(mesh_bcube);
		}
		material_t &mat(model.get_material(mesh->mMaterialIndex, 1)); // alloc_if_needed=1
		bool const is_new_mat(mat.empty());
		
		if (is_new_mat) { // process material if this is the first mesh using it
			assert(scene->mMaterials != nullptr);
			aiMaterial *const material(scene->mMaterials[mesh->mMaterialIndex]); // non-const because older assimp's GetName() is non-const
			assert(material != nullptr);
			// setup and load textures
			mat.a_tid    = load_texture(scene, material, aiTextureType_AMBIENT);
			mat.d_tid    = load_texture(scene, material, aiTextureType_DIFFUSE);
			mat.s_tid    = load_texture(scene, material, aiTextureType_SPECULAR);
			mat.bump_tid = load_texture(scene, material, aiTextureType_NORMALS, 1); // is_normal_map=1; or aiTextureType_HEIGHT?
			//mat.refl_tid = load_texture(material, aiTextureType_REFLECTION); // unused
			// setup colors
			aiColor4D color;
			if (aiGetMaterialColor(material, AI_MATKEY_COLOR_AMBIENT,  &color) == AI_SUCCESS) {mat.ka = aiColor4D_to_colorRGBA(color);}
			if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE,  &color) == AI_SUCCESS) {mat.kd = aiColor4D_to_colorRGBA(color);}
			if (aiGetMaterialColor(material, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS) {mat.ks = aiColor4D_to_colorRGBA(color);}
			if (aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS) {mat.ke = aiColor4D_to_colorRGBA(color);}
			float shininess(0.0), strength(0.0), alpha(1.0);
			
			if (aiGetMaterialFloat(material, AI_MATKEY_SHININESS,          &shininess) == AI_SUCCESS &&
				aiGetMaterialFloat(material, AI_MATKEY_SHININESS_STRENGTH, &strength) == AI_SUCCESS)
			{
				mat.ns = shininess * strength;
			}
			// check for dissolve, but skip if it's 0; might also want to look at AI_MATKEY_COLOR_TRANSPARENT
			if (aiGetMaterialFloat(material, AI_MATKEY_OPACITY, &alpha) == AI_SUCCESS && alpha > 0.0) {mat.alpha = alpha;}
			// Note: can also use aiGetMaterialFloatArray(material, AI_MATKEY_OPACITY, &alpha, &num), where num is int(1)
			// Note: The version of assimp I have installed in Ubuntu doesn't have AI_MATKEY_TRANSMISSION_FACTOR
			aiGetMaterialFloat(material, AI_MATKEY_TRANSPARENCYFACTOR, &mat.tr       );
#ifdef _WIN32 // METALLIC_FACTOR is only available in newer Assimp versions, and not on my linux machine; that's okay because we don't load any models with metallic yet anyway
			aiGetMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR,    &mat.metalness);
#endif
			//if (aiGetMaterialInteger(mtl, AI_MATKEY_ENABLE_WIREFRAME, &wireframe) == AI_SUCCESS) {}
			//if (aiGetMaterialInteger(mtl, AI_MATKEY_TWOSIDED,         &two_sided) == AI_SUCCESS) {}
			// AI_MATKEY_ROUGHNESS_FACTOR?
			// illum? tf?
			// apply special case string match for forcing alpha to 1.0 (for hair, etc.)
			if (!assimp_alpha_exclude_str.empty() && string(material->GetName().C_Str()).find(assimp_alpha_exclude_str) != string::npos) {mat.no_blend = 1;}
		}
		unsigned const first_vertex_offset(mat.add_triangles(verts, indices, 1)); // add_new_block=1; should return 0

		if (load_animations && mesh->HasBones()) { // handle bones
			mesh_bone_data_t &bone_data(mat.get_bone_data_for_last_added_tri_mesh());
			bone_data.vertex_to_bones.resize(first_vertex_offset + mesh->mNumVertices);
			parse_mesh_bones(mesh, bone_data, model_anim, first_vertex_offset);
			for (unsigned i = first_vertex_offset; i < bone_data.vertex_to_bones.size(); ++i) {bone_data.vertex_to_bones[i].normalize();} // normalize weights to 1.0
		}
	}
	void process_node_recur(aiNode const *const node, aiScene const *const scene, model_anim_t &model_anim) {
		assert(node != nullptr);
		//print_assimp_matrix(node->mTransformation);
		// process all the node's meshes (if any), in tree order rather than simply iterating over mMeshes
		for (unsigned i = 0; i < node->mNumMeshes; i++) {process_mesh(scene->mMeshes[node->mMeshes[i]], scene, model_anim);}
		// then do the same for each of its children
		for (unsigned i = 0; i < node->mNumChildren; i++) {process_node_recur(node->mChildren[i], scene, model_anim);}
	}
public:
	file_reader_assimp(model3d &model_, bool load_animations_=0, string const &anim_name_="") :
		model(model_), anim_name(anim_name_), load_animations(load_animations_) {}

	bool read(string const &fn, geom_xform_t const &xf, bool recalc_normals, bool verbose) {
		cur_xf = xf;
		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false); // required for correct FBX model import
		// aiProcess_OptimizeMeshes
		// aiProcess_ValidateDataStructure - for debugging
		// aiProcess_ImproveCacheLocality - optional, but already supported by the model3d class
		// aiProcess_FindDegenerates, aiProcess_FindInvalidData - optional
		// aiProcess_FlipUVs - not needed since this can be done in the texture loading
		// aiProcess_CalcTangentSpace - ???
		unsigned flags(aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_JoinIdenticalVertices |
			           aiProcess_FixInfacingNormals | aiProcess_GenUVCoords | aiProcess_OptimizeMeshes);
		// Note: here we treat the recalc_normals flag as using smooth normals; if the model already contains normals, they're always used
		flags |= (recalc_normals ? aiProcess_GenSmoothNormals : aiProcess_GenNormals);
		if (!load_animations) {flags |= aiProcess_PreTransformVertices | aiProcess_RemoveRedundantMaterials;}
		aiScene const* const scene(importer.ReadFile(fn, flags));
		
		if (scene == nullptr) {
			cerr << "AssImp Import Error (null scene): " << importer.GetErrorString() << endl;
			return 0; // always fatal, even if there's no error string
		}
		if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
			string const error_str(importer.GetErrorString());

			if (!error_str.empty()) { // we have an error message
				cerr << "AssImp Import Error (incomplete scene): " << importer.GetErrorString() << endl;
				return 0; // nonfatal?
			}
			cerr << "Warning: AssImp flagged incomplete scene" << endl; // nonfatal
		}
		if (scene->mRootNode == nullptr) {cout << "Warning: No root node for model" << endl;}
		if (scene->mNumAnimations == 0) {load_animations = 0;} // no animations to load
		model_dir = fn;
		while (!model_dir.empty() && model_dir.back() != '/' && model_dir.back() != '\\') {model_dir.pop_back();} // remove filename from end, but leave the slash
		if (scene->mRootNode) {process_node_recur(scene->mRootNode, scene, model.model_anim_data);}
		unsigned const num_textures(to_load.size());
		load_embedded_textures();
		if (load_animations) {extract_animation_data(scene, model.model_anim_data);}
		model.finalize(); // optimize vertices, remove excess capacity, compute bounding sphere, subdivide, compute LOD blocks
		model.load_all_used_tids();
		
		if (verbose) {
			if (!model.empty()) { // don't print these stats if empty (animations only)
				cout << "bcube: " << model.get_bcube().str() << endl;
				model.show_stats();
			}
			if (num_textures > 0) {cout << "embedded_textures: " << num_textures << endl;}
			if (load_animations ) {cout << "animations: " << model.model_anim_data.animations.size() << ", anim_nodes: " << model.model_anim_data.anim_nodes.size() << endl;}
		}
		return 1;
	}
}; // end file_reader_assimp

bool read_assimp_model(string const &filename, model3d &model, geom_xform_t const &xf, string const &anim_name, int recalc_normals, bool verbose) {
	cout << "Reading model file " << filename << endl;
	timer_t timer("Read AssImp Model"); // 2.37s (1.32s MT) avg across 5 people and 5 zombie models
	bool const load_animations(!anim_name.empty());
	file_reader_assimp reader(model, load_animations, anim_name);
	return reader.read(fix_path_slashes(filename), xf, recalc_normals, verbose);
}

#else // ENABLE_ASSIMP

bool read_assimp_model(string const &filename, model3d &model, geom_xform_t const &xf, int recalc_normals, bool verbose) {
	cerr << "Error: Assimp model import has not been enabled at compile time" << endl;
	return 0;
}

#endif
