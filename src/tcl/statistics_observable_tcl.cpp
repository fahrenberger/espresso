/*
  Copyright (C) 2010,2011,2012,2013,2014 The ESPResSo project
  
  This file is part of ESPResSo.
  
  ESPResSo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  ESPResSo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*/
#include <cstring>
#include "tcl/statistics_observable_tcl.hpp"
#include "statistics_observable.hpp"
#include "particle_data.hpp"
#include "parser.hpp"
//#include "integrate.hpp"
#include "lb.hpp"
#include "pressure.hpp"

/* forward declarations */
int tclcommand_observable_print_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values);
int tclcommand_observable_print(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs);
int tclcommand_observable_update(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs);

static int convert_types_to_ids(IntList * type_list, IntList * id_list); 
  
int parse_id_list(Tcl_Interp* interp, int argc, char** argv, int* change, IntList** ids ) {
  int i,ret;
//  char** temp_argv; int temp_argc;
//  int temp;
  IntList* input=(IntList*)malloc(sizeof(IntList));
  IntList* output=(IntList*)malloc(sizeof(IntList));
  init_intlist(input);
  alloc_intlist(input,1);
  init_intlist(output);
  alloc_intlist(output,1);
  if (argc<1) {
    Tcl_AppendResult(interp, "Error parsing id list\n", (char *)NULL);
    return TCL_ERROR;
  }


  //Observables rely on the partCfg being sortable. If this is not true, an ambiguous error pops up later
  if (!sortPartCfg()) {
    Tcl_AppendResult(interp, "Error parsing particle specifications.\nProbably your particle ids are not contiguous.\n", (char *)NULL);
    return TCL_ERROR;
  }
  if (ARG0_IS_S("ids")) {
    if (!parse_int_list(interp, argv[1],input)) {
      Tcl_AppendResult(interp, "Error parsing id list\n", (char *)NULL);
      return TCL_ERROR;
    } 
    *ids=input;
    for (i=0; i<input->n; i++) {
      if (input->e[i] >= n_part) {
        Tcl_AppendResult(interp, "Error parsing ID list. Given particle ID exceeds the number of existing particles\n", (char *)NULL);
        return TCL_ERROR;
      }
    }
    *change=2;
    return TCL_OK;

  } else if ( ARG0_IS_S("types") ) {
    if (!parse_int_list(interp, argv[1],input)) {
      Tcl_AppendResult(interp, "Error parsing types list\n", (char *)NULL);
      return TCL_ERROR;
    } 
    if( (ret=convert_types_to_ids(input,output))<=0){
        Tcl_AppendResult(interp, "Error parsing types list. No particle with given types found.\n", (char *)NULL);
        return TCL_ERROR;
    } else { 
      *ids=output;
    }
    *change=2;
    return TCL_OK;
  } else if ( ARG0_IS_S("all") ) {
    if( (ret=convert_types_to_ids(NULL,output))<=0){
        Tcl_AppendResult(interp, "Error parsing keyword all. No particle found.\n", (char *)NULL);
        return TCL_ERROR;
    } else { 
      *ids=output;
    }
    *change=1;
    return TCL_OK;
  }

  Tcl_AppendResult(interp, "unknown keyword given to observable: ", argv[0] , (char *)NULL);
  return TCL_ERROR;
}



int tclcommand_parse_profile(Tcl_Interp* interp, int argc, char** argv, int* change, int* dim_A, profile_data** pdata_);
int tclcommand_parse_radial_profile(Tcl_Interp* interp, int argc, char** argv, int* change, int* dim_A, radial_profile_data** pdata);
int sf_print_usage(Tcl_Interp* interp);



