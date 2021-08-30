## Intro

Valhalla is an open source routing engine, with support for time-dependent routing and traffic. 
It doesn't have at the moment any official step-by-step instructions on how to add traffic support, only a description of the feature: https://valhalla.readthedocs.io/en/latest/thor/simple_traffic/

This repository shows how to create an instance of valhalla with both types of supported traffic information:
1. __Predicted traffic__, used for time-based routing (e.g. _find me a route between A and B that leaves tomorrow at 12:00_). This is done through a CSV which contains encoded speeds for a whole week for a given edge.
2. __Live traffic__, used for real-time decisions, and which can be set dynamically. This is done through a traffic.tar file that is memory mapped by valhalla.  

It uses existing tooling plus a new tool `valhalla_traffic_demo_utils` that makes use of data structures and algorithms in the Valhalla source code.

## How to run

1. Build docker image `docker build -t valhalla-traffic .`
    * (takes around 10 min, as it downloads + processes the OSM map for the whole country of Estonia)
2. Start container `docker run -p 8002:8002 -it valhalla-traffic bash`
    * The port forwarding is important for the demos below
3. Inside the container, start the server `valhalla_service /valhalla_tiles/valhalla.json 1`
4. Verify that Valhalla processed the traffic information correctly, by querying the way which we updated (or using the interactive demo in the next step):
```
curl http://localhost:8002/locate --data '{"locations": [{"lat": 59.430462989308495, "lon": 24.771084543317553}], "verbose": true}' | jq
```
One of the ways returned by the query should contain a non-empty `predicted_speeds` array for predicted traffic, and a `live_speed` object for live traffic. This means that Valhalla has the traffic information available.
   
5. Check interactive demo based on the locally running valhalla: https://valhalla.github.io/demos/routing/index-internal.html#loc=15,59.429276,24.776402
    * __Left click__ to place origin / destination for routing
    * __Right click__ to query node/edge info

## Examples

OSM way which has slowed down predicted traffic: https://www.openstreetmap.org/way/233161449#map=15/59.4302/24.7748
![OSM way](screenshots/osm_way.png?raw=true "OSM way")

Valhalla avoiding the way when requested time-dependent routing (Available in routing options):
![OSM way](screenshots/with_date_time.png?raw=true "OSM way")

Valhalla using the way when requested simple, non time-dependent routing:
![OSM way](screenshots/without_date_time.png?raw=true "OSM way")

## Other gotchas

### Predicted traffic
For predicted traffic, speeds must be higher than 5 Km/h to be taken into account. 
Anything lower than that is considered noise.

The free flow and constrained flow speeds are used by default by the routing API.
In order to use the predicted traffic information, the `date_time` parameter needs to be set in the route request.  

### Live traffic
For live traffic there are some more rules. Check the `GetSpeed` function in `baldr/graphtile.h` for the conditions.

## Other example requests:

### Route:
```
curl http://localhost:8002/route --data '{"locations":[{"lat":59.431,"lon":24.768},{"lat":59.43,"lon":24.776}],"costing":"auto","directions_options":{"units":"meters"}}' | jq
```
Decode polyline6: http://valhalla.github.io/demos/polyline/

### DistanceMatrix:
```
curl http://localhost:8002/sources_to_targets --data '{"sources":[{"lat":59.431,"lon":24.768},{"lat":59.43,"lon":24.776},{"lat":59.433851,"lon":24.770216}], "targets":[{"lat":59.429696,"lon":24.759521},{"lat":59.424097,"lon":24.778592},{"lat":59.42268,"lon":24.745916}],"costing":"auto","directions_options":{"units":"kilometers"}}' | jq
```
View isochrone: http://geojson.io/

### Isochrone:

```
curl http://localhost:8002/isochrone --data '{"locations":[{"lat":59.431,"lon":24.768}],"costing":"auto","contours":[{"time":5,"color":"ff0000"}],"date_time":{"type":1,"value":"2021-09-30T23:50"}}'
```
