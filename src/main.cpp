/*
 * ROS map generator
 *  main.cpp
 *
 * Created by Dmitriy Abramov (karvozavr@gmail.com) April, 2017
 */

#include <iostream>
#include "ros_navigation_environment.h"
#include "ros_navigation_environment_renderer.h"
#include "boost/program_options.hpp"
#include <fstream>

void save_yaml(const std::string &file_name, std::ostream &out_file, double resolution) {
  out_file << "image: " << file_name << '\n';
  out_file << "resolution: " << resolution << '\n';
  out_file << "origin: [0.0, 0.0, 0.0]\noccupied_thresh: 0.65\nfree_thresh: 0.196\nnegate: 0\n" << '\n';
}

int main(int argc, char *argv[]) {

  try {

    double actual_corridor_width = 0;
    size_t room_amount = 30;
    double resolution = 0.05;
    double robot_size = 0.5;

    std::string out_file = "occupancy_grid";
    std::string out_dir;

    bool obstacles = false;

    double actual_min_size = 0;
    double actual_max_size = 0;

    int64_t seed = std::chrono::system_clock::now().time_since_epoch().count();

    boost::program_options::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "print usage message")

        ("obstacles,o", boost::program_options::bool_switch(&obstacles),
         "generate obstacles inside rooms (false by default)")

        ("complexity,c", boost::program_options::value(&room_amount)->required(),
         "complexity of environment (aka number of rooms)")

        ("resolution,r", boost::program_options::value(&resolution),
         "resolution of the map: meters per pixel (0.05 by default)")

        ("robot-size,s", boost::program_options::value(&robot_size), "size of the robot in meters")

        ("min-size", boost::program_options::value(&actual_min_size), "minimal possible size of a room")

        ("max-size", boost::program_options::value(&actual_max_size), "maximal possible size of a room")

        ("corridor-width", boost::program_options::value(&actual_corridor_width),
         "corridor width (should be greater than robot size)")

        ("output-dir,d", boost::program_options::value(&out_dir), "output file directory")

        ("name,n", boost::program_options::value(&out_file), "output file name")

        ("random-seed", boost::program_options::value(&seed), "seed for pseudo-random number generation");


    boost::program_options::variables_map variables_map;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), variables_map);

    if (variables_map.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    if (variables_map.count("robot-size") &&
        (!variables_map.count("min-size") || !variables_map.count("max-size"))) {
      std::cout << "Argument error: min-size and max-size required, when robot-size specified" << std::endl;
      return 1;
    }

    variables_map.notify();

    if (room_amount == 0) {
      room_amount = 1;
    }

    int64_t min_size = static_cast<int64_t>(robot_size / resolution) * 4;
    int64_t max_size = min_size * 2;

    if (variables_map.count("min-size")) {
      min_size = std::max(min_size, static_cast<int64_t>(actual_min_size / resolution));
      max_size = std::max(static_cast<int64_t>(actual_max_size / resolution), min_size + 1);
    }

    int64_t corridor_width = static_cast<int64_t>((robot_size / resolution)) * 2;

    if (variables_map.count("corridor-width")) {
      corridor_width = std::max(static_cast<int64_t>((robot_size / resolution)),
                                static_cast<int64_t>(actual_corridor_width / resolution));
    }

    size_t map_size = static_cast<size_t>(std::sqrt(room_amount) * (min_size + max_size) * 2);

    // create random environment
    Randomizer<int64_t> randomizer(seed);
    RosNavigationEnvironment nav_space(room_amount * 2,
                                       min_size,
                                       max_size,
                                       corridor_width,
                                       map_size,
                                       map_size,
                                       &randomizer,
                                       obstacles);

    std::ofstream out_stream;
    out_stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // render and save the environment
    out_stream.open(out_dir + out_file + ".pgm", std::ios::binary);
    RosNavigationEnvironmentRenderer renderer(out_stream, nav_space);
    renderer.save_to_pgm();

    out_stream.close();
    out_stream.open(out_dir + out_file + ".yaml");
    save_yaml(out_file + ".pgm", out_stream, resolution);

  } catch (std::exception &exception) {
    std::cerr << exception.what() << std::endl;
  }

  return 0;
}