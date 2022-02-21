#include "baldr/graphreader.h"
#include "baldr/predictedspeeds.h"
#include "baldr/rapidjson_utils.h"
#include "filesystem.h"
#include "mjolnir/graphtilebuilder.h"

#include <cstdint>
#include <cxxopts.hpp>

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/ptree.hpp>

#include "microtar.h"
#include "config.h"

namespace vm = valhalla::midgard;
namespace vb = valhalla::baldr;
namespace vj = valhalla::mjolnir;

// Mmap and mtar structs copied from test/test.cc
struct MMap {
  MMap(const char* filename) {
    fd = open(filename, O_RDWR);
    struct stat s;
#ifdef _MSC_VER
    _fstat64(fd, &s);
#else
    fstat(fd, &s);
#endif
    data = mmap(0, s.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    length = s.st_size;
  }

  ~MMap() {
    munmap(data, length);
    close(fd);
  }

  int fd;
  void* data;
  size_t length;
};

class MMapGraphMemory final : public valhalla::baldr::GraphMemory {
public:
  MMapGraphMemory(std::shared_ptr<MMap> mmap, char* data_, size_t size_) : mmap_(std::move(mmap)) {
    data = data_;
    size = size_;
  }

private:
  const std::shared_ptr<MMap> mmap_;
};

/** Copy of raw header for use with sizeof() **/
typedef struct {
  char name[100];
  char mode[8];
  char owner[8];
  char group[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  char linkname[100];
  char _padding[255];
} mtar_raw_header_t_;

// Copied from test/test.cc
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
    valhalla::baldr::TrafficSpeed dummy_speed = {constant_encoded_speed, constant_encoded_speed, constant_encoded_speed, constant_encoded_speed, 255, 255, 1, 2, 3, 0};
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

// Copied from test/test.cc
// Updates all edges of all tiles in a traffic tar with the given speed
void customize_live_traffic_data(const boost::property_tree::ptree& config,
                                 uint32_t constant_encoded_speed) {
  // Now we have the tar-file and can go ahead with per edge customizations
  {
    const auto memory =
        std::make_shared<MMap>(config.get<std::string>("mjolnir.traffic_extract").c_str());

    mtar_t tar;
    tar.pos = 0;
    tar.stream = memory->data;
    tar.read = [](mtar_t* tar, void* data, unsigned size) -> int {
      memcpy(data, reinterpret_cast<char*>(tar->stream) + tar->pos, size);
      return MTAR_ESUCCESS;
    };
    tar.write = [](mtar_t* tar, const void* data, unsigned size) -> int {
      memcpy(reinterpret_cast<char*>(tar->stream) + tar->pos, data, size);
      return MTAR_ESUCCESS;
    };
    tar.seek = [](mtar_t* /*tar*/, unsigned /*pos*/) -> int { return MTAR_ESUCCESS; };
    tar.close = [](mtar_t * /*tar*/) -> int { return MTAR_ESUCCESS; };

    // Read every speed tile, and update it with fixed speed of `new_speed` km/h (original speeds are
    // 10km/h)
    valhalla::baldr::GraphReader reader(config.get_child("mjolnir"));
    mtar_header_t tar_header;
    while ((mtar_read_header(&tar, &tar_header)) != MTAR_ENULLRECORD) {
      valhalla::baldr::TrafficTile tile(
          std::make_unique<MMapGraphMemory>(memory,
                                            reinterpret_cast<char*>(tar.stream) + tar.pos +
                                                sizeof(mtar_raw_header_t_),
                                            tar_header.size));

      valhalla::baldr::GraphId tile_id(tile.header->tile_id);

      for (uint32_t index = 0; index < tile.header->directed_edge_count; index++) {
        valhalla::baldr::TrafficSpeed* current =
            const_cast<valhalla::baldr::TrafficSpeed*>(tile.speeds + index);
        current->overall_encoded_speed = constant_encoded_speed;
        current->breakpoint1 = 255;
      }
      mtar_next(&tar);
    }
  }
}

int main(int argc, char** argv) {
  //args
  uint64_t way_id;
  float predicted_speed;
  uint32_t live_speed;
  std::string config_file_path;
  try {
    // clang-format off
    cxxopts::Options options(argv[0],
                             " - Provides utilities for adding traffic to valhalla routing tiles.");

    options.add_options()
        ("h,help", "Print this help message.")
        ("c,config", "Path to the json configuration file.",
            cxxopts::value<std::string>(config_file_path))
        ("get-traffic-dir", "Get the traffic tile dir path of an edge id.",
            cxxopts::value<uint64_t>(way_id))
        ("get-tile-id", "Get the tile id of an edge id.",
            cxxopts::value<uint64_t>(way_id))
        ("generate-predicted-traffic", "Generate base64 for an array of speeds filled with a given constant value.",
            cxxopts::value<float>(predicted_speed))
        ("generate-live-traffic", "Generate a traffic.tar archive with speed in live traffic format for a given tile and with a given constant value. Usage: --generate-live-traffic <tile_id>,<speed_in_kmph>,<time of traffic in seconds since epoch>. The tile id is a way id with the tile part set to 0, e.g. for way id 0/3381/123, tile id is 0/3381/0.",
            cxxopts::value<std::vector<std::string>>())
        ("update-live-traffic", "Update all edges in an existing traffic.tar archive with the given speed. Usage: --update-live-traffic <speed_in_kmph>.",
            cxxopts::value<uint32_t>(live_speed));
    // clang-format on

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      std::cout << options.help() << "\n";
      return EXIT_SUCCESS;
    }

    if (result.count("get-traffic-dir")) {
      valhalla::baldr::GraphId graph_id(way_id);
      auto tile_path = GraphTile::FileSuffix(graph_id);
      auto dir = filesystem::path(tile_path);
      auto dir_str = dir.string();
      boost::replace_all(dir_str, ".gph", ".csv");
      std::cout << dir_str << "\n";

      return EXIT_SUCCESS;
    }

    if (result.count("get-tile-id")) {
      valhalla::baldr::GraphId graph_id(way_id);
      std::cout << graph_id << "\n";

      return EXIT_SUCCESS;
    }

    if (result.count("generate-predicted-traffic")) {
      const int five_minute_buckets_per_week_count = 7 * 24 * 60 / 5;
      std::array<float, five_minute_buckets_per_week_count> historical{};
      historical.fill(predicted_speed);

      auto speeds = valhalla::baldr::compress_speed_buckets(historical.data());
      std::cout << valhalla::baldr::encode_compressed_speeds(speeds.data()) << "\n";

      return EXIT_SUCCESS;
    }

    if (result.count("generate-live-traffic")) {
      // Read the config file
      boost::property_tree::ptree pt;
      if (result.count("config") && filesystem::is_regular_file(config_file_path)) {
        rapidjson::read_json(config_file_path, pt);
      } else {
        std::cerr << "Configuration is required\n\n";
        return EXIT_FAILURE;
      }

      std::vector<std::string> live_trafic_params = result["generate-live-traffic"].as<std::vector<std::string>>();

      std::string tile_id = live_trafic_params[0];
      uint32_t encoded_speed = static_cast<uint32_t>(std::stoul(live_trafic_params[1]));
      uint64_t traffic_update_timestamp = static_cast<uint32_t>(std::stoul(live_trafic_params[2]));

      build_live_traffic_data(pt, tile_id, encoded_speed, traffic_update_timestamp);
      std::cout << "Generated traffic.tar succesfully at " << pt.get<std::string>("mjolnir.traffic_extract") << "\n";
      return EXIT_SUCCESS;
    }

    if (result.count("update-live-traffic")) {
      // Read the config file
      boost::property_tree::ptree pt;
      if (result.count("config") && filesystem::is_regular_file(config_file_path)) {
        rapidjson::read_json(config_file_path, pt);
      } else {
        std::cerr << "Configuration is required\n\n";
        return EXIT_FAILURE;
      }

      customize_live_traffic_data(pt, live_speed);
      std::cout << "Updated traffic.tar succesfully at " << pt.get<std::string>("mjolnir.traffic_extract") << "\n";
      return EXIT_SUCCESS;
    }

    std::cout << options.help() << "\n";

  } catch (cxxopts::OptionException& e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_FAILURE;
}
