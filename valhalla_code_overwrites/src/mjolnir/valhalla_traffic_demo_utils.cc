#include "baldr/graphreader.h"
#include "baldr/predictedspeeds.h"
#include "baldr/rapidjson_utils.h"
#include "filesystem.h"
#include "mjolnir/graphtilebuilder.h"

#include <cstdint>
#include <boost/program_options.hpp>

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/ptree.hpp>

#include "microtar.h"
#include "config.h"

namespace vm = valhalla::midgard;
namespace vb = valhalla::baldr;
namespace vj = valhalla::mjolnir;

namespace bpo = boost::program_options;

/*
Logic copied from valhalla/test/test.cc
This creates a tar file with live traffic information and sets the given constant_encoded_speed to all edges present in the tile.
In a production environment, as only specific edges will have known live speeds, the rest can be set with speeds of 0, as those will be ignored.
*/
void build_live_traffic_data(const boost::property_tree::ptree& config,
                             std::string tile_id,
                             uint32_t constant_encoded_speed,
                             uint64_t traffic_update_timestamp) {

  std::string tile_dir = config.get<std::string>("mjolnir.tile_dir");
  std::string traffic_extract = config.get<std::string>("mjolnir.traffic_extract");

  filesystem::path parent_dir = filesystem::path(traffic_extract).parent_path();
  if (!filesystem::exists(parent_dir)) {
    std::stringstream ss;
    ss << "Traffic extract directory " << parent_dir.string() << " does not exist";
    throw std::runtime_error(ss.str());
  }

  // Begin by seeding the traffic file,
  // per-edge customizations come in the step after
  {
    mtar_t tar;
    auto tar_open_result = mtar_open(&tar, traffic_extract.c_str(), "w");
    if (tar_open_result != MTAR_ESUCCESS) {
      throw std::runtime_error("Could not create traffic tar file");
    }

    valhalla::baldr::GraphReader reader(config.get_child("mjolnir"));

    // Traffic data works like this:
    //   1. There is a separate .tar file containing tile entries matching the main tiles
    //   2. Each tile is a fixed-size, with a header, and entries
    // This loop iterates over the routing tiles, and creates blank
    // traffic tiles with empty records.
    // Valhalla mmap()'s this file and reads from it during route calculation.
    // This loop below creates initial .tar file entries .  Lower down, we make changes to
    // values within the generated traffic tiles and test that routes reflect those changes
    // as expected.

    valhalla::baldr::GraphId tile_graph_id(tile_id);
    auto tile = reader.GetGraphTile(tile_graph_id);

    std::stringstream buffer;
    valhalla::baldr::TrafficTileHeader header = {};
    header.tile_id = tile_graph_id.value;
    header.last_update = traffic_update_timestamp;
    header.traffic_tile_version = valhalla::baldr::TRAFFIC_TILE_VERSION;
    header.directed_edge_count = tile->header()->directededgecount();
    buffer.write(reinterpret_cast<char*>(&header), sizeof(header));
    valhalla::baldr::TrafficSpeed dummy_speed = {constant_encoded_speed, constant_encoded_speed, constant_encoded_speed, constant_encoded_speed, 8, 8, 6, 6, 6, 0};
    for (uint32_t i = 0; i < header.directed_edge_count; ++i) {
      buffer.write(reinterpret_cast<char*>(&dummy_speed), sizeof(dummy_speed));
    }

    uint32_t dummy_uint32 = 0;
    buffer.write(reinterpret_cast<char*>(&dummy_uint32), sizeof(dummy_uint32));
    buffer.write(reinterpret_cast<char*>(&dummy_uint32), sizeof(dummy_uint32));

    /* Write strings to files `test1.txt` and `test2.txt` */
    std::string blanktile = buffer.str();
    std::string filename = valhalla::baldr::GraphTile::FileSuffix(tile_graph_id);
    auto e1 = mtar_write_file_header(&tar, filename.c_str(), blanktile.size());
    if (e1 != MTAR_ESUCCESS) {
      throw std::runtime_error("Could not write tar-file header");
    }
    auto e2 = mtar_write_data(&tar, blanktile.c_str(), blanktile.size());
    if (e2 != MTAR_ESUCCESS) {
      throw std::runtime_error("Could not write tar-file data");
    }

    mtar_finalize(&tar);
    mtar_close(&tar);
  }
}

