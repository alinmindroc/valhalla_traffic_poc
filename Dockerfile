FROM ubuntu:20.04
ENV DISTRIB_ID=Ubuntu
ENV DISTRIB_RELEASE=20.04
ENV DISTRIB_CODENAME=focal
ENV DISTRIB_DESCRIPTION="Ubuntu 20.04.2 LTS"

RUN apt-get update
RUN apt-get install -y software-properties-common
RUN add-apt-repository -y ppa:valhalla-core/valhalla
RUN apt-get update

# Install build dependencies
RUN apt-get install -y cmake make libtool pkg-config g++ gcc curl unzip jq lcov protobuf-compiler vim-common locales libboost-all-dev libcurl4-openssl-dev zlib1g-dev liblz4-dev libprime-server-dev libprotobuf-dev prime-server-bin
RUN apt-get install -y libgeos-dev libgeos++-dev libluajit-5.1-dev libspatialite-dev libsqlite3-dev wget sqlite3 spatialite-bin
RUN apt-get install -y python-is-python3

RUN if [[ $(python -c "print(int($DISTRIB_RELEASE > 15))") > 0 ]]; then sudo apt-get install -y libsqlite3-mod-spatialite; fi
RUN apt-get install -y libsqlite3-mod-spatialite python-all-dev git

RUN git clone --recurse-submodules https://github.com/valhalla/valhalla.git

# Demo utility that uses existing functions from Valhalla code
COPY valhalla_traffic_demo_utils.cc valhalla/src/mjolnir/valhalla_traffic_demo_utils.cc
# New CmakeLists that includes the demo utility for building
COPY CMakeLists.txt valhalla/CMakeLists.txt

# Build valhalla
RUN mkdir valhalla/build
RUN cd valhalla/build; cmake .. -DCMAKE_BUILD_TYPE=Release
RUN cd valhalla/build; make -j$(nproc)
RUN cd valhalla/build; make install

# Generate routing tiles
RUN mkdir valhalla_tiles
RUN cd valhalla_tiles; wget https://download.geofabrik.de/europe/estonia-latest.osm.pbf -O estonia.osm.pbf
# The config is generated without --mjolnir-tile-extract ${PWD}/valhalla_tiles.tar, as traffic routing only works without this
RUN cd valhalla_tiles; valhalla_build_config --mjolnir-tile-dir ${PWD}/valhalla_tiles --mjolnir-timezone ${PWD}/valhalla_tiles/timezones.sqlite --mjolnir-admin ${PWD}/valhalla_tiles/admins.sqlite > valhalla.json
RUN cd valhalla_tiles; valhalla_build_tiles -c valhalla.json estonia.osm.pbf
RUN cd valhalla_tiles; find valhalla_tiles | sort -n | tar cf valhalla_tiles.tar --no-recursion -T -

# Update routing tiles with traffic information
# Create hierarchy of directories for traffic tiles with the same structure as the graph tiles
RUN cd /valhalla_tiles; mkdir traffic; cd valhalla_tiles; find . -type d -exec mkdir -p -- ../traffic/{} \;

# Generate osm ways to valhalla edges mapping:
RUN cd valhalla_tiles; valhalla_ways_to_edges --config valhalla.json
# ^ This generates a file with mappings at valhalla_tiles/ways_edges.txt. The warning about traffic can be safely ignored.
# In order to find the osm id of a way, go to osm editor, edit, click on road, view on openstreetmap.org, check URL
# Let's update the traffic for openstreetmap.org/way/233161449
# The mapping in ways_to_edges.txt is:
# 233161449,1,54660196776,1,54727305640,1,65028516264,1,81268861352,1,83818998184,1,95126841768,1,99824462248,1,100998867368,1,101133085096,1,107642644904,1,107709753768,1,108615723432,1,136633674152,1,136700783016,1,136767891880,1,138512722344,1,138646940072,1,138714048936,1,142539254184
# Format is <osm_way_id>,[<direction: 0 | 1>, <valhalla_edge_id>]. An OSM way can be mapped to multiple valhalla edges, like in this case.

# Generate a csv with speeds for all edges
# Format is edge_id, constrained flow speed (night), free flow speed (day), predicted traffic speeds. Simulate ok free traffic for night and day, but congestions in predicted traffic.
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 54660196776`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` > traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 54727305640`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 65028516264`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 81268861352`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 83818998184`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 95126841768`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 99824462248`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 100998867368`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 101133085096`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 107642644904`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 107709753768`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 108615723432`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 136633674152`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 136700783016`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 136767891880`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 138512722344`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 138646940072`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 138714048936`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv
RUN cd /valhalla_tiles/traffic; echo `valhalla_traffic_demo_utils --get-tile-id 142539254184`,60,50,`valhalla_traffic_demo_utils --get-predicted-traffic 6` >> traffic.csv

# Move the edge to the expected location in the tile hierarchy (all edges have the same tile id)
RUN cd /valhalla_tiles/traffic; mv traffic.csv `valhalla_traffic_demo_utils --get-traffic-dir 54660196776`

# Add traffic information to the routing tiles
RUN cd /valhalla_tiles; valhalla_add_predicted_traffic -t traffic --config valhalla.json
