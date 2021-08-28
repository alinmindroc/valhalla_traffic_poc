#include "baldr/predictedspeeds.h"
#include "filesystem.h"
#include "mjolnir/graphtilebuilder.h"

#include <cmath>
#include <cstdint>

#include <boost/optional.hpp>
#include <boost/program_options.hpp>

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>

namespace vm = valhalla::midgard;
namespace vb = valhalla::baldr;
namespace vj = valhalla::mjolnir;

namespace bpo = boost::program_options;

int main(int argc, char** argv) {
  uint64_t way_id;
  float predicted_speed;

  bpo::options_description options("valhalla_traffic_demo_utils \n"
                                   "\n"
                                   " Usage: valhalla_traffic_demo_utils [options]\n"
                                   "\n"
                                   "provides utils for adding traffic to valhalla routing tiles. "
                                   "\n"
                                   "\n");

  options.add_options()
      ("help,h", "Print this help message.")
      ("get-traffic-dir,tdir", bpo::value<uint64_t>(&way_id),"Get the traffic tile dir path of an edge id")
      ("get-tile-id,tid", bpo::value<uint64_t>(&way_id),"Get the tile id a way id")
      ("get-predicted-traffic,tid", bpo::value<float>(&predicted_speed),"Generate base64 for an array filled with a given speed");

  bpo::positional_options_description pos_options;
  pos_options.add("get-traffic-dir", 1);
  pos_options.add("get-tile-id", 1);
  bpo::variables_map vm;
  try {
    bpo::store(bpo::command_line_parser(argc, argv).options(options).positional(pos_options).run(),
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

  if (vm.count("get-predicted-traffic")) {
    const int five_minute_buckets_per_week_count = 7 * 24 * 60 / 5;
    std::array<float, five_minute_buckets_per_week_count> historical{};
    historical.fill(predicted_speed);

    auto speeds = valhalla::baldr::compress_speed_buckets(historical.data());
    std::cout << valhalla::baldr::encode_compressed_speeds(speeds.data()) << "\n";

    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
