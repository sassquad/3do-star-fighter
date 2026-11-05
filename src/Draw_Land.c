#include "sf_compat.h"
#include "Plot_Land.h"
#include "SF_ARMCell.h"
#include "SF_Screenutils.h"
#include "SF_Celutils.h"
#include "Draw_Land.h"
#include "Setup_Tables.h"
#include "Plot_Graphic.h"
#include "Smoke_Control.h"
#include "Laser_Control.h"
#include "Bit_Control.h"
#include "SF_ARMLink.h"
#include "SF_Io.h"
#include "Maths_Stuff.h"
#include "SF_ARMSky.h"
#include "Sound_Control.h"
#include "Update_Frame.h"
#include "SF_War.h"
#include "Graphics_Set.h"
#include "Weapons.h"
#include "sf_settings.h"

// Test stuff
// #include "OperaMath.h"
// #include "Math_Test.h"

/* The sky and the black polygon under the horizon are laid out in whole
   logical pixels around the horizon point (size_x, size_y), as they always
   were.  In the sub-pixel 3D view (sf_compat.h) the finished quad is moved to
   sub-pixel units, keeping the horizon point's fraction from the projection
   so the sky follows the land smoothly. */
static int32_t sky_frac_x, sky_frac_y;

/* ground objects found round the camera (up to 60 x 60 object-map cells) */
static intptr_t object_found[(SF_OBJECT_DISTANCE_MAX / 2) * (SF_OBJECT_DISTANCE_MAX / 2) + 1];

static void
sky_quad_to_subpix(void)
{
  if (!cel_subpix) {
    return;
  }
  cel_quad.x_pos0 = SUBPIX(cel_quad.x_pos0) + sky_frac_x;
  cel_quad.y_pos0 = SUBPIX(cel_quad.y_pos0) + sky_frac_y;
  cel_quad.x_pos1 = SUBPIX(cel_quad.x_pos1) + sky_frac_x;
  cel_quad.y_pos1 = SUBPIX(cel_quad.y_pos1) + sky_frac_y;
  cel_quad.x_pos2 = SUBPIX(cel_quad.x_pos2) + sky_frac_x;
  cel_quad.y_pos2 = SUBPIX(cel_quad.y_pos2) + sky_frac_y;
  cel_quad.x_pos3 = SUBPIX(cel_quad.x_pos3) + sky_frac_x;
  cel_quad.y_pos3 = SUBPIX(cel_quad.y_pos3) + sky_frac_y;
}

void
draw_land()

