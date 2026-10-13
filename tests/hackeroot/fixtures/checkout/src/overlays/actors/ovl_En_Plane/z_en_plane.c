#include "z_en_plane.h"

void EnPlane_Draw(EnPlane* this);

typedef struct PlaneProfile {
    int id;
    void (*draw)(EnPlane*);
} PlaneProfile;

PlaneProfile En_Plane_Profile = { 1, EnPlane_Draw };

static Vtx sPlaneVtx[] = {
    VTX(1, 2, 3, 4, 5, 6, 7, 8, 9),
};

static Gfx sPlaneDL[] = {
    gsSPVertex(sPlaneVtx, 1, 0),
    gsSPEndDisplayList(),
};

void EnPlane_Draw(EnPlane* this) {
    this->alpha = 0;
}
