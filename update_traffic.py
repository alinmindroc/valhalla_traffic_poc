import sys
import csv
import subprocess

FREE_FLOW_SPEED = 50
CONSTRAINED_FLOW_SPEED = 40
PREDICTED_SPEED = 6 # Simulate congestions in predicted traffic

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <osm_way_id> <way_edges_path>")
    sys.exit(1)

osm_way_id = sys.argv[1]
way_edges_path = sys.argv[2]

mapping = ""

for line in open(way_edges_path):
    if osm_way_id in line:
        mapping = line.strip()
        break

# First get its mapping to edge ids from ways_edges.txt
# Format is <osm_way_id>,[<direction: 0 | 1>, <valhalla_edge_id>]. An OSM way can be mapped to multiple valhalla edges, like in this case.
# We are interested in the valhalla edge ids
edges = mapping.split(",")[1:]
edge_ids = [edges[i] for i in range(len(edges)) if i % 2 == 1]

# Create the traffic.csv file. Format is:
# edge_id, constrained flow speed (night), free flow speed (day), predicted traffic speeds.
traffic_encoded_values = subprocess.run(['valhalla_traffic_demo_utils', '--generate-predicted-traffic', str(PREDICTED_SPEED)], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()

with open ("traffic.csv", "w", newline='') as csvfile:
    csv_writer = csv.writer(csvfile, delimiter=',', lineterminator='\n')
    for edge_id in edge_ids:
        tile_id = subprocess.run(['valhalla_traffic_demo_utils', '--get-tile-id', edge_id], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
        csv_writer.writerow([tile_id, CONSTRAINED_FLOW_SPEED, FREE_FLOW_SPEED, traffic_encoded_values])