{
  int32_t loop, temp_long, height_scaler = 0;
  ship_stack *ship;
  bit_stack *bit;
  smoke_stack *smoke;
  laser_stack *laser;
  int32_t size_x = 0, size_y = 0, size_z, size = 0;
  int32_t x_pos, y_pos, z_pos;
  rotate_node node_data;

  int32_t x_grid, y_grid;
  intptr_t *found = temp_store;

  int32_t draw_long, in_range, in_draw, as_sprite;
  int32_t sound_dist_small_ship = 1 << 30;
  int32_t sound_dist_big_ship = 1 << 30;

  static char *map_base_adr = (char *) &poly_map[0][0];

  graphics_details *object_details = (graphics_details *) static_graphics_adr;
  graphics_details *details;

  // More test stuff
  // mmv3m33d *m ;
  // BuildXYZ( x_mat , y_mat , z_mat , matrix ) ;
  // m->dest = &post_rot[0] ;
  // m->src = &pre_rot[0] ;
  // m->mat = &matrix ;
  // m->n = 1000 ;
  // m->count = 55 ;
  // for (loop = 0 ; loop < 4096 ; loop += 1 )
  //{
  // MulManyVec3Mat33_F16 ( &post_rot[0] , &pre_rot[0] , matrix , 55 ) ;
  // MulManyVec3Mat33DivZ_F16 ( m ) ;
  // plot_ship_graphic( &mc_vars[0] );
  // }

  if (test_mode == 1) {
    armzsort_add(size, players_ship, players_ship->can_see ? 0 : PLOT_AS_SPRITE);
    setup_rotations();
    cel_quad.x_pos0 = -SUBPIX(160);
    cel_quad.y_pos0 = -SUBPIX(120);
    cel_quad.x_pos1 = SUBPIX(160);
    cel_quad.y_pos1 = -SUBPIX(120);
    cel_quad.x_pos2 = SUBPIX(160);
    cel_quad.y_pos2 = SUBPIX(120);
    cel_quad.x_pos3 = -SUBPIX(160);
    cel_quad.y_pos3 = SUBPIX(120);
    cel_quad.shade = 0;
    arm_addmonocel(&cel_quad, 0, 55, 0);
    machine_code_flat_land_plot();
    machine_code_land_plot();
    return;
  }

  // ############################################################################
  // ############################################################################
  // ############################################################################
  // ############################################################################
  // ############################################################################
  // ############################################################################

  // Reset the sound ship pointers
  sound_big_ship = NULL;
  sound_small_ship = NULL;

  // Find any ground objects in the area to plot
  // Only plot the ground objects if you are low
  // (below BUILDING_ALTITUDE in config.txt, default 40; above the game's own
  // 40 they are drawn but are not lock-on candidates)
  if (camera_z_position < (sf_building_altitude << 24) &&
      camera_z_position > -(sf_building_altitude << 24)) {
    // Find near by ground objects: a window of OBJECT_DISTANCE / 2 object-map
    // cells (2 squares each) round the camera, default 32 (+-32 squares).
    // Objects outside the game's own 32-cell window are drawn but are not
    // lock-on candidates.
    int32_t cells = sf_object_distance / 2, x_orig, y_orig;
    x_orig = ((((camera_x_position) >> 25) - 16) & 127) & ~3;
    y_orig = (((camera_y_position) >> 25) - 16) & 127;
    x_grid = (((camera_x_position) >> 25) - cells / 2) & 127;
    y_grid = (((camera_y_position) >> 25) - cells / 2) & 127;
    found = object_found;
    scan_poly_map_window(x_grid, y_grid, found, cells);

    if (camera_z_position < 0) {
      temp_long = -(camera_z_position >> 2);
    } else {
      temp_long = camera_z_position >> 2;
    }

    while (*found >= 0) {
      x_grid = (*found) & 127;
      y_grid = (*found) >> 7;

      // *(map_base_adr+(*found)) = 9 ;

      details = object_details + (*(map_base_adr + (*found)));

      // find the greatest x,y,z dist
      size_x = ((unsigned) x_grid << 25) - camera_x_position;
      if (size_x < 0) {
        size_x = -size_x;
      }
      size_y = ((unsigned) y_grid << 25) - camera_y_position;
      if (size_y < 0) {
        size_y = -size_y;
      }

      size_x -= details->x_size;
      size_y -= details->y_size;

      if (size_x > size_y) {
        size = size_x;
      } else {
        size = size_y;
      }

      size = size + (land_sort_offset << 24) - (4 << 24);

      if (size < 0) {
        size = 0;
      }

      armzsort_add(size, // + temp_long ,
                   (void *) *found,
                   4 | ((((x_grid - x_orig) & 127) < 32 && ((y_grid - y_orig) & 127) < 32 &&
                         camera_z_position < (40 << 24) &&
                         camera_z_position > -(40 << 24))
                            ? 0
                            : PLOT_NO_LOCKON));

      found += 1;
    }
  }

  // Find near by ships to plot
  ship = (ships.info).start_address;

  while ((ship->header).status == 1) {

    if (ship_being_viewed != ship) {
      if ((ship->type >> 4) == BIG_SHIP || (ship->type >> 4) == SMALL_SHIP) {
        x_pos = ship->x_pos;
        y_pos = ship->y_pos;
        z_pos = ship->z_pos;

        // ship->special_data->cloaking_device = SDB_CLOAKING_ON ;

        if (ship->special_data->cloaking_device == SDB_CLOAKING_OFF ||
            ship->who_hit_me > 0) {
          temp_long = (1 << 30);
        } else {
          // printf("lcokseofsdukglskfgdsilfgweriulkjdfbdfbn");
          temp_long = 0;
        }
      } else {
        if (ship->type >= SECTION) {
          x_pos = ship->goto_x;
          y_pos = ship->goto_y;
          z_pos = ship->goto_z;
          temp_long = (1 << 30);
        } else {
          x_pos = ship->x_pos;
          y_pos = ship->y_pos;
          z_pos = ship->z_pos;

          if ((ship->type >> 4) == PARACHUTE ||
              (ship->type >> 4) == SATELLITE) {
            temp_long = (1 << 30);
          } else {
            temp_long = (1 << 29);
          }
        }
      }

      // temp_long is the game's own range (1 << 30 or 1 << 29, 0 when
      // cloaked): it decides can_see and the engine sounds.  draw_long is the
      // drawing range, scaled by OBJECT_DISTANCE (equal at the default).
      draw_long = (temp_long == (1 << 30))   ? (sf_object_distance << 24)
                  : (temp_long == (1 << 29)) ? (sf_object_distance << 23)
                                             : 0;
      in_range = (x_pos - camera_x_position) < temp_long &&
                 (x_pos - camera_x_position) > -temp_long &&
                 (y_pos - camera_y_position) < temp_long &&
                 (y_pos - camera_y_position) > -temp_long &&
                 (z_pos - camera_z_position) < temp_long &&
                 (z_pos - camera_z_position) > -temp_long;
      in_draw = (x_pos - camera_x_position) < draw_long &&
                (x_pos - camera_x_position) > -draw_long &&
                (y_pos - camera_y_position) < draw_long &&
                (y_pos - camera_y_position) > -draw_long &&
                (z_pos - camera_z_position) < draw_long &&
                (z_pos - camera_z_position) > -draw_long;

      if (!in_range) {
        if (temp_long == 0) {
          ship->can_see = TRUE;
        } else {
          ship->can_see = FALSE;
        }
      }

      if (in_range || in_draw) {
        // find the greatest x,y,z dist
        size_x = x_pos - camera_x_position;
        if (size_x < 0) {
          size_x = -size_x;
        }
        size_y = y_pos - camera_y_position;
        if (size_y < 0) {
          size_y = -size_y;
        }
        size_z = z_pos - camera_z_position;
        if (size_z < 0) {
          size_z = -size_z;
        }

        if (size_x > size_y) {
          size = size_x;
        } else {
          size = size_y;
        }
        if (size_z > size) {
          size = size_z;
        }

        // drawn as a distant sprite beyond half the drawing range
        as_sprite = 0;
        if ((ship->type >> 4) == SMALL_SHIP || (ship->type >> 4) == WEAPON ||
            (ship->type >> 4) == PARACHUTE) {
          if (size > (draw_long >> 1)) {
            as_sprite = PLOT_AS_SPRITE;
          }
        }

        if (in_range) { // game state: as the original
          ship->can_see = TRUE;

          if ((ship->type >> 4) == SMALL_SHIP ||
              (ship->type >> 4) == PLAYERS_SHIP) {
            if (size < sound_dist_small_ship) {
              sound_dist_small_ship = size;
              sound_small_ship = ship;
            }
          }

          if ((ship->type >> 4) == SMALL_SHIP || (ship->type >> 4) == WEAPON ||
              (ship->type >> 4) == PARACHUTE) {
            if (size > (temp_long >> 1)) {
              ship->can_see = FALSE;
            }
          }

          if ((ship->type >> 4) == BIG_SHIP) {
            if (size < sound_dist_big_ship) {
              sound_dist_big_ship = size;
              sound_big_ship = ship;
            }
          }
        }

        // if ( ship->type < 256 )
        //{
        //	details = ship_details + ( ship->type ) ;
        //	temp_long = details->clip_size ;
        // }
        // else
        //{
        //	temp_long = 1<<24 ;
        // }

        if (((ship->type) >> 4) == PLAYERS_SHIP || ship->type == WING_POD) {
          if (docked.status != DOCKING_OUT) {
            temp_long = 1 << 24;
          } else {
            temp_long = 0;
          }
        } else {
          temp_long = 0;
        }

        size -= temp_long;
        if (size < 0) {
          size = 0;
        }

        if (in_draw) {
          armzsort_add(size, ship, as_sprite | (in_range ? 0 : PLOT_NO_LOCKON));
        }
      }
    }

    ship = (ship->header).next_address;
  }

  // Sound for internal viewwwwwww
  if (ship_being_viewed != NULL) {
    if ((ship_being_viewed->type >> 4) == SMALL_SHIP ||
        (ship_being_viewed->type >> 4) == PLAYERS_SHIP) {
      sound_small_ship = ship_being_viewed;
    }

    if ((ship_being_viewed->type >> 4) == BIG_SHIP) {
      sound_big_ship = ship_being_viewed;
    }
  }

  // plot the near by smoke
  smoke = (smokes.info).start_address;

  while ((smoke->header).status == 1) {

    if (smoke->x_pos - camera_x_position < (sf_object_distance << 24) &&
        smoke->x_pos - camera_x_position > -(sf_object_distance << 24) &&
        smoke->y_pos - camera_y_position < (sf_object_distance << 24) &&
        smoke->y_pos - camera_y_position > -(sf_object_distance << 24) &&
        smoke->z_pos - camera_z_position < (sf_object_distance << 24) &&
        smoke->z_pos - camera_z_position > -(sf_object_distance << 24)) {

      // find the greatest x,y,z dist
      size_x = (smoke->x_pos) - camera_x_position;
      if (size_x < 0) {
        size_x = -size_x;
      }
      size_y = (smoke->y_pos) - camera_y_position;
      if (size_y < 0) {
        size_y = -size_y;
      }
      size_z = (smoke->z_pos) - camera_z_position;
      if (size_z < 0) {
        size_z = -size_z;
      }

      if (size_x > size_y) {
        size = size_x;
      } else {
        size = size_y;
      }
      if (size_z > size) {
        size = size_z;
      }

      // size -= ((1<<24)*2) ;
      // if (size<0) size = 0;

      armzsort_add(size, smoke, 1);

      // armzsort_add( 0 , smoke , 1) ;

      // plot_smoke(smoke) ;
    }

    smoke = (smoke->header).next_address;
  }

  // plot the near by lasers
  laser = (lasers.info).start_address;

  while ((laser->header).status == 1) {

    if (laser->x_pos - camera_x_position < (sf_object_distance << 24) &&
        laser->x_pos - camera_x_position > -(sf_object_distance << 24) &&
        laser->y_pos - camera_y_position < (sf_object_distance << 24) &&
        laser->y_pos - camera_y_position > -(sf_object_distance << 24) &&
        laser->z_pos - camera_z_position < (sf_object_distance << 24) &&
        laser->z_pos - camera_z_position > -(sf_object_distance << 24)) {

      // find the greatest x,y,z dist
      size_x = (laser->x_pos) - camera_x_position +
               ((laser->x_pos2 - laser->x_pos) >> 1);
      if (size_x < 0) {
        size_x = -size_x;
      }
      size_y = (laser->y_pos) - camera_y_position +
               ((laser->y_pos2 - laser->y_pos) >> 1);
      if (size_y < 0) {
        size_y = -size_y;
      }
      size_z = (laser->z_pos) - camera_z_position +
               ((laser->z_pos2 - laser->z_pos) >> 1);
      if (size_z < 0) {
        size_z = -size_z;
      }

      if (size_x > size_y) {
        size = size_x;
      } else {
        size = size_y;
      }
      if (size_z > size) {
        size = size_z;
      }

      armzsort_add(size, laser, 2);

      // plot_laser(laser) ;
    }
    laser = (laser->header).next_address;
  }

  // plot the near by explosion bits

  bit = (bits.info).start_address;

  while ((bit->header).status == 1) {

    if (bit->x_pos - camera_x_position < (sf_object_distance << 24) &&
        bit->x_pos - camera_x_position > -(sf_object_distance << 24) &&
        bit->y_pos - camera_y_position < (sf_object_distance << 24) &&
        bit->y_pos - camera_y_position > -(sf_object_distance << 24) &&
        bit->z_pos - camera_z_position < (sf_object_distance << 24) &&
        bit->z_pos - camera_z_position > -(sf_object_distance << 24)) {

      // find the greatest x,y,z dist
      size_x = (bit->x_pos) - camera_x_position;
      if (size_x < 0) {
        size_x = -size_x;
      }
      size_y = (bit->y_pos) - camera_y_position;
      if (size_y < 0) {
        size_y = -size_y;
      }
      size_z = (bit->z_pos) - camera_z_position;
      if (size_z < 0) {
        size_z = -size_z;
      }

      if (size_x > size_y) {
        size = size_x;
      } else {
        size = size_y;
      }
      if (size_z > size) {
        size = size_z;
      }

      armzsort_add(size, bit, 3);

      // plot_bit_graphic(bit) ;
    }

    bit = (bit->header).next_address;
  }

  setup_rotations();

  // rotate_sky(&mc_vars);
  // screen_rendersky(mc_vars[0],mc_vars[1]);

  // screen_base( S_SKY );

  // Setup initial sky nodes and plot black polygon down from the horizon
  // to cover where the land might not reach to

  if (planet_info.space_mission != 1 &&
      (camera_y_rotation > ((256 * 3) + 128) ||
       camera_y_rotation < ((256 * 3) - 128))) {
    height_scaler = (camera_z_position - (1 << 28)) >> 19;
    if (height_scaler > 1024) {
      height_scaler = 1024;
    }

    // Screen dimentions = 320 * 220
    // coords 0,0 = centre of screen
    // furthest point from center = sqr ( (320/2)^2 + (220/2)^2 ) = 200

    // Plot the sky
    node_data.x_pos = 0;
    node_data.y_pos = 32768;
    node_data.z_pos = 0;
    rotate_sky_node(&node_data);

    if (node_data.y_pos < 0) {
      node_data.x_pos = -node_data.x_pos;
      node_data.y_pos = -node_data.y_pos;
      node_data.z_pos = -node_data.z_pos;

      node_data.x_rot = (512 + camera_z_rotation) & 1023;

    } else {
      node_data.x_rot = camera_z_rotation;
    }

    if (node_data.y_pos < 2048) {
      node_data.y_pos = 2048;
    }

    {
      int32_t pex = pex_fix(node_data.y_pos, pex_table[(node_data.y_pos) >> 6]);
      int32_t sub_x = (node_data.x_pos * pex) >> (16 - cel_subpix);
      int32_t sub_y = (node_data.z_pos * pex) >> (16 - cel_subpix);
      size_x = sub_x >> cel_subpix; /* whole pixels, exactly as before */
      size_y = sub_y >> cel_subpix;
      sky_frac_x = sub_x - SUBPIX(size_x);
      sky_frac_y = sub_y - SUBPIX(size_y);
    }

    size = find_2d_distance(size_x, size_y);

    arm_rendersky((camera_z_position >> 17), (200 + size));

    if (size > 200) {
      node_data.x_pos = 0;
      node_data.y_pos = 200;
      rotate_2d_node(&node_data);
      size_x = node_data.x_pos;
      size_y = node_data.y_pos;
      sky_frac_x = sky_frac_y = 0;
    }

    size += 200;
    if (size > 400) {
      size = 400;
    }

    size = -size;

    node_data.x_pos = 200;
    node_data.y_pos = 0;
    rotate_2d_node(&node_data);
    cel_quad.x_pos2 = size_x + node_data.x_pos;
    cel_quad.y_pos2 = size_y + node_data.y_pos;

    node_data.x_pos = -200;
    node_data.y_pos = 0;
    rotate_2d_node(&node_data);
    cel_quad.x_pos3 = size_x + node_data.x_pos;
    cel_quad.y_pos3 = size_y + node_data.y_pos;

    node_data.x_pos = -200;
    node_data.y_pos = 30;
    rotate_2d_node(&node_data);
    cel_quad.x_pos0 = size_x + node_data.x_pos;
    cel_quad.y_pos0 = size_y + node_data.y_pos;

    node_data.x_pos = 200;
    node_data.y_pos = 30;
    rotate_2d_node(&node_data);
    cel_quad.x_pos1 = size_x + node_data.x_pos;
    cel_quad.y_pos1 = size_y + node_data.y_pos;

    cel_quad.shade = 0;

    sky_quad_to_subpix();
    arm_addmonocel(&cel_quad, 0, 55, 0);
  }

  // Plot all flat distant land sections and all graphics
  machine_code_flat_land_plot();

  if (planet_info.space_mission != 1) {
    if (camera_y_rotation > ((256 * 3) + 128) ||
        camera_y_rotation < ((256 * 3) - 128)) {
      node_data.x_pos = -200;
      node_data.y_pos = size;
      rotate_2d_node(&node_data);
      cel_quad.x_pos0 = size_x + node_data.x_pos;
      cel_quad.y_pos0 = size_y + node_data.y_pos;

      node_data.x_pos = 200;
      node_data.y_pos = size;
      rotate_2d_node(&node_data);
      cel_quad.x_pos1 = size_x + node_data.x_pos;
      cel_quad.y_pos1 = size_y + node_data.y_pos;

      node_data.x_pos = 200;
      node_data.y_pos = 0;
      rotate_2d_node(&node_data);
      cel_quad.x_pos2 = size_x + node_data.x_pos;
      cel_quad.y_pos2 = size_y + node_data.y_pos;

      node_data.x_pos = -200;
      node_data.y_pos = 0;
      rotate_2d_node(&node_data);
      cel_quad.x_pos3 = size_x + node_data.x_pos;
      cel_quad.y_pos3 = size_y + node_data.y_pos;

      if (height_scaler <= 0) {
        sky_quad_to_subpix();
        arm_plotsky(&cel_quad);
      }

      if (height_scaler > 0) {
        for (loop = -200; loop < 200; loop += 25) {
          node_data.x_pos = loop; // -200 ;
          node_data.y_pos = size;
          rotate_2d_node(&node_data);
          cel_quad.x_pos0 = size_x + node_data.x_pos;
          cel_quad.y_pos0 = size_y + node_data.y_pos;

          node_data.x_pos = loop + 25; // 200 ;
          node_data.y_pos = size;
          rotate_2d_node(&node_data);
          cel_quad.x_pos1 = size_x + node_data.x_pos;
          cel_quad.y_pos1 = size_y + node_data.y_pos;

          temp_long = loop + 25;
          temp_long = ((temp_long * temp_long) >> 10);

          node_data.x_pos = loop + 25; // 200 ;
          node_data.y_pos = (temp_long * height_scaler) >> 10;
          rotate_2d_node(&node_data);
          cel_quad.x_pos2 = size_x + node_data.x_pos;
          cel_quad.y_pos2 = size_y + node_data.y_pos;

          temp_long = loop;
          temp_long = ((temp_long * temp_long) >> 10);

          node_data.x_pos = loop; // -200 ;
          node_data.y_pos = (temp_long * height_scaler) >> 10;
          rotate_2d_node(&node_data);
          cel_quad.x_pos3 = size_x + node_data.x_pos;
          cel_quad.y_pos3 = size_y + node_data.y_pos;

          sky_quad_to_subpix();
          arm_plotsky(&cel_quad);
        }
      }
      plot_stars();
    }
  } else {
    plot_space_stars();
  }

  plot_planets();

  // if (atg_selected != 0 && which_graphics_set != SPACE_GRAPHICS )
  //{
  //	air_to_ground_scan = air_to_ground_scan_temp ;
  //	players_ship->target = (void*) air_to_ground_scan ;
  // }

  // if (ata_selected != 0 && which_graphics_set == SPACE_GRAPHICS )
  //{
  //	air_to_ground_scan = air_to_ground_scan_temp ;
  // }

  if (ata_selected != 0 || atg_selected != 0) {

    air_to_ground_scan = air_to_ground_scan_temp;
    air_to_air_scan = air_to_air_scan_temp;
    // players_ship->target = (void*) air_to_air_scan ;

    if (players_ship->last_fire_request == WEAPON_BEAM_LASER ||
        atg_selected != 0 ||
        (which_graphics_set == SPACE_GRAPHICS && ata_selected != 0)) {
      // If its beam lasers which is nearest - ship or static graphic ?
      if (air_to_ground_x < 0) {
        air_to_ground_x = -air_to_ground_x;
      }
      if (air_to_air_x < 0) {
        air_to_air_x = -air_to_air_x;
      }

      if (air_to_air_x < air_to_ground_x) {
        players_ship->target = (void *) air_to_air_scan;
        air_to_ground_scan = (intptr_t) NULL;
      } else {
        players_ship->target = (void *) air_to_ground_scan;
        air_to_air_scan = (intptr_t) NULL;
      }
    } else {
      players_ship->target = (void *) air_to_air_scan;
      air_to_ground_scan = (intptr_t) NULL;
    }
  }

  air_to_ground_scan_temp = (intptr_t) NULL;
  air_to_air_scan_temp = (intptr_t) NULL;

  air_to_ground_x = (1 << 30);
  air_to_ground_y = (1 << 30);
  air_to_ground_z = (1 << 30);

  air_to_air_x = (1 << 30);
  air_to_air_y = (1 << 30);
  air_to_air_z = (1 << 30);

  // Plot all hilly land sections and all graphics
  machine_code_land_plot();

  // Exit with a big bunch of polys drawn on t' screen
}
