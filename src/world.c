

#include <stdlib.h>
#include <assert.h>

//#define DEBUG

#include "stage.h"
#include "gui.h"

world_t* world_create( server_t* server, connection_t* con, 
		       stg_id_t id, stg_createworld_t* cw )
{
  PRINT_DEBUG3( "world creator %d (%s) on con %p", id, cw->token, con );
  
  world_t* world = calloc( sizeof(world_t),1 );

  world->con = con;
  world->id = id;
  world->token = strdup( cw->token );
  world->models = g_hash_table_new_full( g_int_hash, g_int_equal,
					 NULL, model_destroy_cb );
  
  world->server = server; // stash the server pointer
  
  world->sim_time = 0.0;
  //world->sim_interval = cw->interval_sim;//STG_DEFAULT_WORLD_INTERVAL;
  world->sim_interval = STG_DEFAULT_WORLD_INTERVAL_MS;
  world->wall_interval = cw->interval_real;  
  world->wall_last_update = 0;//stg_timenow();
  world->ppm = cw->ppm;
  
  // todo - have the matrix resolutions fully configurable at startup
  world->matrix = stg_matrix_create( world->ppm, 5, 1 ); 

  world->paused = TRUE; // start paused.
  
  world->win = gui_world_create( world );

  return world;
}

void world_destroy( world_t* world )
{
  assert( world );
  
  PRINT_DEBUG1( "destroying world %d", world->id );
  
  
  stg_matrix_destroy( world->matrix );

  free( world->token );
  g_hash_table_destroy( world->models );

  gui_world_destroy( world );

  free( world );
}

void world_destroy_cb( gpointer world )
{
  world_destroy( (world_t*)world );
}


void world_update( world_t* world )
{
  if( world->paused ) // only update if we're not paused
    return;

  //{
  stg_msec_t timenow = stg_timenow();
  
 
  //PRINT_DEBUG5( "timenow %lu last update %lu interval %lu diff %lu sim_time %lu", 
  //	timenow, world->wall_last_update, world->wall_interval,  
  //	timenow - world->wall_last_update, world->sim_time  );
  
  // if it's time for an update, update all the models
  if( timenow - world->wall_last_update > world->wall_interval )
    {
      stg_msec_t real_interval = timenow - world->wall_last_update;

      printf( " [%d %lu] sim:%lu real:%lu  ratio:%.2f\r",
	      world->id, 
	      world->sim_time,
	      world->sim_interval,
	      real_interval,
	      (double)world->sim_interval / (double)real_interval  );
      
      fflush(stdout);

      //fflush( stdout );
      
      g_hash_table_foreach( world->models, model_update_cb, world );
      
      
      world->wall_last_update = timenow;
      
      world->sim_time += world->sim_interval;
      
    }
  //}
  
#if 0 //DEBUG
  struct timeval tv1;
  gettimeofday( &tv1, NULL );
#endif
  
  gui_world_update( world );
  
#if 0// DEBUG
  struct timeval tv2;
  gettimeofday( &tv2, NULL );
  
  double guitime = (tv2.tv_sec + tv2.tv_usec / 1e6) - 
    (tv1.tv_sec + tv1.tv_usec / 1e6);
  
  printf( " guitime %.4f\n", guitime );
#endif
}

void world_update_cb( gpointer key, gpointer value, gpointer user )
{
  world_update( (world_t*)value );
}

model_t* world_get_model( world_t* world, stg_id_t mid )
{
  return( world ? g_hash_table_lookup( (gpointer)world->models, &mid ) : NULL );
}


// add a model entry to the server & install its default properties
int world_model_create( world_t* world, stg_createmodel_t* cm )
{
  char* token  = cm->token;
  
  // find the lowest integer that has not yet been assigned to a world
  stg_id_t candidate = 0;
  while( g_hash_table_lookup( world->models, &candidate ) )
    candidate++;
  
  PRINT_DEBUG3( "creating model %d:%d (%s)", world->id, candidate, token  );
  
  model_t* mod = model_create( world, candidate, token ); 
  
  g_hash_table_replace( world->models, &mod->id, mod );

  return candidate; // the id of the new model
}

int world_model_destroy( world_t* world, stg_id_t model )
{
  puts( "model destroy" );
  
  stg_target_t tgt;
  tgt.world = world->id;
  tgt.model = model;
  
  server_remove_subs_of_model( world->server, &tgt );

  // delete the model
  g_hash_table_remove( world->models, &model );

  return 0; // ok
}


void world_handle_msg( world_t* world, int fd, stg_msg_t* msg )
{
  assert( world );
  assert( msg );

  switch( msg->type )
    {
    case STG_MSG_WORLD_MODELCREATE:
      {   
	stg_id_t mid = world_model_create( world, (stg_createmodel_t*)msg->payload );
	
	//printf( "writing reply (model id %d, %d bytes) on fd %d\n", 
	//mid, sizeof(mid), fd );

	stg_fd_msg_write( fd,STG_MSG_CLIENT_REPLY,  &mid, sizeof(mid) );
      }
      break;
      
    case STG_MSG_WORLD_MODELDESTROY:
      world_model_destroy( world, *(stg_id_t*)msg->payload );
      break;
      
      
    case STG_MSG_WORLD_PAUSE:
      PRINT_DEBUG1( "world %d pausing", world->id );
      world->paused = TRUE;
      break;

    case STG_MSG_WORLD_RESUME:
      PRINT_DEBUG1( "world %d resuming", world->id );
      world->paused = FALSE;
      break;

    case STG_MSG_WORLD_RESTART:
      PRINT_WARN( "restart not yet implemented" );
      break;
	
    default:
      PRINT_WARN1( "Ignoring unrecognized world message subtype %d.",
		   msg->type & STG_MSG_MASK_MINOR );
      break;
    }
}

void world_print( world_t* world )
{
  printf( " world %d:%s (%d models)\n", 
	  world->id, 
	  world->token,
	  g_hash_table_size( world->models ) );
  
   g_hash_table_foreach( world->models, model_print_cb, NULL );
}

void world_print_cb( gpointer key, gpointer value, gpointer user )
{
  world_print( (world_t*)value );
}

