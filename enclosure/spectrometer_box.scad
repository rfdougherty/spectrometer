/*
Suggested infill: 30%, triangles
  esp32: l=50, w=50, h=30
esp8266: l=45, w=35, h=30
*/


$fs=0.2;
e = 0.02;

// NOTE: length/width/height are exterior dimensions. Interior dimensions 
// are thick*2 smaller than these.
thick = 1.6;
length = 34 + thick*2 + 5;
width= 25 + thick*2 + 2.1;
height = 32 + thick*2 + 0.5;
cornerRad = 3;
slop = 0.1;
lidRimHeight = 4;
usbWidth = 10 + .5;
usbHeight = lidRimHeight + 5;

pillarRad = 2;
pillarSideOffset = 1; // offset from side of board + extra width/2
pillarEndOffset = 2 + 5; // offset from end of board + extra length
pillarHeight = lidRimHeight;

portOffset = 25 + 5.1;
portRad = 3;

// the rim on the lid will be a bit smaller than the box
lidThick = thick + 0.1;

snapRimRad = 0.5;
snapRimOffset = (thick + cornerRad) * 2;
snapRimDelta = 3; // the rims will be symmetric if this is a factor of width & length

centerWidth = width/2-cornerRad;

translate([cornerRad, cornerRad, 0]){
  difference() {
    union() {
      // The main box
      difference() {
        roundedBox(length, width, height, cornerRad);
        translate([thick, thick, thick+slop])
          roundedBox(length-thick*2, width-thick*2, height-thick+slop, cornerRad);
      }
    }
    // snap-rim (hollowed-out cyliner) on the short sides
    for(y=[thick-cornerRad, length-thick-cornerRad])
      translate([snapRimDelta-cornerRad-1, y, height-(lidRimHeight/2)])
        rotate([0, 90, 0])
          cylinder(r=snapRimRad, h=width-snapRimDelta*2+2);
    // snap-rim on the long sides
    for(x=[thick-cornerRad, width-thick-cornerRad])
      translate([x, snapRimDelta-cornerRad-1, height-(lidRimHeight/2)])
        rotate([-90, 0, 0])
          cylinder(r=snapRimRad, h=length-snapRimDelta*2+2);
    // viewport hole
    translate([-cornerRad+width/2, -cornerRad+portOffset, -e])
      rotate([0, 0, 90])
        cylinder(r=portRad, h=lidThick+e*10);
    // usb cut-out
    translate([width/2 - cornerRad, 
               length - cornerRad-thick/2, 
               height-usbHeight/2+e])
      cube([usbWidth, thick+2*e, usbHeight], center=true);
  }
}

// Make the lid
translate([width*2+5, cornerRad, 0]){
  mirror([1,0,0]) {
    union() {
      difference() {
        union() {
          roundedBox(length, width, thick, cornerRad);
          translate([lidThick, lidThick, 0])
            roundedBox(length-lidThick*2, width-lidThick*2, thick+lidRimHeight, cornerRad);
  
          // Add snap-rim spheres along short edges
          for(x=[-width/2+cornerRad*2 : snapRimDelta : width/2-cornerRad*2]) 
            for(y=[lidThick-cornerRad, length-lidThick-cornerRad])
              translate([x+width/2-cornerRad, y, thick+lidRimHeight/2])
                sphere(snapRimRad);
          // Add snap-rim spheres along long edges
          for(y=[-length/2+cornerRad*2 : snapRimDelta : length/2-cornerRad*2]) 
            for(x=[lidThick-cornerRad, width-lidThick-cornerRad])
              translate([x, y+length/2-cornerRad, thick+lidRimHeight/2])
                sphere(snapRimRad);
        }
        // Hollow-out lid
        translate([lidThick*2, lidThick*2, lidThick+slop])
          roundedBox(length-lidThick*4, width-lidThick*4, thick+lidRimHeight+slop, cornerRad);

      }
    }
      // pillars
      translate([-cornerRad + thick + pillarSideOffset + pillarRad, 
                 pillarEndOffset + thick - pillarRad, 
                 thick])
        difference() {
          cylinder(pillarHeight, r=pillarRad);
          translate([0, 0, e])
            cylinder(pillarHeight, r=pillarRad/3);
        }
      translate([width - cornerRad - thick - pillarRad - pillarSideOffset, 
                 pillarEndOffset + thick - pillarRad, 
                 thick])
        difference() {
          cylinder(pillarHeight, r=pillarRad);
          translate([0, 0, e])
            cylinder(pillarHeight, r=pillarRad/3);
        }
  }
}


module roundedBox(length, width, height, radius){
  dRadius = 2*radius;
  //base rounded shape
  minkowski() {
    cube(size=[width-dRadius,length-dRadius, height]);
    cylinder(r=radius, h=0.01);
  }
}