int main(int argc, char** argv) {
  uint64_t way_id;
  float predicted_speed;
  std::string config_file_path;
  std::vector<std::string> live_trafic_params;

  bpo::options_description options("valhalla_traffic_demo_utils \n"
                                   "\n"
                                   " Usage: valhalla_traffic_demo_utils [options]\n"
                                   "\n"
                                   "provides utils for adding traffic to valhalla routing tiles. "
                                   "\n"
                                   "\n");

  options.add_options()
      ("help,h", "Print this help message.")
      ("config,c", boost::program_options::value<std::string>(&config_file_path), "Path to the json configuration file.")
      ("get-traffic-dir", bpo::value<uint64_t>(&way_id),"Get the traffic tile dir path of an edge id")
      ("get-tile-id", bpo::value<uint64_t>(&way_id),"Get the tile id of an edge id")
      ("generate-predicted-traffic", bpo::value<float>(&predicted_speed),"Generate base64 for an array of speeds filled with a given constant value")
      ("generate-live-traffic", bpo::value<std::vector<std::string>>(&live_trafic_params)->multitoken(),"Generate a traffic.tar archive with speed in live traffic format"
             "for a given tile and with a given constant value.\nUsage: --generate-live-traffic <tile_id> <speed> <time of traffic in seconds since epoch>.\n"
             "Tile id is way id with tile_id = 0, e.g. for way id 0/3381/123, tile id is 0/3381/0");


  bpo::variables_map vm;
  try {
    bpo::store(bpo::command_line_parser(argc, argv).options(options).run(),
               vm);
    bpo::notify(vm);
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  if (vm.count("help") || vm.empty()) {
    std::cout << options << "\n";
    return EXIT_SUCCESS;
  }

  if (vm.count("get-traffic-dir")) {
    valhalla::baldr::GraphId graph_id(way_id);
    auto tile_path = GraphTile::FileSuffix(graph_id);
    auto dir = filesystem::path(tile_path);
    auto dir_str = dir.string();
    boost::replace_all(dir_str, ".gph", ".csv");
    std::cout << dir_str << "\n";

    return EXIT_SUCCESS;
  }

  if (vm.count("get-tile-id")) {
    valhalla::baldr::GraphId graph_id(way_id);
    std::cout << graph_id << "\n";

    return EXIT_SUCCESS;
  }

  if (vm.count("generate-predicted-traffic")) {
    const int five_minute_buckets_per_week_count = 7 * 24 * 60 / 5;
    std::array<float, five_minute_buckets_per_week_count> historical{};
    historical.fill(predicted_speed);

    auto speeds = valhalla::baldr::compress_speed_buckets(historical.data());
    std::cout << valhalla::baldr::encode_compressed_speeds(speeds.data()) << "\n";

    return EXIT_SUCCESS;
  }

  if (vm.count("generate-live-traffic")) {
    // Read the config file
    boost::property_tree::ptree pt;
    if (vm.count("config") && filesystem::is_regular_file(config_file_path)) {
      rapidjson::read_json(config_file_path, pt);
    } else {
      std::cerr << "Configuration is required\n\n";
      return EXIT_FAILURE;
    }

    std::string tile_id = live_trafic_params[0];
    uint32_t encoded_speed = static_cast<uint32_t>(std::stoul(live_trafic_params[1]));
    uint64_t traffic_update_timestamp = static_cast<uint32_t>(std::stoul(live_trafic_params[2]));

    build_live_traffic_data(pt, tile_id, encoded_speed, traffic_update_timestamp);
    std::cout << "Generated traffic.tar succesfully at " << pt.get<std::string>("mjolnir.traffic_extract") << "\n";
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
