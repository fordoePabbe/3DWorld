// 3D World - Building Animals (rats, etc)
// by Frank Gennari 1/16/22

#include "3DWorld.h"
#include "function_registry.h"
#include "buildings.h"

extern float fticks;
extern building_params_t global_building_params;

void gen_xy_pos_for_cube_obj(cube_t &C, cube_t const &S, vector3d const &sz, float height, rand_gen_t &rgen);


void building_t::update_animals(unsigned building_ix) {
	if (!has_room_geom() || interior->rooms.empty() || global_building_params.num_rats == 0) return;
	rand_gen_t rgen;
	rgen.set_state(building_ix+1, mat_ix+1); // unique per building
	vector<rat_t> &rats(interior->room_geom->rats);

	if (rats.empty()) { // new building - place rats
		float const base_radius(0.1*get_window_vspace());
		rats.reserve(global_building_params.num_rats);

		for (unsigned n = 0; n < global_building_params.num_rats; ++n) {
			float const radius(base_radius*rgen.rand_uniform(0.8, 1.2));
			point const pos(gen_rat_pos(radius, rgen));
			if (pos == all_zeros) continue; // bad pos? skip this rat
			rats.emplace_back(pos, radius);
		}
	}
	for (auto &rat : rats) {update_rat(rat, rgen);}
}

point building_t::gen_rat_pos(float radius, rand_gen_t &rgen) const {
	for (unsigned n = 0; n < 100; ++n) { // make up to 100 tries
		unsigned const room_ix(rgen.rand() % interior->rooms.size());
		room_t const &room(interior->rooms[room_ix]);
		if (room.z1() > ground_floor_z1) continue; // not on the ground floor or basement
		cube_t place_area(room);
		place_area.expand_by_xy(-(radius + get_wall_thickness()));
		place_area.z1() += get_fc_thickness(); // on top of the floor
		vector3d const sz(radius, radius, 0.5*radius); // height is half radius
		cube_t cand;
		gen_xy_pos_for_cube_obj(cand, place_area, sz, sz.z, rgen);
		// TODO: check for object collisions
		return point(cand.xc(), cand.yc(), cand.z1());
	} // for n
	return all_zeros; // failed
}

void building_t::update_rat(rat_t &rat, rand_gen_t &rgen) const {
	if (rat.speed > 0.0) {
		// TODO: movement logic, collision detection
		rat.pos += rat.speed*rat.dir;
	}
	if (rat.fear > 0.0) {
		// TODO: hide (in opposite direction from fear_pos?)
		rat.fear = max(0.0, (rat.fear - 0.1*(fticks/TICKS_PER_SECOND))); // reduce fear over 10s
	}
	else {
		// explore
	}
	if (rat.dest != rat.pos) {
		// TODO: set speed
		rat.dir = (rat.dest - rat.pos).get_norm(); // TODO: slow turn (like people)
	}
}

void building_t::scare_animals(point const &scare_pos, float sight_amt, float sound_amt) {
	float const amount(min((sight_amt + sound_amt), 1.0f)); // for now we don't differentiate between sight and sound for rats
	assert(amount > 0.0);
	float const max_scare_dist(3.0*get_window_vspace()), scare_dist(max_scare_dist*amount);
	int const scare_room(get_room_containing_pt(scare_pos));
	if (scare_room < 0) return; // error?

	for (auto &rat : interior->room_geom->rats) {
		if (rat.fear > 0.99) continue; // already max fearful (optimization)
		float const dist(p2p_dist(rat.pos, scare_pos));
		if (dist < scare_dist) continue; // optimization
		int const rat_room(get_room_containing_pt(rat.pos));
		assert(rat_room >= 0);
		if (rat_room != scare_room) continue; // only scared if in the same room
		float fear(amount);

		if (sight_amt > 0.0) { // check line of sight
			bool is_visible(1);
			// TODO: visibility check
		}
		fear = fear*max_scare_dist - dist;
		if (fear <= 0.0) continue;
		rat.fear     = min(1.0f, (rat.fear + fear));
		rat.fear_pos = scare_pos;
	} // for rat
}

void building_t::draw_animals(shader_t &s, vector3d const &xlate) const {
	if (!has_room_geom()) return;
	
	for (auto &rat : interior->room_geom->rats) {
		// TODO: draw a 3D model
	} // for rat
}