int tclcommand_observable_print_profile_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values, int groupsize, int shifted) {
  profile_data* pdata=(profile_data*) obs->container;
  char buffer[TCL_DOUBLE_SPACE];
  double data;
  int linear_index;
  double x_offset, y_offset, z_offset, x_incr, y_incr, z_incr;
  if (shifted) {
    x_incr = (pdata->maxx-pdata->minx)/(pdata->xbins);
    y_incr = (pdata->maxy-pdata->miny)/(pdata->ybins);
    z_incr = (pdata->maxz-pdata->minz)/(pdata->zbins);
    x_offset = pdata->minx + 0.5*x_incr;
    y_offset = pdata->miny + 0.5*y_incr;
    z_offset = pdata->minz + 0.5*z_incr;
  } else {
    x_incr = (pdata->maxx-pdata->minx)/(pdata->xbins-1);
    y_incr = (pdata->maxy-pdata->miny)/(pdata->ybins-1);
    z_incr = (pdata->maxz-pdata->minz)/(pdata->zbins-1);
    x_offset = pdata->minx;
    y_offset = pdata->miny;
    z_offset = pdata->minz;
  }
  for (int i = 0; i < pdata->xbins; i++) 
    for (int j = 0; j < pdata->ybins; j++) 
      for (int k = 0; k < pdata->zbins; k++) {
        if (pdata->xbins>1) {
          Tcl_PrintDouble(interp, x_offset + i*x_incr , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }

        if (pdata->ybins>1) {
          Tcl_PrintDouble(interp,y_offset + j*y_incr, buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        if (pdata->zbins>1) {
          Tcl_PrintDouble(interp,z_offset + k*z_incr, buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        linear_index = 0;
        if (pdata->xbins > 1)
          linear_index += i*pdata->ybins*pdata->zbins;
        if (pdata->ybins > 1)
          linear_index += j*pdata->zbins;
        if (pdata->zbins > 1)
          linear_index +=k;

        for (int l = 0; l<groupsize; l++) {
          data=values[groupsize*linear_index+l];
          Tcl_PrintDouble(interp, data , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        Tcl_AppendResult(interp, "\n", (char *)NULL );
      }
  return TCL_OK;
//  Tcl_AppendResult(interp, "\n", (char *)NULL );
}

int tclcommand_observable_print_radial_profile_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values, int groupsize, int shifted) {
  radial_profile_data* pdata=(radial_profile_data*) obs->container;
  char buffer[TCL_DOUBLE_SPACE];
  double data;
  int linear_index;
  double r_offset, phi_offset, z_offset, r_incr, phi_incr, z_incr;
  if (shifted) {
    r_incr = (pdata->maxr-pdata->minr)/(pdata->rbins);
    phi_incr = (pdata->maxphi-pdata->minphi)/(pdata->phibins);
    z_incr = (pdata->maxz-pdata->minz)/(pdata->zbins);
    r_offset = pdata->minr + 0.5*r_incr;
    phi_offset = pdata->minphi + 0.5*phi_incr;
    z_offset = pdata->minz + 0.5*z_incr;
  } else {
    r_incr = (pdata->maxr-pdata->minr)/(pdata->rbins-1);
    phi_incr = (pdata->maxphi-pdata->minphi)/(pdata->phibins-1);
    z_incr = (pdata->maxz-pdata->minz)/(pdata->zbins-1);
    r_offset = pdata->minr;
    phi_offset = pdata->minphi;
    z_offset = pdata->minz;
  }
  for (int i = 0; i < pdata->rbins; i++) 
    for (int j = 0; j < pdata->phibins; j++) 
      for (int k = 0; k < pdata->zbins; k++) {
        if (pdata->rbins>1) {
          Tcl_PrintDouble(interp, r_offset + i*r_incr , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }

        if (pdata->phibins>1) {
          Tcl_PrintDouble(interp, phi_offset + j*phi_incr , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        if (pdata->zbins>1) {
          Tcl_PrintDouble(interp, z_offset + k*z_incr , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        } else {
          Tcl_PrintDouble(interp, 0 , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        linear_index = 0;
        if (pdata->rbins > 1)
          linear_index += i*pdata->phibins*pdata->zbins;
        if (pdata->phibins > 1)
          linear_index += j*pdata->zbins;
        if (pdata->zbins > 1)
          linear_index +=k;
        
        for (int j = 0; j<groupsize; j++) {
          data=values[groupsize*linear_index+j];
          Tcl_PrintDouble(interp, data , buffer);
          Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
        }
        Tcl_AppendResult(interp, "\n", (char *)NULL );
      }
  return TCL_OK;
}

int tclcommand_observable_tclcommand(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  int n_A;
  Observable_Tclcommand_Arg_Container* container;
  if (argc!=3) {
      Tcl_AppendResult(interp, "Usage: observable tclcommand <n_vec> <command>\n", (char *)NULL );
      return TCL_ERROR;
  }
  if (!ARG1_IS_I(n_A)) {
      Tcl_AppendResult(interp, "Error in observable tclcommand: n_vec is not int\n", (char *)NULL );
      return TCL_ERROR;
  }
  container = (Observable_Tclcommand_Arg_Container*) malloc(sizeof(Observable_Tclcommand_Arg_Container));
  container->command = (char*)malloc(strlen(argv[2])*sizeof(char*));
  strcpy(container->command, argv[2]);
  container->n_A = n_A;
  container->interp = interp;

  obs->calculate=&observable_calc_tclcommand;
  obs->update=0;
  obs->n=n_A;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  obs->container=(void*) container;
          
  return TCL_OK;
}

int tclcommand_observable_particle_velocities(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->calculate=&observable_calc_particle_velocities;
  obs->update=0;
  obs->container=ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_particle_body_velocities(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->calculate=&observable_calc_particle_body_velocities;
  obs->update=0;
  obs->container=ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_particle_angular_momentum(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->calculate=&observable_calc_particle_angular_momentum;
  obs->update=0;
  obs->container=ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_particle_body_angular_momentum(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->calculate=&observable_calc_particle_body_angular_momentum;
  obs->update=0;
  obs->container=ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_com_velocity(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp, blocksize;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  argc-=temp+1;
  argv+=temp+1;
  for ( int i = 0; i < argc; i++) {
    printf("%s\n", argv[i]);
  }
  if (argc>0 && ARG0_IS_S("blocked")) {
    if (argc >= 2 && ARG1_IS_I(blocksize) && (ids->n % blocksize ==0 )) {
      obs->calculate=&observable_calc_blocked_com_velocity;
      obs->update=0;
      obs->container=ids;
      obs->n=3*ids->n/blocksize;
      obs->last_value=(double*)malloc(obs->n*sizeof(double));
      *change=3+temp;
      printf("found %d ids and a blocksize of %d, that makes %d dimensions\n", ids->n, blocksize, obs->n);
      return TCL_OK;
    } else {
      Tcl_AppendResult(interp, "com_velocity blocked expected integer argument that fits the number of particles\n", (char *)NULL );
      return TCL_ERROR;
    }
  } else /* if nonblocked com is to be taken */ {
    obs->calculate=&observable_calc_com_velocity;
    obs->update=0;
    obs->container=ids;
    obs->n=3;
    obs->last_value=(double*)malloc(obs->n*sizeof(double));
    *change=1+temp;
    return TCL_OK;
  }
}

int tclcommand_observable_com_position(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp, blocksize;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  argc-=temp+1;
  argv+=temp+1;
  for ( int i = 0; i < argc; i++) {
    printf("%s\n", argv[i]);
  }
  if (argc>0 && ARG0_IS_S("blocked")) {
    if (argc >= 2 && ARG1_IS_I(blocksize) && (ids->n % blocksize ==0 )) {
      obs->calculate=&observable_calc_blocked_com_position;
      obs->update=0;
      obs->container=ids;
      obs->n=3*ids->n/blocksize;
      obs->last_value=(double*)malloc(obs->n*sizeof(double));
      *change=3+temp;
      printf("found %d ids and a blocksize of %d, that makes %d dimensions\n", ids->n, blocksize, obs->n);
      return TCL_OK;
    } else {
      Tcl_AppendResult(interp, "com_velocity blocked expected integer argument that fits the number of particles\n", (char *)NULL );
      return TCL_ERROR;
    }
  } else /* if nonblocked com is to be taken */ {
    obs->calculate=&observable_calc_com_position;
    obs->update=0;
    obs->container=ids;
    obs->n=3;
    obs->last_value=(double*)malloc(obs->n*sizeof(double));
    *change=1+temp;
    return TCL_OK;
  }
}


int tclcommand_observable_com_force(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp, blocksize;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  argc-=temp+1;
  argv+=temp+1;
  for ( int i = 0; i < argc; i++) {
    printf("%s\n", argv[i]);
  }
  if (argc>0 && ARG0_IS_S("blocked")) {
    if (argc >= 2 && ARG1_IS_I(blocksize) && (ids->n % blocksize ==0 )) {
      obs->calculate=&observable_calc_blocked_com_force;
      obs->update=0;
      obs->container=ids;
      obs->n=3*ids->n/blocksize;
      obs->last_value=(double*)malloc(obs->n*sizeof(double));
      *change=3+temp;
      printf("found %d ids and a blocksize of %d, that makes %d dimensions\n", ids->n, blocksize, obs->n);
      return TCL_OK;
    } else {
      Tcl_AppendResult(interp, "com_velocity blocked expected integer argument that fits the number of particles\n", (char *)NULL );
      return TCL_ERROR;
    }
  } else /* if nonblocked com is to be taken */ {
    obs->calculate=&observable_calc_com_force;
    obs->update=0;
    obs->container=ids;
    obs->n=3;
    obs->last_value=(double*)malloc(obs->n*sizeof(double));
    *change=1+temp;
    return TCL_OK;
  }
}

int tclcommand_observable_particle_positions(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
     return TCL_ERROR;
  obs->calculate=&observable_calc_particle_positions;
  obs->update=0;
  obs->container=(void*)ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_particle_forces(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList* ids;
  int temp;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
     return TCL_ERROR;
  obs->calculate=&observable_calc_particle_forces;
  obs->update=0;
  obs->container=(void*)ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}



int tclcommand_observable_stress_tensor(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  obs->calculate=&observable_stress_tensor;
  obs->update=0;
  obs->container=(void*)NULL;
  obs->n=9;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1;
  return TCL_OK;
}


int tclcommand_observable_stress_tensor_acf_obs(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  obs->calculate=&observable_calc_stress_tensor_acf_obs;
  obs->update=0;
  obs->container=(void*)NULL;
  obs->n=6;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1;
  return TCL_OK;
}


int tclcommand_observable_density_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
  int temp;
  profile_data* pdata;
  obs->calculate=&observable_calc_density_profile;
  obs->update=0;
  if (tclcommand_parse_profile(interp, argc-1, argv+1, &temp, &obs->n, &pdata) != TCL_OK ) 
    return TCL_ERROR;
  if (pdata->id_list==0) {
    Tcl_AppendResult(interp, "Error in radial_profile: particle ids/types not specified\n" , (char *)NULL);
    return TCL_ERROR;
  }
  obs->container=(void*)pdata;
  obs->n=pdata->xbins*pdata->ybins*pdata->zbins;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_rdf(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
  rdf_profile_data* pdata = (rdf_profile_data*) malloc(sizeof(rdf_profile_data));
  obs->calculate=&observable_calc_rdf;
  obs->update=0;
  
  pdata->r_bins=100;
  pdata->r_min=0;
  pdata->r_max=-1;
  IntList p1, p2;
  
  init_intlist(&p1);
  init_intlist(&p2);
  
  argc--;
  argv++;
  *change=1;
  
  if (argc < 2 || (!ARG0_IS_INTLIST(p1)) || (!ARG1_IS_INTLIST(p2))) {
    Tcl_ResetResult(interp);
    Tcl_AppendResult(interp, "usage: observable rdf <type_list> <type_list> [<r_min> [<r_max> [<n_bins>]]]", (char *) NULL);
    return (TCL_ERROR);
  }
  argc -= 2;
  argv += 2;
  *change+=2;
  
  if (argc > 0) {
    if (!ARG0_IS_D(pdata->r_min)) return (TCL_ERROR);
    argc--;
    argv++;
    (*change)++;
  }
  if (argc > 0) {
    if (!ARG0_IS_D(pdata->r_max)) return (TCL_ERROR);
    argc--;
    argv++;
    (*change)++;
  }
  if (argc > 0) {
    if (!ARG0_IS_I(pdata->r_bins)) return (TCL_ERROR);
    argc--;
    argv++;
    (*change)++;
  }
  
  /* if not given use default */
  if (pdata->r_max < 0) pdata->r_max = min_box_l / 2.0;

  
  if (!sortPartCfg()) {
    Tcl_AppendResult(interp, "for analyze, store particles consecutively starting with 0.", (char *) NULL);
    return (TCL_ERROR);
  }
  
  //calc_rdf(p1.e, p1.max, p2.e, p2.max, r_min, r_max, r_bins, rdf);
  
  pdata->p1_types = (int *) malloc(p1.n * sizeof(int));
  memcpy(pdata->p1_types, p1.e,p1.n*sizeof(int));
  pdata->n_p1     = p1.n;
  pdata->p2_types = (int *) malloc(p2.n * sizeof(int));
  memcpy(pdata->p2_types, p2.e,p2.n*sizeof(int));
  pdata->n_p2     = p2.n;
  
  obs->container=(void*)pdata;
  obs->n=pdata->r_bins;
  obs->last_value= (double*) malloc(obs->n * sizeof (double));
  return TCL_OK;
}

int tclcommand_observable_lb_velocity_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
#ifndef LB
  return TCL_ERROR;
#else
  int temp;
  profile_data* pdata;
  obs->calculate=&observable_calc_lb_velocity_profile;
  obs->update=0;
  if (tclcommand_parse_profile(interp, argc-1, argv+1, &temp, &obs->n, &pdata) != TCL_OK ) 
    return TCL_ERROR;
  obs->container=(void*)pdata;
  obs->n=3*pdata->xbins*pdata->ybins*pdata->zbins;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
#endif
}


int tclcommand_observable_radial_density_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
  int temp;
  radial_profile_data* rpdata;
  obs->calculate=&observable_calc_radial_density_profile;
  obs->update=0;
  if (tclcommand_parse_radial_profile(interp, argc-1, argv+1, &temp, &obs->n, &rpdata) != TCL_OK ) 
     return TCL_ERROR;
  if (rpdata->id_list==0) {
    Tcl_AppendResult(interp, "Error in radial_profile: particle ids/types not specified\n" , (char *)NULL);
    return TCL_ERROR;
  }
  obs->container=(void*)rpdata;
  obs->n=rpdata->rbins*rpdata->phibins*rpdata->zbins;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_radial_flux_density_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
  int temp;
  radial_profile_data* rpdata;
  obs->calculate=&observable_calc_radial_flux_density_profile;
  obs->update=0;
  if (tclcommand_parse_radial_profile(interp, argc-1, argv+1, &temp, &obs->n, &rpdata) != TCL_OK ) 
     return TCL_ERROR;
  if (rpdata->id_list==0) {
    Tcl_AppendResult(interp, "Error in radial_profile: particle ids/types not specified\n" , (char *)NULL);
    return TCL_ERROR;
  }
  obs->container=(void*)rpdata;
  obs->n=3*rpdata->rbins*rpdata->phibins*rpdata->zbins;
  rpdata->container=(double*)malloc(3*rpdata->id_list->n*sizeof(double));
  double* temptemp=(double*) rpdata->container;
  *temptemp=CONST_UNITITIALIZED;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_flux_density_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
  int temp;
  profile_data* pdata;
  obs->calculate=&observable_calc_flux_density_profile;
  obs->update=0;
  if (tclcommand_parse_profile(interp, argc-1, argv+1, &temp, &obs->n, &pdata) != TCL_OK ) 
     return TCL_ERROR;
  if (pdata->id_list==0) {
    Tcl_AppendResult(interp, "Error in radial_profile: particle ids/types not specified\n" , (char *)NULL);
    return TCL_ERROR;
  }
  obs->container=(void*)pdata;
  obs->n=3*pdata->xbins*pdata->ybins*pdata->zbins;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
}

int tclcommand_observable_lb_radial_velocity_profile(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
#ifndef LB
  return TCL_ERROR;
#else
  int temp;
  radial_profile_data* rpdata;
  obs->calculate=&observable_calc_lb_radial_velocity_profile;
  obs->update=0;
  if (tclcommand_parse_radial_profile(interp, argc-1, argv+1, &temp, &obs->n, &rpdata) != TCL_OK ) 
     return TCL_ERROR;
  obs->container=(void*)rpdata;
  obs->n=3*rpdata->rbins*rpdata->phibins*rpdata->zbins;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
#endif
}

int tclcommand_observable_particle_currents(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs){
#ifdef ELECTROSTATICS
  int temp;
  IntList* ids;
  obs->calculate=&observable_calc_particle_currents;
  obs->update=0;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->container=(void*)ids;
  obs->n=3*ids->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
#else
  Tcl_AppendResult(interp, "Feature ELECTROSTATICS needed for observable particle_currents", (char *)NULL);
  return TCL_ERROR;
#endif
  
}

int tclcommand_observable_currents(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
#ifdef ELECTROSTATICS
  int temp;
  IntList* ids;
  obs->calculate=&observable_calc_currents;
  obs->update=0;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->container=(void*)ids;
  obs->n=3;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
#else
  Tcl_AppendResult(interp, "Feature ELECTROSTATICS needed for observable currents", (char *)NULL);
  return TCL_ERROR;
#endif
} 

int tclcommand_observable_dipole_moment(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
#ifdef ELECTROSTATICS
  int temp;
  IntList* ids;
  obs->calculate=&observable_calc_dipole_moment;
  obs->update=0;
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids) != TCL_OK ) 
    return TCL_ERROR;
  obs->container=(void*)ids;
  obs->n=3;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  *change=1+temp;
  return TCL_OK;
#else
  Tcl_AppendResult(interp, "Feature ELECTROSTATICS needed for observable currents", (char *)NULL);
  return TCL_ERROR;
#endif
}

int tclcommand_observable_structure_factor(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  //Tcl_AppendResult(interp, "Structure Factor not available yet!!", (char *)NULL);
  //return TCL_ERROR;
  int order;
  if (argc > 1 && ARG1_IS_I(order)) {
    obs->calculate=&observable_calc_structure_factor;
    //order_p=(int * )malloc(sizeof(int));
    observable_sf_params* params =(observable_sf_params*) malloc(sizeof(observable_sf_params));
    params->order=order;
   obs->container=(void*) params;
   int order2,i,j,k,l,n ; 
   order2=order*order;
   l=0;
   // lets counter the number of entries for the DSF
   for(i=-order; i<=order; i++) {
     for(j=-order; j<=order; j++) {
       for(k=-order; k<=order; k++) {
         n = i*i + j*j + k*k;
         if (( n<=order2 ) && (n >= 1)) {
           l=l+2;
	 }
       }
     }
   }
   obs->n=l;
   obs->last_value=(double*)malloc(obs->n*sizeof(double));
   *change=2;
   return TCL_OK;
 } else { 
   sf_print_usage(interp);
   return TCL_ERROR; 
 }
}

int tclcommand_observable_print_structure_factor_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values, int groupsize, int shifted) {
  observable_sf_params* params= (observable_sf_params*) obs->container;
  int order=params->order;
  int order2=order*order;
  char buffer[5 * TCL_DOUBLE_SPACE + 7];
  double qfak = 2.0 * PI / box_l[0];
  double* data = obs->last_value;
  int l=0;
  for(int i=-order; i<=order; i++) {
    for(int j=-order; j<=order; j++) {
      for(int k=-order; k<=order; k++) {
	int n = i*i + j*j + k*k;
	if (( n<=order2 ) && ( n>0 )) {
	  sprintf(buffer, "{%f %f %f %f %f} ", qfak * i, qfak*j, qfak*k, data[l],data[l+1]);
	  //sprintf(buffer, "%f %f %f %f %f\n", qfak * i, qfak*j, qfak*k, data[l],data[l+1]);
	  Tcl_AppendResult(interp, buffer, (char *) NULL);
	  l=l+2;
	}
      }
    }
  }
}

int tclcommand_observable_structure_factor_fast(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  //Tcl_AppendResult(interp, "Structure Factor not available yet!!", (char *)NULL);
  //return TCL_ERROR;
  int order;
  if (argc > 1 && ARG1_IS_I(order)) {
    obs->calculate=&observable_calc_structure_factor_fast;
    //order_p=(int * )malloc(sizeof(int));
    observable_sf_params* params =(observable_sf_params*) malloc(sizeof(observable_sf_params));
    params->order=order;
    obs->container=(void*) params;
    params->num_k_vecs=order;
    *change=2;
    argc-=2;
    argv+=2;
    if (argc > 0) {
      if (!ARG0_IS_I(params->num_k_vecs)) return (TCL_ERROR);
      argc--;
      argv++;
      (*change)++;
    }
    int k_density = params->num_k_vecs/params->order;
    int vecs_per_k=-1;
    switch (k_density){
    case 1:
      vecs_per_k=3;
      break;
    case 2:
      vecs_per_k=3+6;
      break;
    case 3:
      vecs_per_k=3+6+4;
      break;
    default:
      Tcl_AppendResult(interp, "so many samples per order not yet implemented", (char*)NULL);
      return TCL_ERROR; 
    }
    obs->n=params->num_k_vecs*vecs_per_k*2;
    obs->last_value=(double*)malloc(obs->n*sizeof(double));
    return TCL_OK;
  } else { 
    sf_print_usage(interp);
    return TCL_ERROR; 
  }
}

int tclcommand_observable_print_structure_factor_fast_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values, int groupsize, int shifted) {
  observable_sf_params* params= (observable_sf_params*) obs->container;
  int k_max = params->num_k_vecs;
  int k_density = k_max/params->order;
  char buffer[3 * TCL_DOUBLE_SPACE + 5];
  double qfak = 2.0 * PI / box_l[0];
  //double* data = obs->last_value;
  double* data = values;
  int l=0;
  for (int i = 0; i < k_max; i++) {
    int order=i/k_density + 1;
    double tfac;
    int average;
    switch (i%k_density){
    case 0: tfac=1;       average=3; break;
    case 1: tfac=sqrt(2); average=6; break;
    case 2: tfac=sqrt(3); average=4; break;
    default:
      Tcl_AppendResult(interp, "so many samples per order not yet implemented", (char*)NULL);
      return TCL_ERROR; 
    }
    //sprintf(buffer, "{%f %f} ", qfak * (order) *tfac, data[i]);
    for (int t=0;t<average;t++){
      sprintf(buffer, "{%f %f %f} ", qfak * (order) *tfac, data[l], data[l+1]);
      //sprintf(buffer, "%f %f %f\n", qfak * (order) *tfac, data[l], data[l+1]);
      l+=2;
      Tcl_AppendResult(interp, buffer, (char *) NULL);
    }
  }
}

// FIXME this is the old implementation of structure factor (before observables and correlations were strictly separated)
//int parse_structure_factor (Tcl_Interp* interp, int argc, char** argv, int* change, void** A_args, int *tau_lin_p, double *tau_max_p, double* delta_t_p) {
//  observable_sf_params* params;
//  int order,order2,tau_lin;
//  int i,j,k,l,n;
//  double delta_t,tau_max;
//  char ibuffer[TCL_INTEGER_SPACE + 2];
//  char dbuffer[TCL_DOUBLE_SPACE];
////  int *vals;
//  double *q_density;
//  params=(observable_sf_params*)malloc(sizeof(observable_sf_params));
//  
//  if(argc!=5) { 
//    sprintf(ibuffer, "%d ", argc);
//    Tcl_AppendResult(interp, "structure_factor  needs 5 arguments, got ", ibuffer, (char*)NULL);
//    sf_print_usage(interp);
//    return TCL_ERROR;
//  }
//  if (ARG_IS_I(1,order)) {
//    sprintf(ibuffer, "%d ", order);
//    if(order>1) {
//      params->order=order;
//      order2=order*order;
//    } else {
//      Tcl_AppendResult(interp, "order must be > 1, got ", ibuffer, (char*)NULL);
//      sf_print_usage(interp);
//      return TCL_ERROR;
//    }
//  } else {
//    Tcl_AppendResult(interp, "problem reading order",(char*)NULL);
//    return TCL_ERROR; 
//  }
//  if (ARG_IS_D(2,delta_t)) {
//    if (delta_t > 0.0) *delta_t_p=delta_t;
//    else {
//      Tcl_PrintDouble(interp,delta_t,dbuffer);
//      Tcl_AppendResult(interp, "delta_t must be > 0.0, got ", dbuffer,(char*)NULL);
//      return TCL_ERROR;
//    }
//  } else {
//    Tcl_AppendResult(interp, "problem reading delta_t, got ",argv[2],(char*)NULL);
//    return TCL_ERROR; 
//  }
//  if (ARG_IS_D(3,tau_max)) {
//    if (tau_max > 2.0*delta_t) *tau_max_p=tau_max;
//    else {
//      Tcl_PrintDouble(interp,tau_max,dbuffer);
//      Tcl_AppendResult(interp, "tau_max must be > 2.0*delta_t, got ", dbuffer,(char*)NULL);
//      return TCL_ERROR;
//    }
//  } else {
//    Tcl_AppendResult(interp, "problem reading tau_max, got",argv[3],(char*)NULL);
//    return TCL_ERROR; 
//  }
//  if (ARG_IS_I(4,tau_lin)) {
//    if (tau_lin > 2 && tau_lin < (tau_max/delta_t+1)) *tau_lin_p=tau_lin;
//    else {
//      sprintf(ibuffer, "%d", tau_lin);
//      Tcl_AppendResult(interp, "tau_lin must be < tau_max/delta_t+1, got ", ibuffer,(char*)NULL);
//      return TCL_ERROR;
//    }
//  } else {
//    Tcl_AppendResult(interp, "problem reading tau_lin, got",argv[4],(char*)NULL);
//    sf_print_usage(interp);
//    return TCL_ERROR; 
//  }
//  // compute the number of vectors
//  l=0;
//  for(i=-order; i<=order; i++) 
//      for(j=-order; j<=order; j++) 
//        for(k=-order; k<=order; k++) {
//          n = i*i + j*j + k*k;
//          if ((n<=order2) && (n>0)) {
//            l++;
//	  }
//        }
//  params->dim_sf=l;
//  params->q_vals=(int*)malloc(3*l*sizeof(double));
//  q_density=(double*)malloc(order2*sizeof(double));
//  for(i=0;i<order2;i++) q_density[i]=0.0;
//  l=0;
//  // Store their values and density
//  for(i=-order; i<=order; i++) 
//      for(j=-order; j<=order; j++) 
//        for(k=-order; k<=order; k++) {
//          n = i*i + j*j + k*k;
//          if ((n<=order2) && (n>0)) {
//	    params->q_vals[3*l  ]=i;
//	    params->q_vals[3*l+1]=j;
//	    params->q_vals[3*l+2]=k;
//	    q_density[n-1]+=1.0;
//            l++;
//	  }
//        }
//  for(i=0;i<order2;i++) q_density[i]/=(double)l;
//  params->q_density=q_density;
//  *A_args=(void*)params;
//  *change=5; // if we reach this point, we have parsed 5 arguments, if not, error is returned anyway
//  return 0;
//}
//

int tclcommand_observable_interacts_with(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  IntList *ids1, *ids2;
  int temp;
  double cutoff;
  obs->calculate=&observable_calc_interacts_with;
  obs->update=0;
  ids1=(IntList*)malloc(sizeof(IntList));
  ids2=(IntList*)malloc(sizeof(IntList));
  iw_params* iw_params_p=(iw_params*) malloc(sizeof(iw_params));
  if (parse_id_list(interp, argc-1, argv+1, &temp, &ids1) != TCL_OK ) {
    free(ids1);
    free(ids2);
    free(iw_params_p);
    return TCL_ERROR;
  }
  iw_params_p=(iw_params*)malloc(sizeof(iw_params));
  iw_params_p->ids1=ids1;
  *change=1+temp;
  if (parse_id_list(interp, argc-3, argv+3, &temp, &ids2) != TCL_OK ) {
    free(ids1);
    free(ids2);
    free(iw_params_p);
    return TCL_ERROR;
  }
  *change+=temp;
  iw_params_p->ids2=ids2;
  if ( argc < 5 || !ARG_IS_D(5,cutoff)) {
    Tcl_AppendResult(interp, "Usage: analyze correlation ... interacts_with id_list1 id_list2 cutoff", (char *)NULL);
    free(ids1);
    free(ids2);
    free(iw_params_p);
  return TCL_ERROR;
  } 
  *change+=1;
  iw_params_p->cutoff=cutoff;
  obs->container=(void*)iw_params_p;
  obs->n=ids1->n; // number of ids from the 1st argument
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  return TCL_OK;
}

int tclcommand_observable_average(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  int reference_observable;
  if (argc < 2) {
    Tcl_AppendResult(interp, "observable new average <reference_id>", (char *)NULL);
    return TCL_ERROR;
  }
  if (!ARG_IS_I(1,reference_observable)) {
    Tcl_AppendResult(interp, "observable new average <reference_id>", (char *)NULL);
    return TCL_ERROR;
  }
  if (reference_observable >= n_observables) {
    Tcl_AppendResult(interp, "The reference observable does not exist.", (char *)NULL);
    return TCL_ERROR;
  }
  observable_average_container* container=(observable_average_container*)malloc(sizeof(observable_average_container));
  container->reference_observable = observables[reference_observable];
  container->n_sweeps = 0;
  obs->n = container->reference_observable->n;
  obs->last_value=(double*)malloc(obs->n*sizeof(double));
  for (int i=0; i<obs->n; i++) 
    obs->last_value[i] = 0;

  obs->container=container;
  obs->update=&observable_update_average;
  obs->calculate=0;

  return TCL_OK;
}



//  if (ARG0_IS_S("textfile")) {
//    // We still can only handle full files
//    if ( argc>1 ) {
//      fds= malloc(sizeof(file_data_source));
//      error = file_data_source_init(fds, argv[1], 0);
//      *change=2;
//      if (!error) {
//        *A_args=(void*) fds;
//        *dim_A = fds->n_columns;
//        *A_fun = (void*)&file_data_source_readline;
//        return TCL_OK;
//      } else {
//        Tcl_AppendResult(interp, "Error reading file ", argv[1] ,"\n", (char *)NULL);
//        Tcl_AppendResult(interp, file_data_source_init_errors[error] ,"\n", (char *)NULL);
//        return TCL_ERROR;
//      }
//    } else {
//      Tcl_AppendResult(interp, "Error in parse_observable textfile: no filename given" , (char *)NULL);
//      return TCL_ERROR;
//    }
//    return TCL_OK;
//  } 
//  if (ARG0_IS_S("tclinput")) {
//    if (argc>1 && ARG1_IS_I(temp)) {
//      *dim_A = temp;
//      *A_fun = &tcl_input;
//      *A_args=malloc(sizeof(tcl_input_data));
//      *change =2;
//      return TCL_OK;
//    } else {
//      Tcl_AppendResult(interp, "\nError in parse_observable tclinfo. You must pass the dimension of the observable." , (char *)NULL);
//      return TCL_ERROR;
//    }
//  }else {
//    return observable_usage(interp);
//  }
//  return 0 ;


#define REGISTER_OBSERVABLE(name,parser,id) \
  if (ARG_IS_S(2,#name)) { \
    observables[id]=(s_observable*)malloc(sizeof(observable)); \
    observable_init(observables[id]); \
    observables[id]->obs_name = (char*)malloc((1+strlen(#name))*sizeof(char)); \
    strcpy(observables[id]->obs_name,#name); \
    if (parser(interp, argc-2, argv+2, &temp, observables[n_observables]) ==TCL_OK) { \
      n_observables++; \
      argc-=1+temp; \
      argv+=1+temp; \
      sprintf(buffer,"%d",id); \
      Tcl_AppendResult(interp,buffer,(char *)NULL);\
      return TCL_OK; \
    } else { \
      free(observables[n_observables]->obs_name);\
      free(observables[n_observables]);\
      Tcl_AppendResult(interp, "\nError parsing observable ", #name, "\n", (char *)NULL); \
      return TCL_ERROR; \
    } \
  } 



int tclcommand_observable(ClientData data, Tcl_Interp *interp, int argc, char **argv){
//  file_data_source* fds;
  char buffer[TCL_INTEGER_SPACE];
  int n;
  int id;
  int temp;
  //int no;

  if (argc<2) {
    Tcl_AppendResult(interp, "Usage!!!\n", (char *)NULL);
    return TCL_ERROR;
  }

  if (argc > 1 && ARG_IS_S(1, "n_observables")) {
	  sprintf(buffer, "%d", n_observables);
    Tcl_AppendResult(interp, buffer, (char *)NULL );
    return TCL_OK;
  }

  
//  if (argc > 1 && ARG1_IS_I(no)) {
// }
  if (argc > 2 && ARG1_IS_S("new") ) {

    // find the next free observable id
    for (id=0;id<n_observables;id++) 
      if ( observables+id == 0 ) break; 
    if (id==n_observables) 
      observables=(observable**) realloc(observables, (n_observables+1)*sizeof(observable*)); 


    REGISTER_OBSERVABLE(average, tclcommand_observable_average,id);
    REGISTER_OBSERVABLE(particle_velocities, tclcommand_observable_particle_velocities,id);
    REGISTER_OBSERVABLE(particle_body_velocities, tclcommand_observable_particle_body_velocities,id);
    REGISTER_OBSERVABLE(particle_angular_momentum, tclcommand_observable_particle_angular_momentum,id);
    REGISTER_OBSERVABLE(particle_body_angular_momentum, tclcommand_observable_particle_body_angular_momentum,id);
    REGISTER_OBSERVABLE(particle_forces, tclcommand_observable_particle_forces,id);
    REGISTER_OBSERVABLE(com_velocity, tclcommand_observable_com_velocity,id);
    REGISTER_OBSERVABLE(com_position, tclcommand_observable_com_position,id);
    REGISTER_OBSERVABLE(com_force, tclcommand_observable_com_force,id);
    REGISTER_OBSERVABLE(particle_positions, tclcommand_observable_particle_positions,id);
    REGISTER_OBSERVABLE(stress_tensor, tclcommand_observable_stress_tensor,id);
    REGISTER_OBSERVABLE(stress_tensor_acf_obs, tclcommand_observable_stress_tensor_acf_obs,id);
    REGISTER_OBSERVABLE(particle_currents, tclcommand_observable_particle_currents,id);
    REGISTER_OBSERVABLE(currents, tclcommand_observable_currents,id);
    REGISTER_OBSERVABLE(dipole_moment, tclcommand_observable_dipole_moment,id);
    REGISTER_OBSERVABLE(structure_factor, tclcommand_observable_structure_factor,id);
    REGISTER_OBSERVABLE(structure_factor_fast, tclcommand_observable_structure_factor_fast,id);
    REGISTER_OBSERVABLE(interacts_with, tclcommand_observable_interacts_with,id);
  //  REGISTER_OBSERVABLE(obs_nothing, tclcommand_observable_obs_nothing,id);
  //  REGISTER_OBSERVABLE(flux_profile, tclcommand_observable_flux_profile,id);
    REGISTER_OBSERVABLE(density_profile, tclcommand_observable_density_profile,id);
    REGISTER_OBSERVABLE(lb_velocity_profile, tclcommand_observable_lb_velocity_profile,id);
    REGISTER_OBSERVABLE(radial_density_profile, tclcommand_observable_radial_density_profile,id);
    REGISTER_OBSERVABLE(radial_flux_density_profile, tclcommand_observable_radial_flux_density_profile,id);
    REGISTER_OBSERVABLE(flux_density_profile, tclcommand_observable_flux_density_profile,id);
    REGISTER_OBSERVABLE(lb_radial_velocity_profile, tclcommand_observable_lb_radial_velocity_profile,id);
    REGISTER_OBSERVABLE(rdf, tclcommand_observable_rdf,id);
    REGISTER_OBSERVABLE(tclcommand, tclcommand_observable_tclcommand,id);
    Tcl_AppendResult(interp, "Unknown observable ", argv[2] ,"\n", (char *)NULL);
    return TCL_ERROR;
  }
  
  if (ARG1_IS_I(n)) {
    if (n>=n_observables || observables+n == NULL ) {
      sprintf(buffer,"%d \n",n);
      Tcl_AppendResult(interp, "Observable with id ", buffer, (char *)NULL);
      Tcl_AppendResult(interp, "is not defined\n", (char *)NULL);
      return TCL_ERROR;
    }
    if (argc > 2 && ARG_IS_S(2,"print")) {
      return tclcommand_observable_print(interp, argc-3, argv+3, &temp, observables[n]);
    }
    if (argc > 2 && ARG_IS_S(2,"update")) {
      return tclcommand_observable_update(interp, argc-3, argv+3, &temp, observables[n]);
    }
    if (argc > 2 && ARG_IS_S(2,"reset") && observables[n]->update == observable_update_average) {
      observable_reset_average(observables[n]);
      return TCL_OK;
    }
    if (argc > 2 && ARG_IS_S(2,"autoupdate") ) {
      if (argc > 3 && ARG_IS_D(3, observables[n]->autoupdate_dt) ) {
        if (observables[n]->autoupdate_dt < 1e-5) {
          observables[n]->autoupdate=0;
        } else {
          observables[n]->autoupdate=1;
          observables_autoupdate = 1;
        }
        return TCL_OK;
      } else {
        Tcl_AppendResult(interp, "Usage observable <id> autoupdate <dt>\n", (char *)NULL);
        return TCL_ERROR;
      }

    }
  }
  Tcl_AppendResult(interp, "Unknown observable ", argv[1] ,"\n", (char *)NULL);
  return TCL_ERROR;
}



static int convert_types_to_ids(IntList * type_list, IntList * id_list){ 
      int i,j,n_ids=0,flag;
      sortPartCfg();
      for ( i = 0; i<n_part; i++ ) {
         if(type_list==NULL) { 
		/* in this case we select all particles */
               flag=1 ;
         } else {  
                flag=0;
                for ( j = 0; j<type_list->n ; j++ ) {
                    if(partCfg[i].p.type == type_list->e[j])  flag=1;
	        }
         }
	 if(flag==1){
              realloc_intlist(id_list, id_list->n=n_ids+1);
	      id_list->e[n_ids] = i;
	      n_ids++;
	 }
      }
      return n_ids;
}


//int file_data_source_init(file_data_source* self, char* filename, IntList* columns) {
//  int counter=1;
//  char* token;
//  if (filename==0)
//    return 1;
//  self->f = fopen(filename, "r");
//  if (! self->f )
//    return 2;
//  fgets(self->last_line, MAXLINELENGTH, self->f);
//  while (self->last_line && self->last_line[0] == 35) {
//    fgets(self->last_line, MAXLINELENGTH, self->f);
//  }
//  if (!self->last_line)
//    return 3;
//// Now lets count the tokens in the first line
//  token=strtok(self->last_line, " \t\n");
//  while (token) {
////    printf("reading token **%s**\n", token);
//    token=strtok(NULL, " \t\n");
//    counter++;
//  }
//  self->n_columns = counter;
//  rewind(self->f);
//  self->data_left=1;
////  printf("I found out that your file has %d columns\n", self->n_columns);
//  if (columns !=0)
//    /// Here we would like to check if we can handle the desired columns, but this has to be implemented!
//    return -1;
//  return 0;
//}

int file_data_source_readline(void* xargs, double* A, int dim_A) {
//  file_data_source* self = xargs;
//  int counter=0;
//  char* token;
//  char* temp;
//
//  temp=fgets(self->last_line, MAXLINELENGTH, self->f);
//  while (temp!= NULL && self->last_line && self->last_line[0] == 35) {
//    temp=fgets(self->last_line, MAXLINELENGTH, self->f);
//  }
//  if (!self->last_line || temp==NULL) {
////    printf("nothing left\n");
//    self->data_left=0;
//    return 3;
//  }
//  token=strtok(self->last_line, " \t\n");
//  while (token) {
////    printf("reading token: ");
//    A[counter]=atof(token);
////    printf("%f ", A[counter]);
//    token=strtok(NULL, " \t\n");
//    counter++;
//    if (counter >= dim_A) {
////      printf("urgs\n");
//      return 4;
//    }
//  }
////  printf("\n");
//  return 0;
//}
//
//int tcl_input(void* data, double* A, unsigned int n_A) {
//  tcl_input_data* input_data = (tcl_input_data*) data;
//  int i, tmp_argc;
//  const char  **tmp_argv;
//  Tcl_SplitList(input_data->interp, input_data->argv[0], &tmp_argc, &tmp_argv);
//  // function prototype from man page:
//  // int Tcl_SplitList(interp, list, argcPtr, argvPtr)
//  if (tmp_argc < n_A) {
//    Tcl_AppendResult(input_data->interp, "Not enough arguments passed to analyze correlation update", (char *)NULL);
//    return 1;
//  }
//  for (i = 0; i < n_A; i++) {
//    if (Tcl_GetDouble(input_data->interp, tmp_argv[i], &A[i]) != TCL_OK) {
//      Tcl_AppendResult(input_data->interp, "error parsing argument ", input_data->argv[i],"\n", (char *)NULL);
//      return 1;
//    }
//  }
  return 0;
}


int tclcommand_parse_profile(Tcl_Interp* interp, int argc, char** argv, int* change, int* dim_A, profile_data** pdata_) {
  int temp;
  *change=0;
  profile_data* pdata=(profile_data*)malloc(sizeof(profile_data));
  *pdata_ = pdata;
  pdata->id_list=0;
  pdata->minx=0;
  pdata->maxx=box_l[0];
  pdata->xbins=1;
  pdata->miny=0;
  pdata->maxy=box_l[1];
  pdata->ybins=1;
  pdata->minz=0;
  pdata->maxz=box_l[2];
  pdata->zbins=1;
  while (argc>0) {
    if (ARG0_IS_S("ids") || ARG0_IS_S("types") || ARG0_IS_S("all")) {
      if (parse_id_list(interp, argc, argv, &temp, &pdata->id_list ) != TCL_OK) {
        Tcl_AppendResult(interp, "Error reading profile: Error parsing particle id information\n" , (char *)NULL);
        return TCL_ERROR;
      } else {
        *change+=temp;
        argc-=temp;
        argv+=temp;
      }
    } else if ( ARG0_IS_S("minx")){
      if (argc>1 && ARG1_IS_D(pdata->minx)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read minz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if ( ARG0_IS_S("maxx") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxx)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read maxz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if (ARG0_IS_S("xbins")) {
      if (argc>1 && ARG1_IS_I(pdata->xbins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read nbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("miny")){
      if (argc>1 && ARG1_IS_D(pdata->miny)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read minz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if ( ARG0_IS_S("maxy") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxy)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read maxz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if (ARG0_IS_S("ybins")) {
      if (argc>1 && ARG1_IS_I(pdata->ybins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read nbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("minz")){
      if (argc>1 && ARG1_IS_D(pdata->minz)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read minz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if ( ARG0_IS_S("maxz") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxz)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read maxz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else  if (ARG0_IS_S("zbins")) {
      if (argc>1 && ARG1_IS_I(pdata->zbins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read nbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else {
      Tcl_AppendResult(interp, "Error in radial_profile: understand argument ", argv[0], "\n" , (char *)NULL);
      return TCL_ERROR;
    }
  }
  
  temp=0;
  if (pdata->xbins <= 0 || pdata->ybins <=0 || pdata->zbins <= 0) {
    Tcl_AppendResult(interp, "Error in profile: the bin number in each direction must be >=1\n" , (char *)NULL);
    temp=1;
  }
  if (temp)
    return TCL_ERROR;
  else
    return TCL_OK;
}


int tclcommand_parse_radial_profile(Tcl_Interp* interp, int argc, char** argv, int* change, int* dim_A, radial_profile_data** pdata_) {
  int temp;
  *change=0;
  radial_profile_data* pdata=(radial_profile_data*)malloc(sizeof(radial_profile_data));
  *pdata_ = pdata;
  pdata->id_list=0;
  if (box_l[0]<box_l[1]) 
    pdata->maxr = box_l[0];
  else 
    pdata->maxr = box_l[1];
  pdata->minr=0;
  pdata->maxphi=PI;
  pdata->minphi=-PI;
  pdata->minz=-box_l[2]/2.;
  pdata->maxz=+box_l[2]/2.;
  pdata->center[0]=box_l[0]/2.;pdata->center[1]=box_l[1]/2.;pdata->center[2]=box_l[2]/2.;
  pdata->rbins=1;
  pdata->zbins=1;
  pdata->phibins=1;
  pdata->axis[0]=0.;
  pdata->axis[1]=0.;
  pdata->axis[2]=1.;
  if (argc < 1) {
    Tcl_AppendResult(interp, "Usage radial_profile id $ids center $x $y $z maxr $r_max nbins $n\n" , (char *)NULL);
    return TCL_ERROR;
  }
  while (argc>0) {
    if (ARG0_IS_S("ids") || ARG0_IS_S("types") || ARG0_IS_S("all")) {
      if (parse_id_list(interp, argc, argv, &temp, &pdata->id_list ) != TCL_OK) {
        Tcl_AppendResult(interp, "Error reading profile: Error parsing particle id information\n" , (char *)NULL);
        return TCL_ERROR;
      } else {
        *change+=temp;
        argc-=temp;
        argv+=temp;
      }
    } else if ( ARG0_IS_S("center")){
      if (argc>3 && ARG1_IS_D(pdata->center[0]) && ARG_IS_D(2,pdata->center[1]) && ARG_IS_D(3,pdata->center[2])) {
        argc-=4;
        argv+=4;
        *change+=4;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read center\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("axis")){
        Tcl_AppendResult(interp, "Using arbitrary axes does not work yet!\n" , (char *)NULL);
        return TCL_ERROR;
      if (argc>3 && ARG1_IS_D(pdata->axis[0]) && ARG_IS_D(2,pdata->axis[1]) && ARG_IS_D(3,pdata->axis[2])) {
        argc-=4;
        argv+=4;
        *change+=4;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read center\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if  ( ARG0_IS_S("maxr") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxr)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read maxr\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if  ( ARG0_IS_S("minr") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxr)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read maxr\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("minz")){
      if (argc>1 && ARG1_IS_D(pdata->minz)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read minz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("maxz") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxz)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read maxz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("minphi")){
      if (argc>1 && ARG1_IS_D(pdata->minphi)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read minz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if ( ARG0_IS_S("maxphi") ) {
      if (argc>1 && ARG1_IS_D(pdata->maxphi)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in profile: could not read maxz\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if (ARG0_IS_S("rbins")) {
      if (argc>1 && ARG1_IS_I(pdata->rbins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read rbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if (ARG0_IS_S("zbins")) {
      if (argc>1 && ARG1_IS_I(pdata->zbins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read rbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else if (ARG0_IS_S("phibins")) {
      if (argc>1 && ARG1_IS_I(pdata->phibins)) {
        argc-=2;
        argv+=2;
        *change+=2;
      } else {
        Tcl_AppendResult(interp, "Error in radial_profile: could not read rbins\n" , (char *)NULL);
        return TCL_ERROR;
      } 
    } else {
      Tcl_AppendResult(interp, "Error in radial_profile: understand argument ", argv[0], "\n" , (char *)NULL);
      return TCL_ERROR;
    }
  }
  
  temp=0;
//  if (pdata->center[0]>1e90) {
//    Tcl_AppendResult(interp, "Error in radial_profile: center not specified\n" , (char *)NULL);
//    temp=1;
//  }
//  if (pdata->maxr>1e90) {
//    Tcl_AppendResult(interp, "Error in radial_profile: maxr not specified\n" , (char *)NULL);
//    temp=1;
//  }
//  if (pdata->rbins<1) {
//    Tcl_AppendResult(interp, "Error in radial_profile: rbins not specified\n" , (char *)NULL);
//    temp=1;
//  }
  if (temp)
    return TCL_ERROR;
  else
    return TCL_OK;
}

int tclcommand_observable_print(Tcl_Interp* interp, int argc, char** argv, int* change, observable * obs) {
  char buffer[TCL_DOUBLE_SPACE];
  if ( observable_calculate(obs) ) {
    Tcl_AppendResult(interp, "\nFailed to compute observable ", obs->obs_name, "\n", (char *)NULL );
    return TCL_ERROR;
  }
  if (argc==0) {
    for (int i = 0; i<obs->n; i++) {
      Tcl_PrintDouble(interp, obs->last_value[i], buffer);
      Tcl_AppendResult(interp, buffer, " ", (char *)NULL );
    }
  } else if (argc>0 && ARG0_IS_S("formatted")) {
    tclcommand_observable_print_formatted(interp, argc-1, argv+1, change, obs, obs->last_value);
  } else {
    Tcl_AppendResult(interp, "Unknown argument to observable print: ", argv[0], "\n", (char *)NULL );
    return TCL_ERROR;
  }
  return TCL_OK;
}

int tclcommand_observable_print_formatted(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs, double* values) {

  if (obs->update == (&observable_update_average)) 
      obs = ((observable_average_container*)obs->container)->reference_observable;

  if (0) {
#ifdef LB
  } else if (obs->calculate == (&observable_calc_lb_velocity_profile)) {
    return tclcommand_observable_print_profile_formatted(interp, argc, argv, change, obs, values, 3, 0);
#endif
  } else if (obs->calculate == (&observable_calc_density_profile)) {
    return tclcommand_observable_print_profile_formatted(interp, argc, argv, change, obs, values, 1, 1);
#ifdef LB
  } else if (obs->calculate == (&observable_calc_lb_radial_velocity_profile)) {
    return tclcommand_observable_print_radial_profile_formatted(interp, argc, argv, change, obs, values, 3, 0);
#endif
  } else if (obs->calculate == (&observable_calc_radial_density_profile)) {
    return tclcommand_observable_print_radial_profile_formatted(interp, argc, argv, change, obs, values, 1, 1);
  } else if (obs->calculate == (&observable_calc_radial_flux_density_profile)) {
    return tclcommand_observable_print_radial_profile_formatted(interp, argc, argv, change, obs, values, 3, 1);
  } else if (obs->calculate == (&observable_calc_flux_density_profile)) {
    return tclcommand_observable_print_profile_formatted(interp, argc, argv, change, obs, values, 3, 1);
  } else if (obs->calculate == (&observable_calc_structure_factor_fast)) {
    return tclcommand_observable_print_structure_factor_fast_formatted(interp, argc, argv, change, obs, values, 3, 1);
  } else if (obs->calculate == (&observable_calc_structure_factor)) {
    return tclcommand_observable_print_structure_factor_formatted(interp, argc, argv, change, obs, values, 3, 1);
  } else { 
    Tcl_AppendResult(interp, "Observable can not be printed formatted\n", (char *)NULL );
    return TCL_ERROR;
  }

}

int tclcommand_observable_update(Tcl_Interp* interp, int argc, char** argv, int* change, observable* obs) {
  if ( observable_update(obs) ) {
    Tcl_AppendResult(interp, "\nFailed to update observable\n", (char *)NULL );
    return TCL_ERROR;
  }
  return TCL_OK;
}

int sf_print_usage(Tcl_Interp* interp) {
  Tcl_AppendResult(interp, "\nusage: structure_factor order delta_t tau_max tau_lin", (char *)NULL);
  return TCL_ERROR;
}

int observable_calc_tclcommand(observable* self) {
  Observable_Tclcommand_Arg_Container* container = (Observable_Tclcommand_Arg_Container*) self->container;
  Tcl_Interp* interp = (Tcl_Interp*) container->interp;
  int error = Tcl_Eval(interp, container->command);
  if (error) {
    return 1;
  }
  char* result = Tcl_GetStringResult(interp);
  char* token;
  int counter=0;
  double* A=self->last_value;
  token = strtok(result, " ");
  while ( token != NULL && counter < self->n) {
    A[counter] = atof(token);
    token = strtok(NULL, " ");
    counter++;
  }
  Tcl_ResetResult(interp);
  if (counter != self->n) {
      return 1;
  }
  return 0;
}
