/* The roads, coast, water and parks for the Tab5 base station's map.
   Replace each S,W,N,E with your area's box in degrees -- south, west, north,
   east, e.g. 51.45,-0.20,51.55,-0.05 -- run it at overpass-api.de (or any
   Overpass mirror), save the JSON, and draw tiles from it:
   python tools/render_tiles.py area.json tiles 12 16 S W N E */
[out:json][timeout:180];
(
  way["highway"~"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link|living_street|service)$"](S,W,N,E);
  way["natural"="coastline"](S,W,N,E);
  way["natural"="water"](S,W,N,E);
  way["leisure"="park"](S,W,N,E);
);
out geom;
