#ifndef SCENE_TABLE_H
#define SCENE_TABLE_H

/**
 * DEFINE_SCENE(name, titleCardName, enumValue, drawConfig, restrictionFlags, persistentCycleFlags)
 */

/* 0x00 */ DEFINE_SCENE(ydan_scene, none, SCENE_DEKU_TREE, SDC_YDAN, 0, 0)
/* 0x01 */ DEFINE_SCENE(ddan_scene, none, SCENE_DODONGOS_CAVERN, SDC_DEFAULT, 0, 0)
/* 0x02 */ DEFINE_SCENE(bdan_scene, none, SCENE_JABU_JABU, SDC_BDAN, 0, (1 << 2))
/* 0x03 */ DEFINE_SCENE(Bmori1_scene, none, SCENE_FOREST_TEMPLE, SDC_DEFAULT, 0, 0)
/* 0x04 */ DEFINE_SCENE(HIDAN_scene, none, SCENE_FIRE_TEMPLE, SDC_DEFAULT, 0, 0)
/* 0x05 */ DEFINE_SCENE(MIZUsin_scene, none, SCENE_WATER_TEMPLE, SDC_DEFAULT, 0, 0)

#endif
