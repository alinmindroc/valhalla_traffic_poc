## Intro

Valhalla is an open source routing engine, with support for time-dependent routing and traffic. 
It doesn't have at the moment any official step-by-step instructions on how to add traffic support, only a description of the feature: https://valhalla.readthedocs.io/en/latest/thor/simple_traffic/

This repository shows how to create an instance of valhalla with traffic information (at the moment only predicted speeds, no live traffic yet).

It uses existing tooling plus a new tool `valhalla_traffic_demo_utils` that makes use of data structures and algorithms in the Valhalla source code.

## How to run

1. Build docker image `docker build -t valhalla-traffic .`
    * (takes around 10 min, as it downloads + processes the OSM map for the whole country of Estonia)
1. Start container `docker run -p 8002:8002 -it valhalla-traffic bash`
    * The port forwarding is important for the demos below
2. Inside the container, start the server `valhalla_service /valhalla_tiles/valhalla.json 1`
3. Check interactive demo based on the locally running valhalla: https://valhalla.github.io/demos/routing/index-internal.html#loc=15,59.429276,24.776402
    * __Left click__ to place origin / destination for routing
    * __Right click__ to get node/edge info

OSM way for which traffic is slowed down: https://www.openstreetmap.org/way/233161449#map=15/59.4302/24.7748
![OSM way](osm_way.png?raw=true "OSM way")

Valhalla avoiding the way when time-dependent routing is requested (Available in routing options):
![OSM way](with_date_time.png?raw=true "OSM way")

Valhalla using the way when time-dependent routing is not requested:
![OSM way](without_date_time.png?raw=true "OSM way")

## Other info

Only speeds higher than 5 Km/h are taken into account for traffic. 
Anything lower than that is considered noise.

The free flow and constrained flow speeds are used by default by the routing API.
In order to use the predicted traffic information, the `date_time` parameter needs to be set.  

Check the dockerfile comments for more info.

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